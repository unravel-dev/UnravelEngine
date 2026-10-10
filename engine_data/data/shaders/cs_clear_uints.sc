/*
 * Zeroes a range of a uint buffer the GPU writes: the counters a frame's passes start from (the GI gather's
 * far-field ray count, the shader print header). bgfx updates no BGFX_BUFFER_COMPUTE_WRITE buffer from the CPU, so the
 * range is cleared by a dispatch ordered before its first reader (buffer_clear.h).
 */

#include "bgfx_compute.sh"

BUFFER_WO(b_clear_uints, uint, 0);

/// x = the range's first uint, y = its length.
uniform vec4 u_clear_uints;

NUM_THREADS(64, 1, 1)
void main()
{
	uint index = gl_GlobalInvocationID.x;
	if(index < uint(u_clear_uints.y))
	{
		b_clear_uints[uint(u_clear_uints.x) + index] = 0u;
	}
}
