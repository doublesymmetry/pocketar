#include "platform/android/gpu_camera_validation.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using namespace pocketar;

int main() {
	const auto black = camera_yuv_to_rgb(0, 128, 128);
	const auto white = camera_yuv_to_rgb(255, 128, 128);
	for (int channel = 0; channel < 3; ++channel) {
		assert(std::abs(black[channel]) < 1e-9);
		assert(std::abs(white[channel] - 255) < 1e-9);
	}
	const auto red = camera_yuv_to_rgb(76, 85, 255);
	assert(red[0] > 253 && red[1] < 2 && red[2] < 2);
	constexpr int width = 128, height = 96, gpu_width = 192, gpu_height = 144;
	std::vector<uint8_t> y(width * height), uv(width * height / 2, 128);
	for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) y[row * width + col] = uint8_t(col + row);
	ImagePlane planes[3] = {{y.data(), y.size(), width, 1}, {uv.data(), uv.size() - 1, width, 2}, {uv.data() + 1, uv.size() - 1, width, 2}};
	float texture_uv[GPU_VALIDATION_PIXELS * 2], image_uv[GPU_VALIDATION_PIXELS * 2];
	assert(gpu_validation_coordinates(gpu_width, gpu_height, texture_uv));
	for (int turns = 0; turns < 4; ++turns) {
		for (int i = 0; i < GPU_VALIDATION_PIXELS; ++i) {
			float x = texture_uv[2 * i], v = texture_uv[2 * i + 1];
			for (int turn = 0; turn < turns; ++turn) { const float old_x = x; x = 1 - v; v = old_x; }
			image_uv[2 * i] = 0.15f + 0.7f * x;
			image_uv[2 * i + 1] = 0.2f + 0.6f * v;
		}
		CameraBlockMeans means{};
		double luma_range = 0, chroma_magnitude = 0;
		assert(cpu_camera_block_means(width, height, planes, image_uv, means, luma_range, chroma_magnitude));
		assert(luma_range > 90 && luma_range < 100);
		assert(chroma_magnitude == 0);
		for (int block = 0; block < 9; ++block) {
			double expected = 0;
			for (int pixel = 0; pixel < 256; ++pixel) {
				const int i = block * 256 + pixel;
				expected += image_uv[2 * i] * width - 0.5 + image_uv[2 * i + 1] * height - 0.5;
			}
			for (int channel = 0; channel < 3; ++channel) assert(std::abs(means[block][channel] - expected / 256) < 1e-5);
		}
	}
	CameraBlockMeans means{};
	double luma_range = 0, chroma_magnitude = 0;
	std::fill(y.begin(), y.end(), 5);
	std::fill(uv.begin(), uv.end(), 148);
	assert(cpu_camera_block_means(width, height, planes, image_uv, means, luma_range, chroma_magnitude));
	assert(luma_range == 0);
	assert(std::abs(chroma_magnitude - std::sqrt(800.0)) < 1e-9);
	image_uv[0] = -0.1f;
	assert(!cpu_camera_block_means(width, height, planes, image_uv, means, luma_range, chroma_magnitude));
	image_uv[0] = std::numeric_limits<float>::quiet_NaN();
	assert(!cpu_camera_block_means(width, height, planes, image_uv, means, luma_range, chroma_magnitude));
	image_uv[0] = 0.5f;
	planes[2].length = 1;
	assert(!cpu_camera_block_means(width, height, planes, image_uv, means, luma_range, chroma_magnitude));
	assert(!gpu_validation_coordinates(16, 16, texture_uv));
	puts("GPU camera BT.601 full, strided samples, crop/rotation and unequal resolutions PASS");
}
