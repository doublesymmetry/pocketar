shader_type canvas_item;
render_mode unshaded, blend_disabled;

uniform sampler2D y_texture : filter_linear, repeat_disable;
uniform sampler2D cbcr_texture : filter_linear, repeat_disable;
uniform sampler2D rgb_texture : filter_linear, repeat_disable;
uniform bool rgb_input = false;
uniform mat3 display_transform;
uniform int color_encoding = 0;
uniform bool imported_chroma = false;

void fragment() {
	vec2 uv = (display_transform * vec3(UV, 1.0)).xy;
	if (rgb_input) {
		COLOR = vec4(texture(rgb_texture, uv).rgb, 1.0);
	} else {
		float y = texture(y_texture, uv).r;
		// CPU FORMAT_RG8 is RG; imported RD RG8 is RRRG and must use .ra (texture_storage.cpp:2296,2871).
		vec4 chroma = texture(cbcr_texture, uv);
		vec2 cbcr = (imported_chroma ? chroma.ra : chroma.rg) - vec2(128.0 / 255.0);
		vec3 rgb = vec3(y + 1.402 * cbcr.y,
			y - 0.344136 * cbcr.x - 0.714136 * cbcr.y,
			y + 1.772 * cbcr.x);
		COLOR = color_encoding == 0 ? vec4(clamp(rgb, 0.0, 1.0), 1.0) : vec4(1.0, 0.0, 1.0, 1.0);
	}
}
