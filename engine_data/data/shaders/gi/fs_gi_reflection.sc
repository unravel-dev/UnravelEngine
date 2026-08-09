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
 * tier (mesh-exact within GI_REFLECTION_MESH_SDF_RANGE, light voxels at hits, the sky past
 * everything).
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
 * round 10); the composite fully covers the authored probe layer where this pass runs, and
 * SSR composites on top.
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
	// Beyond the outermost cascade a world ray has nothing left to test against - the same
	// bound the shadow rays derive (GI_SHADOW_DISTANCE); the probe cage answers past it.
	//
	// DIRECT two-tier call rather than SdfTraceRayEx: that helper hard-caps its detail tier
	// at GI_MESH_SDF_TRACE_RANGE (2 m, the gather's cost bound), and clipmap-tier hits
	// inflate silhouettes by design - acceptable inside an irradiance estimate, directly
	// visible in a mirror IMAGE (fattened boxes and silhouette halos, measured round 5).
	// Reflection rays therefore run mesh-exact SDFs to the finest cascade's full extent and
	// touch the clipmap only past it, where the confidence formula below guarantees the lobe
	// footprint already spans multiple voxels. Expansion NEVER (-1) on the clipmap segment:
	// the same image-vs-estimate argument (round 3); shadow rays made the same trade, and the
	// visibility-gated light voxels at the hit remain the leak defence.
	SdfRayHit hit = SdfTraceInstances(origin, reflected, 0.0,
	                                  min(GI_REFLECTION_MESH_SDF_RANGE, GI_SHADOW_DISTANCE),
	                                  GI_TRACE_MAX_STEPS, GI_PROBE_TRACE_SURFACE_BIAS,
	                                  GI_PROBE_TRACE_RELAXATION, true);
	if(!hit.hit)
	{
		hit = SdfTraceClipmap(origin, reflected, GI_REFLECTION_MESH_SDF_RANGE, GI_SHADOW_DISTANCE,
		                      GI_TRACE_MAX_STEPS, GI_PROBE_TRACE_SURFACE_BIAS,
		                      GI_PROBE_TRACE_RELAXATION, true, -1.0);
	}
	vec3 radiance;
	if(hit.hit)
	{
		vec3 hit_position = origin + reflected * hit.t;
		vec3 hit_normal = hit.normal;
		if(dot(hit_normal, reflected) > 0.0)
		{
			hit_normal = -hit_normal;
		}
		if(hit.exhausted || !GiLightVoxelRead(hit_position, hit_normal, radiance))
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
		// NO cage blend for gloss: the light-voxel read above is already the world tier's
		// prefilter (25 cm blocks, naturally soft), and per-pixel cage reads stamp the 2 m
		// lattice's interpolation pattern into the image - blotches, not blur, nothing like
		// SSR's filtered spread (measured rounds 2 and 8). Blur beyond voxel scale comes from
		// the stochastic lobe integration and the continuity fade into the gather value.
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
	// Coarse voxel detail at mirror roughness is bounded by mesh-exact silhouettes and the
	// temporal integration rather than hidden by darkness, and SSR still composites its
	// crisp result on top.
	gl_FragColor = vec4(mix(radiance, rough_value, gloss_blend), 1.0);
}
