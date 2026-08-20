/*
 * Converts the reflection classify pass's compacted texel count into the trace's indirect
 * dispatch args. One thread, between classify and trace.
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
 */

#include "bgfx_compute.sh"

BUFFER_RW(b_gi_refl_args, uvec4, 0);
BUFFER_RW(b_gi_refl_list, uint, 1);

/// Keep in step with cs_gi_reflection_trace.sc.
#define GI_REFLECTION_DISPATCH_STRIDE 4096u

NUM_THREADS(1, 1, 1)
void main()
{
	uint count = b_gi_refl_list[0];
	uint groups = (count + 63u) / 64u;
	uint x = min(groups, GI_REFLECTION_DISPATCH_STRIDE);
	uint y = (groups + GI_REFLECTION_DISPATCH_STRIDE - 1u) / GI_REFLECTION_DISPATCH_STRIDE;
	dispatchIndirect(b_gi_refl_args, 0u, x, max(y, 1u), 1u);
	b_gi_refl_list[1] = count;
	b_gi_refl_list[0] = 0u;
}
