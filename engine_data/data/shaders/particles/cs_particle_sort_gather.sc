/*
 * Gather dense instances into sorted order: out[i] = in[indices[i]].
 * One linear pass after key/index bitonic.
 */

#include <bgfx_compute.sh>

BUFFER_RO(s_instances, vec4, 0);
BUFFER_RO(s_indices, uint, 1);
BUFFER_WO(s_sorted, vec4, 2);

uniform vec4 u_sort1;

#define u_alive uint(u_sort1.z)

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if(i >= u_alive)
    {
        return;
    }
    uint src = s_indices[i];
    uint dst_base = i * 6u;
    uint src_base = src * 6u;
    for(uint k = 0u; k < 6u; ++k)
    {
        s_sorted[dst_base + k] = s_instances[src_base + k];
    }
}
