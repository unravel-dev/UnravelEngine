#include "../bgfx_compute.sh"

IMAGE3D_RW(i_opacity, rgba16f, 0);
uniform vec4 u_opacity_params0; // xyz unused, w = dim

NUM_THREADS(8, 8, 8)
void main()
{
    ivec3 coord = ivec3(gl_GlobalInvocationID.xyz);
    int dim = int(u_opacity_params0.w + 0.5);
    if(any(greaterThanEqual(coord, ivec3(dim, dim, dim))))
    {
        return;
    }
    imageStore(i_opacity, coord, vec4_splat(0.0));
}
