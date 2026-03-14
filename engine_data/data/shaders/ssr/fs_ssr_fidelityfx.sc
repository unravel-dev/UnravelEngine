$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"
#include "../sampling.sh"

SAMPLER2D(s_color, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_depth, 2);
SAMPLER2D(s_hiz, 3);
SAMPLER2D(s_color_blurred, 4);

uniform vec4 u_ssr_params;
#define u_max_steps         u_ssr_params.x
#define u_depth_tolerance   u_ssr_params.y
#define u_max_rays          u_ssr_params.z
#define u_brightness        u_ssr_params.w

uniform vec4 u_hiz_params;
#define u_hiz_width         u_hiz_params.x
#define u_hiz_height        u_hiz_params.y
#define u_hiz_num_mips      u_hiz_params.z
#define u_ssr_resolution_scale u_hiz_params.w

uniform vec4 u_fade_params;
#define u_fade_in_start     u_fade_params.x
#define u_fade_in_end       u_fade_params.y
#define u_roughness_depth_tolerance u_fade_params.z
#define u_facing_reflections_fading u_fade_params.w

uniform vec4 u_cone_params;
#define u_cone_angle_bias   u_cone_params.x
#define u_max_mip_level     u_cone_params.y
#define u_frame_index_mod       u_cone_params.z
#define u_enable_cone_tracing     u_cone_params.w

uniform mat4 u_prev_view_proj;

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

// Temporal reprojection functions
vec2 WorldToScreenPrevious(vec3 ws_pos)
{
    vec4 prev_clip4 = mul(u_prev_view_proj, vec4(ws_pos, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * vec2_splat(0.5) + vec2_splat(0.5);
}

vec2 ComputePreviousFrameUV(vec2 uv, float z)
{
    vec3 vs_pos = HizComputeViewspacePosition(uv, z);
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
    return adjacentLength - (incircleRadius * 2.0);
}

vec4 ConeSampleWeightedColor(vec2 samplePos, float sampleZ, float mipLevel)
{
    vec4 sampleColor = SampleScreenColor(samplePos, sampleZ, s_color_blurred, mipLevel);
    
    float visibility = 1.0;
    
    if(any(lessThan(samplePos, vec2_splat(0.0))) || any(greaterThan(samplePos, vec2_splat(1.0))))
    {
        visibility = 0.0;
    }
    
    return vec4(sampleColor.rgb * visibility, visibility);
}

vec4 ConeSampleMultiplePoints(vec2 centerPos, float centerZ, float incircleRadius, float mipLevel)
{
    vec4 result = vec4_splat(0.0);
    float totalWeight = 0.0;
    
    vec4 centerSample = ConeSampleWeightedColor(centerPos, centerZ, mipLevel);
    result += centerSample;
    totalWeight += 1.0;
    
    if(incircleRadius > 0.002)
    {
        const int numExtraSamples = 4;
        const float angleStep = 2.0 * PI / float(numExtraSamples);
        
        for(int i = 0; i < numExtraSamples; ++i)
        {
            float angle = float(i) * angleStep;
            vec2 offset = vec2(cos(angle), sin(angle)) * incircleRadius * 0.5;
            vec2 samplePos = centerPos + offset;
            
            vec4 sampleColor = ConeSampleWeightedColor(samplePos, centerZ, mipLevel);
            float weight = 0.5;
            
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }
    
    return totalWeight > 0.0 ? result / totalWeight : result;
}

vec4 ConeTracing(float roughness, vec3 ss_ray_origin, vec3 ss_hit_pos)
{
    float coneTheta = roughness * PI * u_cone_angle_bias;
    vec2 res = HizGetDepthMipResolution(s_hiz, 0);
    
    vec3 deltaPos = ss_hit_pos - ss_ray_origin;
    
    float adjacentLength = length(deltaPos.xy);
    
    BRANCH
    if(adjacentLength < 0.001)
    {
        return texture2DLod(s_color_blurred, ss_hit_pos.xy, 0.0);
    }
    
    vec3 adjacentUnit = normalize(deltaPos);
    
    vec4 reflectionColor = vec4_splat(0.0);
    vec3 samplePos;
    
    int maxSamples = int(u_max_mip_level) + 1;
    if(roughness < 0.1)
    {
        maxSamples = min(maxSamples, 1);
    }
    else if(roughness > 0.5)
    {
        maxSamples = int(u_max_mip_level) + 1;
    }
    else
    {
        maxSamples = int(mix(3.0, float(int(u_max_mip_level) + 1), (roughness - 0.1) / 0.4));
    }
    
    float totalWeight = 0.0;
    int i = 0;
    
    LOOP for(; i < maxSamples; ++i)
    {
        float oppositeLength = IsoscelesTriangleOpposite(adjacentLength, coneTheta);
        float incircleSize = IsoscelesTriangleInRadius(oppositeLength, adjacentLength);
        samplePos = ss_ray_origin + adjacentUnit * (adjacentLength - incircleSize);
        float mipChannel = clamp(log2(incircleSize * max(res.x, res.y)), 0.0, u_max_mip_level);
        
        vec4 newColor = ConeSampleMultiplePoints(samplePos.xy, samplePos.z, incircleSize, mipChannel);
        
        float distanceWeight = 1.0 - float(i) / float(maxSamples);
        float sampleWeight = newColor.a * distanceWeight;
        
        reflectionColor.rgb += newColor.rgb * sampleWeight;
        reflectionColor.a += sampleWeight;
        totalWeight += sampleWeight;
        
        BRANCH
        if(reflectionColor.a >= 0.95)
        {
            //break;
        }
        
        adjacentLength = IsoscelesTriangleNextAdjacent(adjacentLength, incircleSize);
        
        BRANCH
        if(adjacentLength <= 0.0)
        {
            break;
        }
    }
    
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


// SSR-specific hit validation with roughness/facing fade
float ValidateHit(vec3 ss_hit_pos, vec2 uv, vec3 vs_ray_origin, float roughness, vec2 screen_size)
{
    BRANCH
    if(any(lessThan(ss_hit_pos.xy, vec2_splat(0.0))) || any(greaterThan(ss_hit_pos.xy, vec2_splat(1.0))))
        return 0.0;

    vec2 manhattan_dist = abs(ss_hit_pos.xy - uv);
    vec2 inv_screen_size = rcp(screen_size);
	
    BRANCH
    if(all(lessThan(manhattan_dist, inv_screen_size * 0.5)))
        return 0.0;

    float surface_z = HizFetchDepth(s_hiz, screen_size * ss_hit_pos.xy, BASE_LOD);

    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(surface_z == 0.0)
        return 0.0;
#else
    if(surface_z == 1.0)
        return 0.0;
#endif


    vec3 vs_hit_pos = HizComputeViewspacePosition(ss_hit_pos.xy, ss_hit_pos.z);
    vec3 vs_ray_dir = vs_hit_pos - vs_ray_origin;

    vec2 full_res_uv = ss_hit_pos.xy;
    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(full_res_uv, s_normal);

    vec3 vs_normal = mul(u_view, vec4(normal_data.world_normal, 0.0)).xyz;
    float dot_prod = dot(vs_ray_dir, vs_normal);
	
    if(dot_prod > 0)
    {
        return 0.0;
    }

    vec3 vs_hit_surface = HizComputeViewspacePosition(ss_hit_pos.xy, surface_z);
    float dist = length(vs_hit_pos - vs_hit_surface);

    float depth_tolerance = u_depth_tolerance + mix(0.0, u_roughness_depth_tolerance, roughness);
    float confidence = 1.0 - smoothstep(0.0, depth_tolerance, dist);
    confidence *= 10.0;

    vec2 fade_in = vec2(u_fade_in_start, u_fade_in_end);
    vec2 border = smoothstep(vec2_splat(0.0), fade_in, ss_hit_pos.xy) *
                  (1.0 - smoothstep(1.0 - fade_in, vec2_splat(1.0), ss_hit_pos.xy));

    float edge_fade = border.x * border.y;

    float mirror_fade = clamp(max(dot(vs_ray_origin, vs_ray_dir), 0.0) + u_facing_reflections_fading, 0.0, 1.0);

    float roughness_fade = GetRoughnessFade(roughness);

    return clamp(confidence * mirror_fade * edge_fade * roughness_fade, 0.0, 1.0);
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

vec3 GenerateReflectionRay(vec3 V, vec3 N, float roughness, vec2 texCoord, uint i, uint num_rays, int frame_index)
{
#if HAMMERSLEY_TYPE > 0
    BRANCH
    if(num_rays == 1)
    {
        return reflect(-V, N);
    }

	roughness = mix(0.0, MAX_ROUGHNESS, roughness);
	
#if HAMMERSLEY_TYPE == 1
	vec2 scaled_texcoord = texCoord * u_viewRect.zw;
	uvec2 Random = Rand3DPCG16( ivec3( scaled_texcoord, frame_index ) ).xy;
	vec2 E = Hammersley16( i, num_rays, Random );
#else
    int sampleIndex = (frame_index + i + int(texCoord.x * 1024.0) + int(texCoord.y * 1024.0)) % HAMMERSLEY_SAMPLES;
    vec2 E = Hammersley(sampleIndex, HAMMERSLEY_SAMPLES);
    vec2 spatialJitter = fract(texCoord * 543.2103);
    E = fract(E + spatialJitter);
#endif
    float a = roughness * roughness;
    float a2 = a * a;

    vec3 H = ImportanceSampleGGX(E, N, a2);
    vec3 L = normalize(2.0 * dot(V, H) * H - V);

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


void main()
{
    vec2 uv = v_texcoord0;
    GBufferDataNormalMetalRoughness normalData = DecodeGBufferNormalMetalRoughness(uv, s_normal);

    float metallic = normalData.metalness;
    float roughness = normalData.roughness;
    float roughnessFade = GetRoughnessFade(roughness);

    BRANCH
    if(roughnessFade <= 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec2 base_depth_resolution = HizGetDepthMipResolution(s_hiz, BASE_LOD);
    float surface_z = HizFetchDepth(s_hiz, base_depth_resolution * uv, BASE_LOD);

    vec3 vs_normal = mul(u_view, vec4(normalData.world_normal, 0.0)).xyz;

    vec3 ss_ray_origin = vec3(uv, surface_z);
    vec3 vs_ray_origin = HizComputeViewspacePosition(ss_ray_origin.xy, ss_ray_origin.z);
    vec3 vs_ray_dir = normalize(vs_ray_origin);

    uint num_rays = 1;
#if HAMMERSLEY_TYPE > 0
	num_rays = uint(u_max_rays);
#endif

	int frame_number = int(u_frame_index_mod);
	
	bool cone_tracing_enabled = u_enable_cone_tracing > 0.5;
    vec4 output_color = vec4_splat(0.0);
    float total_weight = 0.0;
	
	int max_iterations = int(u_max_steps);
    int adaptive_max_iterations = int(mix(8.0, float(max_iterations), 
                                         1.0 - smoothstep(0.2, 0.8, roughness)));
    max_iterations = min(max_iterations, adaptive_max_iterations);

    LOOP for(uint i = 0; i < num_rays; ++i)
    {
        vec3 vs_reflected_dir = GenerateReflectionRay(-vs_ray_dir, vs_normal, roughness, uv, i, num_rays, frame_number);
        vec3 ss_ray_dir = HizProjectVsDirToSsDir(vs_ray_origin, vs_reflected_dir, ss_ray_origin);

        vec3 ss_hit_pos;
        bool valid_hit = HizHierarchicalRaymarch(s_hiz,
                                                 ss_ray_origin,
                                                 ss_ray_dir,
                                                 base_depth_resolution,
                                                 BASE_LOD,
                                                 max_iterations,
                                                 ss_hit_pos);

        BRANCH
        if(valid_hit)
        {
            float confidence = ValidateHit(ss_hit_pos, uv, vs_ray_origin, roughness, base_depth_resolution);

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
			
            sample_color.rgb *= max(1.0, Luminance(sample_color.rgb)) * u_brightness;
            sample_color.rgb /= 1 + Luminance(sample_color.rgb);

            float sample_confidence = max(confidence, 0.0);
            sample_color.a *= sample_confidence;
            
            output_color += sample_color;
        }
    }

	output_color /= float(num_rays);
    output_color.rgb /= 1 - Luminance(output_color.rgb);

    gl_FragColor = output_color;
}
