#pragma once

/*
 * Single owner of every cross-pass GI constant.
 *
 * Every entry carries its UNIT and its JUSTIFICATION - one of:
 *   - derived:   follows arithmetically from another value here or from a documented argument;
 *   - published: taken from a shipped system's published value (source named);
 *   - setting:   deliberately exposed on gi_component instead of living here.
 * A constant that fits none of those is a defect. Lumen's own parameters live in lumen_constants.h.
 *
 * The shader mirror is engine_data/data/shaders/gi/gi_constants.sh. shaderc cannot consume this
 * header, so the mirror is plain #defines - and the pair is kept honest by a TEST, not a comment:
 * the GI test suite parses the .sh for `#define GI_*` and asserts every table entry matches and no
 * shader-side constant is missing from this table. Editing one side alone fails the suite.
 */

// clang-format off
#define GI_CONSTANTS_TABLE(X)                                                                      \
    /* --- instances --- */                                                                        \
    X(GI_SDF_GRID_PARAMS_VEC4, 2,                                                                  \
      "vec4s", "derived: u_sdf_grid_params - [0] grid origin + cell size, [1] cell counts + the"   \
      " instance base")                                                                            \
    X(GI_EMISSIVE_LIGHT_SOURCE_MIN_LUMINANCE, 0.05f,                                               \
      "radiance luminance", "derived: a material emitting at least this marks its placements as"   \
      " emissive light sources (UE's Emissive Light Source, derived rather than authored), which"  \
      " the Lumen cascades keep however small; dimmer emission is ordinary surface lighting the"   \
      " probes find on their own rays")                                                            \
    /* --- global distance field --- */                                                            \
    X(GI_CLIPMAP_EDIT_THROTTLE_FRAMES, 8,                                                          \
      "frames", "derived: a content change too large for a partial update recomposes the whole"    \
      " level, a full non-toroidal distance volume, so a continuous stream of them (an editor"     \
      " drag of many instances) coalesces to one per this many frames per level (the pending"     \
      " fingerprint diff persists, so the final state lands within one window of release; origin" \
      " re-snaps and partial updates stay immediate)")                                             \
    X(GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME, 2,                                                     \
      "levels", "published: r.AOGlobalDistanceFieldClipmapUpdatesPerFrame (GlobalDistanceField.cpp" \
      ":130): the first level takes its partial updates every frame, the others staggered so"     \
      " about this many update per frame (GetUpdateFrequencyForClipmap, :712-747)")               \
    X(GI_CLIPMAP_MAX_PARTIAL_INSTANCES, 1024,                                                      \
      "instances", "published: a level whose instance set changed in more places than this"        \
      " recomposes in full (GlobalDistanceField.cpp:1258, NumCulledPrimitiveModifiedBounds)")      \
    X(GI_CLIPMAP_MAX_PARTIAL_BOXES, 64,                                                            \
      "boxes", "derived: a partial update dispatches one compose per box; beyond this many the"    \
      " boxes merge into their bounds, which costs voxels rather than dispatches")                \
    X(GI_CLIPMAP_PARTIAL_BOX_ALIGNMENT, 4,                                                         \
      "voxels", "derived: partial boxes start and end on multiples of the compose group edge, a"   \
      " multiple of the coverage downsample and of the object grid cell (2 voxels each)")          \
    X(GI_CLIPMAP_MAX_PARTIAL_FRACTION, 0.25f,                                                      \
      "share of a level", "derived: partial boxes over this share of a level's voxels cost about"  \
      " a full recompose, which then takes the per-update level budget instead of bypassing it")   \
    /* --- exposure --- */                                                                         \
    X(GI_CACHED_LIGHTING_PRE_EXPOSURE, 1.0f,                                                       \
      "scale", "derived: the scale the PERSISTENT stores (the surface cache's lighting atlases,"   \
      " the radiance cache) hold their lighting at - Lumen's"                                      \
      " r.EyeAdaptation.CachedLightingPreExposure, which is 4 EV there. It must be a CONSTANT:"    \
      " the stores outlive any one frame's exposure, so a view-dependent scale would have to"      \
      " invalidate them on every adaptation step (UE resets its caches when the value changes)."   \
      " 1 (0 EV) because this engine's light units already sit about 16x under UE's physical"      \
      " scale, so float16 holds the stored range without an offset; the per-frame side gets its"   \
      " precision from the view pre-exposure instead (gi_pre_exposure.sh)")
// clang-format on

namespace unravel::gi
{

/// The constants as typed constexpr values, generated from the one table above.
#define GI_CONSTANT_EMIT(name, value, unit, why) inline constexpr auto name = value;
GI_CONSTANTS_TABLE(GI_CONSTANT_EMIT)
#undef GI_CONSTANT_EMIT

/// One table row, as the parity test consumes it.
struct gi_constant_row
{
    const char* name;
    double value;
};

/// Every constant with its numeric value, for enumeration by tests and debug UI.
inline constexpr gi_constant_row gi_constant_rows[] = {
#define GI_CONSTANT_ROW(name, value, unit, why) {#name, double(value)},
    GI_CONSTANTS_TABLE(GI_CONSTANT_ROW)
#undef GI_CONSTANT_ROW
};

} // namespace unravel::gi
