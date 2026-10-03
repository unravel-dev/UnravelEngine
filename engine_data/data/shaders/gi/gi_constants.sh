#ifndef __GI_CONSTANTS_SH__
#define __GI_CONSTANTS_SH__

/*
 * MIRROR of engine/engine/rendering/gi/gi_constants.h - the single owner of every cross-pass
 * GI constant. Do not add a constant here without adding it to the table in that header:
 * gi_tests parses this file and fails on any mismatch or orphan, in both directions.
 *
 * Units and justifications live with the table in the header; this file is deliberately bare.
 */

#define GI_SDF_GRID_PARAMS_VEC4                2
#define GI_EMISSIVE_LIGHT_SOURCE_MIN_LUMINANCE 0.05
#define GI_CLIPMAP_EDIT_THROTTLE_FRAMES        8
#define GI_CACHED_LIGHTING_PRE_EXPOSURE        1.0

#endif // __GI_CONSTANTS_SH__
