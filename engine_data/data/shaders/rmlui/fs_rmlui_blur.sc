$input v_texcoord0

#include "../common.sh"

#define BLUR_SIZE 7
#define BLUR_NUM_WEIGHTS ((BLUR_SIZE + 1) / 2)

SAMPLER2D(s_tex, 0);
uniform vec4 u_weights[BLUR_NUM_WEIGHTS];  // Blur weights
uniform vec4 u_texCoordMin;                // Minimum texture coordinates
uniform vec4 u_texCoordMax;                // Maximum texture coordinates
uniform vec4 u_texelOffset; // .xy = texel offset for blur direction

// Gaussian blur kernel size - must be #define for loop bounds
#define KERNEL_SIZE 7


#define WEIGHT0 0.1749379741597446
#define WEIGHT1 0.16556904917484133
#define WEIGHT2 0.14036678002195038
#define WEIGHT3 0.106595183723336
// Gaussian blur kernel weights
// Pre-calculated for performance
CONST_ARRAY_BEGIN(float, KERNEL_WEIGHTS, KERNEL_SIZE)
	WEIGHT3, WEIGHT2, WEIGHT1, WEIGHT0, WEIGHT1, WEIGHT2, WEIGHT3
ARRAY_END();

CONST_ARRAY_BEGIN(float, KERNEL_OFFSETS, KERNEL_SIZE)
    -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0
ARRAY_END();


void main()
{
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    
	
    // This shader expects the vertex shader to provide multiple texture coordinates
    // For now, we'll simulate the blur by sampling multiple times
    vec2 texelOffset = u_texelOffset.xy;
	
#if 1
    
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float offset = float(i - BLUR_NUM_WEIGHTS + 1);
        vec2 sampleCoord = v_texcoord0 - offset * texelOffset;     
        // Check if color_sample is within valid region
        vec2 in_region = step(u_texCoordMin.xy, sampleCoord) * step(sampleCoord, u_texCoordMax.xy);   
        vec4 color_sample = texture2D(s_tex, sampleCoord);
        float weight = u_weights[abs(i - BLUR_NUM_WEIGHTS + 1)].x;    
        color += color_sample * in_region.x * in_region.y * weight;
    }   
    gl_FragColor = color;
#else


    // Dynamic sigma adjustment for different mip levels
    float sigmaMult = 1.0f;
    float totalWeight = 0.0;
	for(int i = 0; i < KERNEL_SIZE; ++i)
    {
        vec2 offset = vec2(KERNEL_OFFSETS[i] * sigmaMult, KERNEL_OFFSETS[i] * sigmaMult);
        vec2 sampleCoord = v_texcoord0 + offset * texelOffset;
        
        vec4 sampleColor = texture2D(s_tex, sampleCoord);
        float weight = KERNEL_WEIGHTS[i];
        vec2 in_region = step(u_texCoordMin.xy, sampleCoord) * step(sampleCoord, u_texCoordMax.xy);   
        color += sampleColor * in_region.x * in_region.y * weight;
        totalWeight += weight;
    }
    
    gl_FragColor = color / totalWeight;
#endif
}
