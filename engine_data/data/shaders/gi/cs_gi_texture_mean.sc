/*
 * One-time GPU capture of a material texture's MEAN colour into the texture-mean buffer
 * (GI v2 plan 3.1). The attribute composer multiplies an instance's base colour factor by
 * this mean, so bounce light carries the surface's true average reflectance instead of the
 * factor alone (white on every textured material - the over-bright-GI failure mode).
 *
 * The mip chain is already a box-filtered mean of the texture, so this samples an 8x8 grid
 * at the mip whose resolution is about 8x8 and averages - for a mipped texture that reads
 * the texture's own average; for a mipless one it degrades to a sparse 64-tap estimate.
 * Sampling goes through the same sampler path the geometry pass uses, so whatever colour
 * decode applies there applies here identically and the mean matches what the renderer
 * actually draws. Dispatched ONCE per texture (1x1x1), never per frame.
 */

#include "bgfx_compute.sh"

SAMPLER2D(s_mean_source, 0);
/// vec4 per slot: rgb = mean colour, a = 1 once captured. Slot 0 is reserved WHITE.
BUFFER_RW(b_gi_texture_means, vec4, 1);

/// x = destination slot, y = mip lod to sample at (log2(max dimension) - 3, floored at 0).
uniform vec4 u_gi_texture_mean_params;

#define GI_TEXTURE_MEAN_GRID 8

NUM_THREADS(1, 1, 1)
void main()
{
	float lod = u_gi_texture_mean_params.y;
	vec3 sum = vec3_splat(0.0);
	for(int y = 0; y < GI_TEXTURE_MEAN_GRID; ++y)
	{
		for(int x = 0; x < GI_TEXTURE_MEAN_GRID; ++x)
		{
			vec2 uv = (vec2(float(x), float(y)) + vec2_splat(0.5)) / float(GI_TEXTURE_MEAN_GRID);
			sum += texture2DLod(s_mean_source, uv, lod).xyz;
		}
	}
	vec3 mean = sum / float(GI_TEXTURE_MEAN_GRID * GI_TEXTURE_MEAN_GRID);
	b_gi_texture_means[uint(u_gi_texture_mean_params.x)] = vec4(mean, 1.0);
}
