/*
 * Scatter CPU-staged spawns into sparse resident sim slots.
 * s_spawn is dense (emit_count particles), s_slots[i] is the destination slot.
 */

#include <bgfx_compute.sh>

BUFFER_RO(s_spawn, vec4, 0);
BUFFER_RO(s_slots, uint, 1);
BUFFER_RW(s_sim, vec4, 2);

uniform vec4 u_spawn0; // count, pad, pad, pad

#define u_spawn_count uint(u_spawn0.x)

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if(i >= u_spawn_count)
    {
        return;
    }
    uint slot = s_slots[i];
    uint dst = slot * 5u;
    uint src = i * 5u;
    s_sim[dst + 0u] = s_spawn[src + 0u];
    s_sim[dst + 1u] = s_spawn[src + 1u];
    s_sim[dst + 2u] = s_spawn[src + 2u];
    s_sim[dst + 3u] = s_spawn[src + 3u];
    s_sim[dst + 4u] = s_spawn[src + 4u];
}
