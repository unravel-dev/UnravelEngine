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

/// Rec.709 luminance (common.sh carries no Luminance helper).
float GiReflLuma(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
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
		// so the composite covers with it - revealing RBUFFER instead is a black hole
		// wherever no probe reaches (see the hold branch below).
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
	vec3 reproject_point = world_position;
	BRANCH
	if(curr.w > 1.001)
	{
		vec3 view_ray = world_position - u_gi_reflection_camera.xyz;
		float view_dist = max(length(view_ray), 1e-4);
		float hit_t = curr.w >= 1.999 ? GI_SHADOW_DISTANCE * 8.0
		                              : (curr.w - 1.0) * GI_SHADOW_DISTANCE;
		reproject_point =
		    u_gi_reflection_camera.xyz + view_ray * ((view_dist + hit_t) / view_dist);
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
	float window = max(u_gi_refl_temporal.w - 1.0, 1.0);
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
		// uncovers RBUFFER, because where no probe reaches that "reveal" is a black hole,
		// not the SH (measured: black bands rimmed with the last held colour at every
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
			float hit_t = (curr.w - 1.0) * GI_SHADOW_DISTANCE;
			vec4 hit_clip = mul(u_viewProj, vec4(world_position + mirror_dir * hit_t, 1.0));
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
	// Neighbourhood bounds and mean from geometric samples only, so a coverage-0 neighbour
	// cannot shrink the AABB of a refined hit (nor drag the firefly reference toward sky).
	vec4 lo = curr;
	vec4 hi = curr;
	vec3 neighbor_sum = vec3_splat(0.0);
	float neighbor_count = 0.0;
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
			vec4 s = texture2DLod(s_refl_raw, uv + vec2(float(x), float(y)) * texel, 0.0);
			if(s.w >= 0.5)
			{
				lo = min(lo, s);
				hi = max(hi, s);
				neighbor_sum += s.xyz;
				neighbor_count += 1.0;
			}
		}
	}
	// FIREFLY GOVERNOR (see the header): cap the new sample at the clamp's multiple of the
	// pixel's own accumulated luminance, floored by this frame's neighbourhood mean.
	float reference = history_texel.w >= 0.5 ? GiReflLuma(history_texel.xyz) : 0.0;
	if(neighbor_count > 0.0)
	{
		reference = max(reference, GiReflLuma(neighbor_sum / neighbor_count));
	}
	BRANCH
	if(reference > 1e-3)
	{
		float ceiling = GI_REFLECTION_FIREFLY_CLAMP * reference;
		float luma = GiReflLuma(curr.xyz);
		if(luma > ceiling)
		{
			curr.xyz *= ceiling / luma;
		}
	}
	// RUNNING MEAN, not a fixed EMA: alpha carries the accumulated frame count (the SSR
	// temporal-resolve convention). A fixed-weight EMA has a permanent variance floor -
	// about a quarter of the sample spread at weight 1/8 - which read as reflections that
	// never converge exactly where the stochastic spread is widest (measured, round 13).
	// The count clamp keeps steady-state responsiveness at one over the settings window.
	vec3 history_rgb = history_texel.w >= 0.5
	                       ? mix(clamp(history_texel.xyz, lo.xyz, hi.xyz), history_texel.xyz, still)
	                       : curr.xyz;
	float prev_count = history_texel.w >= 0.5 ? history_texel.w : 0.0;
	// The count cap grows with stillness (see the release note above) and collapses to the
	// MOTION window on the first moving frame, so trails shorten to a few frames of
	// catch-up while the camera moves.
	float count = min(prev_count, window_eff * mix(1.0, GI_REFLECTION_STILL_WINDOW_SCALE, still)) + 1.0;
	gl_FragColor = vec4(mix(history_rgb, curr.xyz, 1.0 / count), count);
}
