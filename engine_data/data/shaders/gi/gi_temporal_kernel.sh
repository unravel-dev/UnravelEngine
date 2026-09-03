#ifndef __GI_TEMPORAL_KERNEL_SH__
#define __GI_TEMPORAL_KERNEL_SH__

/*
 * Temporal accumulation for the surface cache gather - the shared body.
 *
 * TWO consumers. fs_gi_probe_integrate_temporal.sc (GI_TEMPORAL_FUSED) is the deliverable
 * path: the integrate result feeds the blend in registers, killing the full-target
 * GI_TRACE round trip between the two passes (written by integrate, read back by
 * temporal, one frame apart in cache terms). fs_gi_temporal.sc is the split fallback,
 * and the only form that supports the NEIGHBOURHOOD CLAMP: clamping needs this frame's
 * gather at nine neighbours, which only exists as a texture in the split form - the
 * fused form compiles the clamp out (the pass runs clamp 0 by policy anyway: it fights
 * the placement jitter and eats history under motion [S21 s98]).
 *
 * The gather takes a handful of ray samples per pixel of a strongly bimodal signal -- a ray
 * either finds a lit surface or a shadowed one -- so its per-frame estimate is very noisy, and
 * because the sample pattern is decorrelated every frame that noise MOVES. Averaging across
 * frames is what turns a handful of rays into an effective sample count in the hundreds.
 *
 * Three things make the difference between "blurry but still shimmering" and "settled":
 *
 *   - The blend weight follows an accumulation COUNT rather than being a fixed constant. A fixed
 *     weight is an exponential moving average, which has a permanent variance floor: it converges
 *     to a distribution, not to a value, so it keeps moving forever no matter how long the camera
 *     is held still. Averaging as 1/n while n grows is a true running mean and genuinely settles.
 *   - History is resampled with Catmull-Rom rather than bilinear. Reprojection lands between
 *     texels every frame, and repeated bilinear taps both soften the history and make it crawl.
 *   - Luminance moments are accumulated alongside, giving the spatial filter a per-pixel variance
 *     estimate so it can filter hard where the signal is still noisy and leave settled pixels be.
 *
 * History is validated by reprojecting through the previous view projection and reconstructing
 * the world position actually stored there. Accepting history from a different surface is what
 * smears light behind moving geometry, so the test errs toward rejection: a rejected pixel is
 * noisy for a few frames, a wrongly accepted one is visibly wrong for as long as it survives.
 *
 * DUAL-RATE HISTORY: two running means of the same sample stream ride the MRT - the SLOW
 * lane (cap ~96) is the output and carries the stability, the FAST lane (cap 8) exists to
 * measure it. A small bright emissive source excites amortization phase waves (the
 * world-probe stratum ramp x the light-voxel rotation, ~16-frame period) that a short mean
 * renders as crawling voxel-scale blobs - only a LONG mean averages them, but a long mean
 * alone answers real lighting changes late. The detector closes that: the two lanes' gap
 * has a known noise level (the moments' single-sample variance over both counts), and a
 * luminance gap beyond GI_TEMPORAL_CHANGE_SIGMA of it is a mean SHIFT - the slow lane
 * snaps to the fast one and re-accumulates, so a light turning on lands within the fast
 * window while steady flicker integrates over the slow one.
 *
 * The consumer declares s_gi_history, s_gi_history_fast and s_gi_history_moments at its
 * own free stages before including this; current, depth and world position arrive as
 * parameters (the split wrapper samples them, the fused form already owns them in
 * registers).
 */

#include "gi/gi_constants.sh"

/// Velocity buffer (full camera resolution): RG = total uv-delta (uv_curr - uv_prev,
/// unjittered previous), BA = the OBJECT-ONLY component, split at write time inside the
/// velocity pass. Stage 14 is the one register free in BOTH consumers - the fused form's
/// D3D t-register space is shared between samplers AND buffers (b_gi_probes sits on t7,
/// the SDF tables on t1-t3/t12-t13), so "free" must be judged across both. The kernel owns
/// the declaration. Never re-derive camera velocity from matrices here and compare against
/// this buffer - measured in this engine, the previous view-projection is NOT guaranteed
/// identical across passes within one frame (see the velocity plan).
SAMPLER2D(s_gi_velocity, 14);

uniform mat4 u_gi_prev_view_proj;
uniform mat4 u_gi_prev_inv_view_proj;

/// x = reprojection tolerance as a FRACTION of view distance, y = the FAST lane's
/// accumulation cap, z = the SLOW lane's accumulation cap, w = 1 when a usable history
/// exists.
uniform vec4 u_gi_temporal_params;
#define u_gi_depth_tolerance  u_gi_temporal_params.x
#define u_gi_fast_accum       u_gi_temporal_params.y
#define u_gi_max_accum        u_gi_temporal_params.z
#define u_gi_has_history      (u_gi_temporal_params.w > 0.5)

/// xy = one texel of this buffer, zw = its dimensions.
uniform vec4 u_gi_temporal_texel;
/// xyz = camera position; w = 1 when the velocity buffer is bound and should drive the
/// history reprojection (0 = legacy matrix path).
uniform vec4 u_gi_temporal_camera;
#define u_gi_velocity_available (u_gi_temporal_camera.w > 0.5)

/// DIRTY REGIONS - where an instance moved, appeared, vanished or changed material within the
/// last GI_TEMPORAL_DIRTY_HOLD_FRAMES (surface_cache_system::get_dirty_regions). x = how many
/// (min, max) pairs of u_gi_temporal_bounds are live, y = the soft margin around each in
/// metres (one level-0 probe spacing: the reach of a small mover's bounce pool). Inside a
/// region the slow lane collapses to the fast cap so the stale light the mover left behind
/// flushes; outside, the long mean stays. The old trigger was the content epoch - GLOBAL, so
/// one cube oscillating 30 m away pinned every pixel at the fast cap (measured: static-floor
/// noise doubled in a still shot).
uniform vec4 u_gi_temporal_dirty;
uniform vec4 u_gi_temporal_bounds[GI_TEMPORAL_DIRTY_MAX_BOUNDS * 2];

/// 1 inside a dirty region, fading to 0 over the margin outside its box.
float GiDirtyRegionFactor(vec3 world_position)
{
	int region_count = int(u_gi_temporal_dirty.x);
	float margin = max(u_gi_temporal_dirty.y, 1e-3);
	float factor = 0.0;
	LOOP
	for(int i = 0; i < GI_TEMPORAL_DIRTY_MAX_BOUNDS; ++i)
	{
		if(i >= region_count)
		{
			break;
		}
		vec3 region_min = u_gi_temporal_bounds[i * 2].xyz;
		vec3 region_max = u_gi_temporal_bounds[i * 2 + 1].xyz;
		vec3 outside = max(max(region_min - world_position, world_position - region_max), vec3_splat(0.0));
		factor = max(factor, saturate(1.0 - length(outside) / margin));
	}
	return factor;
}

/// Replaces any non-finite component. A single NaN in the history is otherwise permanent: it
/// propagates through every subsequent blend and spreads outward through the spatial filter.
/// Do not call isnan()/isinf() here: shaderc lowers them to equal/notEqual(float, float),
/// which OpenGL GLSL rejects (those builtins are vector-only). x != x detects NaN, and a
/// finite threshold above RGBA16F range stands in for the Inf test - the SSIL temporal's
/// convention.
vec4 GiSanitize(vec4 v)
{
	const float inf_threshold = 1e30;
	return mix(v, vec4_splat(0.0), vec4(
		v.x != v.x || abs(v.x) > inf_threshold ? 1.0 : 0.0,
		v.y != v.y || abs(v.y) > inf_threshold ? 1.0 : 0.0,
		v.z != v.z || abs(v.z) > inf_threshold ? 1.0 : 0.0,
		v.w != v.w || abs(v.w) > inf_threshold ? 1.0 : 0.0));
}

/**
 * Bicubic (Catmull-Rom) history resample as five bilinear taps.
 *
 * Reprojection almost never lands exactly on a texel, so history is resampled every single frame.
 * With bilinear that repeated resampling compounds: the history softens without limit and the
 * residual noise crawls across the surface. Catmull-Rom is near-lossless by comparison.
 */
vec4 GiSampleHistoryCatmullRom(sampler2D tex, vec2 uv, vec2 tex_size)
{
	vec2 sample_pos = uv * tex_size;
	vec2 tex_pos1 = floor(sample_pos - vec2_splat(0.5)) + vec2_splat(0.5);
	vec2 f = sample_pos - tex_pos1;
	vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	vec2 w3 = f * f * (-0.5 + 0.5 * f);
	// Combining w1+w2 lets the centre cross be a single bilinear tap.
	vec2 w12 = w1 + w2;
	vec2 offset12 = w2 / max(w12, vec2_splat(1e-6));
	vec2 inv_tex_size = vec2_splat(1.0) / max(tex_size, vec2_splat(1.0));
	vec2 tex_pos0 = (tex_pos1 - vec2_splat(1.0)) * inv_tex_size;
	vec2 tex_pos3 = (tex_pos1 + vec2_splat(2.0)) * inv_tex_size;
	vec2 tex_pos12 = (tex_pos1 + offset12) * inv_tex_size;
	vec4 result = GiSanitize(texture2DLod(tex, vec2(tex_pos12.x, tex_pos0.y), 0.0)) * (w12.x * w0.y);
	result += GiSanitize(texture2DLod(tex, vec2(tex_pos0.x, tex_pos12.y), 0.0)) * (w0.x * w12.y);
	result += GiSanitize(texture2DLod(tex, vec2(tex_pos12.x, tex_pos12.y), 0.0)) * (w12.x * w12.y);
	result += GiSanitize(texture2DLod(tex, vec2(tex_pos3.x, tex_pos12.y), 0.0)) * (w3.x * w12.y);
	result += GiSanitize(texture2DLod(tex, vec2(tex_pos12.x, tex_pos3.y), 0.0)) * (w12.x * w3.y);
	// The 5-tap subset omits the corner taps, so normalise: an unbalanced sum shows up as a
	// checkerboard brightness drift that depends on where the resample lands on the texel grid.
	float weight_sum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return result / max(weight_sum, 1e-6);
}

#ifndef GI_TEMPORAL_FUSED
/// x = history clamp width in neighbourhood standard deviations; 0 disables clamping.
/// Split form only: the clamp needs this frame's gather as a TEXTURE (nine neighbour taps),
/// which the fused form does not have.
uniform vec4 u_gi_temporal_clamp;
#define u_gi_clamp_sigma u_gi_temporal_clamp.x

/**
 * Colour range the current frame's 3x3 neighbourhood spans, as mean +/- @p sigma_scale deviations.
 *
 * This is what lets history be CLAMPED rather than accepted or rejected outright, and the
 * distinction matters more than it sounds. A binary test has to be tuned between two failures:
 * reject too readily and every rejected pixel falls back to a single frame of a very noisy gather,
 * which reads as fireflies; reject too rarely and stale history smears behind moving geometry as
 * ghosting. Sub-pixel TAA jitter makes that tuning impossible, because it moves the reprojected
 * sample every frame and high-frequency geometry then fails any strict test constantly, with a
 * static camera and nothing actually changing.
 *
 * Clamping removes the choice: history that agrees with the neighbourhood survives in full, and
 * history that disagrees is pulled to the edge of the current range instead of being thrown away.
 * Outliers cannot persist, and stale values cannot stray far from what this frame actually sees.
 */
void GiNeighbourhoodRange(vec2 uv, float sigma_scale, out vec3 out_min, out vec3 out_max)
{
	vec3 total = vec3_splat(0.0);
	vec3 total_squared = vec3_splat(0.0);
	for(int y = -1; y <= 1; ++y)
	{
		for(int x = -1; x <= 1; ++x)
		{
			vec2 offset = vec2(float(x), float(y)) * u_gi_temporal_texel.xy;
			vec3 c = GiSanitize(texture2DLod(s_gi_current, uv + offset, 0.0)).xyz;
			total += c;
			total_squared += c * c;
		}
	}
	vec3 mean = total / 9.0;
	// max() before the sqrt: the two sums are accumulated independently, so rounding can make the
	// variance very slightly negative on a perfectly flat neighbourhood, and sqrt of that is NaN
	// -- which the blend below would then make permanent.
	vec3 sigma = sqrt(max(total_squared / 9.0 - mean * mean, vec3_splat(0.0)));
	out_min = mean - sigma * sigma_scale;
	out_max = mean + sigma * sigma_scale;
}
#endif // !GI_TEMPORAL_FUSED

/// A first frame: current estimate, one accumulated sample, zero variance.
vec4 GiFreshMoments(vec4 current)
{
	float luma = Luminance(current.xyz);
	return vec4(luma, luma * luma, 1.0, 0.0);
}

/**
 * All of the accumulation logic, returning through out parameters.
 *
 * @p current is this frame's (sanitized) gather for the pixel, @p depth its device depth and
 * @p world_position its reconstruction - sampled by the split wrapper, already in registers
 * in the fused form. The world position is only consumed past the sky test, so a sky pixel's
 * garbage reconstruction is never read.
 *
 * Deliberately NOT writing gl_FragData directly, despite the early exits reading more naturally
 * that way: HLSL fragment outputs are out-parameters of main rather than globals, so assigning to
 * them from a helper fails on the D3D backend alone with an undeclared-identifier error that does
 * not name the output.
 */
void GiResolveTemporal(vec2 uv, vec4 current, float depth, vec3 world_position,
                       out vec4 out_color, out vec4 out_fast, out vec4 out_moments)
{
	if(!u_gi_has_history)
	{
		out_color = current;
		out_fast = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	// Sky: the gather wrote zero here and there is nothing to accumulate.
	if(depth >= 1.0)
	{
		out_color = current;
		out_fast = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	// History UV: camera-consistent pixels ALWAYS use this pass's own matrix reprojection;
	// the velocity buffer's RG drives only OBJECT-motion pixels (gated by BA, the
	// object-only split), where it makes history FOLLOW the mover so lighting accumulates
	// on moving geometry instead of resetting every frame. Trusting RG for camera pixels
	// drags the whole image: the buffer's camera component is written with a previous
	// view-projection that is not reliably this pass's own (measured; open engine issue -
	// see the velocity plan). Same gating as the TAA resolve, where the pattern was proven.
	float object_w = 0.0;
	vec2 prev_uv_object = vec2_splat(0.0);
	if(u_gi_velocity_available)
	{
		vec4 vel4 = texture2DLod(s_gi_velocity, uv, 0.0);
		prev_uv_object = uv - vel4.xy;
		vec2 vel_dim = vec2(textureSize(s_gi_velocity, 0));
		object_w = smoothstep(0.5, 1.5, length(vel4.zw * vel_dim));
	}
	vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
	if(prev_clip4.w <= 0.0 && object_w < 0.5)
	{
		out_color = current;
		out_fast = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	vec3 prev_clip = clipTransform(prev_clip4.xyz / max(prev_clip4.w, 1e-6));
	vec2 prev_uv = mix(prev_clip.xy * 0.5 + 0.5, prev_uv_object, object_w);
	// Off screen last frame: clamping to the edge would smear whatever sat on the border across
	// the whole disoccluded region.
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		out_color = current;
		out_fast = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	// Validate by reconstructing the world position the previous frame actually held there.
	// Comparing WORLD positions keeps the test independent of the depth encoding and projection.
	// OBJECT-MOTION pixels skip it: a mover's world position legitimately changed, so the test
	// would reject its own valid history. Newly revealed background behind a mover is
	// camera-consistent (object_w = 0) and keeps the full test; a mover emerging from behind an
	// occluder can briefly accept the occluder's history, which the dual-rate change detector
	// snaps away within the fast window.
	if(object_w < 0.5)
	{
		float prev_depth = texture2DLod(s_gi_prev_depth, prev_uv, 0.0).x;
		if(prev_depth >= 1.0)
		{
			out_color = current;
			out_fast = current;
			out_moments = GiFreshMoments(current);
			return;
		}
		vec3 prev_clip_stored = clipTransform(vec3(prev_uv * 2.0 - 1.0, toClipSpaceDepth(prev_depth)));
		vec3 prev_world = clipToWorld(u_gi_prev_inv_view_proj, prev_clip_stored);
		// Tolerance scales with view distance so one value works near and far: reprojection error and
		// depth precision both grow with distance.
		float view_distance = max(length(world_position - u_gi_temporal_camera.xyz), 1e-4);
		if(length(prev_world - world_position) > u_gi_depth_tolerance * view_distance)
		{
			out_color = current;
			out_fast = current;
			out_moments = GiFreshMoments(current);
			return;
		}
	}
	// The normal agreement test that used to sit here is gone, and deliberately.
	//
	// It sampled the CURRENT normal buffer at two UVs -- uv and prev_uv -- so it never compared
	// this frame's surface against the previous frame's; it compared two points of this one. That
	// is a weak proxy at best, and under sub-pixel jitter it is actively wrong: the two taps sit a
	// fraction of a pixel apart every frame, which on high-frequency geometry (foliage, railings,
	// ivy) disagree constantly. Every disagreement threw the whole history away and left the pixel
	// showing one frame of a four-ray gather, which is what fireflies ARE -- visible with the
	// camera completely still, because the jitter moves even when nothing else does.
	//
	// The neighbourhood clamp below covers what this was meant to catch, without a cliff.
	vec4 history = GiSampleHistoryCatmullRom(s_gi_history, prev_uv, u_gi_temporal_texel.zw);
#ifndef GI_TEMPORAL_FUSED
	if(u_gi_clamp_sigma > 0.0)
	{
		vec3 range_min;
		vec3 range_max;
		GiNeighbourhoodRange(uv, u_gi_clamp_sigma, range_min, range_max);
		history.xyz = clamp(history.xyz, range_min, range_max);
	}
#endif
	vec4 fast_history = GiSampleHistoryCatmullRom(s_gi_history_fast, prev_uv, u_gi_temporal_texel.zw);
	// Moments stay BILINEAR on purpose. Catmull-Rom reads a 4x4 footprint, which during
	// disocclusion would pull variance from a neighbouring surface and collapse the luminance
	// stop on this one.
	vec4 history_moments = GiSanitize(texture2DLod(s_gi_history_moments, prev_uv, 0.0));
	// 1/n while n grows, so early frames converge fast and the average is a true mean rather than
	// an exponential one with a permanent noise floor. The caps are the two lanes' windows; the
	// fast count is DERIVED from the shared one (a snap resets the shared count to it, so the
	// derivation stays consistent without a second stored counter).
	// The slow lane's cap is REGION-LOCAL: the fast cap inside a dirty region (a changed
	// instance's stale bounce flushes there), the settings window everywhere else - a
	// continuous mix over the margin so the flush boundary never prints as a noise step.
	float slow_cap = mix(max(u_gi_max_accum, 1.0), max(u_gi_fast_accum, 1.0),
	                     GiDirtyRegionFactor(world_position));
	float count = min(history_moments.z + 1.0, slow_cap);
	float count_fast = min(count, max(u_gi_fast_accum, 1.0));
	float alpha = 1.0 / count;
	float luma = Luminance(current.xyz);
	vec4 slow = mix(history, current, alpha);
	vec4 fast = mix(fast_history, current, 1.0 / count_fast);
	// THE CHANGE DETECTOR (see the header): both lanes average the same stream, so their gap's
	// noise variance is the single-sample variance over both counts. A gap beyond
	// GI_TEMPORAL_CHANGE_SIGMA of that is a mean shift - lighting actually changed - and the
	// slow lane snaps to the fast one and re-accumulates from its window. The moments follow
	// the same reset so the denoise's luminance stop widens exactly where history restarted.
	// The floor term keeps quantisation-level gaps on fully converged pixels (variance ~0)
	// from snapping the count for nothing.
	float variance_single = max(history_moments.y - history_moments.x * history_moments.x, 0.0);
	float gap = Luminance(slow.xyz) - Luminance(fast.xyz);
	float gate = GI_TEMPORAL_CHANGE_SIGMA * GI_TEMPORAL_CHANGE_SIGMA * variance_single *
	                 (1.0 / count_fast + 1.0 / count) +
	             1e-6;
	float moments_alpha = alpha;
	if(gap * gap > gate)
	{
		slow = fast;
		count = count_fast;
		moments_alpha = 1.0 / count_fast;
	}
	// Both the estimate AND its resolve weight are accumulated. The weight is the fraction of
	// rays that resolved and is every bit as noisy at four rays per pixel; leaving it unfiltered
	// keeps the consumer's blend flickering after the colour has settled.
	out_color = slow;
	out_fast = fast;
	out_moments = vec4(mix(history_moments.x, luma, moments_alpha),
	                   mix(history_moments.y, luma * luma, moments_alpha),
	                   count,
	                   0.0);
}

#endif // __GI_TEMPORAL_KERNEL_SH__
