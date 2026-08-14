$input v_texcoord0

/*
 * GI reflections (plan phase 9) - the world-space specular tier, layered UNDER SSR: drawn
 * into the reflection buffer over the authored probes, before SSR composites the sharp
 * on-screen result on top. What it adds is exactly what SSR cannot have: reflected content
 * that is off screen or behind the camera.
 *
 * ROUGHNESS TIERING, with the cutoff derived rather than tuned: past roughness ~0.4 the GGX
 * lobe is wide enough that its specular converges to the diffuse irradiance, so those pixels
 * reuse LAST frame's resolved GI - the temporally filtered, denoised per-pixel gather - and
 * pay no ray (the Lumen recipe: rough specular comes from your own gather, never from a raw
 * world lattice, whose 2 m granularity reads as mottling). Sharper lobes trace the SDF world
 * tier: mesh-exact out to a roughness-adaptive range, clipmap as a far-field FINDER with a
 * short mesh-SDF refine at the hit, light voxels at mesh-snapped hits. An unrefined clipmap
 * hit is not an image on sharp pixels - coverage goes to zero so the authored probe layer
 * shows through. The sky answers a true miss.
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
 * round 10). Alpha is coverage: mesh-exact and refined hits cover the probe layer, an
 * unrefined clipmap hit on a sharp pixel does not. SSR composites on top.
 */

#include "../common.sh"
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_LIGHT_VOXEL_READ
#include "gi/gi_light_voxels.sh"

SAMPLER2D(s_gi_normal, 5);
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
	float edge = max(field_blend, SdfClipmapEdgeBlend(level, hit_position, GI_REFLECTION_CASCADE_FADE_VOXELS));
	if(edge > 0.0 && level + 1 < SDF_CLIPMAP_LEVEL_COUNT &&
	   u_sdf_clipmap_levels[level + 1].w > 0.0)
	{
		voxel = max(voxel, u_sdf_clipmap_levels[level + 1].w);
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

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_hiz, uv, 0.0).x;
	if(depth >= 1.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
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
	vec3 rough_value = u_gi_reflection_camera.w > 0.5
	                       ? texture2DLod(s_gi_diffuse, uv, 0.0).xyz
	                       : eval_radiance_sh(s_gi_env_sh, reflected);
	BRANCH
	if(roughness >= GI_REFLECTION_ROUGH_CUTOFF)
	{
		gl_FragColor = vec4(rough_value, 1.0);
		return;
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
	BRANCH
	if(alpha_ggx > 1e-4)
	{
		// TWO independent IGN evaluations for a true 2D point: deriving the second
		// coordinate from the first put every sample on a 1D curve through the unit square,
		// so the azimuthal half of the lobe was never properly covered and high-contrast
		// regions could not converge (measured, round 13).
		float ign_a = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y));
		float ign_b = fract(52.9829189 * fract(0.06711056 * (gl_FragCoord.y + 17.0) + 0.00583715 * (gl_FragCoord.x + 31.0)));
		vec2 xi = fract(vec2(ign_a, ign_b) + u_gi_reflection_jitter.xy);
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
		hit = SdfTraceClipmap(origin, reflected, mesh_range, GI_SHADOW_DISTANCE, GI_TRACE_MAX_STEPS,
		                      GI_REFLECTION_TRACE_SURFACE_BIAS, GI_REFLECTION_TRACE_RELAXATION, true,
		                      -1.0);
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
		if(hit.exhausted ||
		   !GiLightVoxelReadFade(hit_position, hit_normal, rough_value,
		                         GI_REFLECTION_CASCADE_FADE_VOXELS, radiance))
		{
			// A gave-up march ("hits" mid-air when a grazing far ray exhausts its budget) or
			// an unmeasured coarse face must NOT answer with black. The gather's
			// honest-darkness contract exists to stop light leaking INTO the scene; a
			// reflection fallback cannot leak light anywhere - black here only punches holes
			// in the image, which grew with camera distance as rays grazed ever-coarser
			// cascades (measured, round 14). The receiver's own gather value is the smoothest
			// energy-plausible stand-in; faces MEASURED dark stay honestly dark.
			radiance = rough_value;
		}
		if(hit.instance_index == SDF_NO_INSTANCE && !hit.exhausted)
		{
			// Unrefined clipmap isosurface: legitimate lighting for satin, a wrong silhouette
			// on a mirror. Fade coverage out so the probe layer replaces the blob.
			float shape_ok = smoothstep(GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF * 0.5,
			                            GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF, roughness);
			coverage = shape_ok;
			radiance = mix(eval_radiance_sh(s_gi_env_sh, reflected), radiance, shape_ok);
		}
	}
	else
	{
		// MISS means OPEN by our own geometry data - the SDF found nothing within 100 m along
		// this ray, so the sky answers directly. The cage was tried here twice and both reads
		// were wrong: 100 m out it leaves the lattice (dead code), at the receiver it stamps
		// the 2 m interpolation pattern into smooth reflections (round 8).
		radiance = eval_radiance_sh(s_gi_env_sh, reflected);
	}
	// CONSTANT ENERGY across roughness: the lobe's incoming radiance barely changes from a
	// mirror to satin, so the tier serves full weight at EVERY roughness - the early fades
	// (mirror fade, footprint confidence) removed energy at the sharp end, which read as
	// "brightness rises with roughness" next to SSR's constant-energy blur (measured, round
	// 10). Roughness now changes only WHERE the stochastic rays go, exactly as it should.
	// Alpha is coverage for the probe-layer composite, not energy.
	gl_FragColor = vec4(mix(radiance, rough_value, gloss_blend), coverage);
}
