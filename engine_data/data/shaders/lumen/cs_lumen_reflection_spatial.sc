/*
 * Lumen reflections, spatial filter and composite (UE 5.8 LumenReflectionDenoiserSpatialCS,
 * LumenReflectionDenoiserSpatial.usf:56-213, then the specular half of DiffuseIndirectComposite.usf:220-259).
 *
 * Filter: a bilateral over LUMEN_REFLECTION_SPATIAL_SAMPLES disk taps of LUMEN_REFLECTION_SPATIAL_KERNEL_RADIUS x
 * saturate(8 roughness) pixels, run only where the temporal variance is still high relative to the signal or the
 * history is young. Taps weigh by plane distance relative to depth, normal angle against the lobe's half angle
 * and luminance distance in temporal standard deviations. While the history is younger than
 * LUMEN_REFLECTION_SPATIAL_MAX_DISOCCLUSION_FRAMES the filter doubles its taps, widens the normal lobe 4x, drops
 * the luminance stop and tonemaps hard against fireflies in the revealed area. The result does not feed the
 * history.
 *
 * Composite: the traced layer the indirect pass reads (RBUFFER, ComposeIndirectSpecular) holds the reflections x F,
 * F = the traced weight of the pixel's roughness, with 1 - F left to the untraced layer. The indirect pass takes the
 * untraced layer straight from the screen probe gather's rough specular history (its history times the GI intensity,
 * full coverage; fs_pbr_lighting.sh u_probe_layer_params): Lumen does not composite reflection captures or sky
 * specular under its own (UE DiffuseIndirectComposite.usf:220-259).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#define LUMEN_REFLECTION_TILES_STAGE 13
#include "lumen/lumen_reflection_common.sh"

IMAGE2D_WO(s_lumen_reflection_traced_out, rgba16f, 0);
/// The temporal accumulation: rgb = radiance, a = luminance second moment.
SAMPLER2D(s_lumen_reflection_specular, 8);
/// The accumulated frame count, -1 = no reflection.
SAMPLER2D(s_lumen_reflection_frames, 9);
SAMPLER2D(s_lumen_depth, 10);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 11);

/// UE TonemapLighting: heavier with @p disocclusion against fireflies in revealed areas.
vec3 LumenSpatialTonemap(vec3 color, float disocclusion)
{
	return color / (1.0 + disocclusion * LumenReflectionLuminance(color));
}

vec3 LumenSpatialInverseTonemap(vec3 color, float disocclusion)
{
	return color / max(1.0 - disocclusion * LumenReflectionLuminance(color), 1e-4);
}

/// The filtered reflection of a pixel with a valid accumulation (pre-exposed).
vec3 LumenFilterReflection(ivec2 pixel, vec4 accumulated, float frames)
{
	vec3 center = LumenReflectionToDenoiserSpace(accumulated.xyz);
	float center_luminance = LumenReflectionLuminance(center);
	float deviation = sqrt(max(accumulated.w - center_luminance * center_luminance, 0.0));
	float disocclusion = 1.0 - saturate(frames / LUMEN_REFLECTION_SPATIAL_MAX_DISOCCLUSION_FRAMES);
	vec3 sum = LumenSpatialTonemap(center, disocclusion);
	float weight_sum = 1.0;
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	vec4 gbuffer1 = texelFetch(s_lumen_normal, pixel, 0);
	float roughness = gbuffer1.w;
	float radius = LUMEN_REFLECTION_SPATIAL_KERNEL_RADIUS * saturate(roughness * 8.0);
	bool noisy = deviation / max(center_luminance, 0.1) > 0.5 || disocclusion > 0.01;
	BRANCH
	if(radius > 1.0 && noisy)
	{
		vec3 normal = normalize(decodeNormalOctahedron(gbuffer1.xy));
		vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
		float scene_depth = LumenLinearDepth(depth01);
		float lobe = clamp((LumenSpecularLobeHalfAngle(roughness) + 0.01) * mix(1.0, 4.0, disocclusion), 0.01,
		                   0.5 * LUMEN_PI);
		int samples = int(mix(float(LUMEN_REFLECTION_SPATIAL_SAMPLES), 2.0 * float(LUMEN_REFLECTION_SPATIAL_SAMPLES),
		                      disocclusion) + 0.5);
		// A 2x2 seed keeps neighbouring threads coherent (UE: important for the cost over the whole screen).
		uvec2 seed = Rand3DPCG16(ivec3(pixel % 2, int(u_lumen_frame_mod))).xy;
		ivec2 view_size = ivec2(u_lumen_view_size);
		LOOP
		for(int i = 0; i < samples; ++i)
		{
			vec2 offset = LumenUniformSampleDiskConcentric(Hammersley16(uint(i), uint(samples), seed)) * radius;
			ivec2 q = ivec2(vec2(pixel) + offset + 0.5);
			if(any(lessThan(q, ivec2(0, 0))) || any(greaterThanEqual(q, view_size)))
			{
				continue;
			}
			if(!LumenReflectionTileTraces(q) || texelFetch(s_lumen_reflection_frames, q, 0).x < 0.0)
			{
				continue;
			}
			vec4 q_gbuffer1 = texelFetch(s_lumen_normal, q, 0);
			vec3 q_normal = normalize(decodeNormalOctahedron(q_gbuffer1.xy));
			vec3 q_position = LumenWorldFromDepth(LumenPixelUv(q), texelFetch(s_lumen_depth, q, 0).x);
			float relative = abs(dot(q_position - position, normal)) / max(scene_depth, 1e-5);
			float depth_weight = exp2(-LUMEN_REFLECTION_SPATIAL_DEPTH_WEIGHT_SCALE * relative * relative);
			float normal_weight = 1.0 - saturate(acos(saturate(dot(normal, q_normal))) / lobe);
			vec3 q_specular = LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_specular, q, 0).xyz);
			float luminance_delta = abs(center_luminance - LumenReflectionLuminance(q_specular));
			float luminance_weight = mix(exp2(-luminance_delta / max(deviation, 0.001)), 1.0, disocclusion);
			float weight = depth_weight * normal_weight * luminance_weight;
			sum += LumenSpatialTonemap(q_specular, disocclusion) * weight;
			weight_sum += weight;
		}
	}
	return LumenReflectionFromDenoiserSpace(LumenSpatialInverseTonemap(sum / weight_sum, disocclusion));
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if(pixel.x >= int(u_lumen_view_size.x) || pixel.y >= int(u_lumen_view_size.y))
	{
		return;
	}
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		// Sky: no traced layer and no probe coverage.
		imageStore(s_lumen_reflection_traced_out, pixel, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	float roughness = texelFetch(s_lumen_normal, pixel, 0).w;
	float traced_weight = LumenReflectionFadeAlpha(roughness);
	float frames = LumenReflectionTileTraces(pixel) ? texelFetch(s_lumen_reflection_frames, pixel, 0).x : -1.0;
	vec4 traced = vec4(0.0, 0.0, 0.0, 1.0);
	BRANCH
	if(traced_weight > 0.0 && frames >= 0.0)
	{
		vec4 accumulated = texelFetch(s_lumen_reflection_specular, pixel, 0);
		traced = vec4(LumenFilterReflection(pixel, accumulated, frames) * traced_weight, 1.0 - traced_weight);
	}
	imageStore(s_lumen_reflection_traced_out, pixel, traced);
}
