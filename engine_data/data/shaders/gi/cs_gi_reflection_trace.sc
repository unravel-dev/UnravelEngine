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
 *
 * This form also runs the kernel's albedo REMODULATION (GI_LIGHT_VOXEL_READ_ALBEDO): the
 * attribute-albedo volume on stage 11 - the last free stage - and the texture means read
 * from the mean block of the list buffer, where the args pass staged them (layout in
 * cs_gi_reflection_args.sc). The fragment fallback keeps the plain voxel colour rather
 * than spending its own binding surface on the same trick.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"

// The output image sits on stage 7: OpenGL guarantees only eight image units (0-7), while
// a buffer tolerates the high stages, so the trace list moved up to 15.
BUFFER_RO(b_gi_refl_list, uint, 15);

/// A slot of the texture-mean block the args pass staged into the list. Slot 0 is the
/// reserved "no mean" WHITE by the same convention the attribute composer follows - the
/// kernel skips the lookup for it, so this is never fetched with 0.
vec3 GiReflectionMeanAlbedo(uint slot)
{
	uint base = 2u + slot * 3u;
	return vec3(uintBitsToFloat(b_gi_refl_list[base + 0u]),
	            uintBitsToFloat(b_gi_refl_list[base + 1u]),
	            uintBitsToFloat(b_gi_refl_list[base + 2u]));
}

// Arms the kernel's remodulation block and gi_light_voxels.sh's attribute-albedo read
// (SAMPLER3D stage 11). Must precede the kernel include.
#define GI_LIGHT_VOXEL_READ_ALBEDO
#include "gi/gi_reflection_kernel.sh"

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
	uint packed_pixel = b_gi_refl_list[2u + uint(GI_REFLECTION_MEAN_SLOTS) * 3u + index];
	ivec2 pixel = ivec2(int(packed_pixel & 0xffffu), int(packed_pixel >> 16u));
	vec2 frag_coord = vec2(pixel) + vec2_splat(0.5);
	vec2 uv = frag_coord * u_gi_reflection_texel.xy;
	imageStore(s_gi_refl_out, pixel, GiReflectionShade(uv, frag_coord));
}
