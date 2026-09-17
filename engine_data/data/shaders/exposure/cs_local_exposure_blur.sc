/*
 * LOCAL EXPOSURE, stage 2: the blurred log luminance (UE PostProcessLocalExposure.cpp:253-306).
 *
 * A wide Gaussian over the TILE means the grid pass already reduced (cs_local_exposure_grid.sc).
 * It is the second, edge-BLIND estimate of the local level: the tonemapper mixes it with the
 * bilateral value by `local_blurred_blend` and falls back to it entirely where a pixel's own
 * luminance slice holds too little weight to trust. UE blurs a downsampled log-luminance image
 * at a kernel of a percentage of the view width; the tile grid IS that downsample (one tile per
 * 32x32 metering cells, so a few hundred texels at any resolution), which puts a kernel of any
 * sensible width inside a single direct 2D pass - a separable pair would cost more in dispatch
 * overhead than it saves in taps at this size.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_local_exposure_mean, 0);
IMAGE2D_WO(s_local_exposure_blurred, r32f, 1);

/// x = tiles x, y = tiles y, z = blur radius in tiles, w unused.
uniform vec4 u_local_blur_params;

#define u_tiles_x     u_local_blur_params.x
#define u_tiles_y     u_local_blur_params.y
#define u_blur_radius u_local_blur_params.z

/// Tap limit per axis. The radius comes from the settings' kernel percentage against the tile
/// grid width; this bounds the worst case at (2 * 12 + 1)^2 taps of a few-hundred-texel image.
#define LOCAL_EXPOSURE_BLUR_MAX_RADIUS 12

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if (float(tile.x) >= u_tiles_x || float(tile.y) >= u_tiles_y)
	{
		return;
	}

	int radius = int(clamp(u_blur_radius, 1.0, float(LOCAL_EXPOSURE_BLUR_MAX_RADIUS)));
	// sigma = radius / 2: the kernel reaches two standard deviations, where the Gaussian is
	// already under 15% and the truncation does not print as a ring.
	float sigma = max(float(radius) * 0.5, 0.5);
	float inv_two_sigma_squared = 1.0 / (2.0 * sigma * sigma);
	ivec2 last_tile = ivec2(int(u_tiles_x) - 1, int(u_tiles_y) - 1);

	float sum = 0.0;
	float weight_sum = 0.0;
	LOOP
	for (int y = -LOCAL_EXPOSURE_BLUR_MAX_RADIUS; y <= LOCAL_EXPOSURE_BLUR_MAX_RADIUS; ++y)
	{
		if (y < -radius || y > radius)
		{
			continue;
		}
		LOOP
		for (int x = -LOCAL_EXPOSURE_BLUR_MAX_RADIUS; x <= LOCAL_EXPOSURE_BLUR_MAX_RADIUS; ++x)
		{
			if (x < -radius || x > radius)
			{
				continue;
			}
			// Clamped at the border: extending the edge tile is what keeps the blur from
			// darkening toward the frame's edges, where a zero-weighted tap would pull.
			ivec2 tap = clamp(tile + ivec2(x, y), ivec2(0, 0), last_tile);
			float weight = exp(-float(x * x + y * y) * inv_two_sigma_squared);
			sum += texelFetch(s_local_exposure_mean, tap, 0).x * weight;
			weight_sum += weight;
		}
	}

	imageStore(s_local_exposure_blurred, tile, vec4(weight_sum > 0.0 ? sum / weight_sum : 0.0, 0.0, 0.0, 0.0));
}
