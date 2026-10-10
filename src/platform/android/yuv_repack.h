#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace handheld_ar {

struct ImagePlane {
	const uint8_t *data = nullptr;
	size_t length = 0;
	int row_stride = 0;
	int pixel_stride = 0;
};

enum class YUVLayout { NV12, NV21, GENERIC };

inline bool plane_fits(const ImagePlane &p_plane, int p_width, int p_height) {
	return p_plane.data && p_width > 0 && p_height > 0 && p_plane.pixel_stride > 0 &&
		p_plane.row_stride >= (p_width - 1) * p_plane.pixel_stride + 1 &&
		p_plane.length >= size_t(p_height - 1) * p_plane.row_stride + size_t(p_width - 1) * p_plane.pixel_stride + 1;
}

inline bool repack_yuv(int p_width, int p_height, const ImagePlane (&p_planes)[3], uint8_t *r_y, uint8_t *r_uv, YUVLayout &r_layout) {
	const int cw = (p_width + 1) / 2;
	const int ch = (p_height + 1) / 2;
	if (p_planes[0].pixel_stride != 1 || !plane_fits(p_planes[0], p_width, p_height) ||
		!plane_fits(p_planes[1], cw, ch) || !plane_fits(p_planes[2], cw, ch)) {
		return false;
	}
	for (int y = 0; y < p_height; ++y) {
		std::memcpy(r_y + size_t(y) * p_width, p_planes[0].data + size_t(y) * p_planes[0].row_stride, p_width);
	}
	const auto &u = p_planes[1];
	const auto &v = p_planes[2];
	r_layout = YUVLayout::GENERIC;
	if (u.pixel_stride == 2 && v.pixel_stride == 2 && u.row_stride == v.row_stride) {
		if (uintptr_t(v.data) == uintptr_t(u.data) + 1) r_layout = YUVLayout::NV12;
		if (uintptr_t(u.data) == uintptr_t(v.data) + 1) r_layout = YUVLayout::NV21;
	}
	for (int y = 0; y < ch; ++y) {
		uint8_t *dst = r_uv + size_t(y) * cw * 2;
		const size_t offset = size_t(y) * u.row_stride;
		if (r_layout == YUVLayout::NV12 && offset + size_t(cw) * 2 <= u.length) {
			std::memcpy(dst, u.data + offset, size_t(cw) * 2);
		} else {
			// The final Android chroma row need not include the other plane's last byte.
			for (int x = 0; x < cw; ++x) {
				dst[2 * x] = u.data[size_t(y) * u.row_stride + size_t(x) * u.pixel_stride];
				dst[2 * x + 1] = v.data[size_t(y) * v.row_stride + size_t(x) * v.pixel_stride];
			}
		}
	}
	return true;
}

inline bool texture_slot_retired(uint64_t p_swaps, uint64_t p_last_use, uint32_t p_delay, bool p_referenced) {
	return !p_referenced && p_swaps >= p_last_use && p_swaps - p_last_use >= p_delay;
}

}
