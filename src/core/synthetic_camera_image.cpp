#include "synthetic_camera_image.h"

#include <algorithm>
#include <cmath>

namespace godot {

void fill_synthetic_camera_image(int p_width, int p_height, int64_t p_sequence, Ref<Image> &r_y_image, Ref<Image> &r_cbcr_image) {
	PackedByteArray y_data;
	PackedByteArray cbcr_data;
	y_data.resize(p_width * p_height);
	cbcr_data.resize(p_width * p_height * 2);
	uint8_t *y_bytes = y_data.ptrw();
	uint8_t *chroma = cbcr_data.ptrw();
	const Color bars[] = { Color(1, 0, 0), Color(0, 0, 1), Color(0, 1, 0), Color(1, 1, 0), Color(0, 1, 1), Color(1, 0, 1), Color(1, 1, 1), Color(0, 0, 0) };
	auto quantize = [](double p_value) { return uint8_t(std::clamp(std::lround(p_value), 0L, 255L)); };
	for (int y = 0; y < p_height; y++) {
		for (int x = 0; x < p_width; x++) {
			Color rgb = bars[std::min(7, x * 8 / p_width)];
			if (y < 24) {
				const bool bit = (p_sequence >> std::min(15, x * 16 / p_width)) & 1;
				rgb = bit ? Color(1, 1, 1) : Color(0, 0, 0);
			} else if (x % 32 == 0 || y % 32 == 0) {
				rgb = Color(0.25, 0.25, 0.25);
			}
			const int i = y * p_width + x;
			y_bytes[i] = quantize(255 * (0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b));
			chroma[2 * i] = quantize(128 + 255 * (-0.168736 * rgb.r - 0.331264 * rgb.g + 0.5 * rgb.b));
			chroma[2 * i + 1] = quantize(128 + 255 * (0.5 * rgb.r - 0.418688 * rgb.g - 0.081312 * rgb.b));
		}
	}
	if (r_y_image.is_null()) {
		r_y_image.instantiate();
		r_cbcr_image.instantiate();
	}
	r_y_image->set_data(p_width, p_height, false, Image::FORMAT_R8, y_data);
	r_cbcr_image->set_data(p_width, p_height, false, Image::FORMAT_RG8, cbcr_data);
}

}
