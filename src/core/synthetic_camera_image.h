#pragma once

#include <godot_cpp/classes/image.hpp>

namespace godot {

void fill_synthetic_camera_image(int p_width, int p_height, int64_t p_sequence, Ref<Image> &r_y_image, Ref<Image> &r_cbcr_image);

}
