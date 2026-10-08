/*
 * Hi-Z Buffer Downsampling Compute Shader
 * Reads from previous mip level and writes to current mip level
 * Input and output mip levels are set by the C++ code via gfx::set_image
 *
 * Each output texel takes the closest depth of every input texel its uv range overlaps. A tracer that maps a
 * screen uv to texel uv x textureSize(mip) (the Lumen screen traces) sees that range reach into a third input
 * texel along an odd input dimension; without it the coarse texel would miss geometry the ray then skips.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

// Input Hi-Z buffer (previous mip level - set via gfx::set_image with mip-1). Read-write although only read:
// bound read-only beside the write of another mip of the same texture, D3D12 would transition the whole
// resource between the two views; hiz_pass binds it bgfx::Access::ReadWrite.
IMAGE2D_RW(s_hiz_input, r32f, 0);

// Output Hi-Z buffer (current mip level - set via gfx::set_image with mip)
IMAGE2D_WO(s_hiz_output, r32f, 1);

// Uniforms for mip generation
// x: current_mip_width, y: current_mip_height, z: input_mip_width, w: input_mip_height
uniform vec4 u_hiz_params;

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    
    // Check bounds
    if (any(greaterThanEqual(coord, ivec2(u_hiz_params.xy))))
    {
        return;
    }
    
    // The input texels under this texel: 2 per axis, 3 along an odd input dimension
    ivec2 inputSize = ivec2(u_hiz_params.zw);
    ivec2 lastInput = inputSize - ivec2(1, 1);
    ivec2 footprint = ivec2(2 + (inputSize.x & 1), 2 + (inputSize.y & 1));
    ivec2 inputCoord = coord * 2;
    
    // Find minimum depth for conservative occlusion testing (standard depth: 0.0 = near, 1.0 = far)
    float minDepth = 1.0;
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            if (x < footprint.x && y < footprint.y)
            {
                ivec2 texel = min(inputCoord + ivec2(x, y), lastInput);
                minDepth = min(minDepth, imageLoad(s_hiz_input, texel).r);
            }
        }
    }
    
    // Store result
    imageStore(s_hiz_output, coord, vec4(minDepth, 0.0, 0.0, 1.0));
} 