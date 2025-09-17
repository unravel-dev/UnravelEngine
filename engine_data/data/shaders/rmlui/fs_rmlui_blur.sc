$input v_texcoord0

#include "../common.sh"

#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

SAMPLER2D(s_tex, 0);
uniform vec4 u_weights[BLUR_NUM_WEIGHTS];  // Blur weights
uniform vec4 u_texCoordMin;                // Minimum texture coordinates
uniform vec4 u_texCoordMax;                // Maximum texture coordinates
uniform vec4 u_texelOffset; // .xy = texel offset for blur direction

void main()
{
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    
    // This shader expects the vertex shader to provide multiple texture coordinates
    // For now, we'll simulate the blur by sampling multiple times
    vec2 texelOffset = vec2(1.0, 1.0) / textureSize(s_tex, 0);
    
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float offset = float(i - BLUR_NUM_WEIGHTS + 1);
        vec2 sampleCoord = v_texcoord0 + offset * texelOffset;
        
        // Check if sample is within valid region
        vec2 in_region = step(u_texCoordMin.xy, sampleCoord) * step(sampleCoord, u_texCoordMax.xy);
        
        vec4 sample = texture2D(s_tex, sampleCoord);
        float weight = u_weights[abs(i - BLUR_NUM_WEIGHTS + 1) / 4][abs(i - BLUR_NUM_WEIGHTS + 1) % 4];
        
        color += sample * in_region.x * in_region.y * weight;
    }
    
    gl_FragColor = color;
}
