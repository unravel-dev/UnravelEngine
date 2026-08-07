$input v_texcoord0

/*
 * GI v2 integration (plan 3.4): per pixel, the four probes bracketing it, weighted by bilinear
 * position and plane agreement, each evaluated at the PIXEL's own normal from its convolved
 * octahedral irradiance tile. No layers, no traced fallback, no contact rays - pixels no probe
 * serves fall back to the WORLD probes (positional and stable), then to the environment SH.
 * Contact detail is the short-range AO's job, composited after the temporal filter (Phase 6).
 *
 * Output convention unchanged from v1: rgb = irradiance/pi, a = the weight with which it
 * replaces the environment term downstream. The temporal/denoise/upsample chain is untouched.
 */

#include "../common.sh"
#include "../lighting.sh"

#include "gi/sdf_common.sh"
#define GI_WORLD_PROBE_READ
#include "gi/gi_world_probes.sh"
#include "gi/gi_probe_common.sh"

BUFFER_RO(b_gi_probes, vec4, 7);
SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// The convolved irradiance tiles from cs_gi_screen_probe_filter_v2.
SAMPLER2D(s_probe_irradiance, 2);

/// xyz = camera position, w = frame index.
uniform vec4 u_gi_v2_camera;

/// x = settings.intensity, the artistic multiplier on the gathered bounce. Applied to the
/// output rgb only: alpha keeps the measured weight that replaces the environment term
/// downstream, so the knob scales the scene's bounce without eating the sky fallback.
/// y > 0 = contact AO enabled. z > 0 = contact AO debug view (grayscale AO instead of GI).
uniform vec4 u_gi_v2_intensity;

/// Golden angle in radians - a mathematical constant (pi * (3 - sqrt(5))), not a tuning value:
/// successive spiral taps land maximally far apart in angle.
#define GI_CONTACT_AO_GOLDEN_ANGLE 2.39996323

#define GI_V2_INTEGRATE_PLANE_TOLERANCE 0.05

/// Accumulates the 2x2 probe bracket at @p base: bilinear x plane weights, irradiance sampled
/// at the pixel normal via octahedral-wrapped manual bilinear. One function so the jittered and
/// the fallback unjittered brackets run identical code.
void GiV2GatherBracket(vec2 base, vec2 frac, vec3 world_position, vec3 world_normal,
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

/*
 * Short-range CONTACT occlusion (plan phase 6, the Lumen ShortRangeAO role): the Alchemy
 * estimator [Alchemy11] over a golden-angle spiral of depth taps within GI_CONTACT_AO_RADIUS.
 * This is the pixel-rate darkening the probe gather cannot express: occlusion features
 * narrower than a probe tile (the wall strip under an awning, the base of a railing) are
 * averaged away by plane-weighted probe sharing, and the voxel radiance is blind below the
 * attribute resolution. Runs HERE, at trace resolution BEFORE the temporal filter, so the
 * existing temporal + a-trous + bilateral chain absorbs its per-pixel variance - which is
 * what lets GI_CONTACT_AO_SAMPLES stay small. Multiplies the gathered indirect only; direct
 * lighting keeps its shadow maps.
 */
float GiV2ContactAO(vec2 uv, vec3 world_position, vec3 world_normal, float ign)
{
	// Screen-space radius of a world-space GI_CONTACT_AO_RADIUS disc at this depth, measured
	// by projecting an actual world-space offset - no assumptions about the projection's
	// element layout. The tangent direction is arbitrary; only the projected LENGTH is used.
	vec3 tangent = normalize(cross(world_normal, abs(world_normal.y) < 0.9
	                                                 ? vec3(0.0, 1.0, 0.0)
	                                                 : vec3(1.0, 0.0, 0.0)));
	vec4 center_clip = mul(u_viewProj, vec4(world_position, 1.0));
	vec4 rim_clip = mul(u_viewProj, vec4(world_position + tangent * GI_CONTACT_AO_RADIUS, 1.0));
	if(center_clip.w <= 0.0 || rim_clip.w <= 0.0)
	{
		return 1.0;
	}
	vec2 uv_radius = abs(rim_clip.xy / rim_clip.w - center_clip.xy / center_clip.w) * 0.5;
	float radius = max(uv_radius.x, uv_radius.y);
	if(radius <= 1e-5)
	{
		return 1.0;
	}
	// A FIXED fraction of the radius, deliberately not distance-scaled: the bias exists to
	// reject centimetre-scale depth-reconstruction jitter, and a distance term grows past the
	// depth of real occluders within a few metres (measured: railings vanished at 3 m).
	float bias = GI_CONTACT_AO_BIAS * GI_CONTACT_AO_RADIUS;
	float epsilon = 0.0001 * GI_CONTACT_AO_RADIUS * GI_CONTACT_AO_RADIUS;
	float angle = ign * 2.0 * PI;
	float occlusion = 0.0;
	LOOP for(int i = 0; i < GI_CONTACT_AO_SAMPLES; ++i)
	{
		// sqrt-distributed radii cover the disc area-uniformly; golden-angle steps decorrelate
		// the angles; IGN rotates the whole spiral per pixel and per frame for the temporal
		// chain to integrate.
		float t = (float(i) + 0.5) / float(GI_CONTACT_AO_SAMPLES);
		float a = angle + float(i) * GI_CONTACT_AO_GOLDEN_ANGLE;
		vec2 tap_uv = uv + vec2(cos(a), sin(a)) * (radius * sqrt(t));
		if(any(lessThan(tap_uv, vec2_splat(0.0))) || any(greaterThan(tap_uv, vec2_splat(1.0))))
		{
			continue;
		}
		float tap_depth = texture2DLod(s_gi_depth, tap_uv, 0.0).x;
		if(tap_depth >= 1.0)
		{
			continue;
		}
		vec3 tap_clip = clipTransform(vec3(tap_uv * 2.0 - 1.0, toClipSpaceDepth(tap_depth)));
		vec3 tap_world = clipToWorld(u_invViewProj, tap_clip);
		vec3 v = tap_world - world_position;
		// [Alchemy11]: occlusion falls off as 1/|v|^2 and only geometry ABOVE the tangent
		// plane (v.n past the depth-proportional bias) occludes. A tap on far background
		// reconstructs a huge |v| and contributes nothing; a foreground occluder hanging over
		// this pixel reconstructs right where it occludes.
		occlusion += max(0.0, dot(v, world_normal) - bias) / (dot(v, v) + epsilon);
	}
	// Radius-normalised so the sum is dimensionless: a tap at distance R directly above the
	// plane contributes 1/R x R = 1 unit of the K-sample budget.
	float normalised = occlusion * GI_CONTACT_AO_RADIUS * (2.0 / float(GI_CONTACT_AO_SAMPLES));
	return saturate(1.0 - normalised);
}

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	if(depth >= 1.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 world_normal = normalize(nd.world_normal);
	float view_distance = max(length(world_position - u_gi_v2_camera.xyz), 1e-3);
	float plane_tolerance = GI_V2_INTEGRATE_PLANE_TOLERANCE * view_distance;
	vec2 pixel = uv * u_gi_probe_screen.xy;
	vec2 grid = pixel / u_gi_probe_spacing - vec2_splat(0.5);
	// Interpolation JITTER [S21 s39]: offsetting which bracket a pixel reads spatially
	// distributes probe-to-probe differences, which the temporal chain then integrates -
	// without it the probe lattice prints through as tile-sized plateaus. Interleaved
	// gradient noise, animated by frame: spatially low-discrepancy, deterministic.
	float ign = fract(52.9829189 *
	                  fract(0.06711056 * (gl_FragCoord.x + 5.588238 * u_gi_v2_camera.w) +
	                        0.00583715 * gl_FragCoord.y));
	float ign2 = fract(52.9829189 *
	                   fract(0.06711056 * (gl_FragCoord.y + 5.588238 * u_gi_v2_camera.w) +
	                         0.00583715 * gl_FragCoord.x));
	float contact_ao = 1.0;
	BRANCH
	if(u_gi_v2_intensity.y > 0.5)
	{
		contact_ao = GiV2ContactAO(uv, world_position, world_normal, ign);
	}
	BRANCH
	if(u_gi_v2_intensity.z > 0.5)
	{
		gl_FragColor = vec4(contact_ao, contact_ao, contact_ao, 1.0);
		return;
	}
	vec2 jitter = (vec2(ign, ign2) - vec2_splat(0.5)) * GI_V2_INTERPOLATION_JITTER_TILES;
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
	GiV2GatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
	                  irradiance, measured, weight_sum);
	// The PLANE CONSTRAINT on the jitter: a jittered bracket whose probes all fail the plane
	// test would fall through to the world probes and flicker at silhouettes; the unjittered
	// bracket answers instead, which is the "only when the target stays in the pixel's plane"
	// rule [S21 s39] expressed as a fallback.
	if(weight_sum <= 1e-4)
	{
		base = floor(grid);
		frac = grid - base;
		GiV2GatherBracket(base, frac, world_position, world_normal, plane_tolerance, oct_base, oct_frac,
		                  irradiance, measured, weight_sum);
	}
	if(weight_sum > 1e-4 && measured > 1e-4)
	{
		// Normalised over the measured fraction, weighted out over what the probes vouch for.
		gl_FragColor = vec4(irradiance / measured * (u_gi_v2_intensity.x * contact_ao),
		                    saturate(measured / weight_sum));
		return;
	}
	// No screen probe serves this pixel: the world probes answer - positional, stable, leak
	// guarded - before anything falls to the environment term downstream.
	vec3 world_irradiance;
	float sky_fraction;
	if(GiWorldProbeIrradianceCascade(world_position,
	                                 world_normal,
	                                 normalize(u_gi_v2_camera.xyz - world_position),
	                                 u_gi_v2_camera.xyz,
	                                 world_irradiance,
	                                 sky_fraction))
	{
		gl_FragColor = vec4(world_irradiance * (u_gi_v2_intensity.x * contact_ao), 1.0);
		return;
	}
	gl_FragColor = vec4_splat(0.0);
}
