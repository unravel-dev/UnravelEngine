$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_color, 0); // Input color (G-buffer or previous frame)
SAMPLER2D(s_normal, 1);    // Normal buffer
SAMPLER2D(s_depth, 2);     // Depth buffer
SAMPLER2D(s_hiz, 3);       // Hi-Z depth hierarchy
SAMPLER2D(s_color_blurred, 4); // Pre-blurred color buffer with mip chain for cone tracing


uniform vec4 u_ssr_params; // x: max_steps, y: depth_tolerance, z: max_rays, w: brightness
#define u_max_steps         u_ssr_params.x
#define u_depth_tolerance   u_ssr_params.y
#define u_max_rays          u_ssr_params.z
#define u_brightness        u_ssr_params.w

uniform vec4 u_hiz_params; // x: buffer_width, y: buffer_height, z: num_depth_mips, w: ssr_resolution_scale
#define u_hiz_width         u_hiz_params.x
#define u_hiz_height        u_hiz_params.y
#define u_hiz_num_mips      u_hiz_params.z
#define u_ssr_resolution_scale u_hiz_params.w

uniform vec4 u_fade_params; // x: fade_in_start, y: fade_in_end, z: roughness_depth_tolerance, w: facing_reflections_fading
#define u_fade_in_start     u_fade_params.x
#define u_fade_in_end       u_fade_params.y
#define u_roughness_depth_tolerance u_fade_params.z
#define u_facing_reflections_fading u_fade_params.w

uniform vec4 u_cone_params; // x: cone_angle_bias, y: max_mip_level, z: frame_index, w: enable_cone_tracing
#define u_cone_angle_bias   u_cone_params.x
#define u_max_mip_level     u_cone_params.y
#define u_frame_index_mod       u_cone_params.z
#define u_enable_cone_tracing     u_cone_params.w


// Tonemapping parameters removed - now sampling directly from HDR buffer


uniform mat4 u_prev_view_proj; // Previous frame view-projection matrix


// Constants
#define FFX_SSSR_FLOAT_MAX 3.402823466e+38
#define BASE_LOD           0
#define HAMMERSLEY_SAMPLES 16
#define HAMMERSLEY_TYPE 1
#define MAX_ROUGHNESS 0.6

/*
 * FidelityFX-inspired SSR Implementation with Cone Tracing
 * Based on AMD's Stochastic Screen-Space Reflections and Will Pearce's Cone Tracing
 * 
 * CONE TRACING INTEGRATION:
 * This implementation uses Will Pearce's cone tracing algorithm for glossy reflections.
 * Based on "Screen Space Glossy Reflections" article using isosceles triangles to approximate cones.
 * 
 * Required Pipeline Changes:
 * 1. Generate blurred color buffer with mip chain using cs_ssr_blur.sc
 *    - Create multiple mip levels with increasing blur (sigma = mip_level * base_sigma)
 *    - Use separable Gaussian blur for efficiency
 *    - Store in s_color_blurred sampler
 * 
 * 2. Configure cone tracing parameters in u_cone_params:
 *    - x: cone_angle_bias (0.01 - 0.05, controls cone angle: roughness * PI * bias)
 *    - y: max_mip_level (number of blur mip levels - 1, typically 6)
 *    - z: unused (reserved for future use)
 *    - w: unused (reserved for future use)
 * 
 * Key Features:
 * - Smooth surfaces (roughness < 0.1): Use base mip level for sharp reflections
 * - Rough surfaces: Use isosceles triangle cone tracing with multiple samples along the cone
 * - Iterative sampling with visibility accumulation and early termination
 * - Proper mip level selection based on projected cone footprint
 * 
 * References:
 * - https://github.com/GPUOpen-Effects/FidelityFX-SSSR
 * - GPU Pro 5 book chapter 4 by Yasin Uludag
 * - Will Pearce's blog http://roar11.com/2015/07/screen-space-glossy-reflections/
 */
 

float GetRoughnessFade(float roughness)
{
    return MAX_ROUGHNESS - min(roughness, MAX_ROUGHNESS);
}

vec3 FFX_SSSR_ComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

vec2 GetDepthMipResolution(int mipLevel)
{
    // Return SSR target resolution (may be different from depth buffer resolution)
    vec2 full_res = textureSize(s_hiz, mipLevel);
    return full_res;
}

float FetchDepth(vec2 coords, int mipLevel)
{
    return texelFetch(s_hiz, ivec2(coords), mipLevel).r;
}

// Temporal reprojection functions
vec2 WorldToScreenPrevious(vec3 ws_pos)
{
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

// Function to compute previous frame UV coordinates for temporal reprojection
vec2 ComputePreviousFrameUV(vec2 uv, float z)
{
    // Reconstruct world position from current UV and depth
    vec3 vs_pos = FFX_SSSR_ComputeViewspacePosition(uv, z);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));

    return WorldToScreenPrevious(ws_pos.xyz);
}

// Enhanced function that samples reflection color using temporal reprojection and calculates motion-aware edge fade
// Returns: xyz = reflection color, w = motion-aware edge fade multiplier
vec4 SampleScreenColor(vec2 uv, float z, sampler2D colorSampler, float mipLevel)
{
    // Use proper temporal reprojection to get previous frame UV coordinates (calculated once)
    vec2 prev_uv = ComputePreviousFrameUV(uv, z);
	
	BRANCH
    if(any(lessThan(prev_uv.xy, vec2_splat(0.0))) || any(greaterThan(prev_uv.xy, vec2_splat(1.0))))
        prev_uv = uv;

    vec3 prev_color = texture2DLod(colorSampler, prev_uv, mipLevel).rgb;

    // Calculate motion-aware edge fade using the same prev_uv
    vec2 prev_edge_distance = min(prev_uv, 1.0 - prev_uv);
    float prev_edge_fade = saturate(min(prev_edge_distance.x, prev_edge_distance.y) * 100.0);

    // Apply motion-aware edge fade
    float motion_aware_edge_fade = prev_edge_fade;

    return vec4(prev_color, motion_aware_edge_fade);
}

// Cone tracing implementation based on Will Pearce's article
// Uses isosceles triangle to approximate cone in screen space

float IsoscelesTriangleOpposite(float adjacentLength, float coneTheta)
{
    return 2.0 * tan(coneTheta) * adjacentLength;
}

float IsoscelesTriangleInRadius(float a, float h)
{
    float a2 = a * a;
    float fh2 = 4.0 * h * h;
    return (a * (sqrt(a2 + fh2) - a)) / (4.0 * h);
}

float IsoscelesTriangleNextAdjacent(float adjacentLength, float incircleRadius)
{
    // Subtract the diameter of the incircle to get the adjacent side of the next level on the cone
    return adjacentLength - (incircleRadius * 2.0);
}

vec4 ConeSampleWeightedColor(vec2 samplePos, float sampleZ, float mipLevel)
{
    vec4 sampleColor = SampleScreenColor(samplePos, sampleZ, s_color_blurred, mipLevel);
    
    // Enhanced visibility calculation
    // Could be improved with actual visibility buffer or depth-based occlusion
    float visibility = 1.0;
    
    // Check if sample is within screen bounds
    if(any(lessThan(samplePos, vec2_splat(0.0))) || any(greaterThan(samplePos, vec2_splat(1.0))))
    {
        visibility = 0.0;
    }
    
    return vec4(sampleColor.rgb * visibility, visibility);
}

// Enhanced cone sampling with multiple points within inscribed circle
// Based on Will Pearce's suggestion for higher quality
vec4 ConeSampleMultiplePoints(vec2 centerPos, float centerZ, float incircleRadius, float mipLevel)
{
    vec4 result = vec4_splat(0.0);
    float totalWeight = 0.0;
    
    // Sample center point
    vec4 centerSample = ConeSampleWeightedColor(centerPos, centerZ, mipLevel);
    result += centerSample;
    totalWeight += 1.0;
    
    // Sample additional points within inscribed circle for higher quality
    // This is optional and can be controlled via settings
    if(incircleRadius > 0.002) // Only for larger circles
    {
        const int numExtraSamples = 4;
        const float angleStep = 2.0 * PI / float(numExtraSamples);
        
        for(int i = 0; i < numExtraSamples; ++i)
        {
            float angle = float(i) * angleStep;
            vec2 offset = vec2(cos(angle), sin(angle)) * incircleRadius * 0.5;
            vec2 samplePos = centerPos + offset;
            
            vec4 sampleColor = ConeSampleWeightedColor(samplePos, centerZ, mipLevel);
            float weight = 0.5; // Reduced weight for edge samples
            
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }
    
    return totalWeight > 0.0 ? result / totalWeight : result;
}

// Main cone tracing function based on Will Pearce's algorithm
vec4 ConeTracing(float roughness, vec3 ss_ray_origin, vec3 ss_hit_pos)
{
    // Cone angle based on roughness
    float coneTheta = roughness * PI * u_cone_angle_bias; // cone_angle_bias controls cone growth
    // Use SSR target resolution for cone calculations
    vec2 res = GetDepthMipResolution(0);
    
    // Cone tracing using an isosceles triangle to approximate a cone in screen space
    vec3 deltaPos = ss_hit_pos - ss_ray_origin;
    
    float adjacentLength = length(deltaPos.xy);
    
    // Early exit if ray didn't travel far enough
    BRANCH
    if(adjacentLength < 0.001)
    {
        return texture2DLod(s_color_blurred, ss_hit_pos.xy, 0.0);
    }
    
    vec3 adjacentUnit = normalize(deltaPos);
    
    vec4 reflectionColor = vec4_splat(0.0);
    vec3 samplePos;
    
    // Roughness-based sample count optimization
    // Smooth surfaces need fewer samples, rough surfaces need more
    int maxSamples = int(u_max_mip_level) + 1; // Based on max_mip_level
    if(roughness < 0.1)
    {
        maxSamples = min(maxSamples, 1); // Fewer samples for smooth surfaces
    }
    else if(roughness > 0.5)
    {
        maxSamples = int(u_max_mip_level) + 1; // Full samples for rough surfaces
    }
    else
    {
        maxSamples = int(mix(3.0, float(int(u_max_mip_level) + 1), (roughness - 0.1) / 0.4));
    }
    
    float totalWeight = 0.0;
    int i = 0;
    
    LOOP for(; i < maxSamples; ++i)
    {
        // Intersection length is the adjacent side, get the opposite side using trig
        float oppositeLength = IsoscelesTriangleOpposite(adjacentLength, coneTheta);
        
        // Calculate in-radius of the isosceles triangle
        float incircleSize = IsoscelesTriangleInRadius(oppositeLength, adjacentLength);
        
        // Get the sample position in screen space
        samplePos = ss_ray_origin + adjacentUnit * (adjacentLength - incircleSize);
        
        // Convert the in-radius into screen size then check what power N to raise 2 to reach it
        // That power N becomes mip level to sample from
        float mipChannel = clamp(log2(incircleSize * max(res.x, res.y)), 0.0, u_max_mip_level);
        
        // Sample color at this position using enhanced multi-point sampling
        vec4 newColor = ConeSampleMultiplePoints(samplePos.xy, samplePos.z, incircleSize, mipChannel);
        
        // Proper visibility-based accumulation (Will Pearce's method)
        // Weight based on distance from cone center and visibility
        float distanceWeight = 1.0 - float(i) / float(maxSamples);
        float sampleWeight = newColor.a * distanceWeight;
        
        // Accumulate color with proper alpha blending
        reflectionColor.rgb += newColor.rgb * sampleWeight;
        reflectionColor.a += sampleWeight;
        totalWeight += sampleWeight;
        
        // Early termination when we have enough visibility
        BRANCH
        if(reflectionColor.a >= 0.95)
        {
            //break;
        }
        
        adjacentLength = IsoscelesTriangleNextAdjacent(adjacentLength, incircleSize);
        
        // Break if we've traveled too far
        BRANCH
        if(adjacentLength <= 0.0)
        {
            break;
        }
    }
    
    // Normalize by total weight for proper averaging
    if(totalWeight > 0.0)
    {
        reflectionColor.rgb /= totalWeight;
        reflectionColor.a = clamp(reflectionColor.a, 0.0, 1.0);
    }
    else
    {
        reflectionColor = vec4_splat(0.0);
    }

    return reflectionColor;
}


// Initial ray advance to avoid self-intersection
void InitialAdvanceRay(vec3 ss_ray_origin,
                       vec3 ss_ray_dir,
                       vec3 ss_ray_dir_inv,
                       vec2 curr_mip_resolution,
                       vec2 curr_mip_resolution_inv,
                       vec2 floor_offset,
                       vec2 uv_offset,
                       out vec3 ss_pos,
                       out float curr_t)
{
    vec2 curr_mip_pos = curr_mip_resolution * ss_ray_origin.xy;

    // Intersect ray with the half box that is pointing away from the ray ss_ray_origin
    vec2 xy_plane = floor(curr_mip_pos) + floor_offset;
    xy_plane = xy_plane * curr_mip_resolution_inv + uv_offset;

    // o + d * t = p' => t = (p' - o) / d
    vec2 t = xy_plane * ss_ray_dir_inv.xy - ss_ray_origin.xy * ss_ray_dir_inv.xy;
    curr_t = min(t.x, t.y);
    ss_pos = ss_ray_origin + curr_t * ss_ray_dir;
}

// Main ray advancement with depth testing
bool AdvanceRay(vec3 ss_ray_origin,
                vec3 ss_ray_dir,
                vec3 ss_ray_dir_inv,
                vec2 curr_mip_pos,
                vec2 curr_mip_resolution_inv,
                vec2 floor_offse,
                vec2 uv_offset,
                float surface_z,
                inout vec3 ss_pos,
                inout float curr_t)
{
    // Create boundary planes
    vec2 xy_plane = floor(curr_mip_pos) + floor_offse;
    xy_plane = xy_plane * curr_mip_resolution_inv + uv_offset;
    vec3 boundary_planes = vec3(xy_plane, surface_z);

    // Intersect ray with boundaries
    vec3 t = boundary_planes * ss_ray_dir_inv - ss_ray_origin * ss_ray_dir_inv;

// Prevent using z plane when shooting out of the depth buffer
#ifdef INVERTED_DEPTH_RANGE
    t.z = ss_ray_dir.z < 0.0 ? t.z : FFX_SSSR_FLOAT_MAX;
#else
    t.z = ss_ray_dir.z > 0.0 ? t.z : FFX_SSSR_FLOAT_MAX;
#endif

    // Choose nearest intersection
    float t_min = min(min(t.x, t.y), t.z);

#ifdef INVERTED_DEPTH_RANGE
    bool above_surface = surface_z < ss_pos.z;
#else
    bool above_surface = surface_z > ss_pos.z;
#endif

    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the
    // surface. We use the asuint comparison to avoid NaN / Inf logic, also we actually care about bitwise equality here
    // to see if t_min is the t.z we fed into the min3 above.
    bool skipped_tile = floatBitsToUint(t_min) != floatBitsToUint(t.z) && above_surface;

    // Make sure to only advance the ray if we're still above the surface.
    curr_t = above_surface ? t_min : curr_t;

    // Advance ray
    ss_pos = ss_ray_origin + curr_t * ss_ray_dir;

    return skipped_tile;
}

// Main hierarchical ray marching function
bool HierarchicalRaymarch(vec3 ss_ray_origin,
                          vec3 ss_ray_dir,
                          vec2 screen_size,
                          int most_detailed_mip,
                          int max_iterations,
                          inout vec3 ss_hit_pos)
{
    // Compute inverse direction, handling division by zero per component
    vec3 ss_ray_dir_inv;
    ss_ray_dir_inv.x = (ss_ray_dir.x != 0.0) ? rcp(ss_ray_dir.x) : FFX_SSSR_FLOAT_MAX;
    ss_ray_dir_inv.y = (ss_ray_dir.y != 0.0) ? rcp(ss_ray_dir.y) : FFX_SSSR_FLOAT_MAX;
    ss_ray_dir_inv.z = (ss_ray_dir.z != 0.0) ? rcp(ss_ray_dir.z) : FFX_SSSR_FLOAT_MAX;

    // Start on most detailed mip
    int curr_mip = most_detailed_mip;
    vec2 curr_mip_resolution = GetDepthMipResolution(curr_mip);
    vec2 curr_mip_resolution_inv = rcp(curr_mip_resolution);

    // UV offset to intersect with center of next pixel
    vec2 uv_offset = 0.005 * exp2(most_detailed_mip) / (screen_size);
    uv_offset.x = ss_ray_dir.x < 0.0 ? -uv_offset.x : uv_offset.x;
    uv_offset.y = ss_ray_dir.y < 0.0 ? -uv_offset.y : uv_offset.y;

    // Floor offset for ray direction
    vec2 floor_offset;
    floor_offset.x = (ss_ray_dir.x < 0.0) ? 0.0 : 1.0;
    floor_offset.y = (ss_ray_dir.y < 0.0) ? 0.0 : 1.0;

    float curr_t;
    // Initial advance to avoid self-intersection
    InitialAdvanceRay(ss_ray_origin,
                      ss_ray_dir,
                      ss_ray_dir_inv,
                      curr_mip_resolution,
                      curr_mip_resolution_inv,
                      floor_offset,
                      uv_offset,
                      ss_hit_pos,
                      curr_t);
	
    int i = 0;
    LOOP while(i < max_iterations && curr_mip >= most_detailed_mip)
    {
        vec2 curr_mip_pos = curr_mip_resolution * ss_hit_pos.xy;
        float surface_z = FetchDepth(curr_mip_pos, curr_mip);
        bool skipped_tile = AdvanceRay(ss_ray_origin,
                                       ss_ray_dir,
                                       ss_ray_dir_inv,
                                       curr_mip_pos,
                                       curr_mip_resolution_inv,
                                       floor_offset,
                                       uv_offset,
                                       surface_z,
                                       ss_hit_pos,
                                       curr_t);

        // Adjust mip level based on whether we hit or skipped
        curr_mip += skipped_tile ? 1 : -1;

        // Alternative: Keep original scaling (comment above and uncomment below)
        curr_mip_resolution *= skipped_tile ? 0.5 : 2.0;
        curr_mip_resolution_inv *= skipped_tile ? 2.0 : 0.5;

        i++;
    }

    bool valid_hit = i < max_iterations;
    return valid_hit;
}

// Hit validation with depth tolerance and screen bounds
float ValidateHit(vec3 ss_hit_pos, vec2 uv, vec3 vs_ray_origin, float roughness, vec2 screen_size)
{
    // Reject hits outside view frustum
    BRANCH
    if(any(lessThan(ss_hit_pos.xy, vec2_splat(0.0))) || any(greaterThan(ss_hit_pos.xy, vec2_splat(1.0))))
        return 0.0;
	
    //vec2 ge0 = step(vec2_splat(0.0), ss_hit_pos.xy);
    //vec2 le1 = step(-vec2_splat(1.0), -ss_hit_pos.xy);
    //float frustum_valid = ge0.x * ge0.y * le1.x * le1.y;

    // Avoid self intersection
    // Reject if ray didn't advance significantly (account for SSR resolution scaling)
    vec2 manhattan_dist = abs(ss_hit_pos.xy - uv);
    vec2 inv_screen_size = rcp(screen_size); // Use full resolution for proper distance calculation
	
    BRANCH
    if(all(lessThan(manhattan_dist, inv_screen_size * 0.5)))
        return 0.0;
		
    //vec2 less_than_half = step(manhattan_dist, inv_screen_size * 0.5);
    //float both_less = less_than_half.x * less_than_half.y;
    //float dist_valid = 1.0 - both_less;

    // Don't sample from background
    float surface_z = FetchDepth(screen_size * ss_hit_pos.xy, BASE_LOD);

    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(surface_z == 0.0)
        return 0.0;
#else
    if(surface_z == 1.0)
        return 0.0;
#endif


    vec3 vs_hit_pos = FFX_SSSR_ComputeViewspacePosition(ss_hit_pos.xy, ss_hit_pos.z);
    vec3 vs_ray_dir = vs_hit_pos - vs_ray_origin;

    // Sample G-buffers using ACE decode functions (scale to full resolution)
    vec2 full_res_uv = ss_hit_pos.xy; // UV coordinates are already normalized (0-1)
    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(full_res_uv, s_normal);

    // Avoid hitting from the back
    vec3 vs_normal = mul(u_view, vec4(normal_data.world_normal, 0.0)).xyz;
    float dot_prod = dot(vs_ray_dir, vs_normal);
	
    if(dot_prod > 0)
    {
        return 0.0;
    }
    //float backface_valid = 1.0 - step(1e-6, dot_prod);

    // Compute depth difference
    vec3 vs_hit_surface = FFX_SSSR_ComputeViewspacePosition(ss_hit_pos.xy, surface_z);
    float dist = length(vs_hit_pos - vs_hit_surface);

    // Depth tolerance with roughness adjustment
    float depth_tolerance = u_depth_tolerance + mix(0.0, u_roughness_depth_tolerance, roughness);
    float confidence = 1.0 - smoothstep(0.0, depth_tolerance, dist);
    confidence *= 10.0;

    // Fade based on screen edge
    vec2 fade_in = vec2(u_fade_in_start, u_fade_in_end);
    vec2 border = smoothstep(vec2_splat(0.0), fade_in, ss_hit_pos.xy) *
                  (1.0 - smoothstep(1.0 - fade_in, vec2_splat(1.0), ss_hit_pos.xy));

    float edge_fade = border.x * border.y;

    // Fade camera-facing reflections
    float mirror_fade = clamp(max(dot(vs_ray_origin, vs_ray_dir), 0.0) + u_facing_reflections_fading, 0.0, 1.0);

    // Fade based on roughness
    float roughness_fade = GetRoughnessFade(roughness);

    //confidence *= frustum_valid * dist_valid * backface_valid;

    return clamp(confidence * mirror_fade * edge_fade * roughness_fade, 0.0, 1.0);
}

// Enhanced importance sampling and BRDF functions
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 0.00000000023283064365386963;
}

uint RadicalInverse(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return bits;
}

vec2 Hammersley(int i, int N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(uint(i)));
}

vec2 Hammersley16( uint Index, uint NumSamples, uvec2 Random )
{
	float E1 = fract( float(Index) / float(NumSamples) + float( Random.x ) * (1.0 / 65536.0) );
	float E2 = float( ( RadicalInverse(Index) >> 16 ) ^ Random.y ) * (1.0 / 65536.0);
	return vec2( E1, E2 );
}


// 3D random number generator inspired by PCGs (permuted congruential generator)
// Using a **simple** Feistel cipher in place of the usual xor shift permutation step
// @param v = 3D integer coordinate
// @return three elements w/ 16 random bits each (0-0xffff).
// ~8 ALU operations for result.x    (7 mad, 1 >>)
// ~10 ALU operations for result.xy  (8 mad, 2 >>)
// ~12 ALU operations for result.xyz (9 mad, 3 >>)
uvec3 Rand3DPCG16(ivec3 p)
{
	// taking a signed int then reinterpreting as unsigned gives good behavior for negatives
	uvec3 v = uvec3(p);

	// Linear congruential step. These LCG constants are from Numerical Recipies
	// For additional #'s, PCG would do multiple LCG steps and scramble each on output
	// So v here is the RNG state
	v = v * 1664525u + 1013904223u;

	// PCG uses xorshift for the final shuffle, but it is expensive (and cheap
	// versions of xorshift have visible artifacts). Instead, use simple MAD Feistel steps
	//
	// Feistel ciphers divide the state into separate parts (usually by bits)
	// then apply a series of permutation steps one part at a time. The permutations
	// use a reversible operation (usually ^) to part being updated with the result of
	// a permutation function on the other parts and the key.
	//
	// In this case, I'm using v.x, v.y and v.z as the parts, using + instead of ^ for
	// the combination function, and just multiplying the other two parts (no key) for 
	// the permutation function.
	//
	// That gives a simple mad per round.
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;
	v.x += v.y*v.z;
	v.y += v.z*v.x;
	v.z += v.x*v.y;

	// only top 16 bits are well shuffled
	return v >> 16u;
}

float InterleavedGradientNoise( vec2 uv, float FrameId )
{
	// magic values are found by experimentation
	uv += FrameId * (vec2(47, 17) * 0.695);

    const vec3 magic = vec3( 0.06711056, 0.00583715, 52.9829189 );
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

vec3 ImportanceSampleGGX(vec2 E, vec3 N, float a2)
{
    float phi = 2.0 * PI * E.x;
    float cosT = sqrt((1.0 - E.y) / (1.0 + (a2 - 1.0) * E.y));
    float sinT = sqrt(1.0 - cosT * cosT);
    vec3 H;
    H.x = cos(phi) * sinT;
    H.y = sin(phi) * sinT;
    H.z = cosT;
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

// Enhanced ray direction generation for rough surfaces
vec3 GenerateReflectionRay(vec3 V, vec3 N, float roughness, vec2 texCoord, uint i, uint num_rays, int frame_index)
{
#if HAMMERSLEY_TYPE > 0
    BRANCH
    if(num_rays == 1)
    {
        return reflect(-V, N); // Perfect reflection for smooth surfaces
    }

	roughness = mix(0.0, MAX_ROUGHNESS, roughness);
	
#if HAMMERSLEY_TYPE == 1
	vec2 Noise;
	vec2 scaled_texcoord = texCoord * u_viewRect.zw;
	Noise.x = InterleavedGradientNoise( scaled_texcoord, float(frame_index) );
	Noise.y = InterleavedGradientNoise( scaled_texcoord, float(frame_index * 117) );
	uvec2 Random = Rand3DPCG16( ivec3( scaled_texcoord, frame_index ) ).xy;
	vec2 E = Hammersley16( i, num_rays, Random );
#else
    // Use temporal jitter for importance sampling
    int sampleIndex = (frame_index + i + int(texCoord.x * 1024.0) + int(texCoord.y * 1024.0)) % HAMMERSLEY_SAMPLES;
    vec2 E = Hammersley(sampleIndex, HAMMERSLEY_SAMPLES);
    // Add some spatial variation to reduce banding (scale for resolution)
    vec2 spatialJitter = fract(texCoord * 543.2103);
    E = fract(E + spatialJitter);
#endif
    float a = roughness * roughness;
    float a2 = a * a;

    vec3 H = ImportanceSampleGGX(E, N, a2);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);

    // Fallback to perfect reflection if importance sample is invalid
    float NoL = dot(N, L);

    BRANCH
    if(NoL <= 0.0)
    {
        return reflect(-V, N);
    }

    return L;
	
#else
	return reflect(-V, N);

#endif
}


vec3 ProjectVsDirToSsDir(vec3 vs_pos, vec3 vs_dir, vec3 ss_origin)
{
    vec3 end = vs_pos + vs_dir;
    vec4 ss_pj4 = mul(u_proj, vec4(end, 1.0));
    vec3 ss_pj = ss_pj4.xyz / ss_pj4.w;
    ss_pj = clipTransform(ss_pj);
    ss_pj.xy = ss_pj.xy * 0.5 + 0.5;

    // Convert projected depth to texture space to match ss_origin depth space
#if BGFX_SHADER_LANGUAGE_GLSL
    ss_pj.z = ss_pj.z * 0.5 + 0.5; // Convert clip space (-1,1) to texture space (0,1)
#endif

    vec3 result = ss_pj - ss_origin;
    return result;
}

void main()
{
    vec2 uv = v_texcoord0;
    // Sample G-buffers using ACE decode functions at full resolution
    // UV coordinates are normalized (0-1) so they work for both resolutions
    GBufferDataNormalMetalRoughness normalData = DecodeGBufferNormalMetalRoughness(uv, s_normal);

    // Sample material properties
    float metallic = normalData.metalness;
    float roughness = normalData.roughness;
    float roughnessFade = GetRoughnessFade(roughness);

    // Early out if surface is too rough
    BRANCH
    if(roughnessFade <= 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Get base depth resolution (this now returns SSR target resolution)
    vec2 base_depth_resolution = GetDepthMipResolution(BASE_LOD);
    float surface_z = FetchDepth(base_depth_resolution * uv, BASE_LOD);

    // Get view space normal and position
    vec3 vs_normal = mul(u_view, vec4(normalData.world_normal, 0.0)).xyz;

    // ss: screen space, vs: view space, ws: world space
    vec3 ss_ray_origin = vec3(uv, surface_z);
    vec3 vs_ray_origin = FFX_SSSR_ComputeViewspacePosition(ss_ray_origin.xy, ss_ray_origin.z);
    vec3 vs_ray_dir = normalize(vs_ray_origin);

    uint num_rays = 1;
#if HAMMERSLEY_TYPE > 0
	num_rays = uint(u_max_rays);

//    // Adaptive ray count: smooth surfaces get more rays, rough surfaces get fewer
//    float ray_multiplier = 1.0 - smoothstep(0.1, MAX_ROUGHNESS, roughness);
//    num_rays = uint(mix(1.0, float(uint(u_ssr_params.z)), ray_multiplier));
//    num_rays = max(num_rays, 1); // Ensure at least 1 ray
#endif

	int frame_number = int(u_frame_index_mod);
	
	bool cone_tracing_enabled = u_enable_cone_tracing > 0.5;
    vec4 output_color = vec4_splat(0.0);
    float total_weight = 0.0;
	
	// Adaptive max iterations based on roughness and quality needs
	int max_iterations = int(u_max_steps);
    int adaptive_max_iterations = int(mix(8.0, float(max_iterations), 
                                         1.0 - smoothstep(0.2, 0.8, roughness)));
    max_iterations = min(max_iterations, adaptive_max_iterations);

    LOOP for(uint i = 0; i < num_rays; ++i)
    {
        // Calculate proper mirror reflection
        vec3 vs_reflected_dir = GenerateReflectionRay(-vs_ray_dir, vs_normal, roughness, uv, i, num_rays, frame_number);
        vec3 ss_ray_dir = ProjectVsDirToSsDir(vs_ray_origin, vs_reflected_dir, ss_ray_origin);

        // Perform hierarchical ray marching
        vec3 ss_hit_pos;
        bool valid_hit = HierarchicalRaymarch(ss_ray_origin,
                                              ss_ray_dir,
                                              base_depth_resolution,
                                              BASE_LOD,
                                              max_iterations,
                                              ss_hit_pos);

        BRANCH
        if(valid_hit)
        {
            // Validate hit and compute confidence
            float confidence = ValidateHit(ss_hit_pos, uv, vs_ray_origin, roughness, base_depth_resolution);

            // For cone tracing, we need to pass the ray origin and hit position in screen space
            vec4 sample_color;
            BRANCH
            if(cone_tracing_enabled)
            {
                sample_color = ConeTracing(roughness, ss_ray_origin, ss_hit_pos);
            }
            else
            {
				sample_color = SampleScreenColor(ss_hit_pos.xy, ss_hit_pos.z, s_color, 0.0);
            }
			
            // Apply brightness and luminance adjustment
            sample_color.rgb *= max(1.0, Luminance(sample_color.rgb)) * u_brightness;
            sample_color.rgb /= 1 + Luminance(sample_color.rgb);

            // Confidence-weighted accumulation
            float sample_confidence = max(confidence, 0.0);
            sample_color.a *= sample_confidence;
            
            output_color += sample_color;
        }
    }

	output_color /= float(num_rays);
    output_color.rgb /= 1 - Luminance(output_color.rgb);

    gl_FragColor = output_color;
}