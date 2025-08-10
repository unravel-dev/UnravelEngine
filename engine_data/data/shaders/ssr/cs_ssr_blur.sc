$input v_texcoord0

#include "../bgfx_compute.sh"
#include "../common.sh"

IMAGE2D_WO(i_output, rgba8, 0);
IMAGE2D_RO(i_input, rgba8, 1);
SAMPLER2D(s_normal, 2);  // G-buffer normal texture containing roughness

uniform vec4 u_blur_params; // x: mip_level, y: sigma, z: unused, w: unused

/*
 * Separable Gaussian Blur Compute Shader for SSR Cone Tracing
 * Generates pre-blurred color buffer with mip chain
 * Based on Will Pearce's cone tracing requirements
 */


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


// Separable Gaussian blur - vertical pass using image operations
vec3 GaussianBlurDirection(ivec2 coord, float sigma, int mipLevel, ivec2 dir)
{
    vec3 color = vec3_splat(0.0);
    float totalWeight = 0.0;
    
    // Calculate input coordinate from output coordinate
    // For mip 0: input and output are same size (1:1 mapping)
    // For mip > 0: output is half size of input (2:1 mapping)
    ivec2 inputCoord = (mipLevel == 0) ? coord : coord * 2;
    
    // Dynamic sigma adjustment for different mip levels
    float sigmaMult = max(1.0, sigma);
    
    for(int i = 0; i < KERNEL_SIZE; ++i)
    {
        ivec2 offset = ivec2(int(KERNEL_OFFSETS[i] * sigmaMult), int(KERNEL_OFFSETS[i] * sigmaMult));
        ivec2 sampleCoord = inputCoord + offset * dir;
        
        vec3 sampleColor = imageLoad(i_input, sampleCoord).rgb;
        float weight = KERNEL_WEIGHTS[i];
        
        color += sampleColor * weight;
        totalWeight += weight;
    }
    
    return color / totalWeight;
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(i_output);
    
    if(any(greaterThanEqual(coord, size)))
        return;
    
    int mipLevel = int(u_blur_params.x);
    float sigma = u_blur_params.y;
   

    // Use appropriate blur based on mip level
    // Even mip levels use horizontal blur, odd use vertical
    // This creates a separable blur effect when chained properly
	ivec2 dir;
	dir.x = int((mipLevel & 1) == 0);
	dir.y = int((mipLevel & 1) != 0);
	
	vec3 result = vec3_splat(0.0);
	if(mipLevel == 0)
	{
		result = imageLoad(i_input, coord).rgb;
	}
	else
	{
	    result = GaussianBlurDirection(coord, sigma, mipLevel, dir);
	}
	
    
    // Write to output
    imageStore(i_output, coord, vec4(result, 1.0));
} 