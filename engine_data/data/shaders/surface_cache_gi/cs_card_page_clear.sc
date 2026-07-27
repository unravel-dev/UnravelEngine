/*
 * Zero one atlas page (radiance + material + emissive).
 * Required on allocate/recycle — stale cyan pages caused knife-cut GI artifacts.
 */

#include "../bgfx_compute.sh"

IMAGE2D_RW(i_atlas, rgba16f, 0);
IMAGE2D_RW(i_material, rgba16f, 1);
IMAGE2D_RW(i_emissive, rgba16f, 2);

uniform vec4 u_fill_params0; // xy = page origin texels, z = page size

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    int page_size = int(u_fill_params0.z);
    if(any(greaterThanEqual(local, ivec2(page_size, page_size))))
    {
        return;
    }
    ivec2 coord = ivec2(int(u_fill_params0.x), int(u_fill_params0.y)) + local;
    imageStore(i_atlas, coord, vec4_splat(0.0));
    imageStore(i_material, coord, vec4_splat(0.0));
    imageStore(i_emissive, coord, vec4_splat(0.0));
}
