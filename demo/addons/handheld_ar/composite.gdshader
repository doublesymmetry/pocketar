shader_type canvas_item;
render_mode unshaded, blend_premul_alpha;

vec3 linear_to_srgb(vec3 value) {
	return mix(12.92 * value, 1.055 * pow(max(value, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
		step(vec3(0.0031308), value));
}

void fragment() {
	vec4 scene = texture(TEXTURE, UV);
	// Mobile accumulates premultiplied linear RGB; HDR 2D keeps the final transfer function out of the viewport.
	vec3 straight = scene.a > 0.0001 ? scene.rgb / scene.a : vec3(0.0);
	COLOR = vec4(clamp(linear_to_srgb(straight), 0.0, 1.0) * scene.a, scene.a);
}
