/*
 * LOCAL EXPOSURE, stage 2b: the blurred log luminance (UE PostProcessLocalExposure.cpp:253-306,
 * AddGaussianBlurPass in PostProcessWeightedSampleSum.cpp).
 *
 * One axis of a separable Gaussian over the 1/32 resolution log luminance
 * (cs_local_exposure_downsample.sc); the pass runs twice, x then y. UE's kernel exactly: the
 * radius R is half the kernel size percentage of the view width (GetBlurRadius), taps reach R
 * and weigh exp(-16.7 (d / R)^2) (NormalDistributionUnscaled), normalized, with the texture
 * mirrored at its edges (UseMirrorAddressMode). It is the second, edge-BLIND estimate of the
 * local level: the tonemapper mixes it with the bilateral value by `local_blurred_blend` and
 * falls back to it entirely where a pixel's own luminance slice holds no weight.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_local_blur_input, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_WO(i_local_blur_output, r16f, 1);

/// x = texture width, y = texture height, z = kernel radius R in texels, w = axis (0 = x, 1 = y).
uniform vec4 u_local_blur_params;

#define u_width        u_local_blur_params.x
#define u_height       u_local_blur_params.y
#define u_blur_radius  u_local_blur_params.z
#define u_blur_axis    u_local_blur_params.w

/// UE's tap limit (radius clamped to its 32-sample budget). Mirrors local_blur_max_radius in
/// auto_exposure_pass.cpp.
#define LOCAL_EXPOSURE_BLUR_MAX_RADIUS 31
/// UE NormalDistributionUnscaled's LegacyCompatibilityConstant.
#define UE_GAUSSIAN_FALLOFF -16.7

/// UE's mirror address mode on texel indices: -1 reads 0, n reads n - 1.
int mirror_index(int index, int count)
{
	int period = 2 * count;
	int wrapped = index - period * int(floor(float(index) / float(period)));
	return wrapped < count ? wrapped : period - 1 - wrapped;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	if (float(texel.x) >= u_width || float(texel.y) >= u_height)
	{
		return;
	}

	bool along_x = u_blur_axis < 0.5;
	int count = along_x ? int(u_width) : int(u_height);
	int center = along_x ? texel.x : texel.y;
	float radius = max(u_blur_radius, 1e-3);
	int integer_radius = int(min(ceil(radius), float(LOCAL_EXPOSURE_BLUR_MAX_RADIUS)));

	float sum = 0.0;
	float weight_sum = 0.0;
	LOOP
	for (int offset = -LOCAL_EXPOSURE_BLUR_MAX_RADIUS; offset <= LOCAL_EXPOSURE_BLUR_MAX_RADIUS; ++offset)
	{
		if (offset < -integer_radius || offset > integer_radius)
		{
			continue;
		}
		float distance_ratio = float(offset) / radius;
		float weight = exp(UE_GAUSSIAN_FALLOFF * distance_ratio * distance_ratio);
		int tap_index = mirror_index(center + offset, count);
		ivec2 tap = along_x ? ivec2(tap_index, texel.y) : ivec2(texel.x, tap_index);
		sum += texelFetch(s_local_blur_input, tap, 0).x * weight;
		weight_sum += weight;
	}

	imageStore(i_local_blur_output, texel, vec4(sum / max(weight_sum, 1e-20), 0.0, 0.0, 0.0));
}
