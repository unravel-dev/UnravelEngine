#ifndef __GI_SHORT_RANGE_AO_SH__
#define __GI_SHORT_RANGE_AO_SH__

/*
 * The short-range AO's layout at the tier's downsample factor (u_gi_short_range_ao.z: 1 at Epic, one texel per
 * pixel; 2 at High, half resolution), which cs_gi_short_range_ao.sc writes and the gather's temporal reconstructs
 * from (gi_short_range_ao_temporal.sh): each half-resolution texel holds the search of one full-resolution pixel of
 * its 2x2 block, the block's four pixels taken in turn over four frames and neighbouring blocks out of phase (a 4-rooks
 * pattern).
 *
 * The includer includes gi_common.sh (u_gi_frame, u_gi_view).
 */

/// x > 0 when the temporal's histories hold last frame, y > 0 rotates the per-pixel noise of both kernels by the R2
/// sequence over frames (any window of frames stratifies a pixel's samples) instead of a per-frame hash, z = the
/// downsample factor (1 or 2), w = the foreground reject power (the exponent of the foreground samples' fade).
uniform vec4 u_gi_short_range_ao;

#define u_gi_short_range_ao_r2_noise (u_gi_short_range_ao.y > 0.0)
#define u_gi_short_range_ao_is_full_res (u_gi_short_range_ao.z < 1.5)
#define u_gi_short_range_ao_downsample_factor int(u_gi_short_range_ao.z)
#define u_gi_short_range_ao_reject_power u_gi_short_range_ao.w

/// The search and the accumulation store the AO packed in one uint: the unit bent normal scaled by the visibility,
/// mapped from [-1, 1] to 11:11:10 unorm bits.
#define GI_SHORT_RANGE_AO_PACK_XY 2047.0
#define GI_SHORT_RANGE_AO_PACK_Z  1023.0

/// @p bent_and_visibility (a unit bent normal, the visibility) packed; a zero normal packs as +z.
uint GiPackShortRangeAO(vec4 bent_and_visibility)
{
	float bent_length = length(bent_and_visibility.xyz);
	vec3 bent = bent_length > 1e-6 ? bent_and_visibility.xyz / bent_length : vec3(0.0, 0.0, 1.0);
	vec3 encoded = saturate(bent * bent_and_visibility.w * 0.5 + 0.5);
	return (uint(encoded.x * GI_SHORT_RANGE_AO_PACK_XY + 0.5) << 21u) |
	       (uint(encoded.y * GI_SHORT_RANGE_AO_PACK_XY + 0.5) << 10u) |
	       uint(encoded.z * GI_SHORT_RANGE_AO_PACK_Z + 0.5);
}

/// A packed AO back to (unit bent normal, visibility = the packed vector's length).
vec4 GiUnpackShortRangeAO(uint packed)
{
	vec3 encoded = vec3(float(packed >> 21u) / GI_SHORT_RANGE_AO_PACK_XY,
	                    float((packed >> 10u) & 2047u) / GI_SHORT_RANGE_AO_PACK_XY,
	                    float(packed & 1023u) / GI_SHORT_RANGE_AO_PACK_Z);
	vec3 bent = encoded * 2.0 - 1.0;
	float visibility = length(bent);
	return vec4(bent / max(visibility, 1e-4), visibility);
}

/// Two noise values per pixel and frame, spatially blue (interleaved gradient noise) and rotated over frames by the R2
/// sequence or by a hash; @p slot decorrelates the kernels.
vec2 GiShortRangeAONoise(ivec2 pixel, float slot)
{
	vec2 coord = vec2(pixel) + slot * vec2(19.19, 23.71);
	BRANCH
	if(u_gi_short_range_ao_r2_noise)
	{
		return GiSpatioTemporalNoise2D(coord);
	}
	return BlueNoise2D(coord, mod(u_gi_frame_index, float(GI_INTEGRATE_NOISE_PERIOD)));
}

/// The full-resolution pixel texel @p texel searches from this frame: itself at full resolution, one pixel of its 2x2
/// block at half resolution.
ivec2 GiShortRangeAOPixel(ivec2 texel)
{
	BRANCH
	if(u_gi_short_range_ao_is_full_res)
	{
		return texel;
	}
	ivec2 cell = texel - (texel / 2) * 2;
	int index = cell.x + cell.y * 2 + int(mod(u_gi_frame_index, 4.0));
	index -= (index / 4) * 4;
	ivec2 jitter = ivec2(index >= 2 ? 1 : 0, (index == 1 || index == 3) ? 0 : 1);
	return min(texel * 2 + jitter, ivec2(u_gi_view_size) - ivec2(1, 1));
}

#endif // __GI_SHORT_RANGE_AO_SH__
