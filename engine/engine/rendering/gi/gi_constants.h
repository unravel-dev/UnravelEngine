#pragma once

/*
 * Single owner of every cross-pass GI constant (plan: tasks/gi_rewrite_plan.md, section 4).
 *
 * Every entry carries its UNIT and its JUSTIFICATION - one of:
 *   - derived:   follows arithmetically from another value here or from a documented argument;
 *   - published: taken from a shipped system's published value (source named);
 *   - setting:   deliberately exposed on gi_component instead of living here.
 * A constant that fits none of those is a defect by the plan's R9.
 *
 * The shader mirror is engine_data/data/shaders/gi/gi_constants.sh. shaderc cannot consume this
 * header, so the mirror is plain #defines - and the pair is kept honest by a TEST, not a comment:
 * gi_tests parses the .sh for `#define GI_*` and asserts every table entry matches and no
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
    X(GI_LIGHT_VOXEL_VISIBILITY_MIN, 0.25f,                                                        \
      "of the face's cavity cone", "derived: a face is measurable when at least a quarter of"      \
      " its sub-probe-spacing cone escapes (GiBounceCavityVisibility, the same value the"          \
      " ambient is weighted by). Replaced a single-step field-rise gate, which cannot see past"    \
      " a coarse level's blob plateau - small geometry merged into blobs read unexposed on"        \
      " every face and went black wherever only coarse levels covered them; a quarter still"       \
      " separates a blob-buried outward shell (partial escape at 2-4 voxels) from a genuine"       \
      " interior face (closed at every scale)")                                                    \
    X(GI_MAX_ALBEDO, 0.9f,                                                                         \
      "unitless", "derived: bounce feedback has per-channel gain exactly equal to albedo; 1.0 is"  \
      " the neutral-stability point of L = a*L + c, so the gain is held strictly below it")        \
    /* --- world probes (plan 3.3) --- */                                                          \
    X(GI_SHADOW_DISTANCE, 100.0f,                                                                  \
      "m", "derived: beyond the outermost cascade's reach a shadow ray has nothing to test"        \
      " against; 100 m covers the 64 m cascade with margin for lights outside it")                 \
    X(GI_SHADOW_NORMAL_BIAS_VOXELS, 1.0f,                                                          \
      "voxels of the answering level", "published-from-v1-measurement: one voxel clears the"       \
      " level's own hit acceptance without lifting the point past nearby occluders")               \
    X(GI_SHADOW_SURFACE_BIAS, 0.35f,                                                               \
      "voxels of the answering field", "published-from-v1-measurement: the acceptance that"        \
      " removed shadow acne once the relaxation stopped grazing exhaustion (audit A1c)")           \
    X(GI_SHADOW_RELAXATION, 0.0f,                                                                  \
      "acceptance growth per unit t", "derived: zero - a shadow ray accepts CONTACT only. The"     \
      " cone acceptance turns a near-miss within the answering level's voxel (metres at coarse"    \
      " levels) into full occlusion, and a sun ray threading real openings then resolves dark:"    \
      " measured on Sponza, the arcade's light voxels converged black corridor-wide because"       \
      " every ray past t = 10 voxels needed a FULL voxel of clearance through the colonnade and"   \
      " over the far roofline. The grazing-cost role the cone served (audit A1c, when exhaustion"  \
      " still read as LIT) is owned by the exhaustion contract now: budget death answers with"     \
      " clearance / receiver voxel, which reads a graze honestly as penumbra, not washout")        \
    X(GI_SHADOW_RAY_START_VOXELS, 1.0f,                                                            \
      "voxels of the answering level", "published-from-v1-measurement: skip along the ray's own"   \
      " direction rather than lifting the point (see the gather's identical rule). NEVER scale"    \
      " this by incidence: a slope-aware skip teleports through sun-facing walls at contact"       \
      " range (measured, test_shadow_blob_floor_building)")                                        \
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
    X(GI_SELF_SHADOW_BIAS_MAX_VOXELS, 2.0f,                                                        \
      "SDF voxels of the level", "derived: the bias exists to climb out of the query surface's"    \
      " own field shadow, a VOXEL-scale feature - but DDGI's spacing-proportional magnitude"       \
      " (0.225 x spacing = 0.45 m at the 2 m lattice) tunnels through any wall thinner than"       \
      " it: the biased point lands outside, Chebyshev sees the exterior probe unoccluded, and"     \
      " a sunlit exterior floods a closed room (measured, thick-walled test room - the"            \
      " documented DDGI thin-wall failure). Two voxels clears the trace acceptance with margin"    \
      " and stays below any wall the field itself resolves")                                       \
    /* --- screen probe gather (plan 3.4) --- */                                                   \
    X(GI_SCREEN_PROBE_SPACING, 16,                                                                 \
      "trace-resolution pixels", "published: [S21 s34][CVar] ScreenProbeGather.DownsampleFactor;"  \
      " the gi_resolve_pass::settings::probe_spacing default")                                     \
    X(GI_ADAPTIVE_PLANE_TOLERANCE, 0.05f,                                                          \
      "fraction of view distance", "derived: the adaptive gather may substitute a probe's tile"    \
      " with its even-lattice parents' blend only where the integrate pass would have blended"     \
      " those parents at full weight anyway - so the classification reuses the SAME"               \
      " spatial-error rule the integrate bracket and the probe-space filter apply (their local"    \
      " 0.05 plane tolerances). Applied to the COPLANARITY of the parent anchors themselves"       \
      " (cell plane from three corners; the fourth corner and the probe's own anchor must sit"     \
      " within tolerance of it; collinearity in the single-axis case) - never to any normal:"      \
      " G-buffer normals carry normal maps, a pixel-scale depth derivative measures the cobble"    \
      " rather than the street, and both rejected nearly every flat Bistro surface (measured,"     \
      " twice). The parent positions ARE the surface sampled at exactly the scale being"           \
      " interpolated across; an anchor further off their plane than integration tolerates is"      \
      " genuine geometric detail and keeps its traced probe")                                      \
    X(GI_ADAPTIVE_RADIANCE_TOLERANCE, 0.35f,                                                       \
      "fraction of the parents' mean luminance", "derived: geometric sameness is necessary but"    \
      " NOT sufficient - a shadow edge, a lamp falloff, an occlusion gradient live on perfectly"   \
      " flat walls, and substituting the parents' average there washes radiance structure out"     \
      " of the probe field (measured on Bistro: visible smoothing of wall shading). The parents'"  \
      " 4x4 importance mips - filtered probe-space luminance the records already carry - must"     \
      " agree per directional block before the blend may stand in for a measurement. Filtered"     \
      " radiance between neighbouring probes on uniformly lit surfaces varies well under a"        \
      " quarter of the mean (the 3x3 probe filter guarantees smoothness); lighting structure at"   \
      " cell scale moves whole blocks by the mean or more. A third of the mean separates the"      \
      " regimes with the margin on the quality side")                                              \
    X(GI_ADAPTIVE_REVALIDATE_FRAMES, 8,                                                            \
      "frames", "derived: an interpolated probe's own history mip is DERIVED from its parents,"    \
      " so no history test can see structure the first substitution erased - the evidence loop"    \
      " is broken by MEASUREMENT: every skipped probe re-traces on a phase-hashed cadence of"      \
      " this many frames. One third of GI_TEMPORAL_MAX_FRAMES bounds how long sub-cell lighting"   \
      " structure can stay hidden to less than the accumulation window absorbs, for an eighth"     \
      " of the skipped rays; the phase hash keeps neighbouring probes from revalidating in the"    \
      " same frame, so the cost is spread, never pulsed")                                          \
    X(GI_MAX_RAY_RADIANCE, 40.0f,                                                                  \
      "pre-exposed radiance", "published: [CVar] ScreenProbeGather.MaxRayIntensity = 40 firefly"   \
      " clamp at trace time")                                                                      \
    /* --- screen-trace-first (Lumen: HZB traces resolve the near field at pixel precision      \
       [S21 s66-68]; the SDF answers only where the screen cannot) --- */                          \
    X(GI_SCREEN_TRACE_MAX_STEPS, 64,                                                               \
      "Hi-Z iterations", "published-from-SSIL-measurement: the iteration budget the screen"        \
      " stack ships with (ssil_pass max_steps default); the hierarchical march resolves or"        \
      " leaves the screen well inside it at gather ray lengths")                                   \
    X(GI_SCREEN_TRACE_MIN_MIP, 1,                                                                  \
      "Hi-Z mip", "derived: the gather's screen tier serves cone-amortized probe rays (64 per"     \
      " 16-pixel tile), so mip-0 sub-pixel precision is below the cone's own footprint; walking"   \
      " the pyramid no finer than mip 1 halves per-ray traversal in the dominant pass. Hit"        \
      " validation still reads mip-0 depth, and a low-confidence coarse hit falls through to"      \
      " the watertight SDF answer - the failure direction is the pre-screen-tier image, which"     \
      " was measured visually near-identical. SSR/SSIL keep mip 0: they present pixel-exact"       \
      " images, the gather presents filtered irradiance")                                          \
    X(GI_SCREEN_TRACE_DEPTH_TOLERANCE, 0.15f,                                                      \
      "view-space m at zero distance", "published-from-SSIL-measurement: hit acceptance band"      \
      " against the mip-0 depth (ssil_pass depth_tolerance default)")                              \
    X(GI_SCREEN_TRACE_THICKNESS, 0.5f,                                                             \
      "view-space m at full ray range", "published-from-SSIL-measurement: distance-scaled"         \
      " widening of the acceptance band (ssil_pass thickness default) - far hits validate"         \
      " against coarser reconstruction and a fixed band over-rejects them")                        \
    X(GI_SCREEN_TRACE_CONFIDENCE_MIN, 0.5f,                                                        \
      "confidence", "derived: commit-or-fall-through, never blend - below half confidence the"     \
      " watertight SDF answer replaces the screen answer outright, because blending two"           \
      " radiance estimates of the same ray double-counts whichever is wrong")                      \
    /* --- bounce cavity occlusion (the [DFAO] role: sub-probe-spacing visibility for              \
       the ambient the world probes inject) --- */                                                 \
    X(GI_BOUNCE_AO_STEPS, 3,                                                                       \
      "field samples along the face", "derived: doubling distances from one attribute"             \
      " voxel reach 1 + 2 + 4 = 7 voxels, about the world-probe spacing (16 SDF = 8"               \
      " attribute voxels) - EXACTLY the band the probes cannot see: below it the"                  \
      " voxel's own surface dominates the field, above it the probes' Chebyshev"                   \
      " visibility already measures occlusion. Without this term a voxel inside a"                 \
      " sub-spacing cavity (an awning's underside, a window reveal) receives the OPEN"             \
      " ambient of the probe cage around it and glows in exactly the places that"                  \
      " should be darkest")                                                                        \
    X(GI_FILTER_ANGLE_LIMIT_COS, 0.99802673f,                                                      \
      "cos(pi/50)", "published: [GI1.0 s2.1] probe-space filter rejects a neighbour"               \
      " hit whose reprojected direction deviates by more than pi/50")                              \
    /* --- reflections (plan phase 9) --- */                                                       \
    X(GI_REFLECTION_ROUGH_CUTOFF, 0.4f,                                                            \
      "GGX roughness", "derived: the world-probe radiance atlas texel (16x16 octahedral,"          \
      " about a 13-degree half-angle) subtends the same solid angle as a GGX lobe of"              \
      " roughness ~0.4. A rougher lobe is already prefiltered by the atlas - those pixels"         \
      " read the probe cage along the reflection and pay no ray; sharper lobes trace")             \
    X(GI_REFLECTION_TEMPORAL_FRAMES, 8,                                                            \
      "frames", "derived: the stochastic GGX reflection ray (VNDF, R2 sequence per frame)"         \
      " integrates its lobe over this many frames of history. One 8-frame R2 cycle matches"        \
      " the gather's anchor cycle (GI_TEMPORAL_MAX_FRAMES is three of them) - reflections"        \
      " must track moving content faster than irradiance, so one cycle, ~130 ms at 60 Hz."         \
      " The temporal pass clamps history to the 3x3 neighbourhood of the current frame's"         \
      " samples, so stale content cannot outlive a frame regardless of this length")               \
    X(GI_REFLECTION_MESH_SDF_RANGE, 16.0f,                                                         \
      "meters", "derived: the mesh-SDF detail tier for REFLECTION rays runs to the finest"         \
      " cascade's full extent (128 voxels x 0.125 m = 16 m). The gather caps its detail tier"     \
      " at 2 m (GI_MESH_SDF_TRACE_RANGE) because 64 cones amortize the cost and irradiance"        \
      " hides silhouette error - a reflection is ONE ray per pixel presenting an IMAGE, where"     \
      " clipmap-tier inflation is directly visible at any distance (measured: fattened box"        \
      " reflections + silhouette halos on a mirror floor, round 5). Past level 0's extent any"    \
      " lobe wide enough to carry confidence has a cone footprint spanning multiple voxels,"       \
      " so the clipmap legitimately answers. The instance grid walk is the cost knob: only"        \
      " sharp-tier pixels whose screen trace missed pay it")                                       \
    X(GI_REFLECTION_GATHER_FADE_START, 0.3f,                                                       \
      "GGX roughness", "derived: 0.75 x GI_REFLECTION_ROUGH_CUTOFF. The traced tiers now"          \
      " SPREAD with roughness (screen hits average a GGX-cone disk, world hits blend toward"       \
      " the 13-degree prefiltered probe radiance by the lobe/texel angle ratio), so the fade"      \
      " toward the gather-based rough value has one job left: C0 continuity into the rough"        \
      " tier at the cutoff. It starts where the lobe/texel blend has already replaced the"         \
      " majority of the sharp trace ((0.75)^2 = 56 percent), reconciling only the residual -"      \
      " a wide fade from the mirror end read as content dissolving instead of blurring"            \
      " (measured, round 3)")                                                                      \
    /* --- temporal (plan 3.5) --- */                                                              \
    X(GI_INTERPOLATION_JITTER_TILES, 1.0f,                                                      \
      "probe tiles", "published: [CVar] ScreenProbeGather.FullResolutionJitterWidth = 1 - the"     \
      " integration offset jitters within one tile, spatially distributing probe differences so"   \
      " the temporal chain integrates them [S21 s39]; plane weights gate the jittered taps and"    \
      " an all-rejected bracket falls back to the unjittered one")                                 \
    X(GI_IMPORTANCE_SUPERSAMPLE_RATIO, 2.0f,                                                    \
      "x mean texel importance", "derived: a cone holding a concentrated emitter reads brighter"   \
      " than the probe mean; doubling its samples is the smallest step that resolves a bulb"       \
      " smaller than the cone (the failure mode: one centre ray either skewers it or misses it"    \
      " entirely), and gating at twice the mean keeps the extra budget bounded by the bright"      \
      " fraction of the sphere")                                                                   \
    X(GI_TEMPORAL_MAX_FRAMES, 24,                                                                  \
      "frames", "measured: with per-frame cone-direction jitter the window must integrate"         \
      " enough of each cone's R2 sequence that residual sample motion falls below visibility -"    \
      " Lumen's 10 (their budget has far more effective rays) still crawled, 25 measured"          \
      " stable; 24 keeps the window commensurate with the 8-frame anchor-placement cycle"          \
      " (three full cycles). Depth rejection only, no neighbourhood clamp [S21 s98]; the"          \
      " gi_resolve_pass::settings::max_accum_frames default")                                      \
    X(GI_TEMPORAL_DEPTH_TOLERANCE, 0.25f,                                                          \
      "relative depth per unit view distance", "measured: Lumen's Temporal.DistanceThreshold"      \
      " = 0.005 assumes motion-vector reprojection; ours reconstructs the previous position"       \
      " from the depth buffer alone, whose error at edges and grazing angles rejects history"      \
      " constantly at 0.005 (visible jitter, Bistro). 0.25 measured stable with acceptable"        \
      " ghosting; the gi_resolve_pass::settings::reprojection_tolerance default")
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
