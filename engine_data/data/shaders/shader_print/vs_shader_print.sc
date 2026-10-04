$output v_color0, v_texcoord0

/*
 * shader_print's text (UE ShaderPrintDraw.usf DrawSymbolsVS): per printed symbol, two quads the font's size centred
 * on its pen position - its drop shadow in black SHADER_PRINT_SHADOW_OFFSET right and down (UE: a fifth of the font
 * size), then its glyph in its colour. Symbols past the printed count fall outside the view.
 */

#include "../common.sh"
#include <bgfx_compute.sh>

BUFFER_RO(b_shader_print, uint, 0);

/// xy = the view's size in pixels, z = the symbols the buffer holds.
uniform vec4 u_shader_print;

/// shader_print.sh's layout.
#define SHADER_PRINT_HEADER 4
#define SHADER_PRINT_SYMBOL_STRIDE 4
#define SHADER_PRINT_VERTICES_PER_SYMBOL 12
#define SHADER_PRINT_QUAD_VERTICES 6
#define SHADER_PRINT_FONT_SIZE 8.0
#define SHADER_PRINT_SHADOW_OFFSET 0.8

void main()
{
	int symbol = gl_VertexID / SHADER_PRINT_VERTICES_PER_SYMBOL;
	int local = gl_VertexID - symbol * SHADER_PRINT_VERTICES_PER_SYMBOL;
	bool is_shadow = local < SHADER_PRINT_QUAD_VERTICES;
	int corner = is_shadow ? local : local - SHADER_PRINT_QUAD_VERTICES;
	// UE's corners: u = ((corner + 1) / 3) & 1, v = corner & 1.
	int u_step = (corner + 1) / 3;
	vec2 uv = vec2(float(u_step - (u_step / 2) * 2), float(corner - (corner / 2) * 2));
	uint count = min(b_shader_print[0], uint(u_shader_print.z));
	uint base = uint(SHADER_PRINT_HEADER) + uint(symbol) * uint(SHADER_PRINT_SYMBOL_STRIDE);
	vec2 pen = vec2(uintBitsToFloat(b_shader_print[base]), uintBitsToFloat(b_shader_print[base + 1u]));
	uint code = b_shader_print[base + 2u];
	uint packed = b_shader_print[base + 3u];
	vec2 pixel = pen + (uv - 0.5) * SHADER_PRINT_FONT_SIZE + (is_shadow ? vec2_splat(SHADER_PRINT_SHADOW_OFFSET) : vec2_splat(0.0));
	vec2 ndc = vec2(pixel.x / u_shader_print.x * 2.0 - 1.0, 1.0 - pixel.y / u_shader_print.y * 2.0);
	gl_Position = uint(symbol) < count ? vec4(ndc, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
	vec3 color = vec3(float(packed & 255u), float((packed >> 8u) & 255u), float((packed >> 16u) & 255u)) / 255.0;
	v_color0 = vec4(is_shadow ? vec3_splat(0.0) : color, 1.0);
	v_texcoord0 = vec4(uv, float(code), 0.0);
}
