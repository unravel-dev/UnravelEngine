/*
 * Hi-Z Buffer Downsampling Compute Shader
 * Reads from previous mip level and writes to current mip level
 * Input and output mip levels are set by the C++ code via gfx::set_image
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

// Input Hi-Z buffer (previous mip level - set via gfx::set_image with mip-1)
IMAGE2D_RO(s_hiz_input, r32f, 0);

// Output Hi-Z buffer (current mip level - set via gfx::set_image with mip)
IMAGE2D_WO(s_hiz_output, r32f, 1);

// Uniforms for mip generation
uniform vec4 u_hiz_params; // x: current_mip_width, y: current_mip_height, z: unused, w: current_mip_level

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    
    // Check bounds
    if (any(greaterThanEqual(coord, ivec2(u_hiz_params.xy))))
    {
        return;
    }
    
    // Calculate coordinates in the input (previous mip level)
    // Each output texel corresponds to a 2x2 block in the input
    ivec2 inputCoord = coord * 2;
    
    // Sample 4 depth values from the 2x2 block
    float depth0 = imageLoad(s_hiz_input, inputCoord + ivec2(0, 0)).r;
    float depth1 = imageLoad(s_hiz_input, inputCoord + ivec2(1, 0)).r;
    float depth2 = imageLoad(s_hiz_input, inputCoord + ivec2(0, 1)).r;
    float depth3 = imageLoad(s_hiz_input, inputCoord + ivec2(1, 1)).r;
    
    // Find minimum depth for conservative occlusion testing (standard depth: 0.0 = near, 1.0 = far)
    float minDepth = min(min(depth0, depth1), min(depth2, depth3));
    
    // Store result
    imageStore(s_hiz_output, coord, vec4(minDepth, 0.0, 0.0, 1.0));
} 