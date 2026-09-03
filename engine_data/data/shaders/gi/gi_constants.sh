#ifndef __GI_CONSTANTS_SH__
#define __GI_CONSTANTS_SH__

/*
 * MIRROR of engine/engine/rendering/gi/gi_constants.h - the single owner of every cross-pass
 * GI constant. Do not add a constant here without adding it to the table in that header:
 * gi_tests parses this file and fails on any mismatch or orphan, in both directions.
 *
 * Units and justifications live with the table in the header; this file is deliberately bare.
 */

#define GI_TRACE_MAX_STEPS              64
#define GI_MESH_SDF_TRACE_RANGE         2.0
#define GI_EXPAND_MAX_VOXEL_DIAGONALS   0.5
#define GI_EXPAND_RAMP_VOXEL_DIAGONALS  4.0

#define GI_SURFACE_VOXEL_BAND           1.0
#define GI_LIGHT_VOXEL_VISIBILITY_MIN   0.25
#define GI_LIGHT_VOXEL_UPDATE_DENOM     4
#define GI_MAX_ALBEDO                   0.9
#define GI_LIGHT_VOXEL_CULLED_ALPHA     0.00390625

#define GI_SHADOW_DISTANCE              100.0
#define GI_SHADOW_NORMAL_BIAS_VOXELS    1.0
#define GI_SHADOW_SURFACE_BIAS          0.35
#define GI_SHADOW_RELAXATION            0.0
#define GI_SHADOW_RAY_START_VOXELS      1.0
#define GI_SHARED_ORIGIN_REDESCENT_VOXELS 0.15
#define GI_SHARED_ORIGIN_SAMPLES        4
#define GI_SUN_SHADOWMAP_MAX_VOXEL      0.125
#define GI_WORLD_PROBE_DIVISOR          16
#define GI_WORLD_PROBE_RAYS_PER_FRAME   16
#define GI_WORLD_PROBE_OCT_RADIANCE     16
#define GI_WORLD_PROBE_OCT_IRRADIANCE   8
#define GI_WORLD_PROBE_OCT_DEPTH        8
#define GI_WORLD_PROBE_DEPTH_SHARPNESS  50.0
#define GI_PROBE_TRACE_SURFACE_BIAS     0.5
#define GI_WORLD_PROBE_TRACE_BIAS       1.0
#define GI_PROBE_TRACE_RELAXATION       0.05
#define GI_WORLD_PROBE_DEPTH_CLAMP      1.5
#define GI_WORLD_PROBE_WINDOW           16
// Floor for the Chebyshev visibility weight in the DDGI read chain. This is the
// through-wall bleed knob: an exterior sunlit probe adjacent to a sealed room
// contributes floor x crush of its brightness to every interior query no matter
// what the depth moments say. At 0.05 that bleed measured ~0.1-0.5% of sun level,
// which the closed-room bounce amplifies ~10x - invisible at exposure 1, a full
// wash under auto exposure's dark-adaptation gain. 0.005 cuts it 10x (the crush
// then takes it to ~1e-6) while the weight_sum <= 1e-5 fallback still catches
// fully-dead cages, and ITS consumers fail toward darkness, the safe direction.
#define GI_CHEBYSHEV_WEIGHT_FLOOR       0.005
#define GI_WORLD_PROBE_CAGE_VIS_STEPS   40
#define GI_WORLD_PROBE_CAGE_VIS_ACCEPT_VOXELS -0.1
#define GI_WORLD_PROBE_CAGE_VIS_GUARD_VOXELS  1.0
#define GI_WORLD_PROBE_CAGE_VIS_CROSS_VOXELS  0.25
#define GI_WORLD_PROBE_CAGE_VIS_CROSS_SLOPE   0.25
#define GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE 0.15
#define GI_PERCEPTION_CRUSH_THRESHOLD   0.2
#define GI_SELF_SHADOW_BIAS_NORMAL      0.2
#define GI_SELF_SHADOW_BIAS_VIEW        0.8
#define GI_SELF_SHADOW_BIAS_SCALE       0.75
#define GI_SELF_SHADOW_BIAS_K           0.3
#define GI_SELF_SHADOW_BIAS_MAX_VOXELS  2.0

#define GI_SCREEN_PROBE_SPACING         32
#define GI_ADAPTIVE_PLANE_TOLERANCE     0.05
#define GI_ADAPTIVE_RADIANCE_TOLERANCE  0.35
#define GI_ADAPTIVE_REVALIDATE_FRAMES   8
#define GI_MAX_RAY_RADIANCE             40.0
#define GI_SCREEN_TRACE_MAX_STEPS       64
#define GI_SCREEN_TRACE_MIN_MIP         1
#define GI_SCREEN_TRACE_DEPTH_TOLERANCE 0.15
#define GI_SCREEN_TRACE_THICKNESS       0.5
#define GI_SCREEN_TRACE_CONFIDENCE_MIN  0.5
#define GI_BOUNCE_AO_STEPS              3
#define GI_BOUNCE_TINT_MAX_VISIBILITY   0.95
#define GI_BOUNCE_TINT_MIN_AXIS_DOMINANCE 0.8
#define GI_FILTER_ANGLE_LIMIT_COS       0.99802673
#define GI_FILTER_PARALLAX_SCALE        1.5
#define GI_FILTER_ANGLE_RELAX_MAX       0.2
#define GI_REFLECTION_ROUGH_CUTOFF           0.4
#define GI_REFLECTION_GATHER_FADE_START      0.3
#define GI_REFLECTION_MESH_SDF_RANGE_SHARP   16.0
#define GI_REFLECTION_MESH_SDF_RANGE_GLOSS   8.0
#define GI_REFLECTION_TRACE_SURFACE_BIAS     0.25
#define GI_REFLECTION_TRACE_RELAXATION       0.0
#define GI_REFLECTION_REFINE_VOXELS          2.0
#define GI_REFLECTION_REFINE_STEPS           16
#define GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF   0.15
#define GI_REFLECTION_CASCADE_FADE_VOXELS    8.0
#define GI_REFLECTION_TEMPORAL_FRAMES        8
#define GI_REFLECTION_MEAN_SLOTS             1024
#define GI_REFLECTION_REMODULATE_ALBEDO_FLOOR 0.02
#define GI_REFLECTION_REMODULATE_RATIO_MAX   4.0
#define GI_REFLECTION_MIRROR_ROUGHNESS       0.06
#define GI_REFLECTION_CLAMP_MOTION_TEXELS    1.0
#define GI_REFLECTION_MOTION_WINDOW          3.0
#define GI_REFLECTION_STILL_WINDOW_SCALE     4.0
#define GI_REFLECTION_FIREFLY_CLAMP          8.0
#define GI_REFLECTION_MOVER_STILL_CAP        0.125

#define GI_INTERPOLATION_JITTER_TILES 0.75
#define GI_IMPORTANCE_SUPERSAMPLE_RATIO 2.0
#define GI_IMPORTANCE_SUPERSAMPLE_MAX   4
#define GI_TEMPORAL_MAX_FRAMES          24
#define GI_TEMPORAL_FAST_FRAMES         8
#define GI_TEMPORAL_SLOW_FRAMES         96
#define GI_CLIPMAP_EDIT_THROTTLE_FRAMES 8
#define GI_LIGHT_VOXEL_SUN_DITHER       0.25
#define GI_LIGHT_VOXEL_EMA_BLEND        0.125
#define GI_QUIESCENCE_LUMINANCE_FLOOR   0.001
#define GI_QUIESCENCE_STATS_SCALE       1024.0
#define GI_QUIESCENCE_MIN_FRAMES        32
#define GI_QUIESCENCE_MAX_FRAMES        1024
#define GI_QUIESCENCE_CONVERGED_MEAN    0.0005
#define GI_QUIESCENCE_STATIONARY_FRACTION 0.95
#define GI_QUIESCENCE_WINDOW_FRAMES     8
#define GI_QUIESCENCE_COMPARE_FRAMES    32
#define GI_GATHER_FIREFLY_CLAMP         8.0
#define GI_TEMPORAL_CHANGE_SIGMA        3.0
#define GI_TEMPORAL_DEPTH_TOLERANCE     0.1
#define GI_TEMPORAL_DIRTY_HOLD_FRAMES   48
#define GI_TEMPORAL_DIRTY_MAX_BOUNDS    16
#define GI_LIGHT_VOXEL_FADE_VOXELS      16.0
#define GI_LIGHT_VOXEL_INHERIT_CONTRAST 4.0
#define GI_LIGHT_VOXEL_INHERIT_FLOOR    0.0001
#define GI_LIGHT_VOXEL_SEED_ALPHA       0.25
#define GI_DENOISE_REVEAL_STEP          8
#define GI_DENOISE_REVEAL_COUNT         8
#define GI_REFLECTION_ROUGH_WINDOW_SCALE 4.0
#define GI_REFLECTION_RESOLVE_START     0.1
#define GI_WORLD_PROBE_EMA_WINDOWS      16
#define GI_EMISSIVE_NEE_MAX_EMITTERS    64
#define GI_EMISSIVE_NEE_PER_PROBE       4
#define GI_EMISSIVE_NEE_SAMPLES         1
#define GI_EMISSIVE_NEE_MIN_CONE_COS    0.866
#define GI_EMISSIVE_NEE_SEGMENT         1.0
#define GI_EMISSIVE_NEE_MIN_LUMINANCE   0.05
#define GI_WORLD_PROBE_BLEND_BAND       0.5

#endif // __GI_CONSTANTS_SH__
