#ifndef __GI_PROBE_INTEGRATE_KERNEL_SH__
#define __GI_PROBE_INTEGRATE_KERNEL_SH__

/*
 * GI integration (plan 3.4): per pixel, the four probes bracketing it, weighted by bilinear
 * position and plane agreement, each evaluated at the PIXEL's own normal from its convolved
 * octahedral irradiance tile. No layers, no traced fallback, no contact rays - pixels no probe
 * serves fall back to the WORLD probes (positional and stable), then to the environment SH.
 * Contact detail is the short-range AO's job, composited after the temporal filter (Phase 6).
 *
 * Output convention unchanged from v1: rgb = irradiance/pi, a = the weight with which it
 * replaces the environment term downstream.
 *
 * SHARED KERNEL BODY, two consumers: fs_gi_probe_integrate_temporal.sc fuses the temporal
 * blend onto the gather (the deliverable path - the gather never round-trips through the
 * GI_TRACE target), fs_gi_probe_integrate.sc is the split fallback feeding the standalone
 * temporal pass. The jitter's pixel coordinate arrives as a parameter: on the D3D backend
 * gl_FragCoord exists only inside main.
 */

#include "gi/sdf_common.sh"
#define GI_WORLD_PROBE_READ
#include "gi/gi_world_probes.sh"
#include "gi/gi_probe_common.sh"
#include "gi/gi_noise.sh"

BUFFER_RO(b_gi_probes, vec4, 7);
SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// The convolved irradiance tiles from cs_gi_screen_probe_filter.
SAMPLER2D(s_probe_irradiance, 2);
/// The GTAO output (rgb = world bent normal * 0.5 + 0.5, a = visibility), full resolution.
/// Stage 13 is b_sdf_grid_instances in sdf_common.sh, which this program never references
/// (no SDF march here), so the register is free on every backend.
SAMPLER2D(s_gi_gtao, 13);

/// xyz = camera position, w = frame index.
uniform vec4 u_gi_camera;
/// xy = this frame's R2 offset for the interpolation jitter, computed in double on the CPU
/// (see the trace kernel's note on float(frame) precision). zw unused.
uniform vec4 u_gi_jitter;

/// x = settings.intensity, the artistic multiplier on the gathered bounce. Applied to the
/// output rgb only: alpha keeps the measured weight that replaces the environment term
/// downstream, so the knob scales the scene's bounce without eating the sky fallback.
/// y = 1 when the GTAO texture is bound, z = its bent-normal strength: the irradiance tiles
/// (and the world-probe fallback) are then read at the BENT normal - the mean unoccluded
/// direction - so a pixel under an overhang gathers the light it can actually see instead
/// of the full cosine lobe around its geometric normal. Only the lookup direction moves; the
/// plane tests keep the geometric normal. The VISIBILITY multiply stays out of here on
/// purpose: within-plane AO structure is exactly what the plane-guided denoise chain
/// dilutes, so it applies at full resolution after the chain, in the lighting. w unused.
uniform vec4 u_gi_intensity;
#define u_gi_gtao_bound     u_gi_intensity.y
#define u_gi_gtao_bent      u_gi_intensity.z

#define GI_INTEGRATE_PLANE_TOLERANCE 0.05

/// Accumulates the 2x2 probe bracket at @p base: bilinear x plane weights, irradiance sampled
/// at the pixel normal via octahedral-wrapped manual bilinear. One function so the jittered and
/// the fallback unjittered brackets run identical code.
void GiGatherBracket(vec2 base, vec2 frac, vec3 world_position, vec3 world_normal,
                       float plane_tolerance, vec2 oct_base, vec2 oct_frac,
                       inout vec3 radiance, inout float measured, inout float weight_sum,
                       inout float screen_share)
{
	for(int j = 0; j < 2; ++j)
	{
		for(int i = 0; i < 2; ++i)
		{
			int px = int(clamp(base.x + float(i), 0.0, float(u_gi_probe_count_x - 1)));
			int py = int(clamp(base.y + float(j), 0.0, float(u_gi_probe_count_y - 1)));
			uint record =
			    (GiProbeRecord(px, py, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
			if(meta.w < 0.5)
			{
				continue;
			}
			float plane = abs(dot(meta.xyz - world_position, world_normal));
			float plane_weight = saturate(1.0 - plane / plane_tolerance);
			float bilinear =
			    (i == 0 ? 1.0 - frac.x : frac.x) * (j == 0 ? 1.0 - frac.y : frac.y);
			float weight = max(bilinear, 0.01) * plane_weight;
			if(weight <= 1e-4)
			{
				continue;
			}
			ivec2 tile_base = GiProbeAtlasBase(px, py, 0);
			vec4 probe_irradiance = vec4_splat(0.0);
			for(int tap = 0; tap < 4; ++tap)
			{
				ivec2 offset = ivec2(tap % 2, tap / 2);
				ivec2 wrapped = GiOctWrapTexel(ivec2(oct_base) + offset);
				float tap_weight = (offset.x == 0 ? 1.0 - oct_frac.x : oct_frac.x) *
				                   (offset.y == 0 ? 1.0 - oct_frac.y : oct_frac.y);
				probe_irradiance += texelFetch(s_probe_irradiance, tile_base + wrapped, 0) * tap_weight;
			}
			radiance += max(probe_irradiance.xyz, vec3_splat(0.0)) * weight;
			measured += saturate(probe_irradiance.w) * weight;
			weight_sum += weight;
			screen_share += saturate(b_gi_probes[record + uint(GI_PROBE_SCREEN_SHARE)].x) * weight;
		}
	}
}

/// The whole gather for one pixel: rgb = irradiance/pi, a = the measured weight. Depth and
/// the world reconstruction are handed back so the fused temporal reuses them in registers
/// (on the sky and degenerate-normal early-outs the position is never consumed downstream -
/// the temporal's sky test fires first). @p out_screen_share is the bracket-weighted share
/// of the gather that the SCREEN tier answered (0 when the world probes answer), the
/// temporal's camera-motion collapse weight.
vec4 GiIntegrateGather(vec2 uv, vec2 frag_coord, out float out_depth, out vec3 out_world_position,
                       out float out_screen_share)
{
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	out_depth = depth;
	out_world_position = vec3_splat(0.0);
	out_screen_share = 0.0;
	if(depth >= 1.0)
	{
		return vec4_splat(0.0);
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	out_world_position = world_position;
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		return vec4_splat(0.0);
	}
	vec3 world_normal = normalize(nd.world_normal);
	// The direction the irradiance is read at: the GTAO bent normal when bound (see the
	// u_gi_intensity note), the geometric normal otherwise.
	vec3 lookup_normal = world_normal;
	if(u_gi_gtao_bound > 0.5)
	{
		vec4 gtao = texture2DLod(s_gi_gtao, uv, 0.0);
		vec3 bent_normal = gtao.xyz * 2.0 - vec3_splat(1.0);
		if(dot(bent_normal, bent_normal) > 1e-4)
		{
			// The lookup follows the bent normal by how much is occluded (an open pixel keeps its
			// normal), scaled by the strength knob.
			vec3 occluded_normal = normalize(mix(normalize(bent_normal), world_normal, gtao.a));
			lookup_normal = normalize(mix(world_normal, occluded_normal, u_gi_gtao_bent));
		}
	}
	float view_distance = max(length(world_position - u_gi_camera.xyz), 1e-3);
	float plane_tolerance = GI_INTEGRATE_PLANE_TOLERANCE * view_distance;
	vec2 pixel = uv * u_gi_probe_screen.xy;
	vec2 grid = pixel / u_gi_probe_spacing - vec2_splat(0.5);
	// Interpolation JITTER [S21 s39]: offsetting which bracket a pixel reads spatially
	// distributes probe-to-probe differences, which the temporal chain then integrates -
	// without it the probe lattice prints through as tile-sized plateaus. Two-channel IGN
	// per pixel, advanced per frame by R2 in value space (gi_noise.sh).
	vec2 frame_r2 = u_gi_jitter.xy;
	vec2 noise = fract(GiIgnNoise(ivec2(frag_coord.xy)) + frame_r2);
	vec2 jitter = (noise - vec2_splat(0.5)) * GI_INTERPOLATION_JITTER_TILES;
	vec2 jittered_grid = grid + jitter;
	vec2 base = floor(jittered_grid);
	vec2 frac = jittered_grid - base;
	// The lookup normal in octahedral texel space, shared by every probe tap.
	vec2 oct_texel = GiOctEncode(lookup_normal) * float(GI_PROBE_DIR_EDGE) - vec2_splat(0.5);
	vec2 oct_base = floor(oct_texel);
	vec2 oct_frac = oct_texel - oct_base;
	vec3 irradiance = vec3_splat(0.0);
	float measured = 0.0;
	float weight_sum = 0.0;
	float screen_share = 0.0;
	GiGatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
	                  irradiance, measured, weight_sum, screen_share);
	// The PLANE CONSTRAINT on the jitter: a jittered bracket whose probes all fail the plane
	// test would fall through to the world probes and flicker at silhouettes; the unjittered
	// bracket answers instead, which is the "only when the target stays in the pixel's plane"
	// rule [S21 s39] expressed as a fallback.
	if(weight_sum <= 1e-4)
	{
		base = floor(grid);
		frac = grid - base;
		GiGatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
		                  irradiance, measured, weight_sum, screen_share);
	}
	if(weight_sum > 1e-4 && measured > 1e-4)
	{
		out_screen_share = saturate(screen_share / weight_sum);
		// Normalised over the measured fraction, weighted out over what the probes vouch for.
		return vec4(irradiance / measured * u_gi_intensity.x, saturate(measured / weight_sum));
	}
	// No screen probe serves this pixel: the world probes answer - positional, stable, leak
	// guarded - before anything falls to the environment term downstream.
	vec3 world_irradiance;
	float sky_fraction;
	if(GiWorldProbeIrradianceCascade(world_position,
	                                 lookup_normal,
	                                 normalize(u_gi_camera.xyz - world_position),
	                                 u_gi_camera.xyz,
	                                 world_irradiance,
	                                 sky_fraction))
	{
		return vec4(world_irradiance * u_gi_intensity.x, 1.0);
	}
	return vec4_splat(0.0);
}

#endif // __GI_PROBE_INTEGRATE_KERNEL_SH__
