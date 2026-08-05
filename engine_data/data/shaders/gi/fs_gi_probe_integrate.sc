$input v_texcoord0

/*
 * Integrates the screen-space radiance probes into the per-pixel indirect diffuse estimate.
 *
 * Per pixel: find the four probes bracketing it, weight them by bilinear position, plane
 * agreement and facing, and evaluate each probe's SH irradiance around the PIXEL's own normal.
 * The full-sphere probes are what make that sharing work -- neighbouring pixels with different
 * normals integrate different hemispheres of the same probes.
 *
 * The weights are the silhouette guard. A probe anchored across a depth break fails the plane
 * test against this pixel; one on a perpendicular surface fails the facing test. When all four
 * fail -- a pixel on a sliver no probe anchored on this frame -- the output weight collapses to
 * zero and the consumer's environment probe covers it for the frame; the anchor jitter re-rolls
 * every frame, so some frames anchor ON the sliver, and the screen temporal filter averages the
 * states into a stable value. That temporal fallback is why this pass can stay this simple.
 *
 * Output convention identical to fs_gi_resolve.sc: RGB = radiance-mean units, A = the weight
 * with which it replaces the environment probe. The temporal, denoise and upsample passes
 * downstream neither know nor care which gather produced the buffer.
 */

#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_ONLY
// GiHashUint / GiHashCombine, used by the probe header's jitter helpers.
#include "gi/radiance_cache.sh"
#include "gi/gi_probe_common.sh"

BUFFER_RO(b_gi_probes, vec4, 0);

SAMPLER2D(s_gi_depth, 1);
SAMPLER2D(s_gi_normal, 2);
/// This frame's raw radiance atlas, read only by the debug views.
SAMPLER2D(s_probe_radiance, 3);

/// xyz = camera position. Named separately from the gather uniforms so this pass binds only
/// what it reads.
uniform vec4 u_gi_integrate_camera;

/// The probe debug views (u_gi_probe_debug_mode, declared with the shared probe uniforms)
/// separate the three places instability can originate, invisible in the lit image and in each
/// other:
///   1 = the raw atlas IN PLACE: every tile shows its probe's 8x8 octahedral texels. A texel
///       blinking here is trace-side variance (direction, hit, or cache read); if the atlas is
///       still but the image dances, the fault is downstream of tracing.
///   2 = integration health: R = pixel weight sum (dark = probe starvation, the pixel is
///       falling back to the environment), G = resolved fraction.
///   3 = history state: R = blend weight this frame (red = history cut / fresh), G = sample
///       count over its cap (green = converged). Flickering red on a STATIC camera means the
///       validity test is cutting history that should hold -- report exactly that.
/// Debug output alpha is BELOW one so the presentation blit can blend the view over the lit
/// frame, keeping the scene readable underneath the readout.
#define u_gi_probe_debug u_gi_probe_debug_mode
#define GI_PROBE_DEBUG_ALPHA 0.65

/// How far off the pixel's plane a probe may anchor before it stops contributing, as a fraction
/// of view distance -- the same convention as the denoiser's plane tolerance.
#define GI_PROBE_INTEGRATE_PLANE_TOLERANCE 0.05
/// Exponent on facing agreement. High enough that light does not turn corners, low enough that
/// probes on a curved surface still share.
#define GI_PROBE_INTEGRATE_NORMAL_POWER 8.0
/// Weight budget a pixel is topped up to when the gated taps cannot fill it. Around geometric
/// detail -- window reveals, cornices, hedges -- all four bracketing probes can anchor on OTHER
/// surfaces and fail the plane gate; without a fallback those pixels collapse to the environment
/// probe, and because anchors hop as the camera turns, the STARVED SET changes per frame at tile
/// granularity: light visibly crawls. The top-up blends in the best facing-valid probe by exactly
/// the missing weight, so it fades in as the legitimate weight fades out and nothing pops. The
/// cost is bounded light bleed across detail smaller than a tile, which is the strictly better
/// artefact. (The complete fix is adaptive probe placement; this is the proportionate one.)
#define GI_PROBE_INTEGRATE_MIN_WEIGHT 0.15

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
	vec3 world_normal = nd.world_normal;
	if(dot(world_normal, world_normal) < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	world_normal = normalize(world_normal);
	if(u_gi_probe_debug == 1)
	{
		// The raw atlas in place: pixel -> its tile's probe -> the texel its tile-local position
		// maps to. Output weight 1 so the consumer shows it at full strength.
		vec2 pixel_pos = uv * u_gi_probe_screen.xy;
		vec2 tile = floor(pixel_pos / u_gi_probe_spacing);
		tile.x = clamp(tile.x, 0.0, float(u_gi_probe_count_x - 1));
		tile.y = clamp(tile.y, 0.0, float(u_gi_probe_count_y - 1));
		vec2 tile_local = fract(pixel_pos / u_gi_probe_spacing);
		ivec2 texel = ivec2(tile) * GI_PROBE_DIR_EDGE +
		              ivec2(clamp(tile_local * float(GI_PROBE_DIR_EDGE),
		                          vec2_splat(0.0),
		                          vec2_splat(float(GI_PROBE_DIR_EDGE) - 1.0)));
		gl_FragColor = vec4(texelFetch(s_probe_radiance, texel, 0).xyz, GI_PROBE_DEBUG_ALPHA);
		return;
	}
	if(u_gi_probe_debug == 3)
	{
		vec2 pixel_pos = uv * u_gi_probe_screen.xy;
		vec2 tile = floor(pixel_pos / u_gi_probe_spacing);
		int px = int(clamp(tile.x, 0.0, float(u_gi_probe_count_x - 1)));
		int py = int(clamp(tile.y, 0.0, float(u_gi_probe_count_y - 1)));
		vec4 history =
		    b_gi_probes[(GiProbeIndex(px, py) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE) + 11u];
		gl_FragColor = vec4(history.y, saturate(history.x / 32.0), 0.0, GI_PROBE_DEBUG_ALPHA);
		return;
	}
	float view_distance = max(length(world_position - u_gi_integrate_camera.xyz), 1e-3);
	float plane_tolerance = GI_PROBE_INTEGRATE_PLANE_TOLERANCE * view_distance;
	// The probe lattice sits at tile centres: probe (i, j) anchors within tile (i, j), so the
	// continuous probe coordinate of a pixel is its position in tile units, minus the half-tile
	// that puts coordinate 0 at the first tile's centre.
	vec2 pixel = uv * u_gi_probe_screen.xy;
	vec2 grid = pixel / u_gi_probe_spacing - vec2_splat(0.5);
	vec2 base = floor(grid);
	vec2 frac = grid - base;
	float basis[9];
	GiShBasis(world_normal, basis);
	float weights[9];
	GiShIrradianceWeights(weights);
	vec3 radiance = vec3_splat(0.0);
	float resolved = 0.0;
	float weight_sum = 0.0;
	// The best facing-valid probe regardless of the plane gate, for the starvation top-up below.
	float fallback_score = 0.0;
	uint fallback_base = 0u;
	bool fallback_found = false;
	for(int j = 0; j < 2; ++j)
	{
		for(int i = 0; i < 2; ++i)
		{
			int px = int(clamp(base.x + float(i), 0.0, float(u_gi_probe_count_x - 1)));
			int py = int(clamp(base.y + float(j), 0.0, float(u_gi_probe_count_y - 1)));
			uint probe_base = (GiProbeIndex(px, py) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			vec4 meta = b_gi_probes[probe_base + uint(GI_PROBE_META)];
			if(meta.w < 0.5)
			{
				continue;
			}
			vec4 meta2 = b_gi_probes[probe_base + uint(GI_PROBE_META2)];
			float bilinear = (i == 0 ? 1.0 - frac.x : frac.x) * (j == 0 ? 1.0 - frac.y : frac.y);
			float facing_raw = dot(meta2.xyz, world_normal);
			// Fallback candidate: any probe not facing away. Gated far looser than the
			// contribution below on purpose -- it only ever fires when everything stricter
			// has already been rejected.
			if(facing_raw > 0.0 && bilinear > fallback_score)
			{
				fallback_score = bilinear;
				fallback_base = probe_base;
				fallback_found = true;
			}
			float plane = abs(dot(meta.xyz - world_position, world_normal));
			float plane_weight = saturate(1.0 - plane / plane_tolerance);
			float facing = pow(saturate(facing_raw), GI_PROBE_INTEGRATE_NORMAL_POWER);
			float weight = max(bilinear, 0.01) * plane_weight * facing;
			if(weight <= 1e-4)
			{
				continue;
			}
			// SH irradiance around the PIXEL's normal, directly in radiance-mean units; the
			// resolved flag integrates through the same convolution so the output weight stays
			// directional -- a probe that only measured half its sphere only vouches for half.
			vec3 probe_radiance = vec3_splat(0.0);
			float probe_resolved = 0.0;
			for(int k = 0; k < 9; ++k)
			{
				vec4 coefficient = b_gi_probes[probe_base + uint(k)];
				float shaped = basis[k] * weights[k];
				probe_radiance += coefficient.xyz * shaped;
				probe_resolved += coefficient.w * shaped;
			}
			radiance += max(probe_radiance, vec3_splat(0.0)) * weight;
			resolved += saturate(probe_resolved) * weight;
			weight_sum += weight;
		}
	}
	// Starvation top-up: when the gated taps cannot fill the weight budget, the best facing-valid
	// probe supplies exactly the missing weight. Continuous by construction -- the top-up shrinks
	// to zero as legitimate weight appears -- so the starved-set churn that crawled across detail
	// as tiles cannot flip pixels between probe light and the environment any more.
	if(weight_sum < GI_PROBE_INTEGRATE_MIN_WEIGHT && fallback_found)
	{
		float top_up = GI_PROBE_INTEGRATE_MIN_WEIGHT - weight_sum;
		vec3 probe_radiance = vec3_splat(0.0);
		float probe_resolved = 0.0;
		for(int k = 0; k < 9; ++k)
		{
			vec4 coefficient = b_gi_probes[fallback_base + uint(k)];
			float shaped = basis[k] * weights[k];
			probe_radiance += coefficient.xyz * shaped;
			probe_resolved += coefficient.w * shaped;
		}
		radiance += max(probe_radiance, vec3_splat(0.0)) * top_up;
		resolved += saturate(probe_resolved) * top_up;
		weight_sum = GI_PROBE_INTEGRATE_MIN_WEIGHT;
	}
	if(u_gi_probe_debug == 2)
	{
		gl_FragColor = vec4(saturate(weight_sum),
		                    weight_sum > 1e-4 ? saturate(resolved / weight_sum) : 0.0,
		                    0.0,
		                    GI_PROBE_DEBUG_ALPHA);
		return;
	}
	if(weight_sum <= 1e-4 || resolved <= 1e-4)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	// Same convention as the ray gather: RGB is the estimate over the MEASURED fraction of the
	// hemisphere and A is that fraction, because the consumer computes mix(probe, rgb * PI, a).
	// The SH irradiance already contains the dimming of unmeasured directions (their radiance
	// projected as zero), so it is divided back out here and A re-applies it -- without this the
	// unmeasured fraction would darken the image twice.
	gl_FragColor = vec4(radiance / resolved, saturate(resolved / weight_sum));
}
