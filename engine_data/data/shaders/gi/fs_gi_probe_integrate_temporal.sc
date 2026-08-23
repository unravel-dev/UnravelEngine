$input v_texcoord0

/*
 * FUSED integrate + temporal (G5): the gather result feeds the temporal blend in
 * registers, so this frame's gather never round-trips through the GI_TRACE target (a full
 * RGBA16F write + read per frame that existed only to hand one pixel's value to the next
 * pass). Emits the temporal history MRT (color + moments) directly.
 *
 * The temporal kernel compiles with GI_TEMPORAL_FUSED: the neighbourhood clamp needs this
 * frame's gather at nine NEIGHBOURS, which exists only as a texture in the split form -
 * the resolve pass runs clamp 0 by policy (it fights the placement jitter [S21 s98]), and
 * falls back to the split programs if that policy ever changes.
 *
 * Shared bodies: gi_probe_integrate_kernel.sh, gi_temporal_kernel.sh. Stage map: the
 * integrate half owns 0-4 (sdf_common), 2, 7, 8, 9, 11, 15; the temporal half's history
 * samplers take free stages 5, 6, 10.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_probe_integrate_kernel.sh"

SAMPLER2D(s_gi_history, 5);
SAMPLER2D(s_gi_prev_depth, 6);
SAMPLER2D(s_gi_history_moments, 10);
SAMPLER2D(s_gi_history_fast, 12);

#define GI_TEMPORAL_FUSED
#include "gi/gi_temporal_kernel.sh"

#if BGFX_SHADER_LANGUAGE_GLSL >= 420
// gl_FragData is removed from GLSL 4.20 core, and this pass compiles at 430 (its include
// chain carries compute buffers): the render targets are declared explicitly here. Every
// other language keeps gl_FragData, which shaderc maps to the targets itself.
layout(location = 0) out vec4 gi_color_out;
layout(location = 1) out vec4 gi_moments_out;
layout(location = 2) out vec4 gi_fast_out;
#define GI_COLOR_OUT   gi_color_out
#define GI_MOMENTS_OUT gi_moments_out
#define GI_FAST_OUT    gi_fast_out
#else
#define GI_COLOR_OUT   gl_FragData[0]
#define GI_MOMENTS_OUT gl_FragData[1]
#define GI_FAST_OUT    gl_FragData[2]
#endif

void main()
{
	vec2 uv = v_texcoord0;
	float depth;
	vec3 world_position;
	// The gather is finite by construction (probe atlases are sanitized upstream and the
	// weights are clamped), matching the split path: GiSanitize there guards the TEXTURE
	// round-trip this form no longer takes.
	vec4 current = GiIntegrateGather(uv, gl_FragCoord.xy, depth, world_position);
	vec4 color;
	vec4 fast;
	vec4 moments;
	GiResolveTemporal(uv, current, depth, world_position, color, fast, moments);
	GI_COLOR_OUT = color;
	GI_MOMENTS_OUT = moments;
	GI_FAST_OUT = fast;
}
