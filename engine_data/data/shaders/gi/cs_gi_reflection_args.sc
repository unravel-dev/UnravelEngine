/*
 * Converts the reflection classify pass's compacted texel count into the trace's indirect
 * dispatch args, and stages the texture-mean buffer into the list. One 64-lane group,
 * between classify and trace.
 *
 * The trace packs 64 texels per group in X. A 4K FULL-resolution trace target can exceed
 * the 65535 X-group limit of the D3D11 class of backends (8.3 M texels = 129.6 k groups),
 * so groups fold into Y at a fixed stride: X is exact when everything fits in one row,
 * and exactly the stride when it does not - the kernel unflattens with the same constant
 * and bounds-checks the staged count, so the fold's padding groups exit immediately.
 *
 * Also stages the count into the list's [1] and RESETS the append cursor [0] for the next
 * frame's classify - the classify pass is its frame's first writer of the list, so the
 * reset has to live downstream of the last reader of the cursor (this pass; the kernel
 * reads only the staged copy). Raw uint buffer, so the count is not a float round-trip.
 *
 * TEXTURE MEANS: the trace kernel rebuilds a hit instance's own albedo (base colour factor
 * x texture mean) to remodulate the voxel radiance, but every one of its 16 bgfx stages is
 * taken - so this pass copies the mean buffer into the list's mean block (layout below),
 * riding the stage the trace already spends on the list. The mean buffer is never CPU-seeded,
 * but a slot is only ever REFERENCED once its capture has written it (instances carry slot 0
 * until then), so unwritten slots' bits are copied dead; the a < 0.5 -> WHITE guard exists so
 * a NaN-poisoned or half-landed slot answers "no mean" rather than garbage.
 *
 * List layout (keep in step with cs_gi_reflection_classify.sc / cs_gi_reflection_trace.sc):
 *   [0]                                  append cursor
 *   [1]                                  staged trace count
 *   [2 .. 2 + GI_REFLECTION_MEAN_SLOTS*3)  means, rgb float bits per slot
 *   [2 + GI_REFLECTION_MEAN_SLOTS*3 + i]   packed texel coords, y in the high 16 bits
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"

BUFFER_RW(b_gi_refl_args, uvec4, 0);
BUFFER_RW(b_gi_refl_list, uint, 1);
/// vec4 per slot: rgb = mean colour, a = 1 once captured (cs_gi_texture_mean.sc).
BUFFER_RO(b_gi_texture_means, vec4, 2);

/// Keep in step with cs_gi_reflection_trace.sc.
#define GI_REFLECTION_DISPATCH_STRIDE 4096u

NUM_THREADS(64, 1, 1)
void main()
{
	if(gl_LocalInvocationID.x == 0u)
	{
		uint count = b_gi_refl_list[0];
		uint groups = (count + 63u) / 64u;
		uint x = min(groups, GI_REFLECTION_DISPATCH_STRIDE);
		uint y = (groups + GI_REFLECTION_DISPATCH_STRIDE - 1u) / GI_REFLECTION_DISPATCH_STRIDE;
		dispatchIndirect(b_gi_refl_args, 0u, x, max(y, 1u), 1u);
		b_gi_refl_list[1] = count;
		b_gi_refl_list[0] = 0u;
	}
	// Strided mean copy: 16 slots per lane at the group width of 64.
	for(uint slot = gl_LocalInvocationID.x; slot < uint(GI_REFLECTION_MEAN_SLOTS); slot += 64u)
	{
		vec4 mean = b_gi_texture_means[slot];
		vec3 value = mean.w >= 0.5 ? mean.xyz : vec3_splat(1.0);
		uint base = 2u + slot * 3u;
		b_gi_refl_list[base + 0u] = floatBitsToUint(value.x);
		b_gi_refl_list[base + 1u] = floatBitsToUint(value.y);
		b_gi_refl_list[base + 2u] = floatBitsToUint(value.z);
	}
}
