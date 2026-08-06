$input v_texcoord0

/*
 * Integrates the screen-space radiance probes into the per-pixel indirect diffuse estimate --
 * and TRACES the pixels no probe can serve.
 *
 * Per pixel: find the eight probes bracketing it (four tiles, two layers), weight them by
 * bilinear position, plane agreement, facing and measurement CONFIDENCE, and evaluate each
 * probe's SH irradiance around the PIXEL's own normal. The full-sphere probes are what make that
 * sharing work -- neighbouring pixels with different normals integrate different hemispheres of
 * the same probes.
 *
 * The weights are the silhouette guard, and their failure mode is the pass's second job. Probes
 * are a TILE-scale representation: pixels inside grooves, reveals and slit geometry can have no
 * probe whose plane, facing and confidence all agree -- by construction, since anchor selection
 * deliberately refuses to place probes in measurement-hostile spots. Those pixels fall back to
 * the PER-PIXEL traced gather, through the identical per-ray pipeline (gi_gather_common.sh),
 * blended in continuously as probe coverage fades out. That is the hybrid that ends the
 * crevice ping-pong: probes serve the 95% of pixels they represent well, honest rays serve the
 * rest, and the blend weight is a continuous function of coverage so nothing pops.
 *
 * The third job is the SHORT-RANGE CORRECTION, for the failure the weights cannot even see:
 * occlusion that varies ALONG a plane at sub-tile scale -- a wall pixel under an overhang
 * passes every gate the sunlit wall passes. Probe-served pixels cast a couple of contact-range
 * rays and splice what those rays resolve into the probe estimate per hemisphere share; see
 * the comment at the correction itself.
 *
 * Output convention identical to fs_gi_resolve.sc: RGB = radiance-mean units, A = the weight
 * with which it replaces the environment probe. The temporal, denoise and upsample passes
 * downstream neither know nor care which gather produced the buffer.
 */

#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod lives here, not in common.sh.
#include "../lighting.sh"

#define GI_CACHE_READ_ONLY
#include "gi/radiance_cache.sh"
#include "gi/sdf_common.sh"
#include "gi/gi_gather_common.sh"
#include "gi/gi_probe_common.sh"

/// This frame's half of the probe buffer (SH + meta), read only.
BUFFER_RO(b_gi_probes, vec4, 10);

SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// This frame's raw radiance atlas, read only by the debug views.
SAMPLER2D(s_probe_radiance, 5);
/// The cosine-convolved irradiance tiles the filter pass produced: rgb = E / PI at the texel's
/// normal direction, a = the resolved fraction around it. Sampled with octahedral-wrapped
/// manual bilinear, at the PIXEL's own normal.
SAMPLER2D(s_probe_irradiance, 11);

/// The probe debug views (u_gi_probe_debug_mode, declared with the shared probe uniforms):
///   1 = the raw atlas IN PLACE: every tile shows its probe's 8x8 octahedral texels.
///   2 = integration health: R = gated probe weight, G = resolved fraction, B = the fraction
///       supplied by the per-pixel TRACE fallback (blue = probes could not serve this pixel).
///   3 = history state: R = blend weight this frame (red = cut / fresh), G = count over 32.
/// Debug output alpha is BELOW one so the presentation blit can blend the view over the lit
/// frame, keeping the scene readable underneath the readout.
#define GI_PROBE_DEBUG_ALPHA 0.65

/// How far off the pixel's plane a probe may anchor before it stops contributing, as a fraction
/// of view distance -- the same convention as the denoiser's plane tolerance.
#define GI_PROBE_INTEGRATE_PLANE_TOLERANCE 0.05
/// Exponent on facing agreement. High enough that light does not turn corners, low enough that
/// probes on a curved surface still share.
#define GI_PROBE_INTEGRATE_NORMAL_POWER 8.0
/// Coverage ramp of the trace fallback: pure per-pixel rays below START, pure probes above
/// FULL, a continuous mix between. Below START the pixel is genuinely unservable by probes (a
/// groove, a slit) and rays keep it dark and detailed; the FULL threshold is deliberately tight
/// so ordinary edges and foliage stay on probes -- the first cut of this ramp traced a third of
/// the frame and cost more than it saved.
#define GI_PROBE_COVERAGE_START 0.10
#define GI_PROBE_COVERAGE_FULL  0.35
/// Rays the trace fallback spends. Two, not four: fallback pixels also get the screen temporal
/// accumulation downstream, exactly like the per-pixel path always did.
#define GI_PROBE_FALLBACK_RAYS 2
/// The short-range contact correction: per-pixel rays bounded to u_gi_contact_range world
/// units, layered over the probe estimate on every probe-served pixel. Zero rays disables it.
/// The range is the scale of the occlusion probes cannot see -- an overhang, a reveal -- and
/// the rays' near field is capped to it, so their cost is bounded to contact scale too.
#define GI_PROBE_SHORT_RAYS 2

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
	if(u_gi_probe_debug_mode == 1)
	{
		// The raw atlas in place: pixel -> its tile's layer-0 probe -> the texel its tile-local
		// position maps to.
		vec2 pixel_pos = uv * u_gi_probe_screen.xy;
		vec2 tile = floor(pixel_pos / u_gi_probe_spacing);
		int tx = int(clamp(tile.x, 0.0, float(u_gi_probe_count_x - 1)));
		int ty = int(clamp(tile.y, 0.0, float(u_gi_probe_count_y - 1)));
		vec2 tile_local = fract(pixel_pos / u_gi_probe_spacing);
		ivec2 texel = GiProbeAtlasBase(tx, ty, 0) +
		              ivec2(clamp(tile_local * float(GI_PROBE_DIR_EDGE),
		                          vec2_splat(0.0),
		                          vec2_splat(float(GI_PROBE_DIR_EDGE) - 1.0)));
		gl_FragColor = vec4(texelFetch(s_probe_radiance, texel, 0).xyz, GI_PROBE_DEBUG_ALPHA);
		return;
	}
	if(u_gi_probe_debug_mode == 3)
	{
		vec2 pixel_pos = uv * u_gi_probe_screen.xy;
		vec2 tile = floor(pixel_pos / u_gi_probe_spacing);
		int tx = int(clamp(tile.x, 0.0, float(u_gi_probe_count_x - 1)));
		int ty = int(clamp(tile.y, 0.0, float(u_gi_probe_count_y - 1)));
		vec4 history =
		    b_gi_probes[(GiProbeRecord(tx, ty, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE) + 11u];
		gl_FragColor = vec4(history.y, saturate(history.x / 32.0), 0.0, GI_PROBE_DEBUG_ALPHA);
		return;
	}
	float view_distance = max(length(world_position - u_gi_resolve_camera.xyz), 1e-3);
	float plane_tolerance = GI_PROBE_INTEGRATE_PLANE_TOLERANCE * view_distance;
	// The probe lattice sits at tile centres: probe (i, j) anchors within tile (i, j), so the
	// continuous probe coordinate of a pixel is its position in tile units, minus the half-tile
	// that puts coordinate 0 at the first tile's centre.
	vec2 pixel = uv * u_gi_probe_screen.xy;
	vec2 grid = pixel / u_gi_probe_spacing - vec2_splat(0.5);
	vec2 base = floor(grid);
	vec2 frac = grid - base;
	// The pixel normal in octahedral tile-texel space, shared by every probe tap.
	vec2 oct_texel = GiOctEncode(world_normal) * float(GI_PROBE_DIR_EDGE) - vec2_splat(0.5);
	vec2 oct_base = floor(oct_texel);
	vec2 oct_frac = oct_texel - oct_base;
	vec3 radiance = vec3_splat(0.0);
	float resolved = 0.0;
	float weight_sum = 0.0;
	// The facing gate SOFTENS with view distance. Its job -- keeping light from turning corners
	// -- matters at contact scale; at range a probe tile spans metres, per-pixel normals carry
	// the raster's sub-pixel jitter, and a sharp facing power turns that jitter into per-pixel
	// weight flicker: aliasing that shimmers. Softening the exponent with distance keeps the
	// corner separation close up, where it is visible, and trades it for stability far away,
	// where a tile could never resolve the corner anyway.
	float facing_power = mix(GI_PROBE_INTEGRATE_NORMAL_POWER, 2.0, saturate(view_distance / 40.0));
	for(int j = 0; j < 2; ++j)
	{
		for(int i = 0; i < 2; ++i)
		{
			int px = int(clamp(base.x + float(i), 0.0, float(u_gi_probe_count_x - 1)));
			int py = int(clamp(base.y + float(j), 0.0, float(u_gi_probe_count_y - 1)));
			for(int layer = 0; layer < GI_PROBE_LAYERS; ++layer)
			{
				uint probe_base =
				    (GiProbeRecord(px, py, layer) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
				vec4 meta = b_gi_probes[probe_base + uint(GI_PROBE_META)];
				if(meta.w < 0.5)
				{
					continue;
				}
				vec4 meta2 = b_gi_probes[probe_base + uint(GI_PROBE_META2)];
				float bilinear = (i == 0 ? 1.0 - frac.x : frac.x) * (j == 0 ? 1.0 - frac.y : frac.y);
				float facing_raw = dot(meta2.xyz, world_normal);
				// The probe's irradiance at the PIXEL's normal: octahedral-wrapped manual
				// bilinear over the convolved tile. A probe answers "how much light arrives
				// around THIS normal", and the resolved fraction rides in alpha, so the
				// environment share is directional too: a probe vouches exactly for the part of
				// its sphere it measured around this normal.
				ivec2 tile_base = GiProbeAtlasBase(px, py, layer);
				vec4 probe_irradiance = vec4_splat(0.0);
				for(int tap = 0; tap < 4; ++tap)
				{
					ivec2 offset = ivec2(tap % 2, tap / 2);
					ivec2 wrapped = GiOctWrapTexel(ivec2(oct_base) + offset);
					float tap_weight = (offset.x == 0 ? 1.0 - oct_frac.x : oct_frac.x) *
					                   (offset.y == 0 ? 1.0 - oct_frac.y : oct_frac.y);
					probe_irradiance +=
					    texelFetch(s_probe_irradiance, tile_base + wrapped, 0) * tap_weight;
				}
				// Measurement CONFIDENCE is the fraction of the pixel's cosine lobe the probe
				// actually SAMPLED -- and the unsampled region is known exactly: the trace
				// refuses only the cap below the anchor's tangent plane, so coverage is a
				// function of the pixel-to-anchor normal angle alone, approximated here by the
				// half-space fraction of a clamped-cosine lobe. It must NOT be derived from the
				// resolved fraction, as it once was: resolved counts GEOMETRY, so a fully
				// converged probe honestly reporting "mostly sky" lost its vote, every pixel
				// near a silhouette against sky fell below the coverage ramp, and the traced
				// fallback re-rolled its two random rays there every frame -- the edge flicker.
				// A ray that escapes to sky is a measurement; only the untraced cap is not, and
				// that distinction belongs here while the resolved fraction keeps its one job:
				// the weight with which the result replaces the environment probe.
				float confidence = saturate(0.5 + 0.5 * facing_raw);
				float plane = abs(dot(meta.xyz - world_position, world_normal));
				float plane_weight = saturate(1.0 - plane / plane_tolerance);
				float facing = pow(saturate(facing_raw), facing_power);
				float weight = max(bilinear, 0.01) * plane_weight * facing * confidence;
				if(weight <= 1e-4)
				{
					continue;
				}
				radiance += max(probe_irradiance.xyz, vec3_splat(0.0)) * weight;
				resolved += saturate(probe_irradiance.w) * weight;
				weight_sum += weight;
			}
		}
	}
	// Probe result in the output convention: RGB over the measured fraction, A the fraction.
	vec3 probe_rgb = vec3_splat(0.0);
	float probe_alpha = 0.0;
	if(weight_sum > 1e-4 && resolved > 1e-4)
	{
		probe_rgb = radiance / resolved;
		probe_alpha = saturate(resolved / weight_sum);
	}
	// COVERAGE: how much of this pixel the probes can honestly serve. Below full coverage the
	// remainder is TRACED, per pixel, through the identical per-ray pipeline -- the groove that
	// no probe represents gets its real, occluded, detailed lighting instead of a wash from the
	// environment term or a tug-of-war between half-valid probes. These pixels are the few
	// percent at geometric detail, they reuse the screen temporal downstream, and the blend is
	// continuous in coverage, so the handover cannot pop.
	float coverage = saturate((weight_sum - GI_PROBE_COVERAGE_START) /
	                          (GI_PROBE_COVERAGE_FULL - GI_PROBE_COVERAGE_START));
	// One launch preparation and one seed sequence, shared by the short-range correction and
	// the starved-pixel fallback below.
	bool needs_fallback = coverage < 1.0;
	bool needs_short = GI_PROBE_SHORT_RAYS > 0 && coverage > 0.0 && u_gi_contact_range > 0.0;
	GiGatherSetup setup;
	uint seed = 0u;
	if(needs_fallback || needs_short)
	{
		setup = GiPrepareGather(world_position, world_normal);
		uint pixel_seed = GiHashCombine(GiHashUint(uint(gl_FragCoord.x)), uint(gl_FragCoord.y));
		seed = GiHashCombine(pixel_seed, u_gi_frame_index);
	}
	// SHORT-RANGE CORRECTION: the per-pixel contact occlusion probes cannot represent.
	//
	// A probe answers at TILE scale, and its weights gate on plane, facing and position -- so a
	// pixel just under an overhang, on the SAME plane with the SAME normal as the sunlit wall
	// around it, passes every gate at full weight and inherits the open wall's light. The
	// occlusion that distinguishes it is sub-tile and positional ALONG the plane, which no
	// probe weight can see: that was the light leak under every window overhang. A few SHORT
	// rays per pixel re-measure exactly that. Each ray that resolves within contact range
	// REPLACES its share of the hemisphere with the real nearby answer -- through the identical
	// per-ray pipeline, so a cache-lit underside and an occluded-dark crevice both land
	// correctly -- and each ray that escapes the range affirms the probe's answer for its
	// share, environment fraction included. Replacement, never modulation on top, so no energy
	// is counted twice. Skipped once the near field has faded out: contact detail is only
	// visible near the camera, which is the same reasoning the fade itself rests on.
	if(needs_short && setup.near_field > 0.0)
	{
		vec3 short_sum = vec3_splat(0.0);
		float short_resolved = 0.0;
		for(int r = 0; r < GI_PROBE_SHORT_RAYS; ++r)
		{
			seed = GiHashUint(seed);
			float u1 = float(seed & 0xFFFFu) / 65535.0;
			seed = GiHashUint(seed);
			float u2 = float(seed & 0xFFFFu) / 65535.0;
			vec3 direction = GiCosineDirection(world_normal, u1, u2);
			GiRayOutcome outcome = GiGatherRayEx(setup, direction, u_gi_contact_range,
			                                     min(setup.near_field, u_gi_contact_range));
			short_sum += outcome.radiance;
			short_resolved += outcome.resolved;
		}
		if(short_resolved > 0.0)
		{
			// Contact occlusion strength: scale down what contact hits contribute, keeping their
			// occlusion (they stay resolved, so the environment does not refill them). At 0 the
			// hit contributes the cache's radiance there -- energy-correct -- but the cache is a
			// cell-scale mean that misses the multi-bounce light LOSS inside crevices, so strict
			// transport reads brighter than ground truth exactly where the eye expects grounding.
			// The strength dials from correct toward dark, applied only where contact occlusion
			// was actually measured.
			short_sum *= 1.0 - saturate(u_gi_contact_occlusion);
			// Each ray owns 1/N of the hemisphere: resolved rays contribute their measurement,
			// escaped rays hand their share to the probe estimate -- radiance AND alpha, so the
			// environment fraction the probes reported survives in proportion.
			float pass_through = (float(GI_PROBE_SHORT_RAYS) - short_resolved) * probe_alpha;
			probe_rgb = (short_sum + probe_rgb * pass_through) /
			            max(short_resolved + pass_through, 1e-4);
			probe_alpha = (short_resolved + pass_through) / float(GI_PROBE_SHORT_RAYS);
		}
	}
	vec3 traced_rgb = vec3_splat(0.0);
	float traced_alpha = 0.0;
	if(needs_fallback)
	{
		vec3 traced_sum = vec3_splat(0.0);
		float traced_resolved = 0.0;
		for(int r = 0; r < GI_PROBE_FALLBACK_RAYS; ++r)
		{
			seed = GiHashUint(seed);
			float u1 = float(seed & 0xFFFFu) / 65535.0;
			seed = GiHashUint(seed);
			float u2 = float(seed & 0xFFFFu) / 65535.0;
			vec3 direction = GiCosineDirection(world_normal, u1, u2);
			GiRayOutcome outcome = GiGatherRay(setup, direction);
			traced_sum += outcome.radiance;
			traced_resolved += outcome.resolved;
		}
		if(traced_resolved > 0.0)
		{
			traced_rgb = traced_sum / traced_resolved;
			traced_alpha = traced_resolved / float(GI_PROBE_FALLBACK_RAYS);
		}
	}
	if(u_gi_probe_debug_mode == 2)
	{
		gl_FragColor = vec4(saturate(weight_sum), probe_alpha, 1.0 - coverage, GI_PROBE_DEBUG_ALPHA);
		return;
	}
	vec3 out_rgb = mix(traced_rgb, probe_rgb, coverage);
	float out_alpha = mix(traced_alpha, probe_alpha, coverage);
	gl_FragColor = vec4(out_rgb, out_alpha);
}
