/*
 * Advance particle life in a resident sim buffer (sparse slots).
 * Dead slots have lifespan <= 0 and are left untouched.
 */

#include <bgfx_compute.sh>

BUFFER_RW(s_sim, vec4, 0);

uniform vec4 u_sim0; // dt, capacity, pad, pad

#define u_dt        u_sim0.x
#define u_capacity  uint(u_sim0.y)

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if(i >= u_capacity)
    {
        return;
    }
    uint base = i * 5u;
    vec4 s0 = s_sim[base + 0u];
    vec4 s1 = s_sim[base + 1u];
    float life = s0.w;
    float lifespan = s1.w;
    if(lifespan <= 0.0)
    {
        return;
    }
    life += u_dt / max(lifespan, 1e-4);
    if(life > 1.0)
    {
        s1.w = 0.0;
        s0.w = 0.0;
        s_sim[base + 0u] = s0;
        s_sim[base + 1u] = s1;
        return;
    }
    s0.w = life;
    s_sim[base + 0u] = s0;
}
