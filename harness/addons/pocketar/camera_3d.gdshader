shader_type spatial;
render_mode unshaded, cull_disabled, fog_disabled, shadows_disabled, skip_vertex_transform, depth_draw_opaque;

#include "camera_sample.gdshaderinc"

void vertex() {
	// Reverse-Z zero preserves cleared background depth and the entire finite frustum.
	POSITION = vec4(VERTEX.xy * 2.0, 0.0, 1.0);
}

void fragment() {
	vec3 rgb = camera_sample(SCREEN_UV);
	ALBEDO = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), rgb));
}
