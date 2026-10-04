/*
 * UE 5.8 r.Lumen.Reflections.VisualizeTraces (LumenReflectionTracing.usf VisualizeReflectionTracesCS): the reflection
 * ray this frame traced from the pixel under the cursor, as lines (lumen_visualize.sh) - from the pixel out to the
 * trace's distance in the radiance it brought back, and a LUMEN_VISUALIZE_CROSS_SIZE cross in yellow at its end - and
 * UE's text: the pixel, then the trace's distance and radiance. A pixel that traces no ray (UE: no positive tracing
 * depth) writes no lines and only the pixel. The text starts at (0.1, 0.1) of the view as UE's does, below the
 * probe counts while they print there too.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_visualize.sh"

/// The reflection pass's trace targets (lumen_reflection_common.sh) and the scene's device depth.
SAMPLER2D(s_lumen_reflection_ray, 0);
SAMPLER2D(s_lumen_reflection_hit, 1);
SAMPLER2D(s_lumen_reflection_radiance, 2);
SAMPLER2D(s_lumen_depth, 3);
BUFFER_RW(b_lumen_visualize_lines, vec4, 4);
#define SHADER_PRINT_STAGE 5
#include "shader_print/shader_print.sh"

/// xy = the full-resolution pixel under the cursor, z = the text line to start at.
uniform vec4 u_lumen_visualize_reflection;

/// UE AddCrossTWS(HitPoint, 2): three axis lines 2 cm either side of the hit.
#define LUMEN_VISUALIZE_CROSS_SIZE 0.02
#define LUMEN_VISUALIZE_REFLECTION_LINES 4

void LumenWriteLine(int segment, vec3 start, vec3 delta, vec3 color)
{
	int base = segment * LUMEN_VISUALIZE_TRACE_STRIDE;
	b_lumen_visualize_lines[base] = vec4(start, 0.0);
	b_lumen_visualize_lines[base + 1] = vec4(delta, 0.0);
	b_lumen_visualize_lines[base + 2] = vec4(color, 0.0);
}

NUM_THREADS(1, 1, 1)
void main()
{
	ivec2 pixel = ivec2(u_lumen_visualize_reflection.xy);
	vec4 ray = texelFetch(s_lumen_reflection_ray, pixel, 0);
	ShaderPrintContext text = ShaderPrintBegin(vec2(0.1, 0.1));
	for(int skipped = 0; skipped < int(u_lumen_visualize_reflection.z); ++skipped)
	{
		text = ShaderPrintNewline(text);
	}
	text = ShaderPrintSymbols(text, ivec4(_P_, _i_, _x_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_l_, _COLON_, _SPC_, 0));
	text = ShaderPrintInt(text, pixel.x);
	text = ShaderPrintInt(text, pixel.y);
	text = ShaderPrintNewline(text);
	if(ray.w <= 0.0)
	{
		for(int segment = 0; segment < LUMEN_VISUALIZE_REFLECTION_LINES; ++segment)
		{
			LumenWriteLine(segment, vec3_splat(0.0), vec3_splat(0.0), vec3_splat(0.0));
		}
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / vec2(textureSize(s_lumen_depth, 0));
	vec3 position = LumenWorldFromDepth(uv, texelFetch(s_lumen_depth, pixel, 0).x);
	float distance = abs(texelFetch(s_lumen_reflection_hit, pixel, 0).x);
	vec3 radiance = texelFetch(s_lumen_reflection_radiance, pixel, 0).xyz;
	vec3 hit = position + ray.xyz * distance;
	text = ShaderPrintSymbols(text, ivec4(_D_, _i_, _s_, _t_));
	text = ShaderPrintSymbols(text, ivec4(_a_, _n_, _c_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_COLON_, _SPC_, 0, 0));
	text = ShaderPrintFloat(text, distance);
	text = ShaderPrintNewline(text);
	text = ShaderPrintSymbols(text, ivec4(_R_, _a_, _d_, _i_));
	text = ShaderPrintSymbols(text, ivec4(_a_, _n_, _c_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_COLON_, _SPC_, 0, 0));
	text = ShaderPrintFloat(text, radiance.x);
	text = ShaderPrintFloat(text, radiance.y);
	text = ShaderPrintFloat(text, radiance.z);
	LumenWriteLine(0, position, ray.xyz * distance, radiance);
	vec3 yellow = vec3(1.0, 1.0, 0.0);
	LumenWriteLine(1, hit - vec3(LUMEN_VISUALIZE_CROSS_SIZE, 0.0, 0.0), vec3(2.0 * LUMEN_VISUALIZE_CROSS_SIZE, 0.0, 0.0), yellow);
	LumenWriteLine(2, hit - vec3(0.0, LUMEN_VISUALIZE_CROSS_SIZE, 0.0), vec3(0.0, 2.0 * LUMEN_VISUALIZE_CROSS_SIZE, 0.0), yellow);
	LumenWriteLine(3, hit - vec3(0.0, 0.0, LUMEN_VISUALIZE_CROSS_SIZE), vec3(0.0, 0.0, 2.0 * LUMEN_VISUALIZE_CROSS_SIZE), yellow);
}
