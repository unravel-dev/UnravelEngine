/*
 * LOCAL EXPOSURE, stage 1: the luminance bilateral grid (UE PostProcessHistogram.usf:199-225,
 * PostProcessHistogram.cpp:475-542).
 *
 * The grid is a coarse histogram WITH position: one column per screen tile, LOCAL_EXPOSURE_SLICES
 * luminance slices deep, each holding the sum of log2 luminance and the sum of weights of the
 * cells that landed in it (both divided by the tile's cell count, so the stored numbers stay
 * O(1) whatever the tile size). The tonemapper samples the column at the PIXEL's own luminance
 * slice and divides, which yields the mean log luminance of the pixels around it that are
 * roughly as bright as it is - an edge-aware local mean, which is exactly what keeps local
 * exposure from haloing across a window frame the way a plain blur does.
 *
 * FLATTENED, not a 3D texture, though UE's is: an image3D of rg32f is rejected outright by the
 * D3D and SPIR-V paths here, and the GL path cannot sample one in a fragment shader
 * (texture3DLod is unresolved at profile 430 - the same wall the cloud shaders hit). The grid
 * is therefore one 2D RGBA32F image, tiles laid out left to right with their slices end to end:
 * texel (tile_x * slices + slice, tile_y). The tonemapper does its own trilinear gather over
 * that layout, which it would need in any case - a hardware bilinear would blend across slice
 * and tile boundaries indiscriminately.
 *
 * Input is the metering grid's log2 SCENE luminance (cs_luminance_histogram.sc; pre-exposure
 * already removed), so the axis is the same one the histogram bins on.
 *
 * The tile's plain mean log luminance is written out at the same time (i_local_exposure_mean):
 * every slice is already in registers here, so the reduction the blurred-luminance stage would
 * otherwise repeat costs nothing.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_exposure_log_lum, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_WO(i_local_exposure_grid, rgba32f, 1);
IMAGE2D_WO(i_local_exposure_mean, r32f, 2);

/// x = metering grid width, y = metering grid height, z = cells per tile edge, w = slice count.
uniform vec4 u_local_grid_params;
/// x = min log2 luminance, y = 1 / log2 luminance range, z = tiles x, w = tiles y.
uniform vec4 u_local_grid_range;

#define u_meter_width   u_local_grid_params.x
#define u_meter_height  u_local_grid_params.y
#define u_tile_cells    u_local_grid_params.z
#define u_slice_count   u_local_grid_params.w
#define u_min_log_lum   u_local_grid_range.x
#define u_inv_log_range u_local_grid_range.y
#define u_tiles_x       u_local_grid_range.z
#define u_tiles_y       u_local_grid_range.w

/// Luminance slices per tile column. A compile-time constant: it sizes the per-invocation
/// accumulators, and the CPU side mirrors it (auto_exposure_pass::local_exposure_slices).
#define LOCAL_EXPOSURE_SLICES 32
/// Cells per tile edge, likewise fixed so the gather loop has constant bounds.
#define LOCAL_EXPOSURE_TILE_CELLS 32

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if (float(tile.x) >= u_tiles_x || float(tile.y) >= u_tiles_y)
	{
		return;
	}

	float log_sum[LOCAL_EXPOSURE_SLICES];
	float weight_sum[LOCAL_EXPOSURE_SLICES];
	for (int slot = 0; slot < LOCAL_EXPOSURE_SLICES; ++slot)
	{
		log_sum[slot] = 0.0;
		weight_sum[slot] = 0.0;
	}

	ivec2 base = tile * LOCAL_EXPOSURE_TILE_CELLS;
	ivec2 meter_size = ivec2(int(u_meter_width), int(u_meter_height));
	float tile_log_sum = 0.0;
	float cell_count = 0.0;
	float last_slice = max(u_slice_count - 1.0, 1.0);
	LOOP
	for (int y = 0; y < LOCAL_EXPOSURE_TILE_CELLS; ++y)
	{
		LOOP
		for (int x = 0; x < LOCAL_EXPOSURE_TILE_CELLS; ++x)
		{
			ivec2 cell = base + ivec2(x, y);
			// The last tile of each axis is partial; its missing cells simply do not count.
			if (cell.x >= meter_size.x || cell.y >= meter_size.y)
			{
				continue;
			}
			float log_lum = texelFetch(s_exposure_log_lum, cell, 0).x;
			tile_log_sum += log_lum;
			cell_count += 1.0;
			// Split between the two slices around the sample, exactly as the histogram splits
			// between bins: without it a tile's column steps as luminance drifts and the local
			// exposure factor flickers with it.
			float position = saturate((log_lum - u_min_log_lum) * u_inv_log_range) * last_slice;
			float lower_position = floor(position);
			float upper_share = position - lower_position;
			int lower_slice = int(min(lower_position, last_slice));
			int upper_slice = int(min(lower_position + 1.0, last_slice));
			log_sum[lower_slice] += log_lum * (1.0 - upper_share);
			weight_sum[lower_slice] += (1.0 - upper_share);
			log_sum[upper_slice] += log_lum * upper_share;
			weight_sum[upper_slice] += upper_share;
		}
	}

	float inv_cells = cell_count > 0.0 ? 1.0 / cell_count : 0.0;
	int row_base = tile.x * LOCAL_EXPOSURE_SLICES;
	for (int slice = 0; slice < LOCAL_EXPOSURE_SLICES; ++slice)
	{
		imageStore(i_local_exposure_grid,
		           ivec2(row_base + slice, tile.y),
		           vec4(log_sum[slice] * inv_cells, weight_sum[slice] * inv_cells, 0.0, 0.0));
	}
	// An empty tile (off the metering grid entirely) reports the bottom of the axis; the
	// blurred stage's own weighting never reaches it, and the tonemapper clamps regardless.
	imageStore(i_local_exposure_mean,
	           tile,
	           vec4(cell_count > 0.0 ? tile_log_sum * inv_cells : u_min_log_lum, 0.0, 0.0, 0.0));
}
