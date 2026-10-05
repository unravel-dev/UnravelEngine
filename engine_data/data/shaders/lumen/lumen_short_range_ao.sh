#ifndef __LUMEN_SHORT_RANGE_AO_SH__
#define __LUMEN_SHORT_RANGE_AO_SH__

/*
 * The short-range AO's layout at LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR (1 at Epic: one texel per pixel; 2: half
 * resolution), which cs_lumen_short_range_ao.sc writes and the gather's temporal reconstructs from
 * (lumen_short_range_ao_temporal.sh): each half-resolution texel holds the search of one full-resolution pixel of its
 * 2x2 block, the block's four pixels taken in turn over four frames and neighbouring blocks out of phase (a 4-rooks pattern; UE GetDownsampleJitter2x2, StochasticLightingUpsample.ush:16-27, and
 * GetDownsampledCoordJitter, LumenMaterial.ush:87-101).
 *
 * The includer includes lumen_common.sh (u_lumen_frame, u_lumen_view).
 */

/// x > 0 when the temporal's histories hold last frame, y > 0 rotates the per-pixel noise of both kernels by the R2
/// sequence over frames (any window of frames stratifies a pixel's samples, as UE's spatiotemporal blue noise does)
/// instead of a per-frame hash.
uniform vec4 u_lumen_short_range_ao;

/// Two noise values per pixel and frame, spatially blue (interleaved gradient noise) and rotated over frames by the R2
/// sequence or by a hash; @p slot decorrelates the kernels.
vec2 LumenShortRangeAONoise(ivec2 pixel, float slot)
{
	vec2 coord = vec2(pixel) + slot * vec2(19.19, 23.71);
	BRANCH
	if(u_lumen_short_range_ao.y > 0.0)
	{
		return LumenSpatioTemporalNoise2D(coord);
	}
	return BlueNoise2D(coord, mod(u_lumen_frame_index, float(LUMEN_INTEGRATE_NOISE_PERIOD)));
}

/// The full-resolution pixel texel @p texel searches from this frame: itself at full resolution, one pixel of its 2x2
/// block at half resolution.
ivec2 LumenShortRangeAOPixel(ivec2 texel)
{
#if LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR == 1
	return texel;
#else
	ivec2 cell = texel - (texel / 2) * 2;
	int index = cell.x + cell.y * 2 + int(mod(u_lumen_frame_index, 4.0));
	index -= (index / 4) * 4;
	ivec2 jitter = ivec2(index >= 2 ? 1 : 0, (index == 1 || index == 3) ? 0 : 1);
	return min(texel * 2 + jitter, ivec2(u_lumen_view_size) - ivec2(1, 1));
#endif
}

#endif // __LUMEN_SHORT_RANGE_AO_SH__
