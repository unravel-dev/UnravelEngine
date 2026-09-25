$input v_texcoord0

/*
 * GI reflection temporal integrator: folds each frame's single stochastic GGX sample into a
 * reprojected running mean, so the lobe genuinely integrates over
 * GI_REFLECTION_TEMPORAL_FRAMES instead of shimmering. History is clamped to the
 * neighbourhood bounds of THIS frame's GEOMETRIC samples - the standard TAA guard - so
 * disoccluded or moved reflections cannot ghost past one frame, while in-lobe jitter noise
 * averages out; the clamp fades out under a still camera (the motion release below) so
 * sparse-bright content can converge - capped by the MOVER GATE while the velocity pass
 * drew movers, because the release reads receiver motion only and a still camera watching
 * a moving emitter otherwise held its ghost unclamped. Raw alpha is coverage below 1 and
 * encodes the hit distance above 1 (the trace kernel's contract; >= 0.5 image tests hold).
 * A coverage-0 sample is not an image: history is held so a refined mean is not bleached
 * by sky - held with a per-frame COUNT DECAY (see the branch), so a pixel that stops
 * producing images ever again ages out to the probe layer instead of freezing its last
 * mean forever - and a zero count lets the composite reveal the authored probe layer.
 *
 * FIREFLY GOVERNOR (the gather's recipe, GI_REFLECTION_FIREFLY_CLAMP): one VNDF ray per
 * pixel per frame makes a small bright emitter a sparse-spike process on rough surfaces -
 * a hit returns the emitter's (ray-capped) radiance, orders over the local mean, and no
 * running mean can hide an isolated spike entering at 1/count (the dancing red pixels).
 * Each new sample is capped at the clamp's multiple of its REFERENCE: the pixel's own
 * accumulated luminance, floored by the neighbourhood mean of this frame's geometric
 * samples (fetched by the same 3x3 the bounds already pay for). An established bright
 * pixel raises its own ceiling and converges unbiased; the halo near an emitter builds as
 * a stable glow instead of noise. No meaningful reference (fresh surroundings, dark
 * scene): the sample stores unclamped - progressive ramps from black would dim every
 * disocclusion instead.
 */

#include "../common.sh"
// DecodeGBufferNormalMetalRoughnessLod, for the mirror-direction hit rebuild below.
#include "../lighting.sh"
#include "gi/gi_constants.sh"
// The trace's jitter pattern, reproduced per neighbouring texel by the resolve below.
#include "gi/gi_noise.sh"
// The trace's own lobe sampler and density - the resolve re-derives each neighbour's ray.
#include "gi/gi_reflection_sampling.sh"
// Bounded-range YCoCg: the space every average and clamp in this pass runs in.
#include "gi/gi_reflection_denoise.sh"
// The accumulated mean is pre-exposed; the history carries last frame's scale.
#include "../pre_exposure.sh"

SAMPLER2D(s_refl_raw, 0);
SAMPLER2D(s_refl_history, 1);
SAMPLER2D(s_refl_depth, 2);
/// Velocity buffer (full camera resolution): RG = total uv-delta, BA = object-only.
SAMPLER2D(s_refl_velocity, 3);
/// G-buffer normal: the receiver normal rebuilds the mirror direction for the hit-point
/// velocity read (the mover gate).
SAMPLER2D(s_refl_normal, 4);

uniform mat4 u_gi_refl_prev_view_proj;
/// x > 0.5 = the history target holds valid data; yz = 1 / target size; w = accumulation
/// window in frames (the settings knob; GI_REFLECTION_TEMPORAL_FRAMES is its default).
uniform vec4 u_gi_refl_temporal;
/// x > 0.5 = reproject the receiver through the velocity buffer (unjittered convention,
/// correct for moving receivers; the stillness gate then reads TRUE per-pixel motion, so
/// a moving receiver keeps the clamp engaged while a parked one still releases it).
/// y = ceiling on the stillness release: GI_REFLECTION_MOVER_STILL_CAP while the velocity
/// pass drew any mover - or the composed SDF content changed (an instance appeared,
/// vanished, or moved: the structural signal a parked-then-destroyed object leaves when
/// it can no longer draw into the velocity buffer) - within one temporal window, 1.0
/// otherwise. The cap applies
/// EVERYWHERE and the per-pixel hit read below only TIGHTENS it - a departed mover reads
/// static at exactly its ghost's pixels (the current mirror hit is the revealed
/// background), so a depth-confirmed "static now" reading that superseded the cap
/// preserved the trail at the full release (measured; present cannot validate history).
uniform vec4 u_gi_refl_velocity;
/// xyz = camera position (shared with the trace programs - bgfx uniforms are name-global).
uniform vec4 u_gi_reflection_camera;

/// xy = this frame's R2 low-discrepancy offset for the GGX sample - the SAME uniform the
/// trace programs read (bgfx uniforms are name-global), so the resolve below reproduces the
/// exact ray each neighbouring texel fired this frame.
uniform vec4 u_gi_reflection_jitter;

/*
 * The trace kernel's raw-alpha contract, decoded once: coverage below 1, exactly 1 for the
 * rough tier, 1 + t / GI_SHADOW_DISTANCE for a covered geometric hit, 2 for a sky miss.
 * Returns 0 when the sample carries no hit distance at all (rough tier / shape fade), which
 * every caller here treats as "no parallax information".
 *
 * The sky push is deliberate and shared with the reprojection: far enough that translation
 * parallax cancels and rotation alone remains.
 */
float GiReflHitDistance(float raw_alpha)
{
	if(raw_alpha <= 1.001)
	{
		return 0.0;
	}
	return raw_alpha >= 1.999 ? GI_SHADOW_DISTANCE * 8.0 : (raw_alpha - 1.0) * GI_SHADOW_DISTANCE;
}

void main()
{
	vec2 uv = v_texcoord0;
	bool history_flag = u_gi_refl_temporal.x > 0.5;
	vec2 texel = u_gi_refl_temporal.yz;
	vec4 curr = texture2DLod(s_refl_raw, uv, 0.0);
	float depth = texture2DLod(s_refl_depth, uv, 0.0).x;
	BRANCH
	if(depth >= 1.0)
	{
		// Sky: zeros either way (the trace writes zeros for sky).
		gl_FragColor = vec4(curr.xyz, curr.w >= 0.5 ? 1.0 : curr.w);
		return;
	}
	BRANCH
	if(!history_flag)
	{
		// No history: alpha is the accumulation count. A geometric sample starts the running
		// mean at 1; a coverage-0 sample's rgb is already the trace's fallback answer (the
		// shape fade mixes to GiReflectionSkyFallback as coverage drops), stored at count 1
		// so the composite covers with it - revealing the probe layer instead drops the trace's
		// own answer for the environment fill alone wherever no probe reaches (see the hold
		// branch below).
		gl_FragColor = vec4(curr.xyz, 1.0);
		return;
	}
	// VIRTUAL-IMAGE reprojection: camera-consistent pixels ALWAYS use this pass's own matrix
	// reprojection; the velocity buffer's RG drives only OBJECT-motion pixels (BA gate).
	// Trusting RG for camera pixels drags the image - the buffer's camera component is not
	// reliably this pass's own previous view-projection (measured; open engine issue, see
	// the velocity plan). Same gating as the TAA resolve.
	//
	// The point that reprojects is NOT the receiver: reflected content lives at the mirror
	// image of the hit, |camera - P| + hit_t along the view ray through P (exact for a
	// planar reflector - the standard SSR hit-distance reprojection). Reprojecting the
	// RECEIVER fetched history from where the SURFACE was, not where the reflected content
	// was, so camera translation dragged sky and far-content reflections with receiver
	// parallax - motion trails that only caught up once the camera stopped. hit_t rides the
	// raw alpha (the trace kernel's contract): 1 < w < 2 is a geometric hit, w = 2 a sky
	// miss, pushed far enough that translation parallax cancels and rotation alone remains.
	// Rough-tier (w = 1) and shape-fade pixels keep the receiver point - their content is
	// the receiver's own gather / probe capture. Under a parked camera every point on the
	// view ray reprojects onto uv exactly, so the stillness gate below is untouched; under
	// pure rotation the virtual point lands where the receiver would anyway (same ray).
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	// TWO reprojections, deliberately: the virtual image answers WHERE the history is, the
	// receiver answers WHETHER it may be trusted. Conflating them broke the release: a
	// sky-classified pixel's virtual point sits far enough that it reprojects onto uv even
	// while the camera strafes, so pixels the reflected building had just LEFT read as
	// perfectly still, released the clamp at the extended window, and held the building's
	// ghost - a sawtooth trail that snapped only when the sweeping boundary handed the
	// pixel a geometric sample again (real motion measured, clamp re-engaged). The
	// stillness gates therefore measure RECEIVER motion - is this pixel's viewing geometry
	// parked - which is the actual precondition for "the history is my own sample stream";
	// converged sky content survives the engaged clamp anyway (it agrees with the current
	// neighbourhood by construction).
	vec4 recv_clip = mul(u_gi_refl_prev_view_proj, vec4(world_position, 1.0));
	vec3 recv_ndc = clipTransform(recv_clip.xyz / max(recv_clip.w, 1e-6));
	vec2 recv_prev_uv = recv_ndc.xy * 0.5 + 0.5;
	float center_hit_t = GiReflHitDistance(curr.w);
	vec3 reproject_point = world_position;
	BRANCH
	if(center_hit_t > 0.0)
	{
		vec3 view_ray = world_position - u_gi_reflection_camera.xyz;
		float view_dist = max(length(view_ray), 1e-4);
		reproject_point =
		    u_gi_reflection_camera.xyz + view_ray * ((view_dist + center_hit_t) / view_dist);
	}
	vec4 prev_clip = mul(u_gi_refl_prev_view_proj, vec4(reproject_point, 1.0));
	vec3 prev_ndc = clipTransform(prev_clip.xyz / max(prev_clip.w, 1e-6));
	vec2 prev_uv = prev_ndc.xy * 0.5 + 0.5;
	BRANCH
	if(u_gi_refl_velocity.x > 0.5)
	{
		vec4 vel4 = texture2DLod(s_refl_velocity, uv, 0.0);
		vec2 vel_dim = vec2(textureSize(s_refl_velocity, 0));
		float object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
		prev_uv = mix(prev_uv, uv - vel4.xy, object_w);
		// A moving RECEIVER's motion is carried by the velocity buffer, not the matrices.
		recv_prev_uv = mix(recv_prev_uv, uv - vel4.xy, object_w);
	}
	BRANCH
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		// Off-screen history: restart, same fallback-at-count-1 contract as the no-history
		// path above.
		gl_FragColor = vec4(curr.xyz, 1.0);
		return;
	}
	vec4 history_texel = texture2DLod(s_refl_history, prev_uv, 0.0);
	// PRE-EXPOSURE CORRECTION (UE P / Pprev): the mean was accumulated under last frame's
	// scale. The alpha lane carries coverage and the hit distance - unitless, left alone.
	history_texel.xyz *= u_history_pre_exposure_correction;
	// STILLNESS releases the neighbourhood clamp. The clamp exists for disocclusion, but for
	// SPARSE-BRIGHT content (a small emissive under the lobe: hit probability p per frame)
	// it erases the accumulated p*L mean on every miss frame - the estimator cannot converge
	// BY CONSTRUCTION and every hit re-flashes as a dancing dot. Below one texel of
	// reprojection motion the history IS this pixel's own sample stream and may be held
	// unclamped; the release also extends the running-mean window (the count cap below), so
	// spikes enter at 1/(scale x window) weight. Motion is the only per-frame discriminator
	// between ghosts and sparse-bright samples without a velocity buffer - a moving emitter
	// under a still camera can trail over the extended window (accepted, documented). This
	// gate only reads truly still because the whole chain runs on TAA-unjittered matrices
	// (the pass subtracts the jitter; jittered matrices read a parked camera as 0.25-0.5
	// texel/frame of motion and silently kept the clamp engaged).
	// RECEIVER-motion texels, never the fetch offset: see the two-reprojection note above.
	vec2 motion_texels = (uv - recv_prev_uv) / max(texel, vec2_splat(1e-6));
	// Measured receiver stillness, kept SEPARATE from the release gates below: the
	// motion-collapsed window keys on actual motion only, while the release additionally
	// drops for mirrors (determinism gate) and recent movers - a PARKED mirror must keep
	// its full base window for relight-phase integration.
	float still_motion = 1.0 - saturate(length(motion_texels) / GI_REFLECTION_CLAMP_MOTION_TEXELS);
	float still = still_motion;
	// MOVER GATE, part one - the global cap (u_gi_refl_velocity.y, see its declaration):
	// receiver motion is the only thing `still` measured, so a parked camera watching a
	// MOVING emitter held the ghost's history unclamped at the extended window. While any
	// mover was drawn recently the release is capped screen-wide; ghosts flush at roughly
	// the base window while converged static content loses only the release's tail.
	still = min(still, u_gi_refl_velocity.y);
	// DETERMINISM GATE: the release exists for STOCHASTIC pixels - a jittered lobe's sparse
	// hits need an unclamped, extended mean to converge (the p*L estimator above). A mirror
	// pixel (the trace's own determinism gate, same constant, same decode) fires the SAME
	// ray every frame: there is no lobe variance to integrate, every sample is the full
	// truth, and an unclamped extended hold can only preserve stale content. Mirrors
	// therefore keep the clamp engaged and the base window at ANY stillness - history that
	// disagrees with the current neighbourhood dies within frames, which is exactly the
	// surface where departed-content lines proved able to outlive every upstream flush.
	// The decode is shared with the mover gate's mirror-direction rebuild below.
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_refl_normal, 0.0);
	if(nd.roughness <= GI_REFLECTION_MIRROR_ROUGHNESS)
	{
		still = 0.0;
	}
	// ROUGHNESS-SCALED WINDOW (GI_REFLECTION_ROUGH_WINDOW_SCALE): a wide lobe integrates one
	// VNDF ray per frame slowest, so its running mean may grow to several times the settings
	// window (this engine's tuning; Lumen 5.7 caps at 12, 2 on mirrors); mirrors keep the short window and its
	// responsiveness. The mover gate and the clamp still bound ghosting on the longer mean.
	float rough_window_scale =
	    mix(1.0, GI_REFLECTION_ROUGH_WINDOW_SCALE, saturate(nd.roughness / GI_REFLECTION_ROUGH_CUTOFF));
	float window = max(u_gi_refl_temporal.w * rough_window_scale - 1.0, 1.0);
	// MOTION WINDOW: trail length on a blurred high-contrast boundary is the 1/count
	// catch-up time, and the base window's depth reads as a smear band the clamp cannot
	// reject there (a blurred edge's AABB legitimately spans both sides). While measured
	// motion exceeds the clamp threshold, the effective window collapses to
	// GI_REFLECTION_MOTION_WINDOW - the composite's roughness-ramped kernel and the motion
	// itself hide the extra variance, and the full depth returns the frame the camera
	// parks.
	float window_eff = mix(min(GI_REFLECTION_MOTION_WINDOW, window), window, still_motion);
	BRANCH
	if(curr.w < 0.5)
	{
		// Not an image this frame. Hold the geometric mean we already have rather than
		// clamping it against a sky/empty neighbourhood that would bleach a refined
		// history - but held is not immortal. The count obeys the same stillness ceiling
		// as the image path and decays by 1/window per held frame, so a pixel whose trace
		// KEEPS answering non-image (the grazing unrefined-clipmap band at the silhouette
		// of departed content - the ghost stripe at the hit/sky transition) ages out over
		// about a window; a single-frame coverage gap costs one window-fraction of
		// weight and no colour.
		//
		// As the held count dies, the DISPLAYED colour cross-fades to curr.rgb, which on
		// a coverage-0 frame is already the trace's own fallback answer (shape_ok 0 mixes
		// to pure GiReflectionSkyFallback): the steady state of a persistently-non-image
		// pixel is the LIVE sky/probe answer at count 1 - never a bare low alpha that
		// uncovers the probe layer, because where no probe reaches that "reveal" was a black
		// hole before the indirect pass filled it with the SH (measured: black bands rimmed with
		// the last held colour at every
		// persistent-non-image silhouette once the count decayed). The count floors at 1:
		// the fallback IS an image, and the next geometric sample restarts a fresh mean
		// on top of it instead of resurrecting anything.
		float held = min(history_texel.w,
		                 window_eff * mix(1.0, GI_REFLECTION_STILL_WINDOW_SCALE, still));
		held *= 1.0 - 1.0 / max(u_gi_refl_temporal.w, 2.0);
		// The hold may bridge sparse coverage gaps ONLY while the history is this pixel's
		// own still sample stream - hence the stillness factor. Under reprojection motion
		// the fetch is a NEIGHBOUR'S mean dragged at receiver parallax (band content mixes
		// wall and sky, so no single hit distance can reproject it), and the band sweeping
		// across the screen re-inherits converged neighbours every frame - displaying that
		// at full trust was a self-refreshing smear along reflected silhouettes (the
		// wall-edge motion trails). On a deterministic mirror (still forced 0 above) a
		// coverage-0 answer is PERSISTENT, not a gap, so the live fallback is the correct
		// display there at any camera state.
		float trust = saturate(held) * still;
		gl_FragColor = vec4(mix(curr.xyz, history_texel.xyz, trust), max(held, 1.0));
		return;
	}
	// MOVER GATE, part two - the per-pixel TIGHTEN (never lift): rebuild the reflected hit
	// along the mirror direction from the alpha-encoded hit distance (the trace kernel's
	// raw-alpha contract) and read the velocity buffer's OBJECT-ONLY lanes there. Reflected
	// content confirmed moving right now forfeits the release entirely, so a tracked mover's
	// reflection follows at the base window. The mirror direction stands in for the exact
	// stochastic sample - a motion classifier needs the lobe centre, not the sample.
	BRANCH
	if(u_gi_refl_velocity.x > 0.5 && curr.w > 1.001 && curr.w < 1.999)
	{
		BRANCH
		if(dot(nd.world_normal, nd.world_normal) >= 0.5)
		{
			vec3 normal = normalize(nd.world_normal);
			vec3 view = normalize(u_gi_reflection_camera.xyz - world_position);
			vec3 mirror_dir = normalize(reflect(-view, normal));
			vec4 hit_clip =
			    mul(u_viewProj, vec4(world_position + mirror_dir * center_hit_t, 1.0));
			if(hit_clip.w > 1e-6)
			{
				vec3 hit_ndc = clipTransform(hit_clip.xyz / hit_clip.w);
				vec2 hit_uv = hit_ndc.xy * 0.5 + 0.5;
				if(all(greaterThanEqual(hit_uv, vec2_splat(0.0))) &&
				   all(lessThanEqual(hit_uv, vec2_splat(1.0))))
				{
					vec4 hit_vel = texture2DLod(s_refl_velocity, hit_uv, 0.0);
					vec2 vel_dim = vec2(textureSize(s_refl_velocity, 0));
					float hit_motion = smoothstep(0.5, 1.5, length(hit_vel.zw * vel_dim));
					still = min(still, 1.0 - hit_motion);
				}
			}
		}
	}
	// PRE-TEMPORAL RESOLVE (GI_REFLECTION_RESOLVE_START): one GGX ray per pixel per frame is
	// not enough to resolve a wide lobe, but the neighbouring texels sampled the SAME lobe
	// from almost the same point - so their rays can be reused here, for free, before the
	// temporal ever sees this frame's sample. This is Lumen's spatial reconstruction
	// (LumenReflectionResolve.usf:370-613) rather than a blur: each neighbour's own ray is
	// re-derived, its hit point rebuilt, the direction RE-AIMED from this pixel, and the
	// sample weighted by this pixel's own lobe density over the density it was drawn from.
	// Averaging radiance with edge stops alone (what this used to do) has no parallax
	// correction and no BRDF weight, so it over-blurred the sharp end of the band and gave
	// mismatched taps full weight. Fades in with roughness; never on mirrors.
	float resolve_scale =
	    smoothstep(GI_REFLECTION_RESOLVE_START, GI_REFLECTION_GATHER_FADE_START, nd.roughness);
	// A degenerate G-buffer normal has no lobe to reuse under, and normalize() of it is a NaN.
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		resolve_scale = 0.0;
	}
	// THE CENTRE LOBE. The centre tap is weighted by its own BRDF-over-pdf ratio, exactly as
	// the neighbours are, so the two are on one scale - a fixed 1.0 for the centre against
	// physically scaled neighbours is an arbitrary mix. The ratio is O(1) for a VNDF sample
	// (the NDF cancels), so the floor only guards a degenerate frame.
	vec3 center_normal = vec3(0.0, 0.0, 1.0);
	vec3 center_view = vec3(0.0, 0.0, 1.0);
	float center_alpha = GI_REFLECTION_MIN_LOBE_ALPHA;
	float center_weight = 1.0;
	// The texel index the trace seeded its IGN with, derived from the UV rather than from
	// gl_FragCoord: the trace wrote `uv = (pixel + 0.5) * texel`, so uv / texel recovers
	// `pixel + 0.5` and truncates to `pixel` on every backend. gl_FragCoord would be a trap -
	// its origin is top-left on D3D and bottom-left on GL, and a flipped seed would silently
	// re-derive the WRONG ray for every neighbour while still compiling and running.
	ivec2 center_pixel = ivec2(uv / texel);
	BRANCH
	if(resolve_scale > 0.0)
	{
		center_normal = normalize(nd.world_normal);
		center_view = normalize(u_gi_reflection_camera.xyz - world_position);
		center_alpha = max(nd.roughness * nd.roughness, GI_REFLECTION_MIN_LOBE_ALPHA);
		vec2 center_xi = fract(GiIgnNoise(center_pixel) + u_gi_reflection_jitter.xy);
		GiReflectionRay center_ray =
		    GiReflectionMakeRay(center_normal, center_view, nd.roughness, center_xi);
		center_weight = max(GiReflectionSampleWeight(center_normal,
		                                             center_view,
		                                             center_alpha,
		                                             center_ray.direction,
		                                             center_ray.pdf),
		                    1e-3);
	}
	// One 3x3 walk over this frame's GEOMETRIC samples feeds three consumers: the
	// neighbourhood statistics, the firefly reference and the resolve (a coverage-0
	// neighbour is not an image - it cannot widen the clamp box of a refined hit, drag the
	// reference toward sky, or enter the resolve). The samples are kept so the resolve can
	// apply the governor's ceiling to each of them.
	//
	// STATISTICS SPACE: mean and variance in bounded-range YCoCg (gi_reflection_denoise.sh),
	// never the linear-RGB min/max AABB this used to build. A min/max box is set by its
	// single brightest member, so one firefly widened it until it rejected nothing - the
	// recorded "a colour-space clamp flushes only as well as its box is tight" lesson - and
	// it was exactly where the box was widest that ghosts survived.
	vec3 box_sum = vec3_splat(0.0);
	vec3 box_sq_sum = vec3_splat(0.0);
	float box_count = 0.0;
	vec3 neighbor_sum = vec3_splat(0.0);
	float neighbor_count = 0.0;
	float neighbor_luma_max = 0.0;
	vec3 neighbor_rgb[8];
	float neighbor_weight[8];
	int neighbor_index = 0;
	LOOP
	for(int y = -1; y <= 1; ++y)
	{
		LOOP
		for(int x = -1; x <= 1; ++x)
		{
			if(x == 0 && y == 0)
			{
				continue;
			}
			vec2 sample_uv = uv + vec2(float(x), float(y)) * texel;
			vec4 s = texture2DLod(s_refl_raw, sample_uv, 0.0);
			vec3 sample_rgb = vec3_splat(0.0);
			float sample_weight = 0.0;
			if(s.w >= 0.5)
			{
				vec3 sample_denoise = GiReflToDenoiser(s.xyz);
				box_sum += sample_denoise;
				box_sq_sum += sample_denoise * sample_denoise;
				box_count += 1.0;
				neighbor_sum += s.xyz;
				neighbor_count += 1.0;
				neighbor_luma_max = max(neighbor_luma_max, GiReflLuma(s.xyz));
				sample_rgb = s.xyz;
				BRANCH
				if(resolve_scale > 0.0)
				{
					float sample_depth = texture2DLod(s_refl_depth, sample_uv, 0.0).x;
					GBufferDataNormalMetalRoughness snd =
					    DecodeGBufferNormalMetalRoughnessLod(sample_uv, s_refl_normal, 0.0);
					// The composite's edge stops: depth agreement within a small band and a
					// tight normal cone, so the resolve never bleeds across silhouettes. Kept
					// alongside the BRDF weight - the weight describes the lobe, the stops
					// describe the surface, and neither sees what the other does.
					float depth_weight = saturate(1.0 - abs(sample_depth - depth) /
					                                        (GI_TEMPORAL_DEPTH_TOLERANCE * 0.01));
					float nw = saturate(dot(normalize(snd.world_normal), center_normal));
					nw = nw * nw;
					nw = nw * nw;
					nw = nw * nw;
					nw = nw * nw;
					float edge_weight = resolve_scale * depth_weight * nw * nw;
					// A past-cutoff neighbour never traced a ray: its value is the reused
					// diffuse gather, not a sample of any lobe, so it must not be re-weighted
					// as one (it still counts toward the statistics and the reference).
					BRANCH
					if(edge_weight > 0.0 && sample_depth < 1.0 &&
					   snd.roughness < GI_REFLECTION_ROUGH_CUTOFF &&
					   dot(snd.world_normal, snd.world_normal) >= 0.5)
					{
						vec3 sample_clip = clipTransform(
						    vec3(sample_uv * 2.0 - 1.0, toClipSpaceDepth(sample_depth)));
						vec3 sample_position = clipToWorld(u_invViewProj, sample_clip);
						vec3 sample_normal = normalize(snd.world_normal);
						vec3 sample_view =
						    normalize(u_gi_reflection_camera.xyz - sample_position);
						vec2 sample_xi = fract(GiIgnNoise(center_pixel + ivec2(x, y)) +
						                       u_gi_reflection_jitter.xy);
						GiReflectionRay sample_ray = GiReflectionMakeRay(sample_normal,
						                                                sample_view,
						                                                snd.roughness,
						                                                sample_xi);
						// RE-AIM: the neighbour's radiance arrived from its OWN hit point, so
						// from this pixel it lies along a different direction. Clamping the
						// distance to the centre's preserves contacts and keeps a neighbour
						// that sailed into the background from biasing the average (Lumen's
						// own note, LumenReflectionResolve.usf:530).
						vec3 reaimed = sample_ray.direction;
						float sample_hit_t = GiReflHitDistance(s.w);
						if(sample_hit_t > 0.0)
						{
							float traced_t = center_hit_t > 0.0 ? min(sample_hit_t, center_hit_t)
							                                    : sample_hit_t;
							vec3 to_hit =
							    sample_position + sample_ray.direction * traced_t - world_position;
							float to_hit_length = length(to_hit);
							if(to_hit_length > 1e-4)
							{
								reaimed = to_hit / to_hit_length;
							}
						}
						float brdf_weight = GiReflectionSampleWeight(center_normal,
						                                            center_view,
						                                            center_alpha,
						                                            reaimed,
						                                            sample_ray.pdf);
						// A much ROUGHER neighbour has a much smaller density and would
						// otherwise dominate the average; the edge stops cannot see a
						// roughness discontinuity.
						brdf_weight =
						    min(brdf_weight, center_weight * GI_REFLECTION_RESOLVE_WEIGHT_MAX);
						sample_weight = edge_weight * brdf_weight;
					}
				}
			}
			neighbor_rgb[neighbor_index] = sample_rgb;
			neighbor_weight[neighbor_index] = sample_weight;
			++neighbor_index;
		}
	}
	// FIREFLY GOVERNOR (see the header): cap the new sample at the clamp's multiple of the
	// pixel's own accumulated luminance, floored by this frame's neighbourhood mean - the
	// TRIMMED mean, brightest neighbour excluded. With the plain mean a single unclamped
	// emissive hit (up to GI_MAX_RAY_RADIANCE) set every neighbour's ceiling to its own
	// height, and once the resolve existed it spread that hit into a 3x3 of unclamped dots
	// that the 3-frame motion window showed dancing (measured regression on brushed metal).
	// With one neighbour there is nothing to trim: the history alone is the reference.
	//
	// The bounded-range resolve below now suppresses the same spikes by construction, so
	// this governor and its trimmed mean are candidates for removal - but only against the
	// brushed-metal case that put them here, not on principle.
	float reference = history_texel.w >= 0.5 ? GiReflLuma(history_texel.xyz) : 0.0;
	if(neighbor_count > 1.0)
	{
		float trimmed = max(GiReflLuma(neighbor_sum) - neighbor_luma_max, 0.0) / (neighbor_count - 1.0);
		reference = max(reference, trimmed);
	}
	bool has_reference = reference > 1e-3;
	float ceiling = GI_REFLECTION_FIREFLY_CLAMP * reference;
	BRANCH
	if(has_reference)
	{
		float luma = GiReflLuma(curr.xyz);
		if(luma > ceiling)
		{
			curr.xyz *= ceiling / luma;
		}
	}
	// The resolve runs only with an ESTABLISHED reference and applies the same ceiling to
	// every neighbour it averages: without a reference the neighbourhood is sparse spikes
	// on dark, and averaging those can only spread the dots (the no-reference store stays
	// unclamped exactly as before, one pixel per hit). The average itself runs in the
	// bounded range, so a tap that survives the ceiling still cannot carry the mean.
	BRANCH
	if(has_reference && resolve_scale > 0.0)
	{
		vec3 resolve_sum = vec3_splat(0.0);
		float resolve_weight = 0.0;
		LOOP
		for(int i = 0; i < 8; ++i)
		{
			float sample_weight = neighbor_weight[i];
			if(sample_weight <= 0.0)
			{
				continue;
			}
			vec3 sample_rgb = neighbor_rgb[i];
			float sample_luma = GiReflLuma(sample_rgb);
			if(sample_luma > ceiling)
			{
				sample_rgb *= ceiling / sample_luma;
			}
			resolve_sum += GiReflToBounded(sample_rgb) * sample_weight;
			resolve_weight += sample_weight;
		}
		if(resolve_weight > 0.0)
		{
			curr.xyz = GiReflFromBounded((GiReflToBounded(curr.xyz) * center_weight + resolve_sum) /
			                             (center_weight + resolve_weight));
		}
	}
	// The clamp box closes over the RESOLVED centre - the value that is about to be blended
	// is the one the history has to agree with.
	vec3 center_denoise = GiReflToDenoiser(curr.xyz);
	box_sum += center_denoise;
	box_sq_sum += center_denoise * center_denoise;
	box_count += 1.0;
	vec3 box_mean = box_sum / box_count;
	vec3 box_variance = max(box_sq_sum / box_count - box_mean * box_mean, vec3_splat(0.0));
	// Mean +- GI_REFLECTION_CLAMP_SIGMA standard deviations, floored so a flat neighbourhood
	// cannot turn quantisation-level disagreement into a full confidence collapse.
	vec3 box_extent = max(GI_REFLECTION_CLAMP_SIGMA * sqrt(box_variance),
	                      vec3_splat(GI_REFLECTION_CONFIDENCE_EXTENT_FLOOR));
	// RUNNING MEAN, not a fixed EMA: alpha carries the accumulated frame count (the SSR
	// temporal-resolve convention). A fixed-weight EMA has a permanent variance floor -
	// about a quarter of the sample spread at weight 1/8 - which read as reflections that
	// never converge exactly where the stochastic spread is widest (measured, round 13).
	// The count clamp keeps steady-state responsiveness at one over the settings window.
	vec3 history_rgb = curr.xyz;
	float prev_count = 0.0;
	BRANCH
	if(history_texel.w >= 0.5)
	{
		vec3 history_denoise = GiReflToDenoiser(history_texel.xyz);
		vec3 clamped_denoise =
		    clamp(history_denoise, box_mean - box_extent, box_mean + box_extent);
		vec3 clamped = GiReflFromDenoiser(clamped_denoise);
		history_rgb = mix(clamped, history_texel.xyz, still);
		prev_count = history_texel.w;
		// CONFIDENCE COLLAPSE: the clamp bounds what a stale
		// history may SHOW, but not how long it takes to catch up - the running mean still
		// converges at 1/count, and on a blurred high-contrast boundary that catch-up is the
		// smear band behind a reflected mover (the receiver's own stillness never sees
		// reflected motion, so the motion window cannot help there). The distance the
		// history had to be clamped, in units of the neighbourhood's extent, is a per-pixel
		// measure of exactly that disagreement, and it scales the count down: a history far
		// outside the box re-accumulates with a large alpha in the frames that follow instead
		// of only being pinned to the box edge. The floor keeps a little history on a merely
		// noisy pixel; the release (stillness) lifts the collapse in step with the clamp, so
		// sparse-bright content under a parked camera converges exactly as before - the
		// mover cap keeps the collapse engaged while anything moves.
		// Measured directly in the statistics space, so there is no second tonemap here.
		float confidence =
		    saturate(1.0 - length((history_denoise - clamped_denoise) / box_extent));
		confidence = GI_REFLECTION_CONFIDENCE_FLOOR +
		             (1.0 - GI_REFLECTION_CONFIDENCE_FLOOR) * confidence;
		prev_count *= mix(confidence, 1.0, still);
	}
	// The count cap grows with stillness (see the release note above) and collapses to the
	// MOTION window on the first moving frame, so trails shorten to a few frames of
	// catch-up while the camera moves.
	float count = min(prev_count, window_eff * mix(1.0, GI_REFLECTION_STILL_WINDOW_SCALE, still)) + 1.0;
	gl_FragColor = vec4(mix(history_rgb, curr.xyz, 1.0 / count), count);
}
