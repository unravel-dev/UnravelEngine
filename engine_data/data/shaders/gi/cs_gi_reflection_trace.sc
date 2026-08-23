/*
 * COMPUTE form of the GI reflection trace - the deliverable path. The classify pass has
 * already answered sky / degenerate / rough texels and compacted the tracing texels into
 * a dense list, so every 64-lane group here is fully populated with rays; the indirect
 * args pass sized the launch. Shared body: gi_reflection_kernel.sh (the fragment form,
 * fs_gi_reflection.sc, is the fallback when this chain is unavailable).
 *
 * Groups fold into Y past the X-group limit at the args pass's fixed stride; the staged
 * count in the list's [1] bounds the fold's padding threads (their list reads would be
 * garbage coordinates - a store race against classify-answered texels, not just waste).
 */

#include "bgfx_compute.sh"
#include "gi/gi_reflection_kernel.sh"

// The output image sits on stage 7: OpenGL guarantees only eight image units (0-7), while
// a buffer tolerates the high stages, so the trace list moved up to 15.
BUFFER_RO(b_gi_refl_list, uint, 15);
IMAGE2D_WO(s_gi_refl_out, rgba16f, 7);

/// xy = one texel of the trace target, zw = its dimensions.
uniform vec4 u_gi_reflection_texel;

/// Keep in step with cs_gi_reflection_args.sc.
#define GI_REFLECTION_DISPATCH_STRIDE 4096u

NUM_THREADS(64, 1, 1)
void main()
{
	uint flat_group = gl_WorkGroupID.y * GI_REFLECTION_DISPATCH_STRIDE + gl_WorkGroupID.x;
	uint index = flat_group * 64u + gl_LocalInvocationID.x;
	if(index >= b_gi_refl_list[1])
	{
		return;
	}
	// (packed is a reserved word in GLSL)
	uint packed_pixel = b_gi_refl_list[2u + index];
	ivec2 pixel = ivec2(int(packed_pixel & 0xffffu), int(packed_pixel >> 16u));
	vec2 frag_coord = vec2(pixel) + vec2_splat(0.5);
	vec2 uv = frag_coord * u_gi_reflection_texel.xy;
	imageStore(s_gi_refl_out, pixel, GiReflectionShade(uv, frag_coord));
}
