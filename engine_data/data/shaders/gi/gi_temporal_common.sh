#ifndef __GI_TEMPORAL_COMMON_SH__
#define __GI_TEMPORAL_COMMON_SH__

/*
 * History rules shared by the gather's temporal (gi_temporal_kernel.sh) and the rough
 * specular's (fs_gi_rough_specular.sc), so both judge a reprojected history the same way.
 *
 * The includer declares, before including this: s_gi_prev_depth (last frame's
 * full-resolution depth) and u_gi_prev_inv_view_proj (last frame's TAA-unjittered inverse
 * view-projection).
 */

#include "gi/gi_constants.sh"

/**
 * 1 when the history texel centred at @p tap_uv held THIS surface last frame: the world
 * position reconstructed from the previous depth buffer under that texel lies within
 * @p tolerance of @p world_position. The depth is a POINT fetch of the full-resolution
 * previous depth under the history texel's centre - a filtered depth at a silhouette is a
 * value between two surfaces that belongs to neither, exactly what the test must not see.
 * Sky (depth 1) is never a valid history for a surface.
 */
float GiHistoryTapValid(vec2 tap_uv, vec3 world_position, float tolerance)
{
	ivec2 prev_depth_size = textureSize(s_gi_prev_depth, 0);
	ivec2 depth_texel = clamp(ivec2(tap_uv * vec2(prev_depth_size)),
	                          ivec2(0, 0),
	                          prev_depth_size - ivec2(1, 1));
	float prev_depth = texelFetch(s_gi_prev_depth, depth_texel, 0).x;
	if(prev_depth >= 1.0)
	{
		return 0.0;
	}
	vec3 prev_clip_stored = clipTransform(vec3(tap_uv * 2.0 - 1.0, toClipSpaceDepth(prev_depth)));
	vec3 prev_world = clipToWorld(u_gi_prev_inv_view_proj, prev_clip_stored);
	return length(prev_world - world_position) <= tolerance ? 1.0 : 0.0;
}

/**
 * How far the share of a pixel's gather that hit MOVING geometry collapses its history cap,
 * in [0, GI_TEMPORAL_MOVING_MAX]: the bracket-weighted moving share over
 * GI_TEMPORAL_MOVING_FRACTION_FULL, past the GI_TEMPORAL_MOVING_DEAD_ZONE that keeps a few
 * stray hits from shortening a static pixel's window.
 */
float GiTemporalMovingAmount(float moving_share)
{
	float amount = saturate(moving_share / GI_TEMPORAL_MOVING_FRACTION_FULL);
	return saturate(min((amount - GI_TEMPORAL_MOVING_DEAD_ZONE) / (1.0 - GI_TEMPORAL_MOVING_DEAD_ZONE),
	                    GI_TEMPORAL_MOVING_MAX));
}

#endif // __GI_TEMPORAL_COMMON_SH__
