/*
 * Bitonic sort of key/index pairs (not fat instance rows).
 * Ascending on float keys: farthest particles first when keys are -dist_sq.
 */

#include <bgfx_compute.sh>

// Raw: the keys live in a one-float-per-vertex buffer, and bgfx always views a vertex
// buffer as vec4 when it is bound typed.
BUFFER_RAW_RW(s_keys, 0);
BUFFER_RW(s_indices, uint, 1);

uniform vec4 u_sort0;
uniform vec4 u_sort1;

#define u_stage  uint(u_sort0.w)
#define u_pass   uint(u_sort1.x)
#define u_padded uint(u_sort1.y)

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    uint j = i ^ u_pass;
    if(j < i || i >= u_padded || j >= u_padded)
    {
        return;
    }
    float ki = rawLoadFloat(s_keys, i);
    float kj = rawLoadFloat(s_keys, j);
    bool ascending = ((i & u_stage) == 0u);
    bool swap_needed = ascending ? (ki > kj) : (ki < kj);
    if(swap_needed)
    {
        rawStoreFloat(s_keys, i, kj);
        rawStoreFloat(s_keys, j, ki);
        uint ti = s_indices[i];
        s_indices[i] = s_indices[j];
        s_indices[j] = ti;
    }
}
