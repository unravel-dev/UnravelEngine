/*
 * Build per-instance depth sort keys + identity indices for dense packed rows.
 * Key = -dist_sq so ascending bitonic yields back-to-front. Padding -> +inf.
 */

#include <bgfx_compute.sh>

BUFFER_RO(s_instances, vec4, 0);
BUFFER_WO(s_keys, float, 1);
BUFFER_WO(s_indices, uint, 2);

uniform vec4 u_sort0;
uniform vec4 u_sort1;

#define u_eye    u_sort0.xyz
#define u_padded uint(u_sort1.y)
#define u_alive  uint(u_sort1.z)

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if(i >= u_padded)
    {
        return;
    }
    s_indices[i] = i;
    if(i >= u_alive)
    {
        s_keys[i] = 1.0e30;
        return;
    }
    vec3 pos = s_instances[i * 6u].xyz;
    vec3 d = u_eye - pos;
    s_keys[i] = -dot(d, d);
}
