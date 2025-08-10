/*
 * Hi-Z Buffer Generation Compute Shader
 * Converts depth buffer to Hi-Z buffer mip 0
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

// Input depth texture (G-buffer depth)
SAMPLER2D(s_depth, 0);

// Output Hi-Z buffer mip 0
IMAGE2D_WO(s_hiz_output, r32f, 1);

// Uniforms for texture dimensions
uniform vec4 u_hiz_params; // x: output width, y: output height, z: unused, w: unused

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    
    // Check bounds
    if (any(greaterThanEqual(coord, ivec2(u_hiz_params.xy))))
    {
        return;
    }
    
    // Calculate UV coordinates for sampling the depth buffer
    vec2 uv = (vec2(coord) + 0.5) / u_hiz_params.xy;
    
    // Sample and decode depth from G-buffer
    GBufferDataDepth depthData = DecodeGBufferDepthLod(uv, s_depth, 0.0);
    
    // Store linear depth in Hi-Z buffer
    imageStore(s_hiz_output, coord, vec4(depthData.depth01, 0.0, 0.0, 1.0));
} 