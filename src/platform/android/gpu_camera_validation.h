#pragma once

#include "yuv_repack.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace pocketar {

constexpr int GPU_VALIDATION_PIXELS = 9 * 16 * 16;
using CameraRGB = std::array<double, 3>;
using CameraBlockMeans = std::array<CameraRGB, 9>;

inline CameraRGB camera_yuv_to_rgb(double p_y, double p_u, double p_v) {
	const double u = p_u - 128, v = p_v - 128;
	CameraRGB rgb{p_y + 1.402 * v, p_y - 0.344136 * u - 0.714136 * v, p_y + 1.772 * u};
	for (double &channel : rgb) channel = std::clamp(channel, 0.0, 255.0);
	return rgb;
}

inline int gpu_validation_block_start(int p_extent, int p_index) {
	return int(std::round((p_index + 0.5) * p_extent / 3)) - 8;
}

inline bool gpu_validation_coordinates(int p_width, int p_height, float *r_uv) {
	if (p_width < 48 || p_height < 48) return false;
	int index = 0;
	for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col) {
		const int x = gpu_validation_block_start(p_width, col), y = gpu_validation_block_start(p_height, row);
		for (int dy = 0; dy < 16; ++dy) for (int dx = 0; dx < 16; ++dx) {
			r_uv[index++] = (x + dx + 0.5f) / p_width;
			r_uv[index++] = (y + dy + 0.5f) / p_height;
		}
	}
	return true;
}

inline double camera_plane_sample(const ImagePlane &p_plane, int p_width, int p_height, float p_u, float p_v) {
	const double x = std::clamp(double(p_u) * p_width - 0.5, 0.0, double(p_width - 1));
	const double y = std::clamp(double(p_v) * p_height - 0.5, 0.0, double(p_height - 1));
	const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, p_width - 1), y1 = std::min(y0 + 1, p_height - 1);
	const auto pixel = [&](int p_x, int p_y) { return p_plane.data[size_t(p_y) * p_plane.row_stride + size_t(p_x) * p_plane.pixel_stride]; };
	const double top = pixel(x0, y0) * (1 - (x - x0)) + pixel(x1, y0) * (x - x0);
	const double bottom = pixel(x0, y1) * (1 - (x - x0)) + pixel(x1, y1) * (x - x0);
	return top * (1 - (y - y0)) + bottom * (y - y0);
}

inline bool cpu_camera_block_means(int p_width, int p_height, const ImagePlane (&p_planes)[3], const float *p_image_uv, CameraBlockMeans &r_means, double &r_luma_range, double &r_chroma_magnitude) {
	const int cw = (p_width + 1) / 2, ch = (p_height + 1) / 2;
	if (!plane_fits(p_planes[0], p_width, p_height) || !plane_fits(p_planes[1], cw, ch) || !plane_fits(p_planes[2], cw, ch)) return false;
	r_means = {};
	std::array<double, 9> luma{}, cb_means{}, cr_means{};
	for (int i = 0; i < GPU_VALIDATION_PIXELS; ++i) {
		const float u = p_image_uv[2 * i], v = p_image_uv[2 * i + 1];
		if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1) return false;
		const double y = camera_plane_sample(p_planes[0], p_width, p_height, u, v);
		const double cb = camera_plane_sample(p_planes[1], cw, ch, u, v);
		const double cr = camera_plane_sample(p_planes[2], cw, ch, u, v);
		luma[i / 256] += y / 256;
		cb_means[i / 256] += (cb - 128) / 256;
		cr_means[i / 256] += (cr - 128) / 256;
		const CameraRGB rgb = camera_yuv_to_rgb(y, cb, cr);
		for (int channel = 0; channel < 3; ++channel) r_means[i / 256][channel] += rgb[channel] / 256;
	}
	const auto range = std::minmax_element(luma.begin(), luma.end());
	r_luma_range = *range.second - *range.first;
	r_chroma_magnitude = 0;
	for (int block = 0; block < 9; ++block) r_chroma_magnitude += std::hypot(cb_means[block], cr_means[block]) / 9;
	return true;
}

}
