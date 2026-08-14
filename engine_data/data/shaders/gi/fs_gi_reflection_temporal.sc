$input v_texcoord0

/*
 * GI reflection temporal integrator: folds each frame's single stochastic GGX sample into a
 * reprojected running mean, so the lobe genuinely integrates over
 * GI_REFLECTION_TEMPORAL_FRAMES instead of shimmering. History is clamped to the 3x3
 * neighbourhood bounds of THIS frame's GEOMETRIC samples - the standard TAA guard - so
 * disoccluded or moved reflections cannot ghost past one frame, while in-lobe jitter noise
 * averages out. Raw alpha is coverage (mesh-exact / refined hit). A coverage-0 sample is
 * not an image: history is held so a refined mean is not bleached by sky, and a zero count
 * lets the composite reveal the authored probe layer.
 */

#include "../common.sh"
#include "gi/gi_constants.sh"

SAMPLER2D(s_refl_raw, 0);
SAMPLER2D(s_refl_history, 1);
SAMPLER2D(s_refl_depth, 2);

uniform mat4 u_gi_refl_prev_view_proj;
/// x > 0 when the history target holds valid data; yz = 1 / target size; w = accumulation
/// window in frames (the settings knob; GI_REFLECTION_TEMPORAL_FRAMES is its default).
uniform vec4 u_gi_refl_temporal;

void main()
{
	vec2 uv = v_texcoord0;
	vec4 curr = texture2DLod(s_refl_raw, uv, 0.0);
	float depth = texture2DLod(s_refl_depth, uv, 0.0).x;
	BRANCH
	if(depth >= 1.0 || u_gi_refl_temporal.x < 0.5)
	{
		// No history: alpha is coverage. A geometric sample starts the running mean at 1;
		// a coverage-0 sample stays 0 so the composite reveals probes.
		float start = curr.w >= 0.5 ? 1.0 : curr.w;
		gl_FragColor = vec4(curr.xyz, start);
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
		float start = curr.w >= 0.5 ? 1.0 : curr.w;
		gl_FragColor = vec4(curr.xyz, start);
		return;
	}
	vec2 texel = u_gi_refl_temporal.yz;
	vec4 history_texel = texture2DLod(s_refl_history, prev_uv, 0.0);
	BRANCH
	if(curr.w < 0.5)
	{
		// Not an image this frame. Hold the geometric mean we already have; do not clamp
		// against a sky/empty neighbourhood that would bleach a refined history.
		gl_FragColor = vec4(history_texel.xyz, history_texel.w);
		return;
	}
	// Neighbourhood bounds from geometric samples only, so a coverage-0 neighbour cannot
	// shrink the AABB of a refined hit.
	vec4 lo = curr;
	vec4 hi = curr;
	for(int y = -1; y <= 1; ++y)
	{
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
			}
		}
	}
	// RUNNING MEAN, not a fixed EMA: alpha carries the accumulated frame count (the SSR
	// temporal-resolve convention). A fixed-weight EMA has a permanent variance floor -
	// about a quarter of the sample spread at weight 1/8 - which read as reflections that
	// never converge exactly where the stochastic spread is widest (measured, round 13).
	// The count clamp keeps steady-state responsiveness at one over the settings window.
	vec3 history_rgb = history_texel.w >= 0.5 ? clamp(history_texel.xyz, lo.xyz, hi.xyz) : curr.xyz;
	float prev_count = history_texel.w >= 0.5 ? history_texel.w : 0.0;
	float count = min(prev_count, max(u_gi_refl_temporal.w - 1.0, 1.0)) + 1.0;
	gl_FragColor = vec4(mix(history_rgb, curr.xyz, 1.0 / count), count);
}
