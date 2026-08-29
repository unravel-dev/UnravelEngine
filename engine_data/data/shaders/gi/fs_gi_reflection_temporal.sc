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
 * by sky, and a zero count lets the composite reveal the authored probe layer.
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
/// pass drew any mover within one temporal window, 1.0 otherwise. The cap applies
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
		// No history: alpha is coverage. A geometric sample starts the running mean at 1;
		// a coverage-0 sample stays 0 so the composite reveals probes.
		gl_FragColor = vec4(curr.xyz, curr.w >= 0.5 ? 1.0 : curr.w);
		return;
	}
	// Receiver reprojection: camera-consistent pixels ALWAYS use this pass's own matrix
	// reprojection; the velocity buffer's RG drives only OBJECT-motion pixels (BA gate).
	// Trusting RG for camera pixels drags the image - the buffer's camera component is not
	// reliably this pass's own previous view-projection (measured; open engine issue, see
	// the velocity plan). Same gating as the TAA resolve.
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec4 prev_clip = mul(u_gi_refl_prev_view_proj, vec4(world_position, 1.0));
	vec3 prev_ndc = clipTransform(prev_clip.xyz / max(prev_clip.w, 1e-6));
	vec2 prev_uv = prev_ndc.xy * 0.5 + 0.5;
	BRANCH
	if(u_gi_refl_velocity.x > 0.5)
	{
		vec4 vel4 = texture2DLod(s_refl_velocity, uv, 0.0);
		vec2 vel_dim = vec2(textureSize(s_refl_velocity, 0));
		float object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
		prev_uv = mix(prev_uv, uv - vel4.xy, object_w);
	}
	BRANCH
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		gl_FragColor = vec4(curr.xyz, curr.w >= 0.5 ? 1.0 : curr.w);
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
	vec2 motion_texels = (uv - prev_uv) / max(texel, vec2_splat(1e-6));
	float still = 1.0 - saturate(length(motion_texels) / GI_REFLECTION_CLAMP_MOTION_TEXELS);
	// MOVER GATE, part one - the global cap (u_gi_refl_velocity.y, see its declaration):
	// receiver motion is the only thing `still` measured, so a parked camera watching a
	// MOVING emitter held the ghost's history unclamped at the extended window. While any
	// mover was drawn recently the release is capped screen-wide; ghosts flush at roughly
	// the base window while converged static content loses only the release's tail.
	still = min(still, u_gi_refl_velocity.y);
	BRANCH
	if(curr.w < 0.5)
	{
		// Not an image this frame. Hold the geometric mean we already have; do not clamp
		// against a sky/empty neighbourhood that would bleach a refined history.
		gl_FragColor = vec4(history_texel.xyz, history_texel.w);
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
		GBufferDataNormalMetalRoughness nd =
		    DecodeGBufferNormalMetalRoughnessLod(uv, s_refl_normal, 0.0);
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
	// base window on the first moving frame, so responsiveness under motion is unchanged.
	float window = max(u_gi_refl_temporal.w - 1.0, 1.0);
	float count = min(prev_count, window * mix(1.0, GI_REFLECTION_STILL_WINDOW_SCALE, still)) + 1.0;
	gl_FragColor = vec4(mix(history_rgb, curr.xyz, 1.0 / count), count);
}
