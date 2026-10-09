/*
 * LOCAL EXPOSURE, stage 2a: the log luminance at 1/32 of the view resolution, the input of the
 * blurred luminance (UE PostProcessing.cpp:1430-1435 reads its scene downsample chain at 1/32,
 * PostProcessLocalExposure.usf SetupLogLuminanceCS takes log2 of that level's luminance).
 *
 * One texel per LOCAL_EXPOSURE_BLUR_CELLS x LOCAL_EXPOSURE_BLUR_CELLS metering cells (each cell
 * is 4 x 4 view pixels): the LINEAR luminance is averaged and its log2 stored, as UE logs a
 * downsampled colour rather than averaging logs.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_exposure_log_lum, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_WO(i_local_exposure_downsampled, r16f, 1);

/// x = metering grid width, y = metering grid height, z = output width, w = output height.
uniform vec4 u_local_downsample_params;

#define u_meter_width   u_local_downsample_params.x
#define u_meter_height  u_local_downsample_params.y
#define u_output_width  u_local_downsample_params.z
#define u_output_height u_local_downsample_params.w

/// Metering cells per output texel edge. Mirrors auto_exposure_pass::local_exposure_blur_cells.
#define LOCAL_EXPOSURE_BLUR_CELLS 8

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	if (float(texel.x) >= u_output_width || float(texel.y) >= u_output_height)
	{
		return;
	}

	ivec2 base = texel * LOCAL_EXPOSURE_BLUR_CELLS;
	ivec2 meter_size = ivec2(int(u_meter_width), int(u_meter_height));
	float luminance_sum = 0.0;
	float cell_count = 0.0;
	LOOP
	for (int y = 0; y < LOCAL_EXPOSURE_BLUR_CELLS; ++y)
	{
		LOOP
		for (int x = 0; x < LOCAL_EXPOSURE_BLUR_CELLS; ++x)
		{
			ivec2 cell = base + ivec2(x, y);
			// The last texel of each axis is partial; its missing cells do not count.
			if (cell.x >= meter_size.x || cell.y >= meter_size.y)
			{
				continue;
			}
			luminance_sum += exp2(texelFetch(s_exposure_log_lum, cell, 0).x);
			cell_count += 1.0;
		}
	}
	float log_luminance = log2(max(luminance_sum / max(cell_count, 1.0), 1e-30));
	imageStore(i_local_exposure_downsampled, texel, vec4(log_luminance, 0.0, 0.0, 0.0));
}
