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
 * the GI test suite parses the .sh for `#define GI_*` and asserts every table entry matches and no
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
      " (screen gather, reflections, debug), matching the shadow-ray default")                     \
    X(GI_WORLD_PROBE_TRACE_BIAS, 1.0f,                                                             \
      "voxels of the answering level", "derived: FULL-voxel acceptance for world-probe rays"       \
      " alone, the acceptance cap - they are the one consumer tracing the cascade with no mesh"    \
      " tier (near_field 0) while launching inside rooms, and a sub-voxel wall's through-field"    \
      " minimum reaches ~0.87 voxel on diagonal crossings while the far-field expand is still"     \
      " inside its ramp: at the half-voxel default probe rays threaded sealed geometry along the"  \
      " level cross-fade shell (the sealed-box leak: camera-locked porosity fans in the Probe"     \
      " Sky debug view, escaped rays ingesting env SH / sunlit exterior). One voxel covers the"    \
      " worst case; probe reads over-occlude by at most one voxel - the graceful direction")       \
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
    X(GI_CHEBYSHEV_WEIGHT_FLOOR, 0.005f,                                                           \
      "weight", "published [RTXGI] ships 0.05; lowered 10x measured: the floor is the"             \
      " through-wall bleed knob (floor x crush of an exterior sunlit probe reaches every"          \
      " interior query regardless of depth moments) - 0.05 leaked ~0.1-0.5% of sun into sealed"    \
      " rooms, amplified ~10x by the closed-room bounce under auto exposure; 0.005 cuts it"        \
      " below perception while the weight_sum fallback still catches fully-dead cages")            \
    X(GI_WORLD_PROBE_CAGE_VIS_STEPS, 40,                                                           \
      "field samples", "derived: ceil(sqrt(3) x GI_WORLD_PROBE_DIVISOR / 0.7) - the march's"       \
      " uniform fallback step, (segment - guards) / steps, is then at most 0.7 cage voxels on"     \
      " the longest cage diagonal, inside the ~0.8-voxel sub-conviction core of the thinnest"      \
      " wall a level can seal (one voxel: interior reaches -0.5, conviction at -0.1 leaves 0.8),"  \
      " so the walk cannot step across a sealing wall it is responsible for; the sphere-trace"     \
      " acceleration (step = max(field, base)) keeps the typical cost at a few samples and the"    \
      " mid-segment clearance proof settles open cages with one")                                  \
    X(GI_WORLD_PROBE_CAGE_VIS_ACCEPT_VOXELS, -0.1f,                                                \
      "SDF voxels of the cage's level", "derived: conviction depth - NEGATIVE, so a probe is"      \
      " rejected only where the segment passes INSIDE geometry, with a tenth of a voxel of"        \
      " noise margin. Any positive acceptance convicts on PROXIMITY, and legitimate cage"          \
      " segments run parallel to the query's own surface at grazing height by construction:"      \
      " on a flat floor four of the eight cage probes lie in the floor plane and the biased"       \
      " query clears it by only ~0.4 voxel (the view-dominant bias's 0.2 normal share), so a"      \
      " half-voxel acceptance blocked whole flat-ground cages and the all-blocked contract"       \
      " painted black rings/donuts on open ground (measured, 2026-08-12 screenshots). Walls"       \
      " that actually seal have negative cores in the finest field covering the sample, which"     \
      " the march reads; sub-voxel-porous walls at coarse-only coverage stay the documented"       \
      " residual either way")                                                                      \
    X(GI_WORLD_PROBE_CAGE_VIS_GUARD_VOXELS, 1.0f,                                                  \
      "SDF voxels of the cage's level", "derived: endpoint slabs the march does not test - the"    \
      " query end sits at the self-shadow-biased point, whose own surface legitimately reads"      \
      " near zero (the bias clears at most GI_SELF_SHADOW_BIAS_MAX_VOXELS = 2), and a probe"       \
      " hugging the OPEN side of a wall must stay readable from the room it serves; one voxel"     \
      " excludes both contact zones while any wall that actually separates the pair still"         \
      " crosses the tested middle")                                                                \
    X(GI_WORLD_PROBE_CAGE_VIS_VARIANCE_GATE, 0.15f,                                                \
      "probe spacings (std of the depth lobe)", "derived: the march runs ONLY where the depth"     \
      " moments are statistically ambiguous - std above this fraction of the probe spacing."      \
      " The measured leak channel was the SILHOUETTE WEDGE: an 8x8 oct texel mixing wall-at-w"     \
      " with beyond-the-1.5-spacing depth clamp has std >= ~0.2 spacings for any mixture that"     \
      " carries visible energy (p(1-p)(span)^2 with span >= 0.5 spacing, p >= 0.05), while a"      \
      " flat wall or an open lobe under the cos^50 depth lobe measures well under a tenth."       \
      " Low-variance moments are trusted BOTH ways: confidently-visible probes skip the march"     \
      " at full weight, confidently-blocked ones (chebyshev below the floor) are ZEROED"           \
      " rather than floored - the floor plus renormalisation otherwise launders an"                \
      " all-blocked cage's texels back to full amplitude (the radiance reader has no crush)."      \
      " Ungated, the march tripled the light-voxel pass: interiors defeat the mid-segment"         \
      " clearance proof, so every relit face paid 8 walks (measured 0.5 -> 2.0 ms)")               \
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
    X(GI_SCREEN_PROBE_RAYS_PER_FRAME, 16,                                                          \
      "rays per traced probe per frame", "derived: the 8x8 atlas keeps 64 world-anchored"          \
      " directions; each frame traces a 2x2 Bayer stratum of 16 and blends them into that"         \
      " probe's own previous tile (the world-probe windowed-mean recipe). Lumen's 16-ray"          \
      " path is a 4x4 octahedron, not a hole-filled 8x8")                                          \
    X(GI_SCREEN_PROBE_WINDOW, 4,                                                                   \
      "frames", "derived: 64 octahedral texels / GI_SCREEN_PROBE_RAYS_PER_FRAME - every"           \
      " direction is measured exactly once per window. 4 frames sits inside"                       \
      " GI_TEMPORAL_MAX_FRAMES so the pixel filter still sees several complete spheres")           \
    X(GI_SCREEN_PROBE_HISTORY_TILE, 0.25f,                                                         \
      "of the probe tile in world units", "derived: the origin-sameness band for the sticky"       \
      " reconstruct - within it the anchor is the same visibility field and the running-mean"      \
      " count carries. Outside it an UNSCHEDULED origin change (a camera slide) must not"          \
      " collage new cones onto another origin's tile at full trust: the trace inherits the"        \
      " world-reprojected previous probe's tile and count (plane-vetted) or resets. SCHEDULED"     \
      " walks are exempt via ANCHOR.w - they keep the count and fade the new origin in at 1/n."    \
      " The tile is still COPIED on a miss - writing black is what darkened pans")                 \
    X(GI_SCREEN_PROBE_WALK_WINDOWS, 3,                                                             \
      "complete spheres", "derived: stay sticky so the 16/64 stratum fills complete spheres"      \
      " (per-frame Halton is the shimmer). Then the whole lattice Halton-walks together (OFF's"   \
      " shared offset - staggering froze neighbours on different points and printed blotches)"    \
      " and keeps the 1/n count via ANCHOR.w so the walk is a fade, never a reset. Halton is"     \
      " indexed by walk count so the 8-cycle is fully used. No extra rays. Raised 1 -> 2 when"    \
      " the parallax-adaptive probe filter took over blotch dissolution in the near band, then"   \
      " 2 -> 3 (2026-08-25): even as a fade, each walk starts a screen-coherent origin"           \
      " migration - every probe's tile begins blending toward a new anchor on the SAME frame,"    \
      " and the eye picks that synchronized micro-pulse out of far smaller amplitudes than"       \
      " spatial noise (the user-reported probe-space shimmer). Three is the CEILING the"          \
      " tested invariant permits: two walk periods must fit the classic temporal window"          \
      " (2 x 3 x 4 = 24 = GI_TEMPORAL_MAX_FRAMES) so a walked blotch always has a second,"        \
      " differently-anchored epoch inside the pixel mean to fade against")                        \
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
      " hit whose reprojected direction deviates by more than pi/50 - the FLOOR of the"            \
      " parallax-adaptive limit below")                                                            \
    X(GI_FILTER_PARALLAX_SCALE, 1.5f,                                                              \
      "x the intrinsic parallax angle", "derived: a neighbour probe's hit along the SAME"          \
      " octahedral direction reprojects with an error of about baseline/hitT even when both"       \
      " probes see one flat co-planar surface - the offset is geometric, not a visibility"         \
      " disagreement. Against the fixed pi/50 limit that rejected ALL sharing for hits closer"     \
      " than ~16 baselines (~2-5 m at typical pitches) - precisely the band where gather rays"     \
      " read light voxels, so per-probe voxel-sampling bias stood unfiltered as wall blotches."    \
      " The accepted error now scales to this multiple of the intrinsic parallax (1.5 covers"      \
      " the obliquity spread of co-planar hits; different-visibility hits reproject far"           \
      " outside it), with pi/50 as the far-field floor")                                           \
    X(GI_FILTER_ANGLE_RELAX_MAX, 0.2f,                                                             \
      "radians", "derived: cap of the parallax-adaptive limit (~11.5 deg). Below roughly two"      \
      " baselines of hit distance the parallax term would accept nearly anything; contact-scale"   \
      " visibility there belongs to the pixel-precise screen trace, but the cap keeps the"         \
      " probe-space filter from ever dissolving it outright")                                      \
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
    X(GI_REFLECTION_MESH_SDF_RANGE_SHARP, 16.0f,                                                   \
      "meters", "derived: a mirror is one image-ray, so the mesh-exact walk may run past the"      \
      " gather's 2 m contact bound - but beyond a few metres the clipmap-finder + refine path"     \
      " already snaps hits back to the mesh (GI_REFLECTION_REFINE_*) at a tenth of the long"       \
      " grid walk's cost, so the walk only needs to cover the range where refine's window can"     \
      " miss thin silhouettes: level 0's 16 m cube. Was 40 m, which duplicated refine's job on"    \
      " the dominant-cost pixels; the A/B metric is the unrefined-clipmap fraction"                \
      " (instance_index == SDF_NO_INSTANCE && !exhausted) - widen the refine window before"        \
      " raising this back")                                                                        \
    X(GI_REFLECTION_MESH_SDF_RANGE_GLOSS, 8.0f,                                                    \
      "meters", "derived: at GI_REFLECTION_GATHER_FADE_START the GGX lobe already spans"           \
      " clipmap voxels, so mesh-exact silhouettes stop mattering. 8 m is 4x the gather's"          \
      " 2 m contact bound and half the old flat 16 m - the budget that funds the sharp end")       \
    X(GI_REFLECTION_TRACE_SURFACE_BIAS, 0.25f,                                                     \
      "voxels of the answering field", "derived: half of GI_PROBE_TRACE_SURFACE_BIAS."             \
      " Lumen prefers a bit of leak over fattened silhouettes on reflections [S22 p695];"          \
      " a quarter-voxel still clears quantisation without the half-voxel halo the image"           \
      " showed past the mesh-SDF handover")                                                       \
    X(GI_REFLECTION_TRACE_RELAXATION, 0.0f,                                                        \
      "acceptance growth per unit t", "derived: zero - a reflection is an IMAGE. The gather"      \
      " cone fattens distant surfaces by construction (the 16 m blob); exhaustion already"        \
      " falls back to the gather, which is the safe miss for this pass")                           \
    X(GI_REFLECTION_REFINE_VOXELS, 2.0f,                                                           \
      "voxels of the answering clipmap level", "derived: a conservative clipmap hit sits"         \
      " within about one voxel of the mesh surface it fattened; two voxels is the window"         \
      " that still contains that surface, so a short instance-grid walk can snap the"             \
      " silhouette without re-tracing the whole ray")                                              \
    X(GI_REFLECTION_REFINE_STEPS, 16,                                                              \
      "steps", "derived: the refine window is a few metres (2 x coarsest-in-range voxel);"        \
      " 16 sphere-trace steps cover it with margin and leave the 64-step budget on the"           \
      " long clipmap finder")                                                                     \
    X(GI_REFLECTION_CLIPMAP_SHAPE_CUTOFF, 0.15f,                                                   \
      "GGX roughness", "derived: below this the lobe is tight enough that a clipmap voxel"        \
      " (25 cm at the 16 m handover) is a visible wrong silhouette. Unrefined clipmap hits"       \
      " then write zero coverage so the authored probe layer shows through. Above it the"         \
      " stochastic spread hides voxel-scale error and the clipmap may still light the pixel")     \
    X(GI_REFLECTION_CASCADE_FADE_VOXELS, 8.0f,                                                     \
      "voxels of the finer covering level", "derived: the distance field already fades over"      \
      " blend_voxels = 4, but lighting does not - GiLightVoxelRead returns the first measured"    \
      " level, so a mirror shows a knife-edge where resolution doubles and occupancy holes"      \
      " pop in as dark spots (camera-centred cascade boxes projected through the floor)."        \
      " Twice the field band is 1 m at level 0 / 2 m at level 1: wide enough to hide one"        \
      " coarse voxel of isosurface disagreement. Extra taps only inside that band")              \
    X(GI_REFLECTION_MEAN_SLOTS, 1024,                                                              \
      "texture-mean slots", "derived: equals surface_cache_system::texture_mean_capacity"          \
      " (static_assert in gi_reflection_pass.cpp). The trace kernel needs the means to"            \
      " rebuild a hit's own albedo, but every one of the trace's 16 bgfx stages is taken -"        \
      " so the args pass stages the mean buffer into the trace list (3 uints of float bits"        \
      " per slot, between the [0]/[1] header and the pixel block), riding the stage the"           \
      " list already owns")                                                                        \
    X(GI_REFLECTION_REMODULATE_ALBEDO_FLOOR, 0.02f,                                                \
      "unitless albedo", "derived: 5 quanta of the RGBA8 attribute-albedo storage (1/255"          \
      " each) - the divisor floor that keeps the remodulation ratio from amplifying"               \
      " quantisation noise of near-black voxel means into fireflies")                              \
    X(GI_REFLECTION_REMODULATE_RATIO_MAX, 4.0f,                                                    \
      "unitless gain", "derived: cap on hit-albedo / voxel-mean-albedo. Legitimate contrast"       \
      " (mid 0.5 hit beside a dark 0.125 cell mix) reaches about 4; anything above is a"           \
      " mismatched read (radiance and albedo answered by different cascade levels, occupancy"      \
      " holes) and must not multiply energy")                                                      \
    X(GI_REFLECTION_MIRROR_ROUGHNESS, 0.06f,                                                       \
      "decoded G-buffer roughness", "derived: the G-buffer encoder clamps roughness to >= 0.05"    \
      " at write (fs_deferred_geom.sc), so an AUTHORED mirror decodes at the floor - plus up to"   \
      " one UNORM8 quantum (1/255) on the LDR G-buffer format. At or below this threshold the"     \
      " VNDF branch collapses to the deterministic mirror ray. The old gate compared"              \
      " alpha = roughness^2 against 1e-4, which the floor's 2.5e-3 passes 25x over: every"         \
      " authored mirror stochastically jittered, and pixels whose exact ray near-missed a small"   \
      " emissive hit it on tail samples - the measured dancing fireflies. The recorded lesson:"    \
      " any threshold on decoded roughness must account for the encoder floor")                    \
    X(GI_REFLECTION_CLAMP_MOTION_TEXELS, 1.0f,                                                     \
      "trace-target texels of reprojection motion", "derived: below one texel the camera is"       \
      " still and reprojection is exact, so held history IS this pixel's own sample stream -"      \
      " the neighbourhood clamp may release and the running mean converge on sparse-bright"        \
      " content (a small emissive under the lobe: the clamp otherwise erases the accumulated"      \
      " p*L on every miss frame, so the estimator cannot converge BY CONSTRUCTION and every hit"   \
      " re-flashes as a dancing dot). Motion is the only per-frame discriminator between"          \
      " disocclusion ghosts (need the clamp) and sparse-bright samples (the clamp destroys"        \
      " them) without a velocity buffer; a moving emitter under a still camera can still trail"    \
      " - accepted and documented")                                                                \
    X(GI_REFLECTION_STILL_WINDOW_SCALE, 4.0f,                                                      \
      "x the temporal window", "derived: while the clamp is released (still camera) the"           \
      " running-mean count may grow to this multiple of the settings window, so a sparse spike"    \
      " enters at 1/(4W) weight and the mean's variance floor drops 4x where convergence is"       \
      " actually possible. The count cap collapses to the base window on the first moving"         \
      " frame, so responsiveness under motion is unchanged")                                       \
    X(GI_REFLECTION_FIREFLY_CLAMP, 8.0f,                                                           \
      "x the governor's reference", "derived: GI_GATHER_FIREFLY_CLAMP's role at the reflection"    \
      " temporal - one VNDF ray per pixel per frame makes a small bright emitter a sparse-spike"   \
      " process on rough surfaces (the ray cap bounds the spike at 40, still orders over the"      \
      " local mean, and an isolated spike entering a running mean at 1/count is a dancing dot"     \
      " no window hides - measured as moving red pixels around an emissive at r 0.15-0.35)."       \
      " Each new sample is capped at this multiple of its reference: the pixel's accumulated"      \
      " luminance, floored by the neighbourhood mean of the frame's geometric samples (the 3x3"    \
      " the bounds already fetch). An established bright pixel raises its own ceiling and"         \
      " converges unbiased; no reference stores unclamped (disocclusions must not ramp from"       \
      " black). Same multiple as the gather's - the two governors bound the same physics")         \
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
    X(GI_INTERPOLATION_JITTER_TILES, 0.75f,                                                     \
      "probe tiles", "published-then-tuned: [CVar] ScreenProbeGather.FullResolutionJitterWidth"    \
      " = 1 - the integration offset jitters within a tile, spatially distributing probe"          \
      " differences so the temporal chain integrates them [S21 s39]; plane weights gate the"       \
      " jittered taps and an all-rejected bracket falls back to the unjittered one. Trimmed to"    \
      " 0.75 when the parallax-adaptive probe filter started removing lattice print-through"       \
      " upstream: the jitter's job shrank, and its amplitude is shimmer the temporal must"         \
      " re-integrate on every anchor cycle")                                                       \
    X(GI_IMPORTANCE_SUPERSAMPLE_RATIO, 2.0f,                                                    \
      "x mean texel importance", "derived: a cone holding a concentrated emitter reads brighter"   \
      " than the probe mean; doubling its samples is the smallest step that resolves a bulb"       \
      " smaller than the cone (the failure mode: one centre ray either skewers it or misses it"    \
      " entirely), and gating at twice the mean keeps the extra budget bounded by the bright"      \
      " fraction of the sphere")                                                                   \
    X(GI_IMPORTANCE_SUPERSAMPLE_MAX, 4,                                                            \
      "samples per cone", "derived: the ceiling of the importance-proportional allocation"         \
      " ladder (2/4/8x the tile mean earn 2/3/4 samples - powers of"                               \
      " GI_IMPORTANCE_SUPERSAMPLE_RATIO). Four is where the sub-cone (0,2)-net's"                  \
      " stratification is still exact and where the self-budgeting bound settles: block"           \
      " importances sum to sixteen means by definition, so however the energy concentrates a"      \
      " stratum's extra samples stay near half the base ray count in the worst case - the"         \
      " same order the old binary 2x gate already paid")                                           \
    X(GI_TEMPORAL_MAX_FRAMES, 24,                                                                  \
      "frames", "measured: with per-frame cone-direction jitter the window must integrate"         \
      " enough of each cone's R2 sequence that residual sample motion falls below visibility -"    \
      " Lumen's 10 (their budget has far more effective rays) still crawled, 25 measured"          \
      " stable; 24 keeps the window commensurate with the 8-frame anchor-placement cycle"          \
      " (three full cycles). Depth rejection only, no neighbourhood clamp [S21 s98]; the"          \
      " gi_resolve_pass::settings::max_accum_frames default (now the PROBE-SPACE cap; the"         \
      " full-res temporal runs the dual-rate pair below)")                                         \
    X(GI_TEMPORAL_FAST_FRAMES, 8,                                                                  \
      "frames", "derived: one full anchor-placement cycle = two complete 4-frame screen-probe"     \
      " stratum spheres - the shortest window whose mean has seen every ray direction once."       \
      " The dual-rate temporal's fast lane, and the count a detected lighting change resets"       \
      " the slow lane to")                                                                         \
    X(GI_TEMPORAL_SLOW_FRAMES, 96,                                                                 \
      "frames", "measured: a small bright emissive source excites amortization phase waves"        \
      " with a ~16-frame period (the world-probe stratum window x the light-voxel rotation)"       \
      " that a 24-frame mean cannot average - blobs crawl; the user-validated ~100 settles"        \
      " them. 96 = six wave periods and twelve fast cycles. Costs no responsiveness: the"          \
      " change detector (GI_TEMPORAL_CHANGE_SIGMA) snaps the slow lane to the fast one on a"       \
      " real mean shift; the gi_resolve_pass::settings::temporal_slow_frames default")             \
    X(GI_CLIPMAP_EDIT_THROTTLE_FRAMES, 8,                                                          \
      "frames", "derived: a continuously edited instance (an editor drag) re-fingerprints its"     \
      " levels EVERY frame, and each recompose is a full non-toroidal distance volume plus"        \
      " attributes (~1.4 ms) AND bumps the vis-memo generation, making every light-voxel"         \
      " relight a miss (~1.3 ms) - measured 4.8 ms drag frames against 2.8 moving the camera."     \
      " Content-driven recomposes therefore coalesce to one per this many frames per level"        \
      " (the pending fingerprint diff persists, so the final state lands within one window of"     \
      " release; origin re-snaps stay immediate). Two light-voxel rotations, so half the"          \
      " relights hit the memo even mid-drag; the coarse-clipmap GI lags a dragged object by"       \
      " at most ~130 ms - within the gather's own multi-window latency")                           \
    X(GI_SCREEN_PROBE_REINVEST_BUDGET, 0.5f,                                                       \
      "of the full lattice's ray budget", "measured: reinvestment tiers first balanced against"    \
      " the FULL lattice (traced x2 <= all probes -> double strata), but the probes the"           \
      " adaptive classifier leaves traced are the EXPENSIVE ones - geometry breaks, near-field"    \
      " marches - while the skipped flat probes are what diluted the average ray cost, so"         \
      " budget-neutral in ray COUNT was +0.15 ms in ray TIME and adaptive stopped being a win."    \
      " The widened strata may now spend at most this fraction of the full budget: in dense"       \
      " scenes (Bistro street, traced ~half the lattice) nothing reinvests and adaptive keeps"     \
      " its full saving; where probes are genuinely sparse - flat dim walls, the far-emissive"     \
      " flicker case - the arrival density still doubles or quadruples at small absolute cost")    \
    X(GI_LIGHT_VOXEL_SUN_DITHER, 0.25f,                                                            \
      "attribute voxels", "derived: direct lighting is evaluated at one representative point"      \
      " per voxel per relight, so shadow edges stand in the light volume as voxel-scale"           \
      " staircases that the trilinear read softens but cannot remove - and every mirror"           \
      " reflects them. The evaluation point now dithers within the voxel by this amplitude"        \
      " per relight (a low-discrepancy walk keyed on cell and frame): the staircase becomes"       \
      " dither that the world-probe stratum window and the gather temporal integrate into"         \
      " penumbra. A quarter voxel keeps the traced tier's launch clear of the surface (the"        \
      " lift guarantees half a voxel, computed at the true centre); the cavity and tunnel"         \
      " gates stay UN-dithered - their verdicts are memoised as pure functions of the field."      \
      " 0 disables, the A/B")                                                                      \
    X(GI_LIGHT_VOXEL_EMA_BLEND, 0.125f,                                                            \
      "per-relight blend weight", "derived: 1/8 - two full 4-frame relight rotations of"           \
      " history. The volume's relight is sampled (one dithered evaluation point per voxel per"     \
      " rotation, GI_LIGHT_VOXEL_SUN_DITHER), so voxel radiance near shadow edges and 1/r^2"       \
      " falloffs is a limit cycle at the rotation period; the gather and probes are contracted"    \
      " to integrate it, but MIRRORS read the volume raw and the reflection temporal's"            \
      " neighbourhood clamp TRACKS it (measured: shimmer at pure mirror with SSR off). The"        \
      " relight-to-relight EMA makes the volume itself the integrator. The CPU snaps the blend"    \
      " to 1 on light-set or content changes (light hash, vis-memo generation - which also"        \
      " bumps on window scrolls, so a scrolled-in slot never fades in a departed cell's"           \
      " radiance) and after any debug-variant write, so real changes land in one relight")         \
    X(GI_GATHER_FIREFLY_CLAMP, 8.0f,                                                               \
      "x the governor's reference", "derived: a gather ray that lands on a small bright"           \
      " emitter returns a radiance that dominates its probe's whole tile - and a probe whose"      \
      " accumulation just reset (a Halton walk, temporal off) ingests it at full weight, so the"   \
      " probe's entire screen footprint pops red for a frame and fades (the moving-blocks"        \
      " report). Each texel's new sample is clamped to this many times its reference: the"         \
      " texel's own blended history, FLOORED by the reprojected previous tile's mean"              \
      " luminance (world-anchored, so it survives camera-slide re-anchors - a stale dark"          \
      " per-texel reference alone crushed legitimate energy in emissive-lit dark scenes and"       \
      " pumped as it re-ramped on every slide). Never ONLY the tile mean - that crushes a"         \
      " lone bright texel to mean x k / 256; the max() keeps an established bright texel"          \
      " raising its own ceiling and converging unbiased. No reference at all (fresh tile,"         \
      " failed reprojection): the first measurement stores unclamped - progressive ramps"          \
      " from black would dim every disocclusion instead")                                          \
    X(GI_TEMPORAL_CHANGE_SIGMA, 3.0f,                                                              \
      "standard deviations", "derived: the fast and slow lanes are means of the same sample"       \
      " stream, so their gap's variance is the single-sample variance x (1/n_fast +"               \
      " 1/n_slow); a luminance gap beyond three such sigmas is a mean SHIFT (lighting"             \
      " changed), not noise - the slow lane snaps to the fast one and re-accumulates."             \
      " Three sigma = ~0.3% false-snap rate, and a false snap only costs one count reset")         \
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
