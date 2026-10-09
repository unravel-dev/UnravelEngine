/*
 * LOCAL EXPOSURE, stage 1: the luminance bilateral grid (UE PostProcessHistogram.usf:199-225,
 * PostProcessHistogram.cpp:475-542).
 *
 * The grid is a coarse histogram WITH position: one column per screen tile, LOCAL_EXPOSURE_SLICES
 * luminance slices deep, each holding the sum of log2 luminance and the sum of weights of the
 * cells that landed in it. The sums are raw, as UE's are, so a partial tile at the right or bottom
 * edge weighs by the cells it really has when the tonemapper interpolates between tiles. The
 * tonemapper samples the column at the PIXEL's own luminance slice and divides, which yields the
 * mean log luminance of the pixels around it that are roughly as bright as it is - an edge-aware
 * local mean, which is exactly what keeps local exposure from haloing across a window frame the
 * way a plain blur does.
 *
 * A tile is 32 x 32 metering cells = 128 x 128 view pixels, UE's 64 x 64 half-resolution texels.
 * One workgroup per tile, as UE dispatches it: every thread splits its cells between two slices
 * into group-shared fixed-point sums (UE packs its shared sums into integers too), and the first
 * LOCAL_EXPOSURE_SLICES threads write the column.
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
 * already removed, floored at the bottom of the histogram range), so the axis is the same one the
 * histogram bins on.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_exposure_log_lum, 0);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_WO(i_local_exposure_grid, rgba32f, 1);

/// x = metering grid width, y = metering grid height, z = cells per tile edge, w = slice count.
uniform vec4 u_local_grid_params;
/// x = min log2 luminance, y = 1 / log2 luminance range, z = tiles x, w = tiles y.
uniform vec4 u_local_grid_range;

#define u_meter_width   u_local_grid_params.x
#define u_meter_height  u_local_grid_params.y
#define u_slice_count   u_local_grid_params.w
#define u_min_log_lum   u_local_grid_range.x
#define u_inv_log_range u_local_grid_range.y

/// Luminance slices per tile column; the CPU side mirrors it (auto_exposure_pass::local_exposure_slices).
#define LOCAL_EXPOSURE_SLICES 32
/// Cells per tile edge (auto_exposure_pass::local_exposure_tile_cells).
#define LOCAL_EXPOSURE_TILE_CELLS 32
/// Threads per workgroup edge; each covers CELLS_PER_THREAD x CELLS_PER_THREAD cells of the tile.
#define GROUP_EDGE 16
#define CELLS_PER_THREAD 2
/// Fixed-point scales of the shared sums. A tile holds 1024 cells; the weight sum stays below
/// 1024 x WEIGHT_SCALE and the luminance sum (offset to the bottom of the axis, at most 30 stops)
/// below 1024 x 30 x LOG_SCALE, both far inside 32 bits.
#define WEIGHT_SCALE 4096.0
#define LOG_SCALE 4096.0

SHARED uint shared_weight[LOCAL_EXPOSURE_SLICES];
SHARED uint shared_log[LOCAL_EXPOSURE_SLICES];

NUM_THREADS(GROUP_EDGE, GROUP_EDGE, 1)
void main()
{
	uint local_index = gl_LocalInvocationIndex;
	if (local_index < uint(LOCAL_EXPOSURE_SLICES))
	{
		shared_weight[local_index] = 0u;
		shared_log[local_index] = 0u;
	}
	barrier();

	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 meter_size = ivec2(int(u_meter_width), int(u_meter_height));
	ivec2 first_cell = tile * LOCAL_EXPOSURE_TILE_CELLS + ivec2(gl_LocalInvocationID.xy) * CELLS_PER_THREAD;
	float last_slice = max(u_slice_count - 1.0, 1.0);
	for (int y = 0; y < CELLS_PER_THREAD; ++y)
	{
		for (int x = 0; x < CELLS_PER_THREAD; ++x)
		{
			ivec2 cell = first_cell + ivec2(x, y);
			// The last tile of each axis is partial; its missing cells simply do not count.
			if (cell.x >= meter_size.x || cell.y >= meter_size.y)
			{
				continue;
			}
			float log_lum = texelFetch(s_exposure_log_lum, cell, 0).x;
			float offset_log = max(log_lum - u_min_log_lum, 0.0);
			// Split between the two slices around the sample, exactly as the histogram splits
			// between bins: without it a tile's column steps as luminance drifts and the local
			// exposure factor flickers with it.
			float position = saturate(offset_log * u_inv_log_range) * last_slice;
			float lower_position = floor(position);
			float upper_share = position - lower_position;
			uint lower_slice = uint(min(lower_position, last_slice));
			uint upper_slice = uint(min(lower_position + 1.0, last_slice));
			float lower_share = 1.0 - upper_share;
			atomicAdd(shared_weight[lower_slice], uint(lower_share * WEIGHT_SCALE + 0.5));
			atomicAdd(shared_log[lower_slice], uint(offset_log * lower_share * LOG_SCALE + 0.5));
			atomicAdd(shared_weight[upper_slice], uint(upper_share * WEIGHT_SCALE + 0.5));
			atomicAdd(shared_log[upper_slice], uint(offset_log * upper_share * LOG_SCALE + 0.5));
		}
	}
	barrier();

	if (local_index < uint(LOCAL_EXPOSURE_SLICES))
	{
		float weight = float(shared_weight[local_index]) / WEIGHT_SCALE;
		// Back from the offset axis: sum(log) = sum(offset log) + min * sum(weight).
		float log_sum = float(shared_log[local_index]) / LOG_SCALE + u_min_log_lum * weight;
		int slice = int(local_index);
		imageStore(i_local_exposure_grid,
		           ivec2(tile.x * LOCAL_EXPOSURE_SLICES + slice, tile.y),
		           vec4(log_sum, weight, 0.0, 0.0));
	}
}
