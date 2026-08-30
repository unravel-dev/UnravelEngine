#ifndef __GI_REFLECTION_KERNEL_SH__
#define __GI_REFLECTION_KERNEL_SH__

/*
 * GI reflections (plan phase 9) - the world-space specular tier, layered UNDER SSR: drawn
 * into the reflection buffer over the authored probes, before SSR composites the sharp
 * on-screen result on top. What it adds is exactly what SSR cannot have: reflected content
 * that is off screen or behind the camera.
 *
 * SHARED KERNEL BODY, two consumers. cs_gi_reflection_trace.sc is the deliverable path:
 * the classify pass answers sky / degenerate / rough pixels directly and compacts the
 * sharp (tracing) pixels into a dense list, so every 64-lane trace group is fully
 * populated with rays - the fragment form paid a whole wave wherever ONE quad pixel
 * traced, and its worst-case register footprint throttled even the early-out pixels.
 * fs_gi_reflection.sc is that fragment form, kept as the fallback when the compute
 * chain is unavailable.
 *
 * ROUGHNESS TIERING, with the cutoff derived rather than tuned: past roughness ~0.4 the GGX
 * lobe is wide enough that its specular converges to the diffuse irradiance, so those pixels
 * reuse LAST frame's resolved GI - the temporally filtered, denoised per-pixel gather - and
 * pay no ray (the Lumen recipe: rough specular comes from your own gather, never from a raw
 * world lattice, whose 2 m granularity reads as mottling). Sharper lobes trace the SDF world
 * tier: mesh-exact out to a roughness-adaptive range, clipmap as a far-field FINDER with a
 * short mesh-SDF refine at the hit, light voxels at mesh-snapped hits. An unrefined clipmap
 * hit is not an image on sharp pixels - coverage goes to zero so the authored probe layer
 * shows through. A true miss answers with the authored probe layer itself - the multi-probe
 * blended, parallax-projected sky capture at this pixel - and with the sky SH only where no
 * probe reaches (an L2 SH cannot hold clouds or a sun disk; the probes can).
 *
 * ALBEDO REMODULATION (compute form only, GI_LIGHT_VOXEL_READ_ALBEDO): a measured light-voxel
 * answer at a hit with a known instance is rescaled by hit-albedo / voxel-mean-albedo, so the
 * image carries the hit surface's own colour instead of the attribute lattice's 0.25-1 m
 * trilinear mix - see the block at the read site for the full contract.
 *
 * NO SCREEN TIER: screen space belongs to SSR, which composites over this pass with its own
 * stochastic spread, denoise, and fades. Two screen tracers on the same pixel gave two
 * different answers wherever their validation or fades differed (measured, round 6), and a
 * single deterministic ray can never reproduce SSR's filtered result - so this pass never
 * traces the screen at all.
 *
 * STOCHASTIC GGX: each frame the ray direction is importance-sampled from the visible
 * normal distribution (Heitz VNDF) with an R2 sequence per frame + IGN per pixel - the
 * gather's proven jitter recipe - and the temporal pass integrates the lobe over
 * GI_REFLECTION_TEMPORAL_FRAMES of reprojected history. Roughness therefore SPREADS the
 * reflection the way SSR's stochastic trace does, instead of any fixed fade. At mirror
 * roughness alpha collapses the distribution and the ray is deterministic. The
 * GATHER_FADE_START..ROUGH_CUTOFF band still fades into the rough tier for continuity at the
 * cutoff. Per-pixel world-probe cage reads remain deliberately ABSENT: the 2 m lattice's
 * interpolation pattern stamps into the image as blotches, not blur (measured twice, rounds
 * 2 and 8). Output = incoming radiance along the sampled ray at FULL weight - energy is
 * constant across roughness (fading the sharp end read as brightness rising with roughness,
 * round 10). Alpha is coverage below 1 (mesh-exact and refined hits cover the probe layer,
 * an unrefined clipmap hit on a sharp pixel does not) and encodes the HIT DISTANCE above 1
 * for the temporal's mover gate - see the encoding note at the return. SSR composites on
 * top.
 */

#include "../common.sh"
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"
#include "gi/gi_noise.sh"

SAMPLER2D(s_gi_normal, 5);
/// The authored probe layer (RBUFFER right after the probe pass): at trace time it holds
/// exactly the freshly drawn reflection probes - the GI composite and SSR write into it
/// later in the frame. Bound as transparent black when the probe stack did not run this
/// frame, so the read never sees this pass's own previous output.
SAMPLER2D(s_gi_probe_layer, 6);
SAMPLER2D(s_hiz, 8);
/// LAST frame's resolved GI (E/pi per pixel, temporally filtered and denoised): the rough
/// specular source, exactly as Lumen reuses its own gather - a wide lobe converges to the
/// diffuse irradiance, and this is the smoothest estimate of it the engine owns. Reading the
/// raw world-probe cage here instead produced 2 m-scale mottling.
SAMPLER2D(s_gi_diffuse, 9);
SAMPLER2D(s_gi_env_sh, 14);

/// xyz = camera position, w > 0 when s_gi_diffuse holds last frame's resolve.
uniform vec4 u_gi_reflection_camera;
/// xy = this frame's R2 low-discrepancy offset for the GGX sample; zw unused.
uniform vec4 u_gi_reflection_jitter;

/// Heitz 2018 visible-normal GGX sampling; view and result in tangent space (z = normal).
vec3 SampleGGXVNDF(vec3 view_ts, float alpha, float u1, float u2)
{
	vec3 vh = normalize(vec3(alpha * view_ts.x, alpha * view_ts.y, view_ts.z));
	float lensq = vh.x * vh.x + vh.y * vh.y;
	vec3 t1;
	if(lensq > 1e-8)
	{
		t1 = vec3(-vh.y, vh.x, 0.0) / sqrt(lensq);
	}
	else
	{
		t1 = vec3(1.0, 0.0, 0.0);
	}
	vec3 t2 = cross(vh, t1);
	float r = sqrt(u1);
	float phi = 6.283185307 * u2;
	float p1 = r * cos(phi);
	float p2 = r * sin(phi);
	float s = 0.5 * (1.0 + vh.z);
	p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
	vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
	return normalize(vec3(alpha * nh.x, alpha * nh.y, max(1e-6, nh.z)));
}

/// Sky answer for rays our own geometry data calls OPEN: the authored probe layer at this
/// pixel, already multi-probe blended and parallax projected - frequency content an L2 SH
/// cannot hold (clouds, sun disk, horizon). Probe alpha is the layer's own coverage, so
/// unprobed pixels keep the SH answer instead of going black (RBUFFER.rgb is consumed flat
/// by the indirect pass - a hole here IS the final specular).
vec3 GiReflectionSkyFallback(vec2 uv, vec3 direction)
{
	vec4 probe_layer = texture2DLod(s_gi_probe_layer, uv, 0.0);
	float probe_alpha = saturate(probe_layer.w);
	// Full probe coverage is the normal state, and the SH is 9 texelFetches evaluated only
	// to be mixed out. Skip it whenever its weight is below half a quantum of the RGBA16F
	// target. A branch, not a ternary: HLSL ?: is a select that may evaluate both arms.
	BRANCH
	if(probe_alpha >= 0.999)
	{
		return probe_layer.xyz;
	}
	vec3 sky_sh = eval_radiance_sh(s_gi_env_sh, direction);
	return mix(sky_sh, probe_layer.xyz, probe_alpha);
}

/// Mesh-exact walk length: mirrors pay the long range, gloss pays less than the old flat 16 m.
float GiReflectionMeshRange(float roughness)
{
	float range_t = saturate(roughness / max(GI_REFLECTION_GATHER_FADE_START, 1e-4));
	return min(mix(GI_REFLECTION_MESH_SDF_RANGE_SHARP, GI_REFLECTION_MESH_SDF_RANGE_GLOSS, range_t),
	           GI_SHADOW_DISTANCE);
}

/// Snap a clipmap hit back onto a mesh SDF in a short window around the fattened t.
/// The window is sized to the COARSER of the covering pair so a cascade-border hit,
/// whose isosurface can sit a coarse voxel off the mesh, still contains the surface.
SdfRayHit GiReflectionRefine(vec3 origin, vec3 direction, SdfRayHit clipmap_hit)
{
	vec3 hit_position = origin + direction * clipmap_hit.t;
	float field_blend;
	float voxel;
	int level = SdfFindClipmapLevel(hit_position, field_blend, voxel);
	// Nested rather than one &&-chain: SdfFindClipmapLevel returns LEVEL_COUNT off coverage,
	// and HLSL && does not short-circuit, so the flat form indexed one past the uniform array.
	if(level + 1 < SDF_CLIPMAP_LEVEL_COUNT)
	{
		float edge = max(field_blend,
		                 SdfClipmapEdgeBlend(level, hit_position, GI_REFLECTION_CASCADE_FADE_VOXELS));
		if(edge > 0.0)
		{
			if(u_sdf_clipmap_levels[level + 1].w > 0.0)
			{
				voxel = max(voxel, u_sdf_clipmap_levels[level + 1].w);
			}
		}
	}
	voxel = max(voxel, 0.01);
	float window = GI_REFLECTION_REFINE_VOXELS * voxel;
	float t_min = max(clipmap_hit.t - window, 0.0);
	float t_max = min(clipmap_hit.t + window, GI_SHADOW_DISTANCE);
	if(t_min >= t_max)
	{
		return clipmap_hit;
	}
	SdfRayHit refined = SdfTraceInstances(origin, direction, t_min, t_max, GI_REFLECTION_REFINE_STEPS,
	                                      GI_REFLECTION_TRACE_SURFACE_BIAS,
	                                      GI_REFLECTION_TRACE_RELAXATION, true);
	if(refined.hit)
	{
		return refined;
	}
	return clipmap_hit;
}

/// The whole reflection answer for one texel of the trace target: rgb = incoming radiance
/// along the (stochastically sampled) reflection ray, a = coverage for the probe-layer
/// composite. `frag_coord` is the texel centre in target pixels (gl_FragCoord.xy in the
/// fragment form, pixel + 0.5 in the compute form) - it only seeds the IGN.
vec4 GiReflectionShade(vec2 uv, vec2 frag_coord)
{
	float depth = texture2DLod(s_hiz, uv, 0.0).x;
	if(depth >= 1.0)
	{
		return vec4_splat(0.0);
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		return vec4_splat(0.0);
	}
	vec3 normal = normalize(nd.world_normal);
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec3 view = normalize(u_gi_reflection_camera.xyz - world_position);
	vec3 reflected = normalize(reflect(-view, normal));
	// RAW authored roughness for the tiering: MakeRoughnessSafe floors it, and a floored
	// mirror (authored 0) leaked a fraction of the coarse world tier through the fade.
	float roughness = nd.roughness;
	// ROUGH VALUE - the wide-lobe limit. Rough specular converges to the diffuse irradiance,
	// and last frame's resolved GI is the engine's smoothest per-pixel estimate of it (the
	// Lumen recipe: reuse your own denoised gather; never a raw world lattice).
	// An explicit branch on the wave-uniform condition: as a ternary both arms ran, and the
	// SH arm is 9 texelFetches paid by every pixel including the rough fast path below.
	vec3 rough_value;
	BRANCH
	if(u_gi_reflection_camera.w > 0.5)
	{
		rough_value = texture2DLod(s_gi_diffuse, uv, 0.0).xyz;
	}
	else
	{
		rough_value = eval_radiance_sh(s_gi_env_sh, reflected);
	}
	BRANCH
	if(roughness >= GI_REFLECTION_ROUGH_CUTOFF)
	{
		return vec4(rough_value, 1.0);
	}
	// GLOSS CONTINUITY FADE: the traced tiers below spread with roughness on their own, so
	// this fade's only remaining job is C0 continuity into the rough tier at the cutoff -
	// it covers just the residual band (a wide fade read as content dissolving, not blurring).
	float gloss_blend = smoothstep(GI_REFLECTION_GATHER_FADE_START, GI_REFLECTION_ROUGH_CUTOFF, roughness);
	float alpha_ggx = roughness * roughness;
	// STOCHASTIC direction: jitter the ray inside the GGX lobe (VNDF), decorrelated per pixel
	// by IGN and advanced per frame by R2; the temporal pass integrates. A sample below the
	// horizon (VNDF guarantees a valid half-vector, not a valid reflection at grazing) keeps
	// the mirror direction.
	//
	// The determinism gate is on DECODED roughness against the encoder floor, never on alpha
	// against a small epsilon: the G-buffer write clamps roughness to >= 0.05, so an authored
	// mirror decodes at the floor and its alpha (2.5e-3) sailed over the old 1e-4 gate -
	// every mirror pixel jittered, and rays near-missing a small emissive hit it on the
	// VNDF tail as full-radiance fireflies (the recorded decoded-roughness-floor lesson).
	BRANCH
	if(roughness > GI_REFLECTION_MIRROR_ROUGHNESS)
	{
		// TWO independent noise channels for a true 2D point: deriving the second
		// coordinate from the first put every sample on a 1D curve through the unit square,
		// so the azimuthal half of the lobe was never properly covered and high-contrast
		// regions could not converge (measured, round 13; the shared pattern in gi_noise.sh).
		vec2 xi = fract(GiIgnNoise(ivec2(frag_coord)) + u_gi_reflection_jitter.xy);
		vec3 axis;
		if(abs(normal.z) < 0.999)
		{
			axis = vec3(0.0, 0.0, 1.0);
		}
		else
		{
			axis = vec3(1.0, 0.0, 0.0);
		}
		vec3 tangent = normalize(cross(axis, normal));
		vec3 bitangent = cross(normal, tangent);
		vec3 view_ts = vec3(dot(view, tangent), dot(view, bitangent), dot(view, normal));
		vec3 half_ts = SampleGGXVNDF(view_ts, alpha_ggx, xi.x, xi.y);
		vec3 half_ws = normalize(tangent * half_ts.x + bitangent * half_ts.y + normal * half_ts.z);
		vec3 jittered = reflect(-view, half_ws);
		if(dot(jittered, normal) > 1e-3)
		{
			reflected = normalize(jittered);
		}
	}
	// WORLD tier: launch clear of the composed surface, exactly as the gather lifts.
	// (No screen tier - SSR owns screen space and composites over this pass; see header.)
	float voxel;
	float field = SdfSampleClipmapEx(world_position, voxel);
	voxel = max(voxel, 0.01);
	float lift = max(0.0, -field) + GI_PROBE_TRACE_SURFACE_BIAS * voxel;
	vec3 origin = world_position + normal * lift;
	// Adaptive mesh-exact range, then clipmap as a FINDER (expand never: image vs estimate,
	// round 3). A clipmap hit is refined in a short instance-grid window so distant
	// silhouettes snap back to the mesh; an unrefined clipmap hit is not drawn on sharp
	// pixels (coverage 0, authored probes show through). Acceptance is contact-only -
	// the gather cone is what fattened the 16 m handover into boxes.
	float mesh_range = GiReflectionMeshRange(roughness);
	SdfRayHit hit = SdfTraceInstances(origin, reflected, 0.0, mesh_range, GI_TRACE_MAX_STEPS,
	                                  GI_REFLECTION_TRACE_SURFACE_BIAS,
	                                  GI_REFLECTION_TRACE_RELAXATION, true);
	if(!hit.hit)
	{
		// Roughness-adaptive step budget, mirroring the range: a gloss-band ray is spread by
		// the GGX lobe, integrated over the temporal window, and blurred by the composite -
		// its finder does not need mirror precision, and its exhaustion path already fades
		// toward rough_value by design. The sharp band keeps the full budget.
		int finder_steps = int(mix(float(GI_TRACE_MAX_STEPS), 24.0,
		                           saturate(roughness / max(GI_REFLECTION_GATHER_FADE_START, 1e-4))));
		// The last flag declines the exhaustion-path normal: an exhausted reflection ray
		// answers with rough_value and never reads hit.normal, so the four-sample tetrahedral
		// gradient the trace would compute for it is provably dead here.
		hit = SdfTraceClipmap(origin, reflected, mesh_range, GI_SHADOW_DISTANCE, finder_steps,
		                      GI_REFLECTION_TRACE_SURFACE_BIAS, GI_REFLECTION_TRACE_RELAXATION, true,
		                      -1.0, false, false);
		if(hit.hit && !hit.exhausted)
		{
			hit = GiReflectionRefine(origin, reflected, hit);
		}
	}
	vec3 radiance;
	float coverage = 1.0;
	if(hit.hit)
	{
		vec3 hit_position = origin + reflected * hit.t;
		vec3 hit_normal = hit.normal;
		if(dot(hit_normal, reflected) > 0.0)
		{
			hit_normal = -hit_normal;
		}
		// Shape classification FIRST, so the light-voxel read below can be skipped on the
		// sharpest pixels, whose result it fully replaces with the probe layer anyway.
		bool clipmap_shape = hit.instance_index == SDF_NO_INSTANCE && !hit.exhausted;
		float shape_ok = 1.0;
		if(clipmap_shape)
		{
			// Unrefined clipmap isosurface: legitimate lighting for satin, a wrong silhouette
			// on a mirror. Fade coverage out so the probe layer replaces the blob.
			shape_ok = smoothstep(GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF * 0.5,
			                      GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF, roughness);
			coverage = shape_ok;
		}
		// A gave-up march ("hits" mid-air when a grazing far ray exhausts its budget) or
		// an unmeasured coarse face must NOT answer with black. The gather's
		// honest-darkness contract exists to stop light leaking INTO the scene; a
		// reflection fallback cannot leak light anywhere - black here only punches holes
		// in the image, which grew with camera distance as rays grazed ever-coarser
		// cascades (measured, round 14). The receiver's own gather value is the smoothest
		// energy-plausible stand-in; faces MEASURED dark stay honestly dark.
		//
		// Branched, never an || chain: HLSL's || may evaluate both operands, and the
		// light-voxel read is up to two dozen 3D fetches that an exhausted hit discards.
		radiance = rough_value;
		BRANCH
		if(!hit.exhausted && shape_ok > 0.0)
		{
			vec3 measured;
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
			// MATCHED-WEIGHT read (compute form): the radiance and the remodulation
			// denominator below come from ONE walk with identical face-alpha x facing x
			// trilinear weights. The split readers this replaces disagreed wherever face
			// culling thinned the radiance set but not the albedo set - crevices, rims,
			// every silhouette a mirror ray grazes - and the ratio slammed to its clamp:
			// a standing bright outline along reflected junctions, and a x4 window that
			// kept a departed emitter's residual glowing as a line long after the lattice
			// had converged (the "leftover red lines where the cubes passed").
			vec3 measured_albedo;
			bool measured_albedo_ok;
			bool measured_ok = GiLightVoxelReadFadeRemod(hit_position, hit_normal, rough_value,
			                                             GI_REFLECTION_CASCADE_FADE_VOXELS,
			                                             measured, measured_albedo,
			                                             measured_albedo_ok);
#else
			bool measured_ok = GiLightVoxelReadFade(hit_position, hit_normal, rough_value,
			                                        GI_REFLECTION_CASCADE_FADE_VOXELS, measured);
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
			BRANCH
			if(measured_ok)
			{
				radiance = measured;
#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)
				// ALBEDO REMODULATION - the "voxelized colour" fix. The volume stores
				// bounded_albedo * E / pi + emissive per face, and its trilinear read mixes
				// the WINNING albedos of a 0.25-1 m cell neighbourhood - a curtain hit next
				// to a wall answers pink. A mesh-exact (or refined) hit knows its instance,
				// so divide the cell-mixed albedo back out and multiply the hit's own in:
				// colour boundaries move from the attribute lattice to the SDF silhouette,
				// while the irradiance keeps the volume's smooth estimate. Only measured
				// radiance is remodulated (rough_value and the sky fallback are not voxel
				// products); emissive hits are skipped rather than having their emission
				// separated (the albedo ratio does not apply to a source term). With the
				// matched-weight denominator the ratio is exact under locally uniform
				// lighting at every boundary; the floor and cap now bound only
				// quantisation noise and genuinely non-uniform lighting across the
				// footprint. A fallback-mixed answer carries no matching albedo
				// (measured_albedo_ok false) and is served unremodulated.
				BRANCH
				if(hit.instance_index != SDF_NO_INSTANCE && measured_albedo_ok)
				{
					uint material_base = uint(hit.instance_index) * uint(SDF_INSTANCE_STRIDE);
					vec4 material0 = b_sdf_instances[material_base + 8u];
					vec3 hit_emissive = b_sdf_instances[material_base + 9u].xyz;
					BRANCH
					if(dot(hit_emissive, hit_emissive) <= 0.0)
					{
						vec3 hit_albedo = material0.xyz;
						uint mean_slot = uint(material0.w);
						// Slot 0 is the composer's "no mean" convention: factor only.
						if(mean_slot != 0u)
						{
							hit_albedo *= GiReflectionMeanAlbedo(mean_slot);
						}
						hit_albedo = min(hit_albedo, vec3_splat(GI_MAX_ALBEDO));
						vec3 voxel_albedo = min(measured_albedo, vec3_splat(GI_MAX_ALBEDO));
						vec3 ratio = hit_albedo /
						             max(voxel_albedo,
						                 vec3_splat(GI_REFLECTION_REMODULATE_ALBEDO_FLOOR));
						radiance *= clamp(ratio,
						                  vec3_splat(0.0),
						                  vec3_splat(GI_REFLECTION_REMODULATE_RATIO_MAX));
					}
				}
#endif // GI_LIGHT_VOXEL_READ_ALBEDO
				// FIREFLY CLAMP, the gather's per-ray contract applied to the one tier that
				// lacked it: the volume stores emissive UNBOUNDED, so a single ray landing on
				// a small bright emitter returned its full radiance - two orders over the
				// scene - and no temporal window can hide an unbounded spike (1/p samples
				// needed). Only the voxel-measured answer is capped: rough_value is last
				// frame's denoised resolve and the sky fallback is a stable per-pixel image,
				// neither a stochastic spike source.
				radiance = min(radiance, vec3_splat(GI_MAX_RAY_RADIANCE));
			}
		}
		if(clipmap_shape)
		{
			// At shape_ok 1 the mix is the identity and the fallback's fetches are dead;
			// at 0 the radiance above was skipped and the fallback answers alone.
			BRANCH
			if(shape_ok < 1.0)
			{
				radiance = mix(GiReflectionSkyFallback(uv, reflected), radiance, shape_ok);
			}
		}
	}
	else
	{
		// MISS means OPEN by our own geometry data - the SDF found nothing within 100 m along
		// this ray, so the sky answers: the authored probe layer at this pixel, SH where no
		// probe reaches. Coverage stays 1 so the miss remains an image and the temporal mean
		// integrates hit and sky fractions of the lobe together - dropping coverage instead
		// would make the composite full-cover the probes with a geometry-only mean on every
		// glossy silhouette. (The cage was tried here twice and both reads were wrong: 100 m
		// out it leaves the lattice, at the receiver it stamps the 2 m pattern - round 8.)
		radiance = GiReflectionSkyFallback(uv, reflected);
	}
	// CONSTANT ENERGY across roughness: the lobe's incoming radiance barely changes from a
	// mirror to satin, so the tier serves full weight at EVERY roughness - the early fades
	// (mirror fade, footprint confidence) removed energy at the sharp end, which read as
	// "brightness rises with roughness" next to SSR's constant-energy blur (measured, round
	// 10). Roughness now changes only WHERE the stochastic rays go, exactly as it should.
	//
	// RAW ALPHA ENCODING: coverage for the probe-layer composite in [0, 1) (shape-fade),
	// EXACTLY 1.0 for the rough tier (the classify pass writes it too), and above 1.0 the
	// HIT DISTANCE rides along - 1 + t / GI_SHADOW_DISTANCE for a full-coverage geometric
	// hit, 2.0 for a miss (the sky answered). The temporal pass rebuilds the reflected hit
	// from this to read the velocity buffer THERE (its mover gate); every >= 0.5 image test
	// downstream and the composite's saturate() are unchanged by construction.
	float alpha = coverage;
	if(hit.hit && coverage >= 1.0)
	{
		alpha = 1.0 + clamp(hit.t / GI_SHADOW_DISTANCE, 1e-3, 1.0);
	}
	else if(!hit.hit)
	{
		alpha = 2.0;
	}
	return vec4(mix(radiance, rough_value, gloss_blend), alpha);
}

#endif // __GI_REFLECTION_KERNEL_SH__
