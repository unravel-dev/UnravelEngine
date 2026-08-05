$input v_texcoord0

/*
 * Resolves world-space cached radiance into a screen-space indirect diffuse estimate, one
 * cosine-sampled ray bundle per pixel.
 *
 * This is the PER-PIXEL gather. The probe gather (cs_gi_probe_trace + fs_gi_probe_integrate)
 * produces the same output convention from far fewer, coherent rays and is the production path;
 * this one remains as the reference and the diagnostic surface -- its debug modes see individual
 * rays, which the probe path deliberately aggregates away. Both paths share the entire per-ray
 * pipeline through gi_gather_common.sh, so an A/B between them compares the gather ARCHITECTURE
 * and nothing else.
 *
 * The output matches the SSIL convention exactly -- RGB is a hemispherical indirect diffuse
 * estimate in radiance-mean units, A is the weight with which it replaces the environment probe
 * -- so the existing consumer needs no change and the two remain comparable.
 */

#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_ONLY
#include "gi/radiance_cache.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_gather_common.sh"

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// Diagnostic only. See GiDebugUnshade.
SAMPLER2D(s_gi_base_color, 10);
/// PREVIOUS frame's accumulated luminance moments (mean, mean-square, sample count), for the
/// variance-guided ray budget. Written by the temporal pass, so it lags this pass by one frame;
/// that is exactly what makes it usable here without a feedback hazard.
SAMPLER2D(s_gi_prev_moments, 11);

/// Cancels what the CONSUMER will multiply this pass's output by, so a diagnostic written here
/// arrives on screen as the number it is.
///
/// The output of this pass is indirect diffuse, and fs_pbr_lighting.sh spends it as
/// `mix(irradiance, rgb * PI, a)` and then `DiffuseColor * AO * that` (StandardShadingIndirect).
/// For LIGHTING that is exactly right. For a DIAGNOSTIC it is fatal: every debug view was really
/// showing stage fractions times the surface's own albedo, so black wrought iron read black
/// whatever the rays did, a red awning tinted a cyan reading to dark teal, and only near-white
/// stone reported anything close to the truth. Three separate investigations were run off colours
/// that were mostly paint.
///
/// Dividing by the same factors here makes the modulation cancel. The floor keeps a near-black
/// albedo from exploding rather than merely being unreadable, so a dark channel on dark paint
/// stays honest about being unmeasurable there. Exposure and tonemapping still apply and are
/// monotonic, so compare channels against each other, not against an absolute value.
vec3 GiDebugUnshade(vec3 value, vec2 uv)
{
	GBufferDataColorAndAO color_data = DecodeGBufferColorAndAOLod(uv, s_gi_base_color, 0.0);
	vec3 modulation = color_data.base_color * max(color_data.ambient_occlusion, 1e-3);
	return value / max(modulation * PI, vec3_splat(1e-3));
}

/// x != 0 replaces the radiance output with a per-ray DIAGNOSTIC, so the three ways a gather
/// ray can fail are separable in one view instead of inferred from the lit image:
///   R = fraction of rays that HIT geometry at all (low means rays escape to sky)
///   G = fraction whose hit could be ADDRESSED (low means SdfResolveSurfacePoint failed)
///   B = fraction that FOUND a cache entry there (low means the lookup misses)
/// A ray contributes light only when all three succeed, so whichever channel is dark is the
/// stage at fault. Every hypothesis about darkening is a claim about one of these numbers.
uniform vec4 u_gi_resolve_debug;
#define u_gi_debug_mode    int(u_gi_resolve_debug.x)
#define u_gi_debug_enabled (u_gi_debug_mode > 0)
/// y = ray count for SETTLED pixels (0 disables the adaptive budget),
/// z = relative-sigma threshold below which a pixel counts as settled.
#define u_gi_adaptive_min_rays int(u_gi_resolve_debug.y)
#define u_gi_adaptive_sigma    u_gi_resolve_debug.z

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	// Sky: no surface to gather for. Alpha 0 leaves the consumer on its environment probe.
	if(depth >= 1.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 world_normal = nd.world_normal;
	if(dot(world_normal, world_normal) < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	world_normal = normalize(world_normal);
	// Everything a ray needs that belongs to the LAUNCH POINT -- the measured lift, the buried
	// origin escape, the self-read keys, the near-field fade -- shared verbatim with the probe
	// path through gi_gather_common.sh.
	GiGatherSetup setup = GiPrepareGather(world_position, world_normal);
	float debug_self_reads = 0.0;
	// Decorrelate the sample pattern per pixel AND per frame, so the residual error is noise
	// that temporal accumulation can average away rather than a fixed pattern that it cannot.
	uint pixel_seed = GiHashCombine(GiHashUint(uint(gl_FragCoord.x)), uint(gl_FragCoord.y));
	uint seed = GiHashCombine(pixel_seed, u_gi_frame_index);
	vec3 sum = vec3_splat(0.0);
	float resolved = 0.0;
	float debug_hit = 0.0;
	float debug_addressed = 0.0;
	// Rays that read an actual cache ENTRY, which is NOT the same as `resolved`: with
	// occlude-on-cache-miss on, the two differ by exactly the misses, and reporting one as the
	// other hid the only stage that writes black at full weight.
	float debug_found = 0.0;
	// Where the hit LANDED, not merely that there was one: a ray re-hitting the surface it
	// started on looks identical to one that travelled twenty metres in every stage counter.
	float debug_near_hits = 0.0;
	float debug_total_t = 0.0;
	int ray_count = max(u_gi_ray_count, 1);
	// Variance-guided ray budget: spend rays where the estimate is still noisy, not where it has
	// already settled. The temporal pass accumulates per-pixel luminance moments and a sample
	// count; a pixel whose history is deep and whose relative deviation is small has converged,
	// and tracing four rays into it re-measures a number that is already known. Disocclusions
	// reset the count and lighting changes raise the variance, and both immediately restore the
	// full budget exactly where it is needed.
	//
	// The absolute-variance clause keeps DARK settled pixels settled: relative sigma divides by
	// the mean, so near-black pixels would otherwise read as "noisy" forever and keep full rays.
	if(u_gi_adaptive_min_rays > 0)
	{
		vec4 prev_moments = texture2DLod(s_gi_prev_moments, uv, 0.0);
		float accumulated = prev_moments.z;
		float mean = prev_moments.x;
		float variance = max(prev_moments.y - mean * mean, 0.0);
		float relative_sigma = sqrt(variance) / max(mean, 1e-3);
		if(accumulated >= 8.0 && (relative_sigma < u_gi_adaptive_sigma || variance < 1e-6))
		{
			ray_count = min(ray_count, u_gi_adaptive_min_rays);
		}
	}
	for(int i = 0; i < ray_count; ++i)
	{
		seed = GiHashUint(seed);
		float u1 = float(seed & 0xFFFFu) / 65535.0;
		seed = GiHashUint(seed);
		float u2 = float(seed & 0xFFFFu) / 65535.0;
		vec3 direction = GiCosineDirection(world_normal, u1, u2);
		GiRayOutcome outcome = GiGatherRay(setup, direction);
		sum += outcome.radiance;
		resolved += outcome.resolved;
		debug_hit += outcome.hit;
		debug_addressed += outcome.addressed;
		debug_found += outcome.found;
		debug_self_reads += outcome.self_read;
		debug_total_t += outcome.t;
		// "Near" measured in VOXELS of the field that answered, because that is the scale the
		// isosurface can be displaced by, and so the scale a self-hit happens at.
		if(outcome.hit > 0.0 && outcome.t < 4.0 * setup.origin_voxel)
		{
			debug_near_hits += 1.0;
		}
	}
	if(u_gi_debug_enabled)
	{
		float inv_rays = 1.0 / float(ray_count);
		if(u_gi_debug_mode >= 3)
		{
			// Mode 3: the three numbers every remaining theory is about.
			//   R = fraction of rays REJECTED as reading the entry being shaded.
			//   G = the lift actually applied, in voxels.
			//   B = the cascade's signed distance at the shading point, in voxels, mid grey =
			//       exactly on the isosurface -- the INPUT the adaptive lift is derived from.
			vec3 debug_rgb = vec3(debug_self_reads * inv_rays,
			                      saturate(setup.lift / max(setup.origin_voxel, 1e-4)),
			                      saturate(0.5 + setup.origin_distance / max(2.0 * setup.origin_voxel, 1e-4)));
			gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
			return;
		}
		if(u_gi_debug_mode >= 2)
		{
			// Mode 2: WHERE the rays landed.
			//   R = fraction that hit within 4 voxels of the origin -- self-hits on the surface
			//       being shaded. SCALE RELATIVE: trust it where level 0 answers, noise beyond.
			//   G = mean hit distance, scaled so mid grey is a tenth of the ray budget.
			//   B = fraction that resolved at all, for reference.
			float mean_t = debug_hit > 0.0 ? debug_total_t / debug_hit : 0.0;
			vec3 debug_rgb = vec3(debug_near_hits * inv_rays,
			                      saturate(mean_t / max(u_gi_max_distance * 0.1, 1e-3)),
			                      resolved * inv_rays);
			gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
			return;
		}
		// Mode 1: the three stages, and B is FOUND rather than `resolved`. White means the rays
		// really did read cached radiance and any darkness is IN the cache; yellow means they hit
		// addressable geometry the cache has never lit -- zero radiance at full weight, which no
		// origin knob can move.
		vec3 debug_rgb = vec3(debug_hit * inv_rays, debug_addressed * inv_rays, debug_found * inv_rays);
		gl_FragColor = vec4(GiDebugUnshade(debug_rgb, uv), 1.0);
		return;
	}
	if(resolved <= 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	// RGB is the mean over rays that actually resolved; A is the fraction that did. The consumer
	// computes mix(probe, rgb * PI, a), so an unresolved ray costs weight rather than energy.
	gl_FragColor = vec4(sum / resolved, resolved / float(ray_count));
}
