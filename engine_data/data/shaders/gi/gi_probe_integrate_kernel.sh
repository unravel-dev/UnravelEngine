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

/// xyz = camera position, w = frame index.
uniform vec4 u_gi_camera;
/// xy = this frame's R2 offset for the interpolation jitter, computed in double on the CPU
/// (see the trace kernel's note on float(frame) precision). zw unused.
uniform vec4 u_gi_jitter;

/// x = settings.intensity, the artistic multiplier on the gathered bounce. Applied to the
/// output rgb only: alpha keeps the measured weight that replaces the environment term
/// downstream, so the knob scales the scene's bounce without eating the sky fallback.
/// yzw unused. (Contact AO deliberately does NOT live here: within-plane AO structure is
/// exactly what the plane-guided denoise chain dilutes - it applies at FULL resolution after
/// the chain, in fs_gi_upsample.)
uniform vec4 u_gi_intensity;

#define GI_INTEGRATE_PLANE_TOLERANCE 0.05

/// Accumulates the 2x2 probe bracket at @p base: bilinear x plane weights, irradiance sampled
/// at the pixel normal via octahedral-wrapped manual bilinear. One function so the jittered and
/// the fallback unjittered brackets run identical code.
void GiGatherBracket(vec2 base, vec2 frac, vec3 world_position, vec3 world_normal,
                       float plane_tolerance, vec2 oct_base, vec2 oct_frac,
                       inout vec3 radiance, inout float measured, inout float weight_sum)
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
		}
	}
}

/// The whole gather for one pixel: rgb = irradiance/pi, a = the measured weight. Depth and
/// the world reconstruction are handed back so the fused temporal reuses them in registers
/// (on the sky and degenerate-normal early-outs the position is never consumed downstream -
/// the temporal's sky test fires first).
vec4 GiIntegrateGather(vec2 uv, vec2 frag_coord, out float out_depth, out vec3 out_world_position)
{
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	out_depth = depth;
	out_world_position = vec3_splat(0.0);
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
	// The pixel normal in octahedral texel space, shared by every probe tap.
	vec2 oct_texel = GiOctEncode(world_normal) * float(GI_PROBE_DIR_EDGE) - vec2_splat(0.5);
	vec2 oct_base = floor(oct_texel);
	vec2 oct_frac = oct_texel - oct_base;
	vec3 irradiance = vec3_splat(0.0);
	float measured = 0.0;
	float weight_sum = 0.0;
	GiGatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
	                  irradiance, measured, weight_sum);
	// The PLANE CONSTRAINT on the jitter: a jittered bracket whose probes all fail the plane
	// test would fall through to the world probes and flicker at silhouettes; the unjittered
	// bracket answers instead, which is the "only when the target stays in the pixel's plane"
	// rule [S21 s39] expressed as a fallback.
	if(weight_sum <= 1e-4)
	{
		base = floor(grid);
		frac = grid - base;
		GiGatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
		                  irradiance, measured, weight_sum);
	}
	if(weight_sum > 1e-4 && measured > 1e-4)
	{
		// Normalised over the measured fraction, weighted out over what the probes vouch for.
		return vec4(irradiance / measured * u_gi_intensity.x, saturate(measured / weight_sum));
	}
	// No screen probe serves this pixel: the world probes answer - positional, stable, leak
	// guarded - before anything falls to the environment term downstream.
	vec3 world_irradiance;
	float sky_fraction;
	if(GiWorldProbeIrradianceCascade(world_position,
	                                 world_normal,
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
