shader_type canvas_item;
render_mode unshaded, blend_disabled;

#include "camera_sample.gdshaderinc"

void fragment() {
	COLOR = vec4(camera_sample(UV), 1.0);
}
