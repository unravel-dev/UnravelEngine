#pragma once

/*
 * Single owner of the constants of the Lumen-style screen probe gather (lumen_gather_pass).
 *
 * Every value is UE 5.8 Lumen's at Epic scalability with software ray tracing, cited by file:line
 * relative to UE_5.8/Engine (S = Shaders/Private/Lumen, R = Source/Runtime/Renderer/Private/Lumen,
 * INI = Config/BaseScalability.ini). Distances are converted from centimetres to metres.
 *
 * The shader mirror is engine_data/data/shaders/lumen/lumen_constants.sh; the GI test suite parses it
 * and asserts every entry below matches and no shader-side LUMEN_* constant is missing here.
 */

// clang-format off
#define LUMEN_CONSTANTS_TABLE(X)                                                                   \
    X(LUMEN_PROBE_DOWNSAMPLE_FACTOR, 16,                                                           \
      "px", "r.Lumen.ScreenProbeGather.DownsampleFactor at Epic, INI [GlobalIlluminationQuality@3]") \
    X(LUMEN_PROBE_TRACE_RES, 8,                                                                    \
      "texels", "r.Lumen.ScreenProbeGather.TracingOctahedronResolution: 8 x 8 equal-area texels,"  \
      " one ray each before importance sampling (R/LumenScreenProbeGather.cpp)")                    \
    X(LUMEN_PROBE_JITTER_PERIOD, 8,                                                                \
      "frames", "placement Hammersley16(frame % 8) and ray-direction blue noise slice frame % 8"   \
      " (S/LumenScreenProbeCommon.ush:67-76, LumenScreenProbeTracingCommon.ush:8-22)")              \
    X(LUMEN_PROBE_PIXEL_STRIDE, 4096,                                                              \
      "px", "probe records pack their pixel as x + y x stride, exact in a float below 2^24, so"    \
      " Lumen views need both axes at most this (UE packs x | y << 16 in a uint)")                 \
    X(LUMEN_ADAPTIVE_SAMPLES_X, 4,                                                                 \
      "samples", "adaptive placement candidates per uniform probe along x: NumAdaptiveProbes"      \
      " 8 at Epic (INI GI@3) -> 4 x 2 (R/LumenScreenProbeGather.cpp:554-568)")                     \
    X(LUMEN_ADAPTIVE_SAMPLES_Y, 2,                                                                 \
      "samples", "the candidates along y (same source)")                                           \
    X(LUMEN_ADAPTIVE_ALLOCATION_FRACTION, 0.5f,                                                    \
      "", "r.Lumen.ScreenProbeGather.AdaptiveProbeAllocationFraction: at most"                     \
      " trunc(uniform x 0.5) adaptive probes (R/LumenScreenProbeGather.cpp:2242-2243)")            \
    X(LUMEN_MAX_RAY_INTENSITY, 10.0f,                                                              \
      "pre-exposed", "per-ray clamp of max3(radiance) before compositing into the probe"            \
      " (S/LumenScreenProbeFiltering.usf:77-85)")                                                  \
    X(LUMEN_SCREEN_TRACE_MAX_ITERATIONS, 50,                                                       \
      "steps", "r.Lumen.ScreenProbeGather.ScreenTraces.MaxIterations (S/LumenScreenProbeTracing.usf:54-366)") \
    X(LUMEN_SCREEN_TRACE_RELATIVE_THICKNESS, 0.02f,                                                \
      "ratio", "hit when the crossing is within 2% of the depth behind the depth buffer"           \
      " (HZBTracing.ush:206, LumenScreenProbeTracing.usf)")                                        \
    X(LUMEN_SCREEN_TRACE_HISTORY_DEPTH_TEST, 0.005f,                                               \
      "device z", "screen hit kept only if last frame's depth at the reprojected hit agrees,"      \
      " x lerp(0.5, 2, noise) (S/LumenScreenProbeTracing.usf:291-312)")                            \
    X(LUMEN_SCREEN_TRACE_MISS_OFFSET, 0.02f,                                                       \
      "m", "a screen miss reports its distance + 2 cm (S/LumenScreenProbeTracing.usf:276-290)")     \
    X(LUMEN_TRACE_RESUME_PULLBACK, 0.1f,                                                           \
      "m", "each tracing stage resumes 10 cm (2 x SurfaceBias) before the previous stage ended"    \
      " (S/LumenScreenProbeTracing.usf:591, 767)")                                                 \
    X(LUMEN_SURFACE_BIAS, 0.05f,                                                                   \
      "m", "SDF stage origin offset along the ray and the normal (R/LumenDiffuseIndirect.cpp:25)") \
    X(LUMEN_MAX_TRACE_DISTANCE, 200.0f,                                                            \
      "m", "PP LumenMaxTraceDistance 20000 cm (R/LumenDiffuseIndirect.cpp:228-231)")               \
    X(LUMEN_FILTER_PASSES, 3,                                                                      \
      "passes", "r.Lumen.ScreenProbeGather.SpatialFilterNumPasses (R/LumenScreenProbeFiltering.cpp:583-621)") \
    X(LUMEN_FILTER_POSITION_WEIGHT_SCALE, 1000.0f,                                                 \
      "", "exp2(-scale (dz / z)^2) position weight (S/LumenScreenProbeFiltering.usf:292-297)")     \
    X(LUMEN_FILTER_MAX_HIT_ANGLE_DEGREES, 10.0f,                                                   \
      "deg", "SpatialFilterMaxRadianceHitAngle (S/LumenScreenProbeFiltering.usf:413-450)")         \
    X(LUMEN_INTERP_DEPTH_WEIGHT, 10000.0f,                                                         \
      "", "exp2(-w r^2), r = plane distance / depth (S/LumenScreenProbeGather.usf:147-210)")        \
    X(LUMEN_INTERP_FALLBACK_DEPTH_WEIGHT, 1000.0f,                                                 \
      "", "the looser weight used when the primary weights sum below 0.01"                          \
      " (S/LumenScreenProbeGather.usf:1211-1227)")                                                 \
    X(LUMEN_INTERP_MIN_WEIGHT, 0.01f,                                                              \
      "", "MIN_PROBE_INTERPOLATION_WEIGHT (S/LumenScreenProbeGather.usf:36)")                      \
    X(LUMEN_FULL_RES_JITTER_WIDTH, 1.0f,                                                           \
      "tiles", "r.Lumen.ScreenProbeGather.FullResolutionJitterWidth (S/LumenScreenProbeGather.usf:1167-1196)") \
    X(LUMEN_FULL_RES_JITTER_NEAR, 5.0f,                                                            \
      "m", "the jitter width halves from this depth (S/LumenScreenProbeGather.usf:1172)")           \
    X(LUMEN_FULL_RES_JITTER_PLANE_WEIGHT, 1000000.0f,                                              \
      "", "a jittered pixel is used only if exp2(-w r^2) > 0.01 (S/LumenScreenProbeGather.usf:1187-1195)") \
    X(LUMEN_TEMPORAL_MAX_FRAMES, 10,                                                               \
      "frames", "r.Lumen.ScreenProbeGather.Temporal.MaxFramesAccumulated (R/LumenScreenProbeGather.cpp:227-231)") \
    X(LUMEN_TEMPORAL_DEPTH_THRESHOLD, 0.01f,                                                       \
      "ratio", "history tap kept within 1% x U(0.5, 1.5) / lerp(0.1, 1, NoV) of the reprojected"   \
      " depth (StochasticLighting/StochasticLightingTileClassification.usf:884-1085)")              \
    X(LUMEN_TEMPORAL_MIN_TAP_WEIGHT, 0.01f,                                                        \
      "", "a history tap with a smaller bilinear weight is ignored (same file)")                   \
    X(LUMEN_MIN_TRACE_DISTANCE, 0.0001f,                                                           \
      "m", "r.Lumen.DiffuseIndirect.MinTraceDistance 0, clamped to 0.01 cm: the distance-field"    \
      " stage starts at max(this, screen distance - pullback) (R/LumenDiffuseIndirect.cpp:57-63)") \
    X(LUMEN_GLOBAL_SDF_EXPAND_VOXELS, 0.5f,                                                        \
      "voxels", "global SDF surface expansion: ClipmapVoxelExtent, half a voxel"                   \
      " (GlobalDistanceFieldUtils.ush; analysis c)")                                               \
    X(LUMEN_GLOBAL_SDF_EXPAND_RAMP_VOXELS, 1.0f,                                                   \
      "voxels", "travel over which the expansion ramps up from zero at the ray origin (same file)") \
    X(LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS, 0.5f,                                                      \
      "voxels", "step floor: MinStepFactor 1 x ClipmapVoxelExtent (same file)")                    \
    X(LUMEN_GLOBAL_SDF_MAX_STEPS, 256,                                                             \
      "steps", "march budget per ray (Lumen: 256 per clipmap, same file)")                         \
    X(LUMEN_GLOBAL_SDF_COVERED_EXPAND_SCALE, 1.0f,                                                 \
      "", "r.LumenScene.GlobalSDF.CoveredExpandSurfaceScale: the expansion where a one-sided"      \
      " mesh is near (R/GlobalDistanceField.cpp:258-264)")                                         \
    X(LUMEN_GLOBAL_SDF_NOT_COVERED_EXPAND_SCALE, 0.6f,                                             \
      "", "r.LumenScene.GlobalSDF.NotCoveredExpandSurfaceScale: the expansion where only"          \
      " two-sided meshes are near (R/GlobalDistanceField.cpp:266-272)")                            \
    X(LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE, 4.0f,                                           \
      "", "r.LumenScene.GlobalSDF.NotCoveredMinStepScale: the min step there"                      \
      " (R/GlobalDistanceField.cpp:281-287)")                                                      \
    X(LUMEN_GLOBAL_SDF_DITHER_STEP_THRESHOLD, 0.5f,                                                \
      "", "r.LumenScene.GlobalSDF.DitheredTransparencyStepThreshold: per-step hit chance"          \
      " in uncovered space for dithered tracers (R/GlobalDistanceField.cpp:289-295)")              \
    X(LUMEN_GLOBAL_SDF_DITHER_TRACE_THRESHOLD, 0.9f,                                               \
      "", "r.LumenScene.GlobalSDF.DitheredTransparencyTraceThreshold: the per-trace"               \
      " chance (R/GlobalDistanceField.cpp:297-303)")                                               \
    X(LUMEN_GLOBAL_SDF_COVERAGE_BAND_VOXELS, 4.0f,                                                 \
      "voxels", "a voxel is covered when a one-sided mesh lies within this: ONE_SIDED_BAND_SIZE,"  \
      " 8 voxel extents (S/../DistanceField/GlobalDistanceFieldUtils.ush:5)")                      \
    X(LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS, 0.2f,                                                    \
      "m", "r.AOGlobalDistanceField.MinMeshSDFRadius: an object whose world bounding sphere is"   \
      " smaller is left out of the global SDF and its object grid, unless an emissive light"      \
      " source (R/../GlobalDistanceField.cpp:195, 412-418; SD/../GlobalDistanceField.usf:125)")    \
    X(LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS_VOXELS, 0.5f,                                             \
      "voxels", "r.AOGlobalDistanceField.MinMeshSDFRadiusInVoxels: the same floor in voxels of"   \
      " each level, the larger of the two applies (R/../GlobalDistanceField.cpp:203, 415)")        \
    X(LUMEN_GLOBAL_SDF_FLAT_GRADIENT, 0.00001f,                                                    \
      "m", "a hit's central-difference gradient shorter than this falls back to the reversed ray"  \
      " (UE 0.001 cm, GlobalDistanceFieldShared.ush:310)")                                         \
    X(LUMEN_SDF_SELF_LIGHTING_FADE_START, 0.75f,                                                   \
      "voxels", "distance-field hit radiance x smoothstep(1.5, 2.0 ClipmapVoxelExtent, hit time):" \
      " black within 0.75 voxel of the origin (SoftwareRayTracing.ush:651-656)")                   \
    X(LUMEN_SDF_SELF_LIGHTING_FADE_END, 1.0f,                                                      \
      "voxels", "the fade completes at one voxel of travel (same file)")                           \
    X(LUMEN_SCREEN_TRACE_BIAS_TEXELS, 2.0f,                                                        \
      "half px", "screen-trace origin lifted along the probe normal by twice the world size of"    \
      " half a pixel at its depth (ScreenTraceUtils.ush:17-26)")                                   \
    X(LUMEN_SCREEN_TRACE_NEAR_STOP, 0.99f,                                                         \
      "ratio", "a ray toward the camera stops at 1% of its origin depth (HZBTracing.ush:33-251)")  \
    X(LUMEN_SCREEN_TRACE_VIGNETTE_SCALE, 5.0f,                                                     \
      "", "screen hit kept with probability min(V(hit), V(history)), V(s) = saturate(1 - |v|^2),"  \
      " v = saturate(5 |s| - 4): the outer 10% of each screen half (S/LumenScreenProbeTracing.usf:291-312)") \
    X(LUMEN_SCREEN_TRACE_VIGNETTE_OFFSET, 4.0f,                                                    \
      "", "the vignette offset (same formula)")                                                    \
    X(LUMEN_SCREEN_TRACE_HISTORY_NOISE_MIN, 0.5f,                                                  \
      "ratio", "the history depth test threshold scales by lerp(0.5, 2, IGN) (same file)")         \
    X(LUMEN_SCREEN_TRACE_HISTORY_NOISE_MAX, 2.0f,                                                  \
      "ratio", "upper end of that scale")                                                          \
    X(LUMEN_COMPOSITE_DITHER_SLICE_OFFSET, 31,                                                     \
      "slices", "the re-binning dither reads blue noise slice frame % 8 + 31 (S/LumenScreenProbeFiltering.usf:100)") \
    X(LUMEN_SH_TEXELS_PER_PROBE, 7,                                                                \
      "texels", "SH3 storage: c0 RGB, then (c1..c4), (c5..c8) per colour channel"                  \
      " (S/LumenScreenProbeFiltering.usf:866-910)")                                                \
    X(LUMEN_FULL_RES_JITTER_FAR_SCALE, 0.5f,                                                       \
      "ratio", "the full-resolution jitter width scales by lerp(1, 0.5, saturate((depth - near) / ramp))" \
      " (S/LumenScreenProbeGather.usf:1167-1196)")                                                 \
    X(LUMEN_FULL_RES_JITTER_RAMP, 5.0f,                                                            \
      "m", "depth range over which the jitter width halves (same file)")                           \
    X(LUMEN_INTEGRATE_NOISE_PERIOD, 64,                                                            \
      "frames", "per-pixel blue noise slices cycle with frame & 63 (BlueNoise.ush:23-26)")         \
    X(LUMEN_TEMPORAL_DEPTH_NOISE_MIN, 0.5f,                                                        \
      "ratio", "history depth threshold x lerp(0.5, 1.5, blue noise) (SLTC.usf:884-1085)")         \
    X(LUMEN_TEMPORAL_DEPTH_NOISE_MAX, 1.5f,                                                        \
      "ratio", "upper end of that scale")                                                          \
    X(LUMEN_TEMPORAL_MIN_NOV, 0.1f,                                                                \
      "cos", "history depth threshold / lerp(0.1, 1, NoV): up to 10x wider at grazing angles (same file)") \
    X(LUMEN_TEMPORAL_COUNT_LEVELS, 15,                                                             \
      "levels", "the frame count is stored in 4 bits: N quantized to multiples of"                 \
      " MaxFramesAccumulated / 15 (S/LumenScreenProbeGatherTemporal.ush:71-91)")                   \
    X(LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT, 4.0f,                                                 \
      "", "blend 1 / (1 + 4 N) when this frame has no valid estimate"                              \
      " (S/LumenScreenProbeGatherTemporal.usf:288-583)")                                           \
    X(LUMEN_TEMPORAL_HISTORY_GUARD, 0.53125f,                                                      \
      "px", "history taps are clamped 0.5 + 1/32 px inside last frame's view (StochasticLighting.cpp:1095-1111)") \
    X(LUMEN_RADIANCE_CACHE_CLIPMAPS, 4,                                                            \
      "clipmaps", "radiance cache clipmaps, camera-centred, each twice the previous"               \
      " (r.Lumen.ScreenProbeGather.RadianceCache.NumClipmaps, R/LumenScreenProbeGather.cpp:602-608)") \
    X(LUMEN_RADIANCE_CACHE_GRID, 48,                                                               \
      "cells", "probe cells per clipmap axis (GridResolution, R/LumenScreenProbeGather.cpp:633-639)") \
    X(LUMEN_RADIANCE_CACHE_EXTENT, 25.0f,                                                          \
      "m", "clipmap 0 half extent (ClipmapWorldExtent 2500 cm): cell 0 = 2 x 25 m / 48 = 1.04 m"   \
      " (R/LumenRadianceCache.cpp:1299, 1233-1236)")                                               \
    X(LUMEN_RADIANCE_CACHE_PROBE_RES, 32,                                                          \
      "texels", "equal-area octahedral radiance texels per probe axis at Epic (ProbeResolution, BaseScalability.ini:434)") \
    X(LUMEN_RADIANCE_CACHE_ATLAS_PROBES_X, 128,                                                    \
      "probes", "probe atlas width in probes (R/LumenRadianceCache.cpp:221-234)")                  \
    X(LUMEN_RADIANCE_CACHE_MAX_PROBES, 8192,                                                       \
      "probes", "probe pool: 128 x 64 (Lumen 128 x 128 at R11G11B10; RGBA16F here, half the pool)") \
    X(LUMEN_RADIANCE_CACHE_TRACE_BUDGET, 150,                                                      \
      "probes", "probes re-traced per frame beyond the new ones, Epic (NumProbesToTraceBudget, BaseScalability.ini:435)") \
    X(LUMEN_RADIANCE_CACHE_MAX_TRACES, 2752,                                                       \
      "probes", "per-frame trace cap (temp atlas capacity, R/LumenRadianceCache.cpp:1625-1650)")   \
    X(LUMEN_RADIANCE_CACHE_KEEP_FRAMES, 8,                                                         \
      "frames", "an unmarked probe survives this long (NumFramesToKeepCachedProbes, R/LumenScreenProbeGather.cpp:671-676)") \
    X(LUMEN_RADIANCE_CACHE_DOWNSAMPLE_DISTANCE, 40.0f,                                             \
      "m", "probes this far from the camera trace a quarter of the rays (DownsampleDistanceFromCamera," \
      " R/LumenRadianceCache.cpp:108-114)")                                                        \
    X(LUMEN_RADIANCE_CACHE_COST_NORMAL, 4,                                                         \
      "units", "trace cost of a full-resolution probe; 1 unit = (R / 2)^2 rays (LumenRadianceCacheUpdate.ush:10-21)") \
    X(LUMEN_RADIANCE_CACHE_COST_DOWNSAMPLED, 1,                                                    \
      "units", "trace cost of a downsampled probe (same file)")                                    \
    X(LUMEN_RADIANCE_CACHE_PRIORITY_BUCKETS, 16,                                                   \
      "buckets", "log2 age histogram: bucket 0 = new, 15 = traced since its last use (LumenRadianceCacheUpdate.usf:221-241)") \
    X(LUMEN_RADIANCE_CACHE_REPROJECTION_RADIUS, 1.5f,                                              \
      "x TMin", "sphere parallax radius of the hand-off lookup (ReprojectionRadiusScale, LumenRadianceCacheInterpolation.ush:348)") \
    X(LUMEN_RADIANCE_CACHE_FILTER_MAX_ANGLE, 0.2f,                                                 \
      "rad", "probe neighbour filter angle weight 1 - angle / 0.2 rad (LumenRadianceCache.usf:1055-1090)") \
    X(LUMEN_RADIANCE_CACHE_NO_HIT, 65504.0f,                                                       \
      "m", "probe depth of a ray that left the scene (f16 max; Lumen decodes 65503 cm)")           \
    X(LUMEN_IS_MIN_PDF_TO_TRACE, 0.1f,                                                             \
      "", "texels whose BRDF PDF reaches this keep at least it; texels below it are recycled into 4x refinement of" \
      " the brightest (r.Lumen.ScreenProbeGather.ImportanceSample.MinPDFToTrace, R/LumenScreenProbeImportanceSampling.cpp:52-58)") \
    X(LUMEN_IS_HISTORY_MIN_WEIGHT, 0.1f,                                                           \
      "", "a history probe feeds the lighting PDF only when exp2(-10000 r^2) passes this (S/LumenScreenProbeImportanceSampling.usf:63-142)") \
    X(LUMEN_IS_MIN_LIGHTING_SUM, 0.0001f,                                                          \
      "", "the lighting PDF is normalised by max(sum, this) (same file)")                          \
    X(LUMEN_IS_DISOCCLUSION_MAX_FRAMES, 4,                                                         \
      "frames", "a footprint pixel with fewer frames of history is young (r.Lumen.ScreenProbeGather.SpatialFilter.DisocclusionMaxFrames," \
      " R/LumenScreenProbeFiltering.cpp:21-34)")                                                   \
    X(LUMEN_IS_DISOCCLUSION_FRACTION, 0.4f,                                                        \
      "", "a probe is disoccluded when this share of its footprint is young; its filter drops the angle weight" \
      " (S/LumenScreenProbeGatherScreenData.usf:150-215)")                                         \
    X(LUMEN_MAX_ROUGHNESS_TO_TRACE, 0.4f,                                                          \
      "roughness", "traced reflections below this roughness: PP"                                   \
      " LumenMaxRoughnessToTraceReflections (Engine/Private/Scene.cpp:658,"                        \
      " S/LumenReflectionsCombine.ush:12-16)")                                                     \
    X(LUMEN_ROUGHNESS_FADE_LENGTH, 0.1f,                                                           \
      "roughness", "r.Lumen.Reflections.RoughnessFadeLength: the traced weight fades over"         \
      " this band (R/LumenReflections.cpp:425)")                                                   \
    X(LUMEN_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR, 0.8f,                                        \
      "roughness", "r.Lumen.ScreenProbeGather.MaxRoughnessToEvaluateRoughSpecular: above it"       \
      " the rough specular is the diffuse E / pi (R/LumenScreenProbeGather.cpp:355-360)")          \
    X(LUMEN_ROUGH_SPECULAR_FADE_LENGTH, 0.2f,                                                      \
      "roughness", "the rough specular fades into E / pi over this band below the maximum"         \
      " (S/LumenScreenProbeTileClassication.ush:36)")                                              \
    X(LUMEN_ROUGH_SPECULAR_MIN_ROUGHNESS, 0.2f,                                                    \
      "roughness", "rough specular lobes widen to ~2x2 texels of a probe"                          \
      " (S/LumenScreenProbeGather.usf:1519-1521)")                                                 \
    X(LUMEN_ROUGH_SPECULAR_SAMPLES, 4,                                                             \
      "samples", "GGX samples of the probe radiance per pixel"                                     \
      " (S/LumenScreenProbeGather.usf:1515)")                                                      \
    X(LUMEN_SPECULAR_SAMPLE_BIAS, 0.1f,                                                            \
      "", "BiasBSDFImportantSample squeezes E.y toward 0.5 by this share"                          \
      " (S/LumenScreenProbeGather.usf:863-870)")                                                   \
    X(LUMEN_PROBE_RADIANCE_BORDER, 1,                                                              \
      "texels", "border of the filtered probe radiance for bilinear lookups, 1 << (NumMips -"      \
      " 1) with NumMips 1 (R/LumenScreenProbeFiltering.cpp:60,"                                    \
      " R/LumenScreenProbeGather.cpp:2221)")                                                       \
    X(LUMEN_REFLECTION_MAX_RAY_INTENSITY, 40.0f,                                                   \
      "pre-exposed", "r.Lumen.Reflections.MaxRayIntensity: per-ray clamp of max3(radiance)"        \
      " (R/LumenReflections.cpp:179-184)")                                                         \
    X(LUMEN_REFLECTION_GGX_SAMPLING_BIAS, 0.1f,                                                    \
      "", "r.Lumen.Reflections.GGXSamplingBias: E.y *= 1 - bias drops the widest tail of the"      \
      " lobe (S/LumenReflections.usf:283-294)")                                                    \
    X(LUMEN_REFLECTION_MIRROR_ROUGHNESS, 0.001f,                                                   \
      "roughness", "below it the ray is the mirror direction (S/LumenReflections.usf:366)")        \
    X(LUMEN_REFLECTION_MIN_PDF, 0.0001f,                                                           \
      "", "cone angle = 1 / max(pdf, this) (S/LumenReflections.usf:387)")                          \
    X(LUMEN_REFLECTION_MIN_CONE_ANGLE, 0.00001f,                                                   \
      "rad", "MinReflectionConeAngle: a mirror ray keeps a positive cone"                          \
      " (S/LumenReflectionCommon.ush)")                                                            \
    X(LUMEN_REFLECTION_SCREEN_TRACE_RELATIVE_THICKNESS, 0.005f,                                    \
      "ratio", "r.Lumen.Reflections.HierarchicalScreenTraces.RelativeDepthThickness"               \
      " (R/LumenReflectionTracing.cpp:54)")                                                        \
    X(LUMEN_REFLECTION_HISTORY_DEPTH_TEST, 0.005f,                                                 \
      "device z", "r.Lumen.Reflections.HierarchicalScreenTraces.HistoryDepthTestRelativeThickn"    \
      "ess (R/LumenReflectionTracing.cpp:62, S/LumenReflectionTracing.usf:182-198)")               \
    X(LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH, 0.01f,                                          \
      "ratio", "r.Lumen.Reflections.SampleSceneColorRelativeDepthThickness: a distance-field"      \
      " hit reads last frame colour when it lies this close to the depth buffer"                   \
      " (S/LumenScreenTracing.ush:78-137)")                                                        \
    X(LUMEN_REFLECTION_SCENE_COLOR_NORMAL_COS, 0.0871557f,                                         \
      "cos", "cos of r.Lumen.Reflections.SampleSceneColorNormalTreshold 85 deg"                    \
      " (R/LumenReflectionTracing.cpp:278-282)")                                                   \
    X(LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS, 4.0f,                                             \
      "half voxels", "the distance field resumes this far before the screen trace end"             \
      " (S/LumenReflectionTracing.usf:732-735)")                                                   \
    X(LUMEN_REFLECTION_SDF_STEP_DITHER, 0.95f,                                                     \
      "", "near-mirror rays step by lerp(this, 1 / this, IGN) of the distance"                     \
      " (S/LumenReflectionTracing.usf:741-742)")                                                   \
    X(LUMEN_REFLECTION_SDF_STEP_DITHER_CONE, 0.0122718463f,                                        \
      "rad", "pi / 256: rays with a wider cone step undithered"                                    \
      " (S/LumenReflectionTracing.usf:742)")                                                       \
    X(LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE, 10.0f,                                              \
      "pre-exposed", "r.Lumen.Reflections.DenoiserTonemapRange: the denoisers average L / (1"      \
      " + Y / range) (R/LumenReflections.cpp:224-231, S/LumenReflectionDenoiserCommon.ush)")       \
    X(LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES, 5,                                                  \
      "samples", "r.Lumen.Reflections.ScreenSpaceReconstruction.NumSamples at Epic"                \
      " (R/LumenReflections.cpp:202, 1502)")                                                       \
    X(LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS, 8.0f,                                         \
      "px", "r.Lumen.Reflections.ScreenSpaceReconstruction.KernelRadius, x saturate(8"             \
      " roughness) (S/LumenReflectionResolve.usf:391-398)")                                        \
    X(LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS, 0.05f,                                        \
      "roughness", "mirrors below it skip the reconstruction"                                      \
      " (S/LumenReflectionResolve.usf:393)")                                                       \
    X(LUMEN_REFLECTION_TEMPORAL_MAX_FRAMES, 12.0f,                                                 \
      "frames", "r.Lumen.Reflections.Temporal.MaxFramesAccumulated"                                \
      " (R/LumenReflections.cpp:154)")                                                             \
    X(LUMEN_REFLECTION_TEMPORAL_MIRROR_FRAMES, 2.0f,                                               \
      "frames", "mirror pixels accumulate lerp(this, max, roughness / 0.05) frames"                \
      " (S/LumenReflectionDenoiserTemporal.usf:410-414)")                                          \
    X(LUMEN_REFLECTION_TEMPORAL_DISTANCE_THRESHOLD, 0.03f,                                         \
      "ratio", "r.Lumen.Reflections.Temporal.DistanceThreshold: surface history taps farther"      \
      " than this share of the depth are disoccluded (R/LumenReflections.cpp:171)")                \
    X(LUMEN_REFLECTION_NEIGHBORHOOD_CLAMP_SCALE, 1.0f,                                             \
      "sigma", "r.Lumen.Reflections.Temporal.NeighborhoodClampScale"                               \
      " (R/LumenReflections.cpp:160)")                                                             \
    X(LUMEN_REFLECTION_SPATIAL_KERNEL_RADIUS, 8.0f,                                                \
      "px", "r.Lumen.Reflections.BilateralFilter.KernelRadius, x saturate(8 roughness)"            \
      " (R/LumenReflections.cpp:248)")                                                             \
    X(LUMEN_REFLECTION_SPATIAL_SAMPLES, 4,                                                         \
      "samples", "r.Lumen.Reflections.BilateralFilter.NumSamples, doubled while disoccluded"       \
      " (R/LumenReflections.cpp:253)")                                                             \
    X(LUMEN_REFLECTION_SPATIAL_DEPTH_WEIGHT_SCALE, 10000.0f,                                       \
      "", "r.Lumen.Reflections.BilateralFilter.DepthWeightScale: exp2(-scale (plane distance"      \
      " / depth)^2) (R/LumenReflections.cpp:261)")                                                 \
    X(LUMEN_REFLECTION_SPATIAL_MAX_DISOCCLUSION_FRAMES, 2.0f,                                      \
      "frames", "r.Lumen.Reflections.BilateralFilter.MaxDisocclusionFrames"                        \
      " (R/LumenReflections.cpp:270)")                                                             \
    X(LUMEN_GLOBAL_SDF_RESOLUTION, 252,                                                            \
      "voxels", "voxels per axis of every level of the global distance field in Lumen views (distance only), as" \
      " UE's (R/LumenScene.cpp:69)")                                                               \
    X(LUMEN_GLOBAL_SDF_EXTENT, 50.0f,                                                              \
      "m", "extent of global distance field level 0 in Lumen views, doubling per level: 19.8 cm voxels to 25 m," \
      " UE's r.LumenScene.GlobalSDF.ClipmapExtent (R/LumenScene.cpp:77)")                         \
    X(LUMEN_SCENE_DIRECT_UPDATE_FACTOR, 32,                                                        \
      "", "r.LumenScene.DirectLighting.UpdateFactor at Epic: a frame relights the card tiles of a square of"  \
      " atlas / sqrt(this) texels (R/LumenSceneLighting.cpp:98-126; BaseScalability.ini:429)")     \
    X(LUMEN_SCENE_RADIOSITY_UPDATE_FACTOR, 64,                                                     \
      "", "r.LumenScene.Radiosity.UpdateFactor at Epic, the radiosity's budget the same way"         \
      " (BaseScalability.ini:430)")                                                                \
    X(LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE, 25.0f,                                               \
      "m", "a card page's update speed is 1 / (1 + distance / this): the first global SDF clipmap's extent" \
      " (S/LumenSceneLighting.usf:147; R/LumenSceneLighting.cpp:581)")                            \
    X(LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN, 5.0f,                                                   \
      "m", "a card page within this of the view frustum updates twice as fast"                      \
      " (MaxDistanceFromFrustumToPrioritize, S/LumenSceneLighting.usf:130-153)")                    \
    X(LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES, 2048,                                                 \
      "frames", "the age a never-lit card page ranks with (S/LumenSceneLighting.usf:169-173)")     \
    X(LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR, 1,                                                   \
      "px", "the short-range AO is searched per pixel at Epic (r.Lumen.ScreenProbeGather.ShortRangeAO." \
      "DownsampleFactor=1, INI [GlobalIlluminationQuality@3]; 2 = High's half resolution with a rotating" \
      " pixel per 2x2 block, S/LumenMaterial.ush:87-101)")                                         \
    X(LUMEN_SHORT_RANGE_AO_SLICE_COUNT, 2,                                                         \
      "slices", "view-space slices of the horizon search per pixel (r.Lumen.ScreenProbeGather.ShortRangeAO." \
      "HorizonSearch.SliceCount, R/LumenScreenSpaceBentNormal.cpp:105)")                           \
    X(LUMEN_SHORT_RANGE_AO_STEPS_PER_SLICE, 3,                                                     \
      "steps", "depth samples per slice direction, spaced quadratically (HorizonSearch.StepsPerSlice," \
      " R/LumenScreenSpaceBentNormal.cpp:113; S/LumenScreenSpaceBentNormal.usf:353-358)")          \
    X(LUMEN_SHORT_RANGE_AO_RADIUS_PROBE_TILES, 2.0f,                                               \
      "probe tiles", "search radius: twice the screen probe spacing (32 px), the detail the probe lattice cannot" \
      " resolve (R/LumenScreenProbeGather.cpp:2665)")                                              \
    X(LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_DISTANCE, 0.3f,                                       \
      "ratio", "samples this fraction of the pixel depth in front of it fade out of the horizon"   \
      " (HorizonSearch.ForegroundSampleRejectDistanceFraction, R/LumenScreenSpaceBentNormal.cpp:121)") \
    X(LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_POWER, 1.0f,                                          \
      "exponent", "shape of that fade (HorizonSearch.ForegroundSampleRejectPower, R/LumenScreenSpaceBentNormal.cpp:129)") \
    X(LUMEN_SHORT_RANGE_AO_MIN_VISIBILITY, 0.03f,                                                  \
      "ratio", "floor of the integrated visibility (S/LumenScreenSpaceBentNormal.usf:425)")        \
    X(LUMEN_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE, 1.0f,                                         \
      "std devs", "the AO history is clamped to the 3 x 3 neighbourhood mean +- this x its deviation" \
      " (ShortRangeAO.Temporal.NeighborhoodClampScale, R/LumenScreenSpaceBentNormal.cpp:34;"       \
      " S/LumenScreenProbeGatherTemporal.usf:234-284)")                                            \
    X(LUMEN_SHORT_RANGE_AO_MAX_MULTIBOUNCE_ALBEDO, 0.5f,                                           \
      "albedo", "cap of the albedo the AO multi-bounce fit uses (ShortRangeAO.MaxMultibounceAlbedo," \
      " R/LumenScreenSpaceBentNormal.cpp:56; DiffuseIndirectComposite.usf:195)")                   \
    X(LUMEN_SHORT_RANGE_AO_UPSAMPLE_DEPTH_WEIGHT, 5000.0f,                                         \
      "1 / ratio^2", "a half-resolution AO sample counts for a pixel by exp2(-this x (plane distance /" \
      " depth)^2) of the pixel it was searched from (S/StochasticLighting/StochasticLightingTileClassification" \
      ".usf:279-380 ComputeUpsampleWeights)")                                                      \
    X(LUMEN_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT, 0.01f,                                          \
      "weight", "below this summed weight no half-resolution sample reconstructs the pixel; the history" \
      " carries it (StochasticLightingTileClassification.usf:831)")
// clang-format on

namespace unravel::gi::lumen
{

/// The constants as typed constexpr values, generated from the one table above.
#define LUMEN_CONSTANT_EMIT(name, value, unit, why) inline constexpr auto name = value;
LUMEN_CONSTANTS_TABLE(LUMEN_CONSTANT_EMIT)
#undef LUMEN_CONSTANT_EMIT

/// One table row, as the parity test consumes it.
struct lumen_constant_row
{
    const char* name;
    double value;
};

/// Every constant with its numeric value, for enumeration by tests.
inline constexpr lumen_constant_row lumen_constant_rows[] = {
#define LUMEN_CONSTANT_ROW(name, value, unit, why) {#name, double(value)},
    LUMEN_CONSTANTS_TABLE(LUMEN_CONSTANT_ROW)
#undef LUMEN_CONSTANT_ROW
};

} // namespace unravel::gi::lumen
