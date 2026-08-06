#pragma once

/*
 * Single owner of every cross-pass GI v2 constant (plan: tasks/gi_rewrite_plan.md, section 4).
 *
 * Every entry carries its UNIT and its JUSTIFICATION - one of:
 *   - derived:   follows arithmetically from another value here or from a documented argument;
 *   - published: taken from a shipped system's published value (source named);
 *   - setting:   deliberately exposed on gi_component instead of living here.
 * A constant that fits none of those is a defect by the plan's R9.
 *
 * The shader mirror is engine_data/data/shaders/gi/gi_constants.sh. shaderc cannot consume this
 * header, so the mirror is plain #defines - and the pair is kept honest by a TEST, not a comment:
 * gi_v2_tests parses the .sh for `#define GI_*` and asserts every table entry matches and no
 * shader-side constant is missing from this table. Editing one side alone fails the suite.
 *
 * Source abbreviations:
 *   [S22 pN]   Lumen, SIGGRAPH 2022 Advances deck (Wright, Narkowicz, Kelly), page N
 *   [S21 sN]   Lumen radiance caching, SIGGRAPH 2021 Advances deck (Wright), slide N
 *   [CVar]     UE 5.4.4 engine default, verified against the cvar dump
 *   [DDGI19/21] Majercik et al., JCGT 2019 / "Scaling Probe-Based GI", JCGT 2021
 *   [RTXGI]    NVIDIA RTXGI-DDGI SDK source defaults
 *   [SDFGI]    Godot 4 SDFGI source (gi.h / sdfgi_integrate.glsl)
 *   [GI1.0]    AMD GI-1.0 technical report, 2022
 * Full quotations: the reports under tasks/research/.
 */

// clang-format off
#define GI_CONSTANTS_TABLE(X)                                                                      \
    /* --- tracing (plan 3.1) --- */                                                               \
    X(GI_TRACE_MAX_STEPS, 64,                                                                      \
      "steps", "published: [S22 p36] mesh SDF march cap; exhaustion REPORTS A HIT (over-occlude,"  \
      " never launder a give-up into lit)")                                                        \
    X(GI_MESH_SDF_TRACE_RANGE, 2.0f,                                                               \
      "m", "published: [S22 p44] per-instance detail tracing capped at 2 m, global SDF beyond")    \
    X(GI_EXPAND_MAX_VOXEL_DIAGONALS, 0.5f,                                                         \
      "voxel diagonals", "published: [S22 p48] runtime thin-surface expand cap = half the voxel"   \
      " diagonal; grows linearly from zero at the ray origin so contact shadows survive")          \
    X(GI_EXPAND_RAMP_VOXEL_DIAGONALS, 4.0f,                                                        \
      "voxel diagonals of travel", "derived: the ramp exists only to protect the contact zone"     \
      " where the launch lift (about one voxel) is the leak defence; full expand engages beyond"   \
      " it, and four diagonals gives that zone margin without deferring thin-geometry protection"  \
      " past contact range. Lumen publishes the ramp's existence but not its slope [S22 p50]")     \
    /* --- light voxels (plan 3.2) --- */                                                          \
    X(GI_SURFACE_VOXEL_BAND, 1.0f,                                                                 \
      "voxels of the cascade", "derived: a voxel represents surface while the isosurface lies"     \
      " within its trilinear support, which is one voxel")                                         \
    X(GI_LIGHT_VOXEL_UPDATE_DENOM, 4,                                                              \
      "frames", "published: [SDFGI] frames_to_update_light default - dynamic light re-injection"   \
      " amortised over 4 frames")                                                                  \
    X(GI_LIGHT_VOXEL_MIN_ALPHA, 0.25f,                                                             \
      "blend weight", "derived: 1 / GI_LIGHT_VOXEL_UPDATE_DENOM, so a change is fully absorbed"    \
      " within one rotation of the update list")                                                   \
    X(GI_LIGHT_VOXEL_EXPOSURE_MIN, 0.25f,                                                          \
      "field rise per attribute voxel", "derived: a face is exposed when the field RISES along"    \
      " it; 1-Lipschitz bounds the rise over one voxel at 1.0 and plateaus/parallel surfaces"      \
      " show ~0, so a quarter voxel separates the two with margin for trilinear smoothing and"     \
      " R8 quantisation")                                                                          \
    X(GI_MAX_ALBEDO, 0.9f,                                                                         \
      "unitless", "derived: bounce feedback has per-channel gain exactly equal to albedo; 1.0 is"  \
      " the neutral-stability point of L = a*L + c, so the gain is held strictly below it")        \
    /* --- world probes (plan 3.3) --- */                                                          \
    X(GI_WORLD_PROBE_DIVISOR, 16,                                                                  \
      "SDF voxels per probe cell", "published: [SDFGI] PROBE_DIVISOR - 9^3 probe lattice per"      \
      " cascade at resolution 128")                                                                \
    X(GI_WORLD_PROBE_RAYS_PER_FRAME, 16,                                                           \
      "rays per probe per frame", "derived: 16x16 octahedral = 256 directions, refreshed one"      \
      " stratum of 16 per frame over the GI_WORLD_PROBE_WINDOW - every direction sampled exactly"  \
      " once per window, so the atlas is a windowed mean with ZERO steady-state variance (the"     \
      " [SDFGI] ring-buffer integrator's stability property, materialised in the atlas itself)")   \
    X(GI_WORLD_PROBE_OCT_RADIANCE, 16,                                                             \
      "texels per edge", "derived: between [S22 p161] 32x32 world probes and [DDGI19] 8x8;"        \
      " 16x16 = 256 directions keeps 4 cascades of history affordable (plan section 6)")           \
    X(GI_WORLD_PROBE_OCT_IRRADIANCE, 8,                                                            \
      "texels per edge", "published: [DDGI19] 8x8 irradiance octahedral")                          \
    X(GI_WORLD_PROBE_OCT_DEPTH, 8,                                                                 \
      "texels per edge", "derived: [DDGI19] ships 16x16 raw depth; ours is DERIVED per frame from" \
      " the 16x16 radiance atlas's hitT with the cos^GI_WORLD_PROBE_DEPTH_SHARPNESS lobe, so the"  \
      " lobe filtering DDGI gets from blending happens in the convolution and 8x8 stores the"      \
      " already-filtered moments, sharing the irradiance tile layout")                             \
    X(GI_WORLD_PROBE_DEPTH_SHARPNESS, 50.0f,                                                       \
      "cosine exponent", "published: [RTXGI] probeDistanceExponent = 50 depth-lobe sharpening")    \
    X(GI_PROBE_TRACE_SURFACE_BIAS, 0.5f,                                                           \
      "voxels of the answering level", "derived: half-voxel hit acceptance - the midpoint of the"  \
      " voxel the field cannot resolve below; the tracing default for settings-less consumers"     \
      " (world probes, debug), matching the shadow-ray default")                                   \
    X(GI_PROBE_TRACE_RELAXATION, 0.05f,                                                            \
      "acceptance growth per unit t", "derived: the cone that bounds grazing-ray cost; carried"    \
      " from the measured resolve-pass default (audit: bounds the near-parallel case that"         \
      " otherwise burns the whole step budget), capped at one voxel inside the trace")             \
    X(GI_WORLD_PROBE_DEPTH_CLAMP, 1.5f,                                                            \
      "probe spacings", "published: [RTXGI] probeMaxRayDistance = 1.5 * spacing during distance"   \
      " blending - Chebyshev only ever asks about the cage around the query, so recording depth"   \
      " beyond it just inflates variance")                                                         \
    X(GI_WORLD_PROBE_WINDOW, 16,                                                                   \
      "frames", "derived: GI_WORLD_PROBE_OCT_RADIANCE^2 / GI_WORLD_PROBE_RAYS_PER_FRAME - one"     \
      " full refresh of every direction per window; also the far-field reaction latency (R4:"      \
      " within the 30-frame far-field target)")                                                    \
    X(GI_CHEBYSHEV_WEIGHT_FLOOR, 0.05f,                                                            \
      "weight", "published: [RTXGI] visibility weight floor - never fully zero, a fallback is"     \
      " needed when no probe has visibility")                                                      \
    X(GI_PERCEPTION_CRUSH_THRESHOLD, 0.2f,                                                         \
      "weight", "published: [DDGI19] w *= w^2/threshold^2 below this - suppresses dim leaks the"   \
      " eye's log response would otherwise amplify")                                               \
    X(GI_SELF_SHADOW_BIAS_NORMAL, 0.2f,                                                            \
      "unitless", "published: [DDGI21 Eq.2] normal component of the self-shadow bias direction")   \
    X(GI_SELF_SHADOW_BIAS_VIEW, 0.8f,                                                              \
      "unitless", "published: [DDGI21 Eq.2] view (camera) component of the bias direction")        \
    X(GI_SELF_SHADOW_BIAS_SCALE, 0.75f,                                                            \
      "of min probe spacing", "published: [DDGI21 Eq.2] bias magnitude factor")                    \
    X(GI_SELF_SHADOW_BIAS_K, 0.3f,                                                                 \
      "unitless", "published: [DDGI21 Eq.2] user scalar default")                                  \
    /* --- screen probe gather (plan 3.4) --- */                                                   \
    X(GI_SCREEN_PROBE_SPACING, 16,                                                                 \
      "trace-resolution pixels", "published: [S21 s34][CVar] ScreenProbeGather.DownsampleFactor")  \
    X(GI_SCREEN_PROBE_OCT, 8,                                                                      \
      "texels per edge", "published: [S21 s33][CVar] TracingOctahedronResolution = 8 (64 rays)")   \
    X(GI_ADAPTIVE_PROBE_FRACTION, 0.5f,                                                            \
      "of uniform probe count", "published: [CVar] AdaptiveProbeAllocationFraction = 0.5")         \
    X(GI_MAX_RAY_RADIANCE, 40.0f,                                                                  \
      "pre-exposed radiance", "published: [CVar] ScreenProbeGather.MaxRayIntensity = 40 firefly"   \
      " clamp at trace time")                                                                      \
    X(GI_FILTER_ANGLE_LIMIT_COS, 0.99802673f,                                                      \
      "cos(pi/50)", "published: [GI1.0 s2.1] probe-space filter rejects a neighbour hit whose"     \
      " reprojected direction deviates by more than pi/50")                                        \
    /* --- temporal (plan 3.5) --- */                                                              \
    X(GI_V2_INTERPOLATION_JITTER_TILES, 1.0f,                                                      \
      "probe tiles", "published: [CVar] ScreenProbeGather.FullResolutionJitterWidth = 1 - the"     \
      " integration offset jitters within one tile, spatially distributing probe differences so"   \
      " the temporal chain integrates them [S21 s39]; plane weights gate the jittered taps and"    \
      " an all-rejected bracket falls back to the unjittered one")                                 \
    X(GI_V2_IMPORTANCE_SUPERSAMPLE_RATIO, 2.0f,                                                    \
      "x mean texel importance", "derived: a cone holding a concentrated emitter reads brighter"   \
      " than the probe mean; doubling its samples is the smallest step that resolves a bulb"       \
      " smaller than the cone (the failure mode: one centre ray either skewers it or misses it"    \
      " entirely), and gating at twice the mean keeps the extra budget bounded by the bright"      \
      " fraction of the sphere")                                                                   \
    X(GI_TEMPORAL_MAX_FRAMES, 10,                                                                  \
      "frames", "published: [CVar] Temporal.MaxFramesAccumulated = 10; depth rejection only, no"   \
      " neighbourhood clamp [S21 s98]")                                                            \
    X(GI_TEMPORAL_DEPTH_TOLERANCE, 0.005f,                                                         \
      "relative depth", "published: [CVar] Temporal.DistanceThreshold = 0.005")                    \
    X(GI_FAST_UPDATE_MOVING_FRACTION, 0.1f,                                                        \
      "of interpolated lighting", "published: [S21 s99][CVar] FractionOfLightingMovingForFast"     \
      "UpdateMode = 0.1")
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
