/*
 * Temporal stability probe (temporal_probe_pass): folds one displayed frame into per-pixel
 * statistics. The RGBA32F sums hold r = running mean luminance and g = sum of squared
 * deviations (Welford's update, which stays exact in single precision over thousands of
 * frames where summing L and L^2 would not), b = sum of the absolute change against last
 * frame's luminance reprojected through the velocity buffer, a = frames that change was
 * measured on. Luminance is in 8-bit display levels (Rec. 709 weights on the display-encoded
 * image), the unit of the capture tooling.
 *
 * The change is NOT measured where it cannot be trusted: object motion (the velocity buffer's
 * object part), a reprojection off screen, a previous depth that disagrees with this pixel's,
 * or a depth edge in the cross neighbourhood (a bilinear previous sample there mixes surfaces).
 */

#include "../bgfx_compute.sh"
#include "../shaderlib.sh"

SAMPLER2D(s_color, 0);
SAMPLER2D(s_velocity, 1);
SAMPLER2D(s_depth, 2);
SAMPLER2D(s_prev_depth, 3);
SAMPLER2D(s_prev_luma, 4);
IMAGE2D_RW(s_sums, rgba32f, 5);
IMAGE2D_WO(s_luma_out, r32f, 6);

/// x, y = image size in pixels; z = 1 when the velocity buffer is bound; w = 1 when last frame's
/// luminance exists (0 on the first frame of a measurement, which also resets the sums).
uniform vec4 u_probe_params;
/// x = this frame's 1-based index in the measurement; y = 1 when last frame's depth is bound; z = 1 for the
/// low-pass lane (PROBE_LOWPASS_RADIUS).
uniform vec4 u_probe_params2;

/// Relative linear-depth disagreement beyond which a neighbour or a reprojected sample is
/// another surface.
#define PROBE_DEPTH_TOLERANCE 0.05
/// Object motion, in pixels, above which the change is not measured.
#define PROBE_OBJECT_MOTION_PIXELS 0.5
/// Box radius of the LOW-PASS lane: every statistic runs on the mean luminance of a (2r + 1)^2 box, so the
/// sub-pixel offset of a reprojection under camera motion stops reading textured detail as change (a static Base
/// Color view read 1.7 levels of raw change at 3 degrees per frame) while patch-scale flicker stays.
#define PROBE_LOWPASS_RADIUS 2

bool ProbeSameSurface(float depth, float other)
{
	return abs(other - depth) <= PROBE_DEPTH_TOLERANCE * max(abs(depth), 1e-4);
}

/// The displayed luminance at @p uv in 8-bit levels: the pixel itself, or the low-pass lane's box mean.
float ProbeDisplayLuma(vec2 uv, vec2 texel)
{
	vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);
	if(u_probe_params2.z < 0.5)
	{
		return dot(saturate(texture2DLod(s_color, uv, 0.0).rgb), luma_weights) * 255.0;
	}
	float sum = 0.0;
	for(int y = -PROBE_LOWPASS_RADIUS; y <= PROBE_LOWPASS_RADIUS; ++y)
	{
		for(int x = -PROBE_LOWPASS_RADIUS; x <= PROBE_LOWPASS_RADIUS; ++x)
		{
			vec2 tap = uv + vec2(float(x), float(y)) * texel;
			sum += dot(saturate(texture2DLod(s_color, tap, 0.0).rgb), luma_weights);
		}
	}
	float taps = float((2 * PROBE_LOWPASS_RADIUS + 1) * (2 * PROBE_LOWPASS_RADIUS + 1));
	return sum / taps * 255.0;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	vec2 size = u_probe_params.xy;
	if(coord.x >= int(size.x) || coord.y >= int(size.y))
	{
		return;
	}
	vec2 texel = vec2_splat(1.0) / size;
	vec2 uv = (vec2(coord) + vec2_splat(0.5)) * texel;
	float luma = ProbeDisplayLuma(uv, texel);
	imageStore(s_luma_out, coord, vec4(luma, 0.0, 0.0, 1.0));
	bool has_previous = u_probe_params.w > 0.5;
	vec4 sums = has_previous ? imageLoad(s_sums, coord) : vec4_splat(0.0);
	float mean_old = has_previous ? sums.r : luma;
	float mean_new = mean_old + (luma - mean_old) / max(u_probe_params2.x, 1.0);
	float change = 0.0;
	float measured = 0.0;
	if(has_previous)
	{
		vec2 prev_uv = uv;
		bool valid = true;
		if(u_probe_params.z > 0.5)
		{
			vec4 velocity = texture2DLod(s_velocity, uv, 0.0);
			valid = length(velocity.ba * size) <= PROBE_OBJECT_MOTION_PIXELS;
			prev_uv = uv - velocity.rg;
		}
		valid = valid && prev_uv.x >= 0.0 && prev_uv.y >= 0.0 && prev_uv.x <= 1.0 && prev_uv.y <= 1.0;
		float depth = screenSpaceToViewSpaceDepth(texture2DLod(s_depth, uv, 0.0).x);
		float depth_left = screenSpaceToViewSpaceDepth(texture2DLod(s_depth, uv - vec2(texel.x, 0.0), 0.0).x);
		float depth_right = screenSpaceToViewSpaceDepth(texture2DLod(s_depth, uv + vec2(texel.x, 0.0), 0.0).x);
		float depth_up = screenSpaceToViewSpaceDepth(texture2DLod(s_depth, uv - vec2(0.0, texel.y), 0.0).x);
		float depth_down = screenSpaceToViewSpaceDepth(texture2DLod(s_depth, uv + vec2(0.0, texel.y), 0.0).x);
		valid = valid && ProbeSameSurface(depth, depth_left) && ProbeSameSurface(depth, depth_right) &&
		        ProbeSameSurface(depth, depth_up) && ProbeSameSurface(depth, depth_down);
		if(u_probe_params2.y > 0.5)
		{
			float prev_depth = screenSpaceToViewSpaceDepth(texture2DLod(s_prev_depth, prev_uv, 0.0).x);
			valid = valid && ProbeSameSurface(depth, prev_depth);
		}
		if(valid)
		{
			change = abs(luma - texture2DLod(s_prev_luma, prev_uv, 0.0).x);
			measured = 1.0;
		}
	}
	imageStore(s_sums,
	           coord,
	           vec4(mean_new, sums.g + (luma - mean_old) * (luma - mean_new), sums.b + change, sums.a + measured));
}
