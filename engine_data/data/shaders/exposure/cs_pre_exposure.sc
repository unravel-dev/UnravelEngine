/*
 * The GPU's share of the view pre-exposure for this frame (pre_exposure.sh), written before any pass of
 * the frame writes or reads scene lighting.
 *
 * The whole pre-exposure is the manual exposure (the CPU's share, u_pre_exposure.x of the passes) times the
 * adapted exposure and the average local exposure the last frame left in AUTO_EXPOSURE, clamped to the
 * pre-exposure range; the GPU's share is that over the manual exposure. Without auto exposure the input is a white texture: share 1.
 *
 * Output (i_pre_exposure, the view's environment SH, read and written in place at PRE_EXPOSURE_GPU_TEXEL,
 * after the nine coefficients the irradiance pass writes later in the frame): x = share, y = 1 / share,
 * z = share / the last frame's share, w = the last frame's share.
 */

#include "bgfx_compute.sh"

/// AUTO_EXPOSURE (r = adapted exposure, a = average local exposure), or a white texture.
SAMPLER2D(s_exposure, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_RW(i_pre_exposure, rgba32f, 1);

/// x = the manual exposure (the CPU's share), y / z = the range the whole pre-exposure is clamped to
/// (pre_exposure_state::min_value / max_value).
uniform vec4 u_pre_exposure_params;

/// The texel of the share (pre_exposure.sh, pre_exposure_state::gpu_texel).
#define PRE_EXPOSURE_GPU_TEXEL 9

#define u_manual_exposure      u_pre_exposure_params.x
#define u_min_pre_exposure     u_pre_exposure_params.y
#define u_max_pre_exposure     u_pre_exposure_params.z

/// Exposures at or above this read as invalid, like the tonemapper's read of AUTO_EXPOSURE.
#define MAX_VALID_EXPOSURE 1.0e10

NUM_THREADS(1, 1, 1)
void main()
{
	vec4 exposure = texelFetch(s_exposure, ivec2(0, 0), 0);
	float adapted = max(exposure.x, 0.0) * max(exposure.w, 0.0);
	// NaN fails every comparison: it, and a missing or broken value, read as no adaptation.
	if(!(adapted > 0.0 && adapted < MAX_VALID_EXPOSURE))
	{
		adapted = 1.0;
	}
	float manual = max(u_manual_exposure, u_min_pre_exposure);
	float pre_exposure = clamp(manual * adapted, u_min_pre_exposure, u_max_pre_exposure);
	float share = pre_exposure / manual;
	float previous = imageLoad(i_pre_exposure, ivec2(PRE_EXPOSURE_GPU_TEXEL, 0)).x;
	if(!(previous > 0.0 && previous < MAX_VALID_EXPOSURE))
	{
		previous = share;
	}
	imageStore(i_pre_exposure, ivec2(PRE_EXPOSURE_GPU_TEXEL, 0), vec4(share, 1.0 / share, share / previous, previous));
}
