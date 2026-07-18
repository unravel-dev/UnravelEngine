/*
 * Bitonic sort of dense instance rows by distance to eye (back-to-front).
 * Operates in-place on s_instances (6 vec4 per instance).
 * Alive count comes from s_counter; dispatch over next_pow2(capacity).
 */

#include <bgfx_compute.sh>

BUFFER_RW(s_instances, vec4, 0);
BUFFER_RO(s_counter, uint, 1);

uniform vec4 u_sort0;
uniform vec4 u_sort1;

#define u_eye    u_sort0.xyz
#define u_stage  uint(u_sort0.w)
#define u_pass   uint(u_sort1.x)
#define u_padded uint(u_sort1.y)

// Ascending sort key: farthest first (negated dist), padding -> +inf at end.
float instance_sort_key(uint idx, uint alive)
{
    if(idx >= alive)
    {
        return 1.0e30;
    }
    vec3 pos = s_instances[idx * 6u].xyz;
    vec3 d = u_eye - pos;
    return -dot(d, d);
}

void swap_instances(uint a, uint b)
{
    uint ba = a * 6u;
    uint bb = b * 6u;
    for(uint k = 0u; k < 6u; ++k)
    {
        vec4 tmp = s_instances[ba + k];
        s_instances[ba + k] = s_instances[bb + k];
        s_instances[bb + k] = tmp;
    }
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint j = i ^ u_pass;
    if(j < i || i >= u_padded || j >= u_padded)
    {
        return;
    }
    uint alive = s_counter[0];
    float ki = instance_sort_key(i, alive);
    float kj = instance_sort_key(j, alive);
    bool ascending = ((i & u_stage) == 0u);
    bool swap_needed = ascending ? (ki > kj) : (ki < kj);
    if(swap_needed)
    {
        swap_instances(i, j);
    }
}
