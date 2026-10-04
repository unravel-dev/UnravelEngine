$input v_color0, v_texcoord0

/*
 * A shader_print glyph (UE ShaderPrintDraw.usf DrawSymbolsPS): the font's coverage at the quad's texel, premultiplied.
 */

#include "../common.sh"

/// The glyphs side by side, 8 x 8 texels each from SHADER_PRINT_FIRST_CODE on (shader_print_font).
SAMPLER2D(s_shader_print_font, 1);

#define SHADER_PRINT_GLYPH_SIZE 8
#define SHADER_PRINT_FIRST_CODE 32
#define SHADER_PRINT_LAST_CODE 127

void main()
{
	int code = clamp(int(v_texcoord0.z + 0.5), SHADER_PRINT_FIRST_CODE, SHADER_PRINT_LAST_CODE);
	ivec2 texel = min(ivec2(floor(v_texcoord0.xy * float(SHADER_PRINT_GLYPH_SIZE))),
	                  ivec2(SHADER_PRINT_GLYPH_SIZE - 1, SHADER_PRINT_GLYPH_SIZE - 1));
	float coverage = texelFetch(s_shader_print_font,
	                            ivec2((code - SHADER_PRINT_FIRST_CODE) * SHADER_PRINT_GLYPH_SIZE + texel.x, texel.y),
	                            0).x;
	gl_FragColor = vec4(v_color0.rgb * coverage, coverage);
}
