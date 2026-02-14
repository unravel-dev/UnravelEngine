/*
 * Color Mip Downsample Compute Shader
 * Generates mip chain for 2D RGBA textures (e.g. cubemap faces).
 * D3D12 does not implement GenerateMips for render targets, so we use compute.
 * Reads from previous mip level and writes to current mip level.
 * Input and output mip levels are set by the C++ code via gfx::set_image
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

// Input texture (previous mip level - set via gfx::set_image with mip-1)
IMAGE2D_RO(s_input, rgba8, 0);

// Output texture (current mip level - set via gfx::set_image with mip)
IMAGE2D_WO(s_output, rgba8, 1);

// Uniforms: x=width, y=height
uniform vec4 u_params;

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(u_params.xy);
    if (any(greaterThanEqual(coord, size)))
    {
        return;
    }
    ivec2 inputCoord = coord * 2;
    vec4 c0 = imageLoad(s_input, inputCoord + ivec2(0, 0));
    vec4 c1 = imageLoad(s_input, inputCoord + ivec2(1, 0));
    vec4 c2 = imageLoad(s_input, inputCoord + ivec2(0, 1));
    vec4 c3 = imageLoad(s_input, inputCoord + ivec2(1, 1));
    vec4 result = (c0 + c1 + c2 + c3) * 0.25;
    imageStore(s_output, coord, result);
}
