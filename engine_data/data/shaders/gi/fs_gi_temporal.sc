$input v_texcoord0

/*
 * Temporal accumulation for the surface cache gather.
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
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gi_current, 0);
SAMPLER2D(s_gi_history, 1);
SAMPLER2D(s_gi_depth, 2);
SAMPLER2D(s_gi_prev_depth, 3);
SAMPLER2D(s_gi_normal, 4);
SAMPLER2D(s_gi_history_moments, 5);

uniform mat4 u_gi_prev_view_proj;
uniform mat4 u_gi_prev_inv_view_proj;

/// x = reprojection tolerance as a FRACTION of view distance, y = minimum normal agreement,
/// z = accumulation cap in frames, w = 1 when a usable history exists.
uniform vec4 u_gi_temporal_params;
#define u_gi_depth_tolerance  u_gi_temporal_params.x
#define u_gi_normal_threshold u_gi_temporal_params.y
#define u_gi_max_accum        u_gi_temporal_params.z
#define u_gi_has_history      (u_gi_temporal_params.w > 0.5)

/// xy = one texel of this buffer, zw = its dimensions.
uniform vec4 u_gi_temporal_texel;
/// x = history clamp width in neighbourhood standard deviations; 0 disables clamping.
uniform vec4 u_gi_temporal_clamp;
#define u_gi_clamp_sigma u_gi_temporal_clamp.x
uniform vec4 u_gi_temporal_camera;

#define GI_COLOR_OUT   gl_FragData[0]
#define GI_MOMENTS_OUT gl_FragData[1]

/// Replaces any non-finite component. A single NaN in the history is otherwise permanent: it
/// propagates through every subsequent blend and spreads outward through the spatial filter.
vec4 GiSanitize(vec4 v)
{
	return mix(v, vec4_splat(0.0), vec4(
		isnan(v.x) || isinf(v.x) ? 1.0 : 0.0,
		isnan(v.y) || isinf(v.y) ? 1.0 : 0.0,
		isnan(v.z) || isinf(v.z) ? 1.0 : 0.0,
		isnan(v.w) || isinf(v.w) ? 1.0 : 0.0));
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

/// A first frame: current estimate, one accumulated sample, zero variance.
vec4 GiFreshMoments(vec4 current)
{
	float luma = Luminance(current.xyz);
	return vec4(luma, luma * luma, 1.0, 0.0);
}

/**
 * All of the accumulation logic, returning through out parameters.
 *
 * Deliberately NOT writing gl_FragData directly, despite the early exits reading more naturally
 * that way: HLSL fragment outputs are out-parameters of main rather than globals, so assigning to
 * them from a helper fails on the D3D backend alone with an undeclared-identifier error that does
 * not name the output.
 */
void GiResolveTemporal(vec2 uv, out vec4 out_color, out vec4 out_moments)
{
	vec4 current = GiSanitize(texture2DLod(s_gi_current, uv, 0.0));
	if(!u_gi_has_history)
	{
		out_color = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	// Sky: the gather wrote zero here and there is nothing to accumulate.
	if(depth >= 1.0)
	{
		out_color = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
	if(prev_clip4.w <= 0.0)
	{
		out_color = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	vec3 prev_clip = clipTransform(prev_clip4.xyz / prev_clip4.w);
	vec2 prev_uv = prev_clip.xy * 0.5 + 0.5;
	// Off screen last frame: clamping to the edge would smear whatever sat on the border across
	// the whole disoccluded region.
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		out_color = current;
		out_moments = GiFreshMoments(current);
		return;
	}
	// Validate by reconstructing the world position the previous frame actually held there.
	// Comparing WORLD positions keeps the test independent of the depth encoding and projection.
	float prev_depth = texture2DLod(s_gi_prev_depth, prev_uv, 0.0).x;
	if(prev_depth >= 1.0)
	{
		out_color = current;
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
		out_moments = GiFreshMoments(current);
		return;
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
	if(u_gi_clamp_sigma > 0.0)
	{
		vec3 range_min;
		vec3 range_max;
		GiNeighbourhoodRange(uv, u_gi_clamp_sigma, range_min, range_max);
		history.xyz = clamp(history.xyz, range_min, range_max);
	}
	// Moments stay BILINEAR on purpose. Catmull-Rom reads a 4x4 footprint, which during
	// disocclusion would pull variance from a neighbouring surface and collapse the luminance
	// stop on this one.
	vec4 history_moments = GiSanitize(texture2DLod(s_gi_history_moments, prev_uv, 0.0));
	// 1/n while n grows, so early frames converge fast and the average is a true mean rather than
	// an exponential one with a permanent noise floor. The cap keeps it responsive to lighting
	// that actually changes instead of freezing forever.
	float count = min(history_moments.z + 1.0, max(u_gi_max_accum, 1.0));
	float alpha = 1.0 / count;
	float luma = Luminance(current.xyz);
	// Both the estimate AND its resolve weight are accumulated. The weight is the fraction of
	// rays that resolved and is every bit as noisy at four rays per pixel; leaving it unfiltered
	// keeps the consumer's blend flickering after the colour has settled.
	out_color = mix(history, current, alpha);
	out_moments = vec4(mix(history_moments.x, luma, alpha),
	                   mix(history_moments.y, luma * luma, alpha),
	                   count,
	                   0.0);
}

void main()
{
	vec4 color;
	vec4 moments;
	GiResolveTemporal(v_texcoord0, color, moments);
	GI_COLOR_OUT = color;
	GI_MOMENTS_OUT = moments;
}
