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
    // Instance rows are 8 vec4s (vs_particle_instanced i_data0..i_data7).
    uint dst_base = i * 8u;
    uint src_base = src * 8u;
    for(uint k = 0u; k < 8u; ++k)
    {
        s_sorted[dst_base + k] = s_instances[src_base + k];
    }
}
