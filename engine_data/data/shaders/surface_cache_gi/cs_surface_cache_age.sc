/*
 * Amortized soft confidence decay for stale atlas pages.
 * Never wipe radiance RGB — that made look-away feel like screen-space GI reset.
 */

#include "../bgfx_compute.sh"

IMAGE2D_RW(i_atlas, rgba16f, 0);

uniform vec4 u_age_params0; // xy page0 origin, zw page1 origin (texels); -1 = unused
uniform vec4 u_age_params1; // xy page2 origin, zw page3 origin
uniform vec4 u_age_params2; // x = page size, y = decay, z = page count (1..4)

#define u_page_size u_age_params2.x
#define u_decay     u_age_params2.y
#define u_page_count u_age_params2.z

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(local, ivec2(int(u_page_size), int(u_page_size)))))
    {
        return;
    }
    int page_count = int(u_page_count);
    LOOP for(int p = 0; p < 4; ++p)
    {
        if(p >= page_count)
        {
            break;
        }
        vec2 origin;
        if(p == 0)
        {
            origin = u_age_params0.xy;
        }
        else if(p == 1)
        {
            origin = u_age_params0.zw;
        }
        else if(p == 2)
        {
            origin = u_age_params1.xy;
        }
        else
        {
            origin = u_age_params1.zw;
        }
        if(origin.x < 0.0)
        {
            continue;
        }
        ivec2 coord = ivec2(int(origin.x), int(origin.y)) + local;
        vec4 v = imageLoad(i_atlas, coord);
        // Soft confidence floor — keep world radiance across look-away / dolly.
        v.a = max(v.a * u_decay, 0.35);
        imageStore(i_atlas, coord, v);
    }
}
