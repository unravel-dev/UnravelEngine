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
    X(GI_LIGHT_VOXEL_CULLED_ALPHA, 0.00390625f,                                                    \
      "unitless", "derived: 1/256, the provenance alpha a CULLED voxel face stores. Alpha 0 is"    \
      " the never-measured mark that lets every light-voxel reader fall back to a coarser"         \
      " level, and a culled face is not unmeasured - its cavity cone is closed at this level,"     \
      " so the correct answer is DARK. Falling back handed exactly the crevice and thin-slab"      \
      " faces to a coarser level whose cell straddles the geometry the cull reacted to and"        \
      " whose shadow ray launches from its sunlit side (measured: the GI Room's door tunnel"       \
      " floor lit through the 25 cm baffle from level 2). One quantum of the RGBA16F"              \
      " alpha's useful range: carries no energy, weighs nothing against a measured neighbour"      \
      " in the trilinear mix, and clears the readers' 1e-4 measured threshold on its own")         \
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
    X(GI_SHARED_ORIGIN_REDESCENT_VOXELS, 0.15f,                                                    \
      "level voxels", "derived: the per-voxel sun memo's shared shadow ray launches from the"      \
      " voxel centre lifted along the light by the centre's depth plus half an attribute"          \
      " voxel, and that lift crosses any occluder thinner than itself standing between the"        \
      " voxel and the sun (measured: door-tunnel floor faces lit through a 25 cm baffle,"          \
      " the shared ray starting on the baffle's sunlit side). Leaving the voxel's own"             \
      " surface the level field RISES along the lift; a drop below the running peak by more"       \
      " than this many voxels means the segment entered another surface, and the share is"         \
      " refused for that voxel (every face then launches its own ray, at the face's own"           \
      " scale). The trilinear field's interpolation ripple across a voxel stays under 0.1"         \
      " voxel, so 0.15 rejects real re-entries and never the ripple")                              \
    X(GI_SHARED_ORIGIN_SAMPLES, 4,                                                                 \
      "samples", "derived: the lift is at most the centre depth plus half an attribute voxel -"    \
      " under two level voxels for a surface cell - so four samples put one every half"            \
      " voxel, and a 25 cm slab (a full voxel at level 1) always receives one inside its dip."     \
      " Four trilinear fetches per voxel per relight, on the memo-establishing face only,"         \
      " against the ~100 m sphere trace they guard")                                               \
    X(GI_SUN_SHADOWMAP_MAX_VOXEL, 0.125f,                                                          \
      "m", "derived: the sun shadow-map tier's ceiling on the ANSWERING LEVEL's voxel. The"        \
      " tier biases the receiver by one level voxel of light-space depth"                          \
      " (GI_SUN_SHADOWMAP_SLOPE_COVER_VOXELS, which the +-0.5-voxel quadrature over a whole"       \
      " attribute face genuinely needs), so it reports LIT through any occluder thinner than"      \
      " that voxel - a field-free sun injector into every sealed room whose roof is thinner"       \
      " than a coarse cascade cell, bypassing the SDF, the cage visibility and the dead-probe"     \
      " gate alike (measured: interior ceiling brightest, sun-white, falling off downward,"        \
      " walls merely bouncing it). At metre-scale faces NO bias is simultaneously acne-free"       \
      " and leak-free, so the tier must decline rather than guess: above this the traced"          \
      " field answers, exactly as it did before the tier existed. 0.125 m keeps ONLY level 0"      \
      " at the runtime cascade (resolution 128 over a 16 m base extent = 0.125 m voxels):"         \
      " the bias must stay below the thinnest geometry a scene is expected to seal, and at"        \
      " level 1 the 0.25 m bias EQUALS a 25 cm door slab or baffle, which the tier then"           \
      " reports lit for any sun within 60 degrees of grazing (depth through the slab ="            \
      " thickness x cos(incidence) < bias). Level 0 sits 2x below that slab; CSM cascade 0 -"      \
      " the only split the tier binds - covers the near frustum slice where level 0 lives."       \
      " Coarser levels inside the level-0 window inherit its faces instead (the light-voxel"     \
      " kernel's fine-level pull), so the map's answer reaches them without a tap of their own;"  \
      " a lifted single-tap variant for the coarse levels was measured and rejected (a second"   \
      " launch per cascade cost +0.4 ms, and one launch under cascade 0 lit thin-walled sealed"   \
      " cells)")                                                                                   \
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
      " painted black rings/donuts on open ground (measured in screenshots). Walls"                \
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
    X(GI_WORLD_PROBE_CAGE_VIS_CROSS_VOXELS, 0.25f,                                                 \
      "SDF voxels of the cage's level", "MEASURED on the oracle's marches"                         \
      " (test_world_probe_cage_visibility_seals_box): the depth a minimum must reach before a"     \
      " rise can convict. A 0.1 m wall - below the 0.125 m level-0 voxel, so it has no negative"   \
      " core for ACCEPT_VOXELS to find - bottoms out at 0.090 voxels head-on and 0.096 at 45"      \
      " degrees, while a cage segment LEAVING flat ground bottoms at 0.891 and one grazing"        \
      " along it holds 0.17-0.28. 0.25 sits 2.8x above the crossings and 3.6x below the"           \
      " departure. Cannot be used as a proximity test on its own (that is the flat-ground"         \
      " black-donut failure); it only qualifies a MINIMUM, and the rise below is what makes"       \
      " the verdict a crossing")                                                                   \
    X(GI_WORLD_PROBE_CAGE_VIS_CROSS_SLOPE, 0.25f,                                                  \
      "field rise per unit travel", "derived + MEASURED: the field is 1-Lipschitz and a sphere"    \
      " trace steps by its own reading, so the rate along the walk IS the sine of the segment's"   \
      " incidence on the surface. Measured climbing out of the 0.1 m wall: 1.00 head-on, 0.71"     \
      " at 45 degrees; a segment grazing flat ground holds 0.008 and one ENDING on a surface"      \
      " never climbs at all - a straight segment over a PLANE has no V by construction, it can"    \
      " only approach or recede. 0.25 convicts crossings steeper than ~14 degrees while sitting"   \
      " 30x above the graze")                                                                      \
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
    X(GI_SCREEN_PROBE_SPACING, 32,                                                                 \
      "full-resolution pixels", "measured: the gi_resolve_pass::settings::probe_spacing default"   \
      " - THE ray-budget knob now that the probe-space temporal is gone (cost scales with the"     \
      " inverse square; the removal's full-rate cost is recovered here as spatial density"         \
      " instead of temporal staleness). 32 was A/B'd against 16 at 4K and judged"                  \
      " indistinguishable: the adaptive interp already thins flat regions, the"                    \
      " integrate's plane-weighted 4-probe blend + bilateral upsample + denoise band-limit"        \
      " spatially at this scale, and the Hi-Z screen tier keeps near-field occlusion"              \
      " pixel-precise regardless of lattice pitch. For reference, Lumen's shipped uniform grid"    \
      " (ScreenProbeGather.DownsampleFactor [S21 s34]) is one probe per 16x16 full-res pixels"     \
      " backed by ADAPTIVE REFINEMENT probes at interpolation failures; ours is one per 32x32"     \
      " backed by adaptive SKIPPING - the refinement direction is the open quality lever if"       \
      " sparse lattices ever show silhouette errors")                                              \
    /* The probe-space temporal (direction strata blended 1/n into the tile, sticky anchors,   */ \
    /* scheduled Halton walks) was REMOVED: averaging in probe space turns white               */ \
    /* per-frame noise into probe-granular correlated drift the downstream temporal cannot     */ \
    /* remove (measured as still-camera moving blobs across three schemes). All 64 texels      */ \
    /* trace fresh every frame; ray budget scales with probe_spacing instead.                  */ \
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
    X(GI_BOUNCE_TINT_MAX_VISIBILITY, 0.95f,                                                        \
      "cavity visibility", "derived: gate for the bounce's near-edge tint fill. The cavity"        \
      " march attenuates the cage ambient by its visibility, but the BLOCKED fraction of the"      \
      " face's cone contributed black - a white floor face beside a red wall lost exactly the"     \
      " wall's red, the sub-spacing colour adjacency a probe-spacing cage cannot represent"        \
      " (the DDGI-family chroma wash). The fill re-locates the encroaching surface within the"     \
      " march's own band and injects its light-voxel radiance at weight (1 - visibility), the"     \
      " energy the attenuation removed. Above this visibility the blocked sliver is"               \
      " negligible and the locator's field taps are all cost. Stability: the new"                  \
      " voxel->voxel edge multiplies albedo x (1 - visibility) per hop, bounded by"                \
      " GI_MAX_ALBEDO x (1 - GI_LIGHT_VOXEL_VISIBILITY_MIN) < 1 on every surviving face"           \
      " (culled faces still store zero), and a same-cell self-read is refused outright -"          \
      " the series is the light bouncing inside the cavity, converging under the relight EMA")     \
    X(GI_BOUNCE_TINT_MIN_AXIS_DOMINANCE, 0.8f,                                                     \
      "unitless", "derived: the near-edge tint fill selects the blocker's face slab by the"        \
      " DOMINANT AXIS of the field gradient, and its leak defence is that the two sides of a"      \
      " wall live in different slabs - which holds only while the gradient is axis-aligned. At"    \
      " a convex silhouette edge the field rounds to ~45 degrees, two axes read 1/sqrt(2) ="       \
      " 0.707, and the pick is decided by an epsilon: the slab chosen can be the face AROUND"      \
      " the corner, lit by an emitter the reader cannot see (measured: cyan emissive bleeding"     \
      " around a box corner onto its shadowed face). The threshold must sit above that tie;"       \
      " 0.8 (within ~37 degrees of an axis) leaves margin for field noise. Below it the fill"      \
      " declines and the blocked fraction contributes black - the pre-fill behaviour, toward"      \
      " darkness")                                                                                 \
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
    X(GI_REFLECTION_MOTION_WINDOW, 3.0f,                                                           \
      "frames of running-mean depth under camera motion", "derived: trail length on a blurred"     \
      " high-contrast boundary (a bright building over sky on a gloss floor) is the 1/count"       \
      " catch-up time, and the base window's 8 frames read as a visible smear band that the"       \
      " neighbourhood clamp cannot reject (a blurred edge's AABB legitimately spans both sides)."  \
      " While MEASURED reprojection motion exceeds the clamp threshold the count cap collapses"    \
      " to this instead - the composite's roughness-ramped spatial kernel and the motion itself"   \
      " hide the extra variance. Keys on measured motion, never on the release gates: the mirror"  \
      " determinism gate forces the release to 0 permanently, and a PARKED mirror must keep its"   \
      " full base window for relight-phase integration")                                           \
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
    X(GI_REFLECTION_MOVER_STILL_CAP, 0.125f,                                                       \
      "stillness fraction", "derived: ceiling on the temporal's stillness release while the"       \
      " velocity pass drew any mover within one temporal window. The release reads RECEIVER"       \
      " motion only, so a still camera watching a moving emitter held its ghost unclamped and"     \
      " x4-windowed - and the per-pixel hit read can only TIGHTEN, never lift, the cap: a"         \
      " DEPARTED mover reads static at exactly its ghost's pixels (the current mirror hit is"      \
      " the revealed background - present cannot validate history), so any 'static now'"           \
      " reading that superseded the cap preserved the trail (measured, emissive-cube shooter)."    \
      " 0.125 keeps 87.5% of the neighbourhood clamp engaged: ghosts flush within about one"       \
      " window while converged static content under a parked camera loses at most the release's"   \
      " tail. Costs nothing while no mover is on screen")                                          \
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
      " full-res temporal runs the dual-rate pair below")                                          \
    X(GI_TEMPORAL_FAST_FRAMES, 8,                                                                  \
      "frames", "derived: one full anchor-placement cycle (the 8-cycle Halton) - the shortest"     \
      " window whose mean has seen every placement jitter once. The dual-rate temporal's fast"     \
      " lane, the count a detected lighting change resets the slow lane to, AND the slow"          \
      " lane's cap while the lighting-change signal is hot: the dim penumbra of a moved"           \
      " emissive shifts the mean by less than the lane noise (shift/sigma = sqrt(p/(1-p)) for"     \
      " rare-arrival content), so the 3-sigma detector is provably blind to it and at the old"     \
      " hot cap of GI_TEMPORAL_MAX_FRAMES the penumbra decayed as a visible second-long trail."    \
      " Flushing at the fast rate while hot trades transient shimmer (bounded by the"              \
      " quiescence hold) for reactivity, and costs nothing while still")                           \
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
    X(GI_QUIESCENCE_LUMINANCE_FLOOR, 0.001f,                                                       \
      "radiance, scene linear", "derived: the relight convergence statistic measures each"         \
      " face's change RELATIVE to its luminance, so a decaying sealed room registers next to"      \
      " a sunlit exterior (an absolute sum is owned by the exterior and hides the room's"          \
      " tail - the very tail the gate must not freeze). Below this absolute luminance the"         \
      " change is taken against the floor instead: at the exposures the exposure pass allows"      \
      " a dark room (its dark-adaptation slope caps the lift near 1.5x neutral) 1e-3 is under"     \
      " one 8-bit step, so a change below it is invisible and must not hold the passes open")      \
    X(GI_QUIESCENCE_STATS_SCALE, 1024.0f,                                                          \
      "fixed-point steps per unit", "derived: the per-level sums are integer image atomics"        \
      " (no float atomics on the SM5 / GLSL 4.3 floor); 1024 steps per unit of relative"           \
      " change resolve 0.1% per face, and the worst-case sum (2^18 relit faces at full"            \
      " change x 1024) stays under 2^32")                                                          \
    X(GI_QUIESCENCE_MIN_FRAMES, 32,                                                                \
      "frames", "derived: two complete world-probe windows (GI_WORLD_PROBE_WINDOW): the probe"     \
      " atlas is a windowed mean that reaches its fixed point one window after the last"           \
      " voxel change, and the bounce feeds back once more through the voxels. Nothing"             \
      " freezes earlier however still the relight reads")                                          \
    X(GI_QUIESCENCE_MAX_FRAMES, 1024,                                                              \
      "frames", "derived: the hard ceiling on how long a still scene keeps the world passes"       \
      " alive, 4x the old fixed settle: a relight that never reads stationary (an unstable"        \
      " feedback loop, a dithered edge storm) is bounded here instead of running forever."         \
      " At 0.99 per relight - the slowest closed-room tail the loop gain allows - 256"             \
      " relights leave 7%, the accepted cost of that rare case")                                   \
    X(GI_QUIESCENCE_CONVERGED_MEAN, 0.0005f,                                                       \
      "relative change per relit face", "derived: half of one 8-bit step of a face at unit"        \
      " luminance, per relight. A volume whose mean relative change is below this rewrites"        \
      " values no reader can distinguish, so the passes stop at GI_QUIESCENCE_MIN_FRAMES"          \
      " instead of the old 256 - static exteriors freeze 8x sooner")                               \
    X(GI_QUIESCENCE_STATIONARY_FRACTION, 0.95f,                                                    \
      "unitless", "derived: at rest the relight is a stationary process (the sun dither at"        \
      " shadow edges, folded in by the EMA) whose mean relative change never reaches the"          \
      " converged floor, while a decaying tail shrinks; the mean over the last"                    \
      " GI_QUIESCENCE_WINDOW_FRAMES against the same mean GI_QUIESCENCE_COMPARE_FRAMES"            \
      " earlier separates them. The slowest tail the bounce loop can carry (0.99 per"              \
      " relight) falls to 0.92 over 32 frames, so 0.95 still catches it; the dither noise of"      \
      " two-rotation means sits well inside 5%")                                                   \
    X(GI_QUIESCENCE_WINDOW_FRAMES, 8,                                                              \
      "frames", "derived: two relight rotations (GI_LIGHT_VOXEL_UPDATE_DENOM) - every face in"     \
      " the mean twice, which cancels the per-phase difference of the rotation's face sets")       \
    X(GI_QUIESCENCE_COMPARE_FRAMES, 32,                                                            \
      "frames", "derived: eight rotations apart - far enough for the slowest decay to show (see"   \
      " GI_QUIESCENCE_STATIONARY_FRACTION), short enough that a stationary scene freezes"          \
      " within 40 frames of stillness")                                                            \
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
    X(GI_TEMPORAL_DEPTH_TOLERANCE, 0.1f,                                                           \
      "relative depth per unit view distance", "Lumen's Temporal.DistanceThreshold = 0.005"        \
      " assumes motion-vector reprojection; ours still reconstructs the previous position"         \
      " from the depth buffer (velocity-era note: camera pixels keep the matrix+depth path,"       \
      " so reconstruction error at edges and grazing angles remains and 0.005 still"               \
      " over-rejects). The historical 0.25 also absorbed MOVING receivers failing the test;"       \
      " those now skip it via the velocity buffer's object split, so the slack tightened to"       \
      " 0.1 - less stale-light bleed across depth edges under camera motion. Live-tunable as"      \
      " gi_resolve_pass::settings::reprojection_tolerance (this is its default)")                \
    X(GI_TEMPORAL_DIRTY_HOLD_FRAMES, 48,                                                           \
      "frames", "derived: how long a moved instance's region keeps the temporal's FAST cap"       \
      " after its last change. The stale light a mover leaves behind reaches the gather"          \
      " through the world side with serialised latency (recompose <= GI_CLIPMAP_EDIT_THROTTLE"    \
      "_FRAMES 8, relight rotation 4 with the EMA at write-through, probe fast window 4), so"      \
      " the last stale gather lands ~16 frames after the object stops; flushing it at the fast"    \
      " rate to ~1% takes another 32 (0.875^32). Before this the trigger was GLOBAL: any moving"   \
      " instance anywhere pinned every pixel's slow lane at the fast cap for the motion plus 256"  \
      " frames (measured: a cube 30 m away doubled static-floor noise in a still shot) - the"      \
      " region-local hold keeps the flush where the stale light is")                             \
    X(GI_TEMPORAL_DIRTY_MAX_BOUNDS, 16,                                                            \
      "regions", "derived: the uniform-array budget for changed-instance regions (two vec4s"      \
      " each) the temporal tests per pixel. Beyond it the pass falls back to the global fast"      \
      " cap, exactly the pre-localisation behaviour - a crowd of movers is a scene that IS"        \
      " changing everywhere")                                                                     \
    X(GI_LIGHT_VOXEL_FADE_VOXELS, 16.0f,                                                            \
      "voxels of the finer covering level", "derived: the gather's and probes' light-voxel"        \
      " reads cross-fade between cascade levels over this band - GI_REFLECTION_CASCADE_FADE"      \
      "_VOXELS's role for irradiance. The first-success walk switched hit radiance from 0.25 m"   \
      " to 0.5 m voxels at a knife edge 8 m from the camera, and that edge sweeps every surface"  \
      " as the camera translates (measured: 50-60% brightness pops in a dark corridor at the"     \
      " level-0 re-snap). Four times the field's blend band (4 m at level 0): inside the"          \
      " level-0 window the coarser level's faces are inherited means of the finer ones, so the"   \
      " band mixes matching data at no leak cost, and what residual the levels still disagree"    \
      " on (fattening beyond the window) becomes a gradient over metres of travel instead of a"   \
      " step")                                                                                     \
    X(GI_LIGHT_VOXEL_INHERIT_CONTRAST, 4.0f,                                                      \
      "luminance ratio", "derived: a coarse light-voxel face inherits the mean of its measured"   \
      " finer children only while their brightest and darkest lie within this ratio. Sun"        \
      " against sky is 10x or more, relight noise between children well under 2x, so children"   \
      " that disagree by more straddle a lighting edge (a sun pool's rim, a thin wall with a"      \
      " lit and a dark side) that the coarse face cannot hold as one value; the coarse relight"  \
      " answers at the face centre instead. Not the guard against the exterior leaking in - a"   \
      " lone exposed child on the far side of a wall agrees with itself; that case is closed by"  \
      " running the pull only after this level's own exposure gates (see the kernel)")            \
    X(GI_LIGHT_VOXEL_INHERIT_FLOOR, 0.0001f,                                                       \
      "radiance luminance", "derived: the darkest child's luminance is floored here before the"  \
      " contrast ratio, so a black child beside any lit one reads as disagreement while two"      \
      " near-black children (below the readers' own 1e-4 measured threshold) still agree")       \
    X(GI_LIGHT_VOXEL_SEED_ALPHA, 0.25f,                                                            \
      "unitless", "derived: the provenance alpha of a light-voxel face SEEDED from the parent"    \
      " level when its cell scrolls into a window. Above the readers' 1e-4 measured threshold"    \
      " (the seed is read, premultiplied, like any measurement) and below the relight EMA's 0.5"  \
      " measured test, so the first relight writes through and replaces the seed outright."       \
      " Zero-claimed cells stayed black until their first relight - up to one 4-frame rotation"   \
      " - dragging a dark frontier through the near field every 2 m of travel")                   \
    X(GI_DENOISE_REVEAL_STEP, 8,                                                                   \
      "texels", "derived: a-trous spacing of the REVEAL pass run after the three compute"          \
      " passes (steps 1, 2, 4) for pixels whose accumulation count is still low - the ReBLUR"     \
      " history-fix idea (blur radius from accumulated frames). Twice the last regular step,"     \
      " so a just-revealed pixel is reconstructed from a 32-texel reach instead of 8")             \
    X(GI_DENOISE_REVEAL_COUNT, 8,                                                                  \
      "accumulated frames", "derived: the reveal pass passes through pixels at or above this"     \
      " count - one fast window, past which the running mean has averaged enough gathers that"    \
      " the regular chain's reach suffices (measured: revealed regions stayed 3-4x noisier than"  \
      " converged ones for 14+ frames with the fixed reach)")                                     \
    X(GI_REFLECTION_ROUGH_WINDOW_SCALE, 4.0f,                                                      \
      "x the reflection temporal window", "published: Lumen reflections accumulate 32 frames"     \
      " (Reflections.Temporal.MaxFramesAccumulated) against the gather's 10; the window here"      \
      " scales from the settings value at mirror roughness to this multiple at"                   \
      " GI_REFLECTION_ROUGH_CUTOFF, where the lobe is widest and one VNDF ray per frame"          \
      " integrates slowest. Sharp reflections keep the short window and its responsiveness")      \
    X(GI_REFLECTION_RESOLVE_START, 0.1f,                                                           \
      "GGX roughness", "derived: the pre-temporal spatial resolve of the 3x3 raw neighbourhood"   \
      " (stochastic-SSR's resolve stage, edge-stopped, blend-free) fades in from here to"          \
      " GI_REFLECTION_GATHER_FADE_START. Below it the lobe is tight enough that neighbours"        \
      " sample different content and the resolve would only blur; above it one ray per pixel"     \
      " cannot resolve a small emitter under the lobe (measured: speckle on brushed metal at"     \
      " roughness 0.35), and nine samples per frame is the cheapest variance reduction the pass"  \
      " already fetches")                                                                          \
    X(GI_WORLD_PROBE_EMA_WINDOWS, 16,                                                              \
      "probe windows", "derived: the world-probe atlas is now a converging running mean over"     \
      " this many complete windows (256 frames) of directions JITTERED inside their texel, in"     \
      " place of the zero-variance mean over fixed texel centres. Fixed centres are BIASED per"    \
      " probe - a small emitter is skewered or missed per direction and neighbouring probes"       \
      " disagree - which entered the voxel bounce as the blotch field on emissive-lit walls"       \
      " (measured: temporal std 1.24 vs 0.11 in sunlit cells). A capped mean has a variance"      \
      " floor of sigma^2 / (2N - 1): four windows left ~40% of the per-sample spread as a"          \
      " standing probe flicker (measured); sixteen leaves ~18% while converging within ~3 s,"      \
      " and the quiescence gate freezes the settled atlas. Light and content changes bypass"       \
      " the mean entirely: their fast windows sample texel centres at write-through - the"         \
      " deterministic atlas of before - and the mean resumes from it when the scene settles")     \
    X(GI_EMISSIVE_NEE_MAX_EMITTERS, 64,                                                           \
      "emitters", "derived: cap on the emitter SEGMENTS the probes sample explicitly per"       \
      " frame (next-event estimation), brightest by power. Every probe scores every entry once"  \
      " (one lane per entry in the trace group), so the cap bounds that cost; emitters beyond"   \
      " it are found by the cone rays as before. Also the stride of the table appended to the"   \
      " SDF instance buffer, which the tracers already bind - no stage was free for a buffer")   \
    X(GI_EMISSIVE_NEE_PER_PROBE, 4,                                                               \
      "emitters", "derived: emitters a probe samples explicitly each frame, the top of its"       \
      " luminance x solid-angle score. Four covers a room with a panel and strips; the rest of"   \
      " the list still contributes through the cone rays, weighted by the balance heuristic")     \
    X(GI_EMISSIVE_NEE_SAMPLES, 1,                                                                  \
      "rays", "derived: aimed rays per CELL whose footprint touches a selected emitter's cone,"     \
      " on top of the cell's own jittered rays (which lose one supersample first, never their"     \
      " last). A cone narrower than a cell is served by one cell, a wider one by every cell it"    \
      " touches - the aimed count grows with the emitter's apparent size until"                    \
      " GI_EMISSIVE_NEE_MIN_CONE_COS hands it back to the cell rays. One keeps every lane within"  \
      " the importance ladder's four samples")                                                    \
    X(GI_EMISSIVE_NEE_MIN_CONE_COS, 0.866f,                                                        \
      "cosine", "derived: an emitter whose bounding-sphere cone is wider than this (30 degrees"    \
      " half angle, 0.84 sr against a 0.2 sr octahedral cell) is not aimed at: the cell rays"     \
      " already land a sample in it several times per frame, and aiming would only spread"       \
      " extra rays across every cell it covers. Aiming pays exactly where the cone is small")    \
    X(GI_EMISSIVE_NEE_SEGMENT, 1.0f,                                                             \
      "metres", "derived: an emissive instance longer than this on an axis is split into"        \
      " that many segments, each its own bounding sphere. The sphere of a 7 m light strip is"    \
      " 3.5 m across and swallows every probe near it - the strip was never aimed at - while"    \
      " seven 1 m segments subtend cones a probe can aim inside. One level-0 window's worth of" \
      " probe spacing: a segment no longer than the lattice it serves")                          \
    X(GI_EMISSIVE_NEE_MIN_LUMINANCE, 0.05f,                                                        \
      "radiance luminance", "derived: emissive below this never enters the table - the readers'" \
      " measured-darkness level; a faintly glowing surface is fine on the cone rays alone")       \
    X(GI_WORLD_PROBE_BLEND_BAND, 0.5f,                                                             \
      "probe spacings", "derived: width of the cross-fade between a cascade's cage and the"        \
      " next, measured inward from the usable extent (3 spacings). Half a spacing is one sixth"   \
      " of the extent; Godot smoothsteps over two of eight, DDGI21 over the last full cell. One"  \
      " spacing was measured and reverted: the wider band admitted half-visible coarse cages"     \
      " and brightened a corridor interior even with the far blend scaled by the near cage's"     \
      " visible fraction. The irradiance cascade, the radiance completion and the light-voxel"    \
      " bounce twin all read it - they must stay in step")
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
