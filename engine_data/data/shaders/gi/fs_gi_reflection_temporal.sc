$input v_texcoord0

/*
 * GI reflection temporal integrator: folds each frame's single stochastic GGX sample into a
 * reprojected running mean, so the lobe genuinely integrates over
 * GI_REFLECTION_TEMPORAL_FRAMES instead of shimmering. History is clamped to the
 * neighbourhood bounds of THIS frame's GEOMETRIC samples - the standard TAA guard - so
 * disoccluded or moved reflections cannot ghost past one frame, while in-lobe jitter noise
 * averages out. Raw alpha is coverage (mesh-exact / refined hit). A coverage-0 sample is
 * not an image: history is held so a refined mean is not bleached by sky, and a zero count
 * lets the composite reveal the authored probe layer.
 *
 * CHECKERBOARD (flag bit below): the trace produces half the texels per frame by pixel
 * parity. Traced texels integrate as always, with their clamp AABB built from the four
 * DIAGONAL raw neighbours - the same parity, so every bound is a real sample (and four
 * fewer taps than the 3x3). Untraced texels carry their reprojected history forward,
 * clamped against the same diagonal bounds, WITHOUT advancing the count - the mean then
 * weights each traced sample exactly as the full-rate path does, so the converged result
 * is identical in expectation. An untraced texel with no usable history (first frame,
 * off-screen reprojection) reconstructs from the traced diagonals rather than leaving a
 * hole that would flash the probe layer during fast pans.
 */

#include "../common.sh"
#include "gi/gi_constants.sh"

SAMPLER2D(s_refl_raw, 0);
SAMPLER2D(s_refl_history, 1);
SAMPLER2D(s_refl_depth, 2);

uniform mat4 u_gi_refl_prev_view_proj;
/// x = packed flags as an exact small float: +1 history target holds valid data,
/// +2 checkerboard enabled, +4 frame parity. yz = 1 / target size; w = accumulation
/// window in frames (the settings knob; GI_REFLECTION_TEMPORAL_FRAMES is its default).
uniform vec4 u_gi_refl_temporal;

/// The traced diagonal neighbours' geometric mean, for an untraced texel with no history.
vec4 GiChequerFill(vec2 uv, vec2 texel)
{
	vec3 sum = vec3_splat(0.0);
	float weight = 0.0;
	LOOP
	for(int i = 0; i < 4; ++i)
	{
		vec2 offset = vec2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
		vec4 s = texture2DLod(s_refl_raw, uv + offset * texel, 0.0);
		if(s.w >= 0.5)
		{
			sum += s.xyz;
			weight += 1.0;
		}
	}
	if(weight > 0.0)
	{
		return vec4(sum / weight, 1.0);
	}
	return vec4_splat(0.0);
}

void main()
{
	vec2 uv = v_texcoord0;
	int flags = int(u_gi_refl_temporal.x + 0.5);
	bool history_flag = (flags & 1) != 0;
	bool checkerboard = (flags & 2) != 0;
	int frame_parity = (flags >> 2) & 1;
	bool traced = true;
	if(checkerboard)
	{
		traced = (((int(gl_FragCoord.x) + int(gl_FragCoord.y) + frame_parity) & 1) == 0);
	}
	vec2 texel = u_gi_refl_temporal.yz;
	vec4 curr = texture2DLod(s_refl_raw, uv, 0.0);
	float depth = texture2DLod(s_refl_depth, uv, 0.0).x;
	BRANCH
	if(depth >= 1.0)
	{
		// Sky: zeros either way (the trace writes zeros for sky and for untraced texels).
		gl_FragColor = vec4(curr.xyz, curr.w >= 0.5 ? 1.0 : curr.w);
		return;
	}
	BRANCH
	if(!history_flag)
	{
		if(traced)
		{
			// No history: alpha is coverage. A geometric sample starts the running mean at
			// 1; a coverage-0 sample stays 0 so the composite reveals probes.
			float start = curr.w >= 0.5 ? 1.0 : curr.w;
			gl_FragColor = vec4(curr.xyz, start);
			return;
		}
		gl_FragColor = GiChequerFill(uv, texel);
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec4 prev_clip = mul(u_gi_refl_prev_view_proj, vec4(world_position, 1.0));
	vec3 prev_ndc = clipTransform(prev_clip.xyz / max(prev_clip.w, 1e-6));
	vec2 prev_uv = prev_ndc.xy * 0.5 + 0.5;
	BRANCH
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		if(traced)
		{
			float start = curr.w >= 0.5 ? 1.0 : curr.w;
			gl_FragColor = vec4(curr.xyz, start);
			return;
		}
		gl_FragColor = GiChequerFill(uv, texel);
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
	BRANCH
	if(traced && curr.w < 0.5)
	{
		// Not an image this frame. Hold the geometric mean we already have; do not clamp
		// against a sky/empty neighbourhood that would bleach a refined history.
		gl_FragColor = vec4(history_texel.xyz, history_texel.w);
		return;
	}
	// Neighbourhood bounds from geometric samples only, so a coverage-0 neighbour cannot
	// shrink the AABB of a refined hit. Checkerboard: the four diagonals are this texel's
	// own parity and therefore real samples this frame; full rate keeps the whole 3x3.
	vec4 lo = vec4_splat(1e30);
	vec4 hi = vec4_splat(-1e30);
	float bound_count = 0.0;
	if(traced)
	{
		if(curr.w >= 0.5)
		{
			lo = curr;
			hi = curr;
			bound_count = 1.0;
		}
	}
	BRANCH
	if(checkerboard)
	{
		LOOP
		for(int i = 0; i < 4; ++i)
		{
			vec2 offset = vec2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
			vec4 s = texture2DLod(s_refl_raw, uv + offset * texel, 0.0);
			if(s.w >= 0.5)
			{
				lo = min(lo, s);
				hi = max(hi, s);
				bound_count += 1.0;
			}
		}
	}
	else
	{
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
					bound_count += 1.0;
				}
			}
		}
	}
	BRANCH
	if(!traced)
	{
		// Untraced texel: history carried forward, clamped against this frame's traced
		// diagonals so disocclusion cannot ghost - the count does NOT advance, which is what
		// keeps every traced sample's weight in the mean identical to the full-rate path.
		if(history_texel.w >= 0.5)
		{
			vec3 held = bound_count > 0.0
			                ? mix(clamp(history_texel.xyz, lo.xyz, hi.xyz), history_texel.xyz, still)
			                : history_texel.xyz;
			gl_FragColor = vec4(held, history_texel.w);
			return;
		}
		gl_FragColor = GiChequerFill(uv, texel);
		return;
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
