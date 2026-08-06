/*
 * Fills a uint buffer with one value. bgfx forbids CPU updates on compute-writable buffers
 * (BX_ASSERT "Can't update GPU write buffer from CPU" - and in release the check is compiled
 * out, so the behaviour would silently differ per config), so the one-time seeds the GI
 * buffers need - cell-id sentinels, zeroed cursors - are GPU dispatches of this shader.
 */

#include "bgfx_compute.sh"

BUFFER_WO(b_fill_target, uint, 0);

/// x = entry count, y = fill value's raw bits (reinterpreted, so 0xFFFFFFFF sentinels survive
/// the float uniform path unchanged).
uniform vec4 u_gi_buffer_fill_params;

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index >= uint(u_gi_buffer_fill_params.x))
	{
		return;
	}
	b_fill_target[index] = floatBitsToUint(u_gi_buffer_fill_params.y);
}
