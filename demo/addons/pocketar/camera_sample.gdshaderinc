uniform sampler2D y_texture : filter_linear, repeat_disable;
uniform sampler2D cbcr_texture : filter_linear, repeat_disable;
uniform sampler2D rgb_texture : filter_linear, repeat_disable;
uniform bool rgb_input = false;
uniform mat3 display_transform;
uniform bool imported_chroma = false;

vec3 camera_sample(vec2 view_uv) {
	vec2 uv = (display_transform * vec3(view_uv, 1.0)).xy;
	if (rgb_input) {
		return texture(rgb_texture, uv).rgb;
	}
	float y = texture(y_texture, uv).r;
	// CPU FORMAT_RG8 is RG; imported RD RG8 is RRRG and must use .ra.
	vec4 chroma = texture(cbcr_texture, uv);
	vec2 cbcr = (imported_chroma ? chroma.ra : chroma.rg) - vec2(128.0 / 255.0);
	return clamp(vec3(y + 1.402 * cbcr.y,
		y - 0.344136 * cbcr.x - 0.714136 * cbcr.y,
		y + 1.772 * cbcr.x), 0.0, 1.0);
}
