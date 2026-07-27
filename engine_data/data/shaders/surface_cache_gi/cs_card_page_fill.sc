/*
 * Fill one atlas page with constant albedo + emissive (mesh material seed).
 * Runs without G-buffer so cards light correctly off-screen.
 */

#include "../bgfx_compute.sh"

IMAGE2D_RW(i_material, rgba16f, 0);
IMAGE2D_RW(i_emissive, rgba16f, 1);

uniform vec4 u_fill_params0; // xy = page origin texels, z = page size
uniform vec4 u_fill_albedo;  // rgb + coverage
uniform vec4 u_fill_emissive; // rgb + unused

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
    imageStore(i_material, coord, u_fill_albedo);
    imageStore(i_emissive, coord, vec4(u_fill_emissive.rgb, 1.0));
}
