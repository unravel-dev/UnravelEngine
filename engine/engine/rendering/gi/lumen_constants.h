#pragma once

/*
 * Single owner of the constants of the screen probe gather (lumen_gather_pass) and the GI passes around it.
 * Distances are in metres.
 * Each row is X(NAME, VALUE, unit, why); the why text says what the value controls and what changing it trades.
 *
 * The shader mirror is engine_data/data/shaders/lumen/lumen_constants.sh; the GI test suite parses it
 * and asserts every entry below matches and no shader-side LUMEN_* constant is missing here.
 */

// clang-format off
#define LUMEN_CONSTANTS_TABLE(X)                                                                   \
    X(LUMEN_PROBE_DOWNSAMPLE_FACTOR, 16,                                                           \
      "px", "screen probe spacing in pixels at the epic tier (high: 32), halved from final"        \
      " gather quality 6 and clamped so the probe atlas fits the largest texture"                  \
      " (lumen_pass::get_probe_downsample_factor, lumen_gather_pass); halving it quadruples the"   \
      " probes and their traces")                                                                  \
    X(LUMEN_PROBE_TRACE_RES, 8,                                                                    \
      "texels", "rays per probe axis at final gather quality 1: one ray per equal-area"            \
      " octahedral texel of an 8 x 8 map before importance sampling moves them; the quality"       \
      " scales it by sqrt(quality) to 4, 8 or 16 (lumen_pass::get_probe_trace_resolution), and"    \
      " the gather programs without a _res4 / _res16 suffix are compiled for it")                  \
    X(LUMEN_PROBE_JITTER_PERIOD, 8,                                                                \
      "frames", "period of the probe jitter: one screen-wide placement offset per frame,"          \
      " hammersley16(frame % 8, 8) (lumen_gather_pass), and the per-tile ray jitter's blue-noise"  \
      " slice frame % 8 (lumen_common.sh); a divisor of lumen_pass::frame_index_period")           \
    X(LUMEN_PROBE_MAX_VIEW_EXTENT, 16384,                                                          \
      "px", "largest view axis the gather serves: a probe record carries its pixel as the bits"    \
      " x | y << 15 | 1 << 30 of a float, a normal float the RGBA32F record stores bit-exact"      \
      " while y stays below 32512 (lumen_common.sh); the gather skips a larger view with a"        \
      " warning")                                                                                  \
    X(LUMEN_ADAPTIVE_SAMPLES_X, 4,                                                                 \
      "samples", "adaptive probe candidates per uniform tile along x at the epic tier: 4 x 2 = 8"  \
      " per tile (high: 4 x 4, lumen_pass::get_adaptive_probe_layout); more candidates catch"      \
      " more of the pixels the uniform probes cannot interpolate, at more placement tests per"     \
      " tile")                                                                                     \
    X(LUMEN_ADAPTIVE_SAMPLES_Y, 2,                                                                 \
      "samples", "the candidates along y at the epic tier (high: 4)")                              \
    X(LUMEN_ADAPTIVE_ALLOCATION_FRACTION, 0.5f,                                                    \
      "", "adaptive probe capacity over the uniform probe count: at most trunc(uniform x 0.5)"     \
      " adaptive probes, in probe atlas rows below the uniform ones"                               \
      " (lumen_adaptive_probes::get_capacity); probes spawned past it are dropped, and a larger"   \
      " share grows the atlas")                                                                    \
    X(LUMEN_MAX_RAY_INTENSITY, 10.0f,                                                              \
      "pre-exposed", "per-ray clamp of max3(radiance) before compositing into the probe: lower"    \
      " cuts the noise of rare bright rays and the energy they carry; it also sets the"            \
      " composite's fixed point, where all of a probe's rays at the clamp sum to exactly"          \
      " 2^32 - 1")                                                                                 \
    X(LUMEN_SCREEN_TRACE_MAX_ITERATIONS, 50,                                                       \
      "steps", "Hi-Z iterations per screen ray of the probe and reflection traces; a trace that"   \
      " spends them ends uncertain, neither a hit nor a miss, and the distance field resumes"      \
      " from the last point it proved free")                                                       \
    X(LUMEN_SCREEN_TRACE_RELATIVE_THICKNESS, 0.02f,                                                \
      "ratio", "a probe screen ray's crossing is a hit when the ray lies at most this share of"    \
      " the surface's linear depth behind the depth buffer; deeper, it passed behind the surface"  \
      " and rewinds to the last point it proved free (lumen_screen_trace.sh)")                     \
    X(LUMEN_SCREEN_TRACE_HISTORY_DEPTH_TEST, 0.005f,                                               \
      "device z", "a probe screen hit reads last frame's colour only where last frame's device"    \
      " depth at its reprojection agrees within this x lerp(0.5, 2, noise); a hit that fails"      \
      " (occluded or newly revealed) goes on to the distance field")                               \
    X(LUMEN_SCREEN_TRACE_MISS_OFFSET, 0.02f,                                                       \
      "m", "a screen trace that ends without a crossing (off screen or at its end) vouches for"    \
      " its distance + 2 cm; the next stage resumes there less its pullback:"                      \
      " LUMEN_TRACE_RESUME_PULLBACK for the probes, LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS"     \
      " for the reflections")                                                                      \
    X(LUMEN_TRACE_RESUME_PULLBACK, 0.1f,                                                           \
      "m", "each tracing stage of the screen probes resumes 10 cm (2 x LUMEN_SURFACE_BIAS)"        \
      " before the distance the previous stage vouched for, so consecutive stages overlap")        \
    X(LUMEN_SURFACE_BIAS, 0.05f,                                                                   \
      "m", "distance-field stage origin offset: the probe position moves this far along the ray"   \
      " and along the normal (the reflections: along the ray only), off the surface it sits on")   \
    X(LUMEN_MAX_TRACE_DISTANCE, 200.0f,                                                            \
      "m", "the default of gi_settings::diffuse_settings::max_trace_distance, the farthest a GI"   \
      " ray travels (diffuse and reflections); the outermost global SDF level reaches as far"      \
      " from the camera")                                                                          \
    X(LUMEN_FILTER_PASSES, 3,                                                                      \
      "passes", "spatial filter passes over the probe radiance, ping-ponging between atlases;"     \
      " each pass mixes a probe with its uniform neighbours, so more passes blur wider and fewer"  \
      " keep more noise; the last pass writes the filtered radiance, next frame's history")        \
    X(LUMEN_FILTER_POSITION_WEIGHT_SCALE, 1000.0f,                                                 \
      "", "spatial filter neighbour weight exp2(-scale (dz / z)^2) on the probes' view depths:"    \
      " half weight at a 3.2% relative depth difference")                                          \
    X(LUMEN_FILTER_MAX_HIT_ANGLE_DEGREES, 10.0f,                                                   \
      "deg", "spatial filter angle weight 1 - angle / this between a texel's ray and the"          \
      " direction from the probe to the neighbour's hit point (its distance clamped to the"        \
      " probe's own, which keeps contact shadows): a neighbour that saw a point 10 degrees or"     \
      " more off is dropped")                                                                      \
    X(LUMEN_FILTER_MOVING_THRESHOLD, 0.01f,                                                        \
      "", "a probe whose moving fraction (the share of its rays that hit a moving surface)"        \
      " exceeds it filters with 8 more neighbours and no angle weight, as the temporal will"       \
      " shorten its history")                                                                      \
    X(LUMEN_INTERP_DEPTH_WEIGHT, 10000.0f,                                                         \
      "", "probe interpolation weight exp2(-w r^2), r = the probe's distance from the pixel's"     \
      " tangent plane / the pixel's depth: half weight at 1% of the depth")                        \
    X(LUMEN_INTERP_FALLBACK_DEPTH_WEIGHT, 1000.0f,                                                 \
      "", "the looser weight (half at 3.2% of the depth) used when the primary weights of a"       \
      " pixel's probes sum below LUMEN_INTERP_MIN_WEIGHT")                                         \
    X(LUMEN_INTERP_MIN_WEIGHT, 0.01f,                                                              \
      "", "smallest weight that counts: adaptive probes go where the uniform probes' weights for"  \
      " a pixel sum below it, a uniform corner below it takes its tile's best adaptive probe, a"   \
      " pixel whose weights still sum below it takes the fallback weights, and the jitter and"     \
      " footprint plane tests compare against it")                                                 \
    X(LUMEN_FULL_RES_JITTER_WIDTH, 1.0f,                                                           \
      "tiles", "the integrate's interpolation jitter in probe tiles, halved from final gather"     \
      " quality 4 (lumen_pass::get_full_res_jitter_width): it turns the probe lattice into noise"  \
      " the temporal averages away")                                                               \
    X(LUMEN_FULL_RES_JITTER_NEAR, 5.0f,                                                            \
      "m", "view depth from which the jitter width shrinks, reaching"                              \
      " LUMEN_FULL_RES_JITTER_FAR_SCALE at this + LUMEN_FULL_RES_JITTER_RAMP")                     \
    X(LUMEN_FULL_RES_JITTER_PLANE_WEIGHT, 1000000.0f,                                              \
      "", "a jittered pixel is used only if exp2(-w r^2) > LUMEN_INTERP_MIN_WEIGHT, r = its"       \
      " distance from the pixel's plane / depth: within about 0.26% of the depth")                 \
    X(LUMEN_TEMPORAL_MAX_FRAMES, 10,                                                               \
      "frames", "frames the gather's temporal accumulates at most at final gather update"          \
      " speed 1; lumen_pass::get_temporal_max_frames divides it by sqrt(speed), the speed in"      \
      " [0.5, 8], and halves it while the scene is edited; more frames average more noise away"    \
      " and react slower")                                                                         \
    X(LUMEN_TEMPORAL_DEPTH_THRESHOLD, 0.01f,                                                       \
      "ratio", "a history tap is kept when its depth lies within 1% x lerp(0.5, 1.5, blue noise)"  \
      " / lerp(0.1, 1, NoV) of the reprojected depth (lumen_history.sh)")                          \
    X(LUMEN_TEMPORAL_MIN_TAP_WEIGHT, 0.01f,                                                        \
      "", "a history tap with a smaller bilinear weight is ignored")                               \
    X(LUMEN_MIN_TRACE_DISTANCE, 0.0001f,                                                           \
      "m", "0.01 cm: the distance-field stage starts at max(this, the screen's distance -"         \
      " LUMEN_TRACE_RESUME_PULLBACK), so its start stays positive")                                \
    X(LUMEN_GLOBAL_SDF_EXPAND_VOXELS, 0.5f,                                                        \
      "voxels", "surface expansion of the global SDF march, in voxels of the level answering a"    \
      " sample: a sample this close to a surface is a hit, pulled back by the expansion; larger"   \
      " errs toward occlusion, smaller lets rays pass closer to surfaces (lumen_global_sdf.sh)")   \
    X(LUMEN_GLOBAL_SDF_EXPAND_RAMP_VOXELS, 1.0f,                                                   \
      "voxels", "the expansion ramps in from zero over this many voxels of the ray's travel from"  \
      " its biased start (diffuse rays) or of the largest distance it has kept from any surface"   \
      " (radiance cache and reflection rays), so a ray leaving a surface at a grazing angle does"  \
      " not hit it at once")                                                                       \
    X(LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS, 0.5f,                                                      \
      "voxels", "step floor of the march in voxels of the answering level (x"                      \
      " LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE where only two-sided meshes are near): it"     \
      " bounds the steps a ray spends grazing a surface")                                          \
    X(LUMEN_GLOBAL_SDF_MAX_STEPS, 256,                                                             \
      "steps", "march budget per clipmap level; a ray that spends it continues where it leaves"    \
      " the level, in the next one, and past the last level it is a miss; more steps cross"        \
      " cluttered space farther at more cost per ray")                                             \
    X(LUMEN_GLOBAL_SDF_LEVEL_EXIT_VOXELS, 0.05f,                                                   \
      "voxels", "a ray that spent a level's budget resumes this far past the level's box, in the next level") \
    X(LUMEN_GLOBAL_SDF_COVERED_EXPAND_SCALE, 1.0f,                                                 \
      "", "scale of the surface expansion where a one-sided mesh is near (coverage 1); the march"  \
      " blends to LUMEN_GLOBAL_SDF_NOT_COVERED_EXPAND_SCALE as the coverage falls")                \
    X(LUMEN_GLOBAL_SDF_NOT_COVERED_EXPAND_SCALE, 0.6f,                                             \
      "", "scale of the surface expansion where only two-sided meshes are near (coverage 0):"      \
      " foliage and curtains stay thinner to the march, and smaller lets more light through them") \
    X(LUMEN_GLOBAL_SDF_NOT_COVERED_MIN_STEP_SCALE, 4.0f,                                           \
      "", "the min step grows by lerp(this, 1, coverage) where only two-sided meshes are near;"    \
      " with LUMEN_GLOBAL_SDF_MIN_STEP_VOXELS it sets the 2-voxel distance within which the"       \
      " march reads the coverage")                                                                 \
    X(LUMEN_GLOBAL_SDF_DITHER_STEP_THRESHOLD, 0.5f,                                                \
      "", "dithered tracers (screen probes, radiance cache) count a sample under the expansion"    \
      " as solid only when its step noise x (1 - coverage) is at most this: in uncovered space"    \
      " each such step hits with probability 0.5")                                                 \
    X(LUMEN_GLOBAL_SDF_DITHER_TRACE_THRESHOLD, 0.9f,                                               \
      "", "the per-trace test beside it: trace noise x (1 - coverage) at most this, so 10% of"     \
      " the traces pass through uncovered space entirely")                                         \
    X(LUMEN_GLOBAL_SDF_COVERAGE_BAND_VOXELS, 4.0f,                                                 \
      "voxels", "a voxel's coverage is 0 where every mesh within this many voxels of it is"        \
      " two-sided, 1 elsewhere; the partial updates recompose at least this far around an"         \
      " instance that moves, appears or leaves (global_sdf_clipmap::compute_level_influence)")     \
    X(LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS, 0.2f,                                                    \
      "m", "an object whose world bounding sphere is no larger than max(this,"                     \
      " LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS_VOXELS voxels) is left out of a global SDF level and"   \
      " its object grid, unless an emissive light source; both floors scale by 1 / the scene"      \
      " detail (global_sdf_clipmap::settings::object_radius_scale)")                               \
    X(LUMEN_GLOBAL_SDF_MIN_OBJECT_RADIUS_VOXELS, 0.5f,                                             \
      "voxels", "the same floor in voxels of each level, the larger of the two applying: coarse"   \
      " levels leave out objects no larger than half their voxel")                                 \
    X(LUMEN_GLOBAL_SDF_FLAT_GRADIENT, 0.00001f,                                                    \
      "m", "a hit's central-difference gradient shorter than this falls back to the reversed ray") \
    X(LUMEN_SDF_SELF_LIGHTING_FADE_START, 0.75f,                                                   \
      "voxels", "a distance-field hit's radiance x smoothstep(0.75, 1 voxel, hit distance):"       \
      " black within 0.75 voxel of the march origin, where the ray can find the surface it left")  \
    X(LUMEN_SDF_SELF_LIGHTING_FADE_END, 1.0f,                                                      \
      "voxels", "the fade completes at one voxel of travel")                                       \
    X(LUMEN_SCREEN_TRACE_BIAS_TEXELS, 2.0f,                                                        \
      "half px", "screen-trace origin lifted along the probe normal by twice the world size of"    \
      " half a pixel at its depth, out of its own depth texel's reach (lumen_screen_trace.sh)")    \
    X(LUMEN_SCREEN_TRACE_NEAR_STOP, 0.99f,                                                         \
      "ratio", "a screen ray toward the camera ends after this share of the way to the camera"     \
      " plane, at 1% of its origin's view depth")                                                  \
    X(LUMEN_SCREEN_TRACE_VIGNETTE_SCALE, 5.0f,                                                     \
      "", "screen hit kept with probability min(V(hit), V(history)), V(s) = saturate(1 - |v|^2),"  \
      " v = saturate(5 |s| - 4), s = uv x 2 - 1: the hits fade out over the outer 10% of the"      \
      " screen at each edge")                                                                      \
    X(LUMEN_SCREEN_TRACE_VIGNETTE_OFFSET, 4.0f,                                                    \
      "", "the vignette's offset: the fade starts at |s| = offset / scale = 0.8")                  \
    X(LUMEN_SCREEN_TRACE_HISTORY_NOISE_MIN, 0.5f,                                                  \
      "ratio", "the screen traces' history depth tolerance scales by lerp(this,"                   \
      " LUMEN_SCREEN_TRACE_HISTORY_NOISE_MAX, interleaved gradient noise)")                        \
    X(LUMEN_SCREEN_TRACE_HISTORY_NOISE_MAX, 2.0f,                                                  \
      "ratio", "upper end of that scale")                                                          \
    X(LUMEN_COMPOSITE_DITHER_SLICE_OFFSET, 31,                                                     \
      "slices", "experiment_jitter_slice_dither: the composite re-bins with ray-jitter slice frame % 8 + 31") \
    X(LUMEN_SH_TEXELS_PER_PROBE, 7,                                                                \
      "texels", "SH3 storage per probe in RGBA16F texels: c0 rgb with the probe's moving"          \
      " fraction, then (c1..c4), (c5..c8) per colour channel: 1 + 3 x 2 (cs_lumen_probe_sh.sc)")   \
    X(LUMEN_FULL_RES_JITTER_FAR_SCALE, 0.5f,                                                       \
      "ratio", "the full-resolution jitter width scales by lerp(1, this, saturate((depth -"        \
      " LUMEN_FULL_RES_JITTER_NEAR) / LUMEN_FULL_RES_JITTER_RAMP))")                               \
    X(LUMEN_FULL_RES_JITTER_RAMP, 5.0f,                                                            \
      "m", "depth range over which the jitter width falls to LUMEN_FULL_RES_JITTER_FAR_SCALE")     \
    X(LUMEN_INTEGRATE_NOISE_PERIOD, 64,                                                            \
      "frames", "period of the per-pixel blue noise of the history depth test and the"             \
      " short-range AO's hash noise: frame % 64, a divisor of lumen_pass::frame_index_period")     \
    X(LUMEN_TEMPORAL_DEPTH_NOISE_MIN, 0.5f,                                                        \
      "ratio", "the history depth threshold scales by lerp(this, LUMEN_TEMPORAL_DEPTH_NOISE_MAX,"  \
      " blue noise)")                                                                              \
    X(LUMEN_TEMPORAL_DEPTH_NOISE_MAX, 1.5f,                                                        \
      "ratio", "upper end of that scale")                                                          \
    X(LUMEN_TEMPORAL_MIN_NOV, 0.1f,                                                                \
      "cos", "history depth threshold / lerp(0.1, 1, NoV): up to 10x wider at grazing angles")     \
    X(LUMEN_TEMPORAL_COUNT_LEVELS, 15,                                                             \
      "levels", "the gather history's alpha stores the frame count and the fast update amount in"  \
      " 16 levels each, count + 16 x amount: an integer below 256, exact in the RGBA16F history;"  \
      " N is quantized to multiples of the maximum / 15 (lumen_common.sh)")                        \
    X(LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT, 4.0f,                                                 \
      "", "blend 1 / (1 + 4 N) when this frame has no valid estimate (no probe interpolates the"   \
      " pixel, no AO sample reconstructs it): the history carries the pixel")                      \
    X(LUMEN_TEMPORAL_HISTORY_GUARD, 0.53125f,                                                      \
      "px", "history taps are clamped 0.5 + 1/32 px inside last frame's view, so the 2 x 2"        \
      " bilinear footprint stays inside it")                                                       \
    X(LUMEN_TEMPORAL_MOVING_RELATIVE_SPEED, 0.005f,                                                \
      "ratio", "a hit marks its ray moving when its surface moved against the probe this frame"    \
      " by more than this share of max(probe depth, LUMEN_TEMPORAL_MOVING_MIN_DEPTH)"              \
      " (distance-field hits: the hit instance's own motion); moving rays shorten the probe's"     \
      " history (the fast update)")                                                                \
    X(LUMEN_TEMPORAL_MOVING_MIN_DEPTH, 1.0f,                                                       \
      "m", "the probe depth floor of that ratio: closer probes compare motion against 1 m")        \
    X(LUMEN_TEMPORAL_FAST_UPDATE_MOVING_FRACTION, 0.1f,                                            \
      "", "share of a pixel's interpolated probe lighting that is moving at which the fast"        \
      " update is full: amount = saturate(moving / this), then rescaled by"                        \
      " LUMEN_TEMPORAL_FAST_UPDATE_THRESHOLD")                                                     \
    X(LUMEN_TEMPORAL_FAST_UPDATE_THRESHOLD, 0.2f,                                                  \
      "", "fast update amount = saturate((moving / fraction - 0.2) / 0.8): no fast update below"   \
      " a fifth of the fraction")                                                                  \
    X(LUMEN_TEMPORAL_FAST_UPDATE_MAX_AMOUNT, 0.9f,                                                 \
      "", "fast update cap: the frame count is limited to (1 - amount) x the maximum, so at"       \
      " least a tenth of the maximum frames stays; a pixel holds at least last frame's amount,"    \
      " up to this cap")                                                                           \
    X(LUMEN_RADIANCE_CACHE_CLIPMAPS, 4,                                                            \
      "clipmaps", "radiance cache clipmaps, camera-centred, each twice the previous (25 to 200 m"  \
      " from the camera); a trace record packs the clipmap in 2 bits, so 4 at most"                \
      " (lumen_radiance_cache_common.sh)")                                                         \
    X(LUMEN_RADIANCE_CACHE_GRID, 48,                                                               \
      "cells", "probe cells per clipmap axis; a trace record packs each cell coordinate in 6"      \
      " bits (64 at most); more cells place probes closer, at more probes and traces")             \
    X(LUMEN_RADIANCE_CACHE_EXTENT, 25.0f,                                                          \
      "m", "clipmap 0 half extent: cell 0 = 2 x 25 m / 48 = 1.04 m, doubling per clipmap to"       \
      " 8.3 m cells reaching 200 m")                                                               \
    X(LUMEN_RADIANCE_CACHE_PROBE_RES, 32,                                                          \
      "texels", "equal-area octahedral radiance texels per probe axis at the epic tier (high:"     \
      " 16, lumen_pass::get_radiance_cache_probe_resolution); a multiple of 16, so a downsampled"  \
      " probe's (R / 2)^2 rays fill whole 8 x 8 trace tiles; the atlases grow with its square")    \
    X(LUMEN_RADIANCE_CACHE_ATLAS_PROBES_X, 128,                                                    \
      "probes", "probe atlas width in probe tiles, the pool filling 64 rows: 4096 radiance"        \
      " texels wide at 32 texels per probe")                                                       \
    X(LUMEN_RADIANCE_CACHE_MAX_PROBES, 8192,                                                       \
      "probes", "probe pool: 128 x 64 tiles of the two RGBA16F probe atlases, about 136 MiB at"    \
      " 32 x 32 texels; a frame that needs more probes leaves the extra cells without one (they"   \
      " read black)")                                                                              \
    X(LUMEN_RADIANCE_CACHE_TRACE_BUDGET, 150,                                                      \
      "probes", "probes re-traced per frame beyond the new ones at the epic tier (high: 100),"     \
      " times the final gather update speed in [0.5, 4] and ten times as many while the scene is"  \
      " edited (lumen_pass::get_radiance_cache_trace_budget)")                                     \
    X(LUMEN_RADIANCE_CACHE_MAX_TRACES, 2752,                                                       \
      "probes", "per-frame trace cap while the cache continues, new probes included (probes past"  \
      " it wait for a later frame); a frame that rebuilds the cache traces the whole pool;"        \
      " higher refreshes more probes in a frame at a higher worst-case frame cost")                \
    X(LUMEN_RADIANCE_CACHE_KEEP_FRAMES, 8,                                                         \
      "frames", "an unmarked probe keeps its slot and texels this many frames after its last"      \
      " use, so a view that turns back finds it traced; longer holds more of the pool")            \
    X(LUMEN_RADIANCE_CACHE_DOWNSAMPLE_DISTANCE, 40.0f,                                             \
      "m", "probes this far from the camera trace a quarter of the rays"                           \
      " (LUMEN_RADIANCE_CACHE_COST_DOWNSAMPLED), each filling 2 x 2 texels")                       \
    X(LUMEN_RADIANCE_CACHE_COST_NORMAL, 4,                                                         \
      "units", "trace cost of a full-resolution probe: R x R rays in units of (R / 2)^2; the"      \
      " per-frame cost budget is the trace budget x this"                                          \
      " (lumen_radiance_cache::get_trace_cost_budget)")                                            \
    X(LUMEN_RADIANCE_CACHE_COST_DOWNSAMPLED, 1,                                                    \
      "units", "trace cost of a downsampled probe: its (R / 2)^2 rays, a quarter of a full one")   \
    X(LUMEN_RADIANCE_CACHE_PRIORITY_BUCKETS, 16,                                                   \
      "buckets", "trace priority histogram: bucket 0 = never traced, then 15 - log2(frames since"  \
      " the last trace / (clipmap + 1)), 15 = traced since its last use; the budget admits the"    \
      " buckets most urgent first")                                                                \
    X(LUMEN_RADIANCE_CACHE_REPROJECTION_RADIUS, 1.5f,                                              \
      "x TMin", "parallax sphere radius of the hand-off lookup: each of the eight probes is read"  \
      " in the direction from itself to where the ray leaves this sphere around it"                \
      " (lumen_radiance_cache_sample.sh)")                                                         \
    X(LUMEN_RADIANCE_CACHE_FILTER_MAX_ANGLE, 0.2f,                                                 \
      "rad", "probe neighbour filter angle weight 1 - angle / 0.2 rad between a texel's"           \
      " direction and the direction from the probe to the neighbour's hit: only the angularly"     \
      " consistent far field is shared")                                                           \
    X(LUMEN_RADIANCE_CACHE_NO_HIT, 65504.0f,                                                       \
      "m", "hit distance a probe ray stores when it leaves the scene: the largest finite"          \
      " float16, exact in the RGBA16F atlas alpha; the filter does not clamp it to the probe's"    \
      " own distance")                                                                             \
    X(LUMEN_FLOAT16_MAX, 65504.0f,                                                                 \
      "", "largest finite float16: radiance stored in RGBA16F is clamped below it")                \
    X(LUMEN_OBJECT_GRID_MAX_ID, 65535,                                                             \
      "", "object grid cells hold instance index + 1 as 16-bit unorm: ids up to this are exact")   \
    X(LUMEN_IS_MIN_PDF_TO_TRACE, 0.1f,                                                             \
      "", "texels whose BRDF PDF reaches this keep a ray PDF of at least this; after the rank"     \
      " sort every three lowest texels below it go to the highest remaining one, which traces"     \
      " four rays at the 2N x 2N level instead of one (cs_lumen_probe_generate_rays.sc)")          \
    X(LUMEN_IS_HISTORY_MIN_WEIGHT, 0.1f,                                                           \
      "", "a history probe feeds the lighting PDF only when exp2(-LUMEN_INTERP_DEPTH_WEIGHT r^2)"  \
      " passes this, r = its distance from the probe's plane / depth")                             \
    X(LUMEN_IS_MIN_LIGHTING_SUM, 0.0001f,                                                          \
      "", "the lighting PDF is normalised by max(sum, this), so an unlit probe's PDF stays"        \
      " finite")                                                                                   \
    X(LUMEN_IS_DISOCCLUSION_MAX_FRAMES, 4,                                                         \
      "frames", "a footprint pixel with fewer frames of history is young")                         \
    X(LUMEN_IS_DISOCCLUSION_FRACTION, 0.4f,                                                        \
      "", "a probe is disoccluded when this share of its footprint is young; its spatial filter"   \
      " then drops the angle weight, blurring revealed content more while it has no history")      \
    X(LUMEN_MAX_ROUGHNESS_TO_TRACE, 0.4f,                                                          \
      "roughness", "pixels below this roughness trace reflection rays: the default of"             \
      " gi_settings::reflection_settings::max_roughness_to_trace; rougher pixels read the"         \
      " gather's rough specular")                                                                  \
    X(LUMEN_ROUGHNESS_FADE_LENGTH, 0.1f,                                                           \
      "roughness", "the traced reflections' weight fades from 1 to 0 over this band below the"     \
      " trace roughness, into the rough specular")                                                 \
    X(LUMEN_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR, 0.8f,                                        \
      "roughness", "from this roughness the rough specular is the diffuse E / pi instead of GGX"   \
      " samples of the probes; higher samples wider lobes too, at their cost")                     \
    X(LUMEN_ROUGH_SPECULAR_FADE_LENGTH, 0.2f,                                                      \
      "roughness", "the rough specular fades into E / pi over this band below"                     \
      " LUMEN_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR")                                           \
    X(LUMEN_ROUGH_SPECULAR_MIN_ROUGHNESS, 0.2f,                                                    \
      "roughness", "roughness floor of the rough specular's GGX lobe: smoother pixels sample the"  \
      " probes' radiance with this roughness")                                                     \
    X(LUMEN_ROUGH_SPECULAR_SAMPLES, 4,                                                             \
      "samples", "GGX visible-normal samples of the probes' radiance per pixel (Hammersley"        \
      " points, per-pixel seed), averaged in x / (1 + Y); each sample reads the pixel's probes"    \
      " again")                                                                                    \
    X(LUMEN_SPECULAR_SAMPLE_BIAS, 0.1f,                                                            \
      "", "the rough specular's polar sample E.y is squeezed toward 0.5 by this share, into"       \
      " [0.05, 0.95]: the lobe's peak and widest tail each lose 5% of the polar range")            \
    X(LUMEN_PROBE_RADIANCE_BORDER, 1,                                                              \
      "texels", "border around each probe's filtered radiance, wrapped across the octahedron's"    \
      " folds so a hardware bilinear lookup stays inside the probe's tile (one mip,"               \
      " cs_lumen_probe_border.sc)")                                                                \
    X(LUMEN_REFLECTION_MAX_RAY_INTENSITY, 40.0f,                                                   \
      "pre-exposed", "per-ray clamp of max3(radiance) of the reflection traces: lower cuts the"    \
      " noise of rare bright rays and the energy of bright reflected sources")                     \
    X(LUMEN_REFLECTION_GGX_SAMPLING_BIAS, 0.1f,                                                    \
      "", "a reflection ray's polar sample E.y is scaled by 1 - this, which drops the widest 10%"  \
      " of the lobe's polar range (gi_reflection_sampling.sh)")                                    \
    X(LUMEN_REFLECTION_MIRROR_ROUGHNESS, 0.001f,                                                   \
      "roughness", "below it the ray is the mirror direction (cone"                                \
      " LUMEN_REFLECTION_MIN_CONE_ANGLE) instead of a GGX sample")                                 \
    X(LUMEN_REFLECTION_MIN_PDF, 0.0001f,                                                           \
      "", "cone angle = 1 / max(pdf, this): finite where the pdf vanishes")                        \
    X(LUMEN_REFLECTION_MIN_CONE_ANGLE, 0.0001f,                                                    \
      "rad", "the cone angle of a mirror ray and the floor of every ray's cone: positive and a"    \
      " normal float16 (the smallest is 6.1e-5), so the RGBA16F ray texture never flushes it to"   \
      " the 0 that marks a pixel without a ray")                                                   \
    X(LUMEN_REFLECTION_SCREEN_TRACE_RELATIVE_THICKNESS, 0.005f,                                    \
      "ratio", "a reflection screen crossing is a hit when the ray lies at most this share of"     \
      " the surface's linear depth behind the depth buffer, a quarter of the probe traces'"        \
      " LUMEN_SCREEN_TRACE_RELATIVE_THICKNESS")                                                    \
    X(LUMEN_REFLECTION_HISTORY_DEPTH_TEST, 0.005f,                                                 \
      "device z", "a reflection screen hit reads last frame's colour only where last frame's"      \
      " device depth at its reprojection agrees within this x lerp(0.5, 2, noise)")                \
    X(LUMEN_REFLECTION_SCENE_COLOR_RELATIVE_DEPTH, 0.01f,                                          \
      "ratio", "a distance-field hit reads last frame's colour instead of the surface cache when"  \
      " it lies within this share of the depth buffer's depth and faces the camera"                \
      " (LUMEN_REFLECTION_SCENE_COLOR_NORMAL_COS); also that read's history depth tolerance")      \
    X(LUMEN_REFLECTION_SCENE_COLOR_NORMAL_COS, 0.0871557f,                                         \
      "cos", "cos 85 deg: that read needs the hit to face the camera within 85 degrees")           \
    X(LUMEN_REFLECTION_SDF_PULLBACK_HALF_VOXELS, 4.0f,                                             \
      "half voxels", "the reflections' distance-field stage resumes this far (2 voxels of the"     \
      " level where the screen trace ended) before the distance the screen vouched for, so the"    \
      " field's surface expansion starts outside the surface")                                     \
    X(LUMEN_REFLECTION_SDF_STEP_DITHER, 0.95f,                                                     \
      "", "near-mirror rays step by lerp(this, 1 / this, interleaved gradient noise) of the"       \
      " distance, against stepping artefacts")                                                     \
    X(LUMEN_REFLECTION_SDF_STEP_DITHER_CONE, 0.0122718463f,                                        \
      "rad", "pi / 256: rays whose cone reaches this step undithered; narrower cones dither"       \
      " more, fully at 0")                                                                         \
    X(LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE, 10.0f,                                              \
      "pre-exposed", "the reflection resolve and denoisers average L / (1 + Y / range),"           \
      " Y = luminance: lower compresses bright samples harder, fewer fireflies and darker"         \
      " highlights")                                                                               \
    X(LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES, 5,                                                  \
      "samples", "neighbouring rays the reflection resolve reuses per pixel at the epic tier"      \
      " (high: 3), scaled by the reflection quality, never below it nor above 64"                  \
      " (lumen_pass::get_reflection_reconstruction_samples)")                                      \
    X(LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS, 8.0f,                                         \
      "px", "reconstruction disk radius: the trace downsample factor x this x"                     \
      " saturate(8 roughness) pixels; skipped where no wider than a traced block")                 \
    X(LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS, 0.05f,                                        \
      "roughness", "pixels at or below this roughness skip the neighbour reuse")                   \
    X(LUMEN_REFLECTION_TEMPORAL_MAX_FRAMES, 12.0f,                                                 \
      "frames", "frames the reflection temporal accumulates at most: N = min(N x"                  \
      " (0.75 confidence + 0.25) + 1, this); more average more noise and lag lighting changes"     \
      " longer")                                                                                   \
    X(LUMEN_REFLECTION_TEMPORAL_MIRROR_FRAMES, 2.0f,                                               \
      "frames", "mirror pixels accumulate lerp(this, max, roughness / 0.05) frames while the"      \
      " traces run at full resolution: mirrors keep few frames and leave their noise to TAA")      \
    X(LUMEN_REFLECTION_TEMPORAL_DISTANCE_THRESHOLD, 0.03f,                                         \
      "ratio", "a surface history tap is kept when last frame's depth there is within this x"      \
      " U(0.5, 1.5) / clamp(NoV, 0.1, 1) of the reprojected depth; farther taps are disoccluded")  \
    X(LUMEN_REFLECTION_NEIGHBORHOOD_CLAMP_SCALE, 1.0f,                                             \
      "sigma", "both reflection histories are clamped to the 5 x 5 neighbourhood mean +- this x"   \
      " its standard deviation in YCoCg; the clamp's distance lowers the confidence and so the"    \
      " frame count")                                                                              \
    X(LUMEN_REFLECTION_RECONSTRUCTED_CLAMP_SCALE, 2.0f,                                            \
      "", "pixels the downsampled traces skip widen the history clamp by this: their"              \
      " neighbourhood has no traced centre")                                                       \
    X(LUMEN_REFLECTION_RECONSTRUCTED_HISTORY_FRAMES, 4.0f,                                         \
      "frames", "pixels the downsampled traces skip blend 1 / (N + this) of this frame, leaning"   \
      " on the history, which converges to the upsampled result")                                  \
    X(LUMEN_REFLECTION_SPATIAL_KERNEL_RADIUS, 8.0f,                                                \
      "px", "bilateral filter disk radius: this x saturate(8 roughness) pixels; the filter runs"   \
      " only where the temporal variance is high or the history is young")                         \
    X(LUMEN_REFLECTION_SPATIAL_SAMPLES, 4,                                                         \
      "samples", "disk taps of the reflections' spatial filter, doubled while the history is"      \
      " younger than LUMEN_REFLECTION_SPATIAL_MAX_DISOCCLUSION_FRAMES")                            \
    X(LUMEN_REFLECTION_SPATIAL_DEPTH_WEIGHT_SCALE, 10000.0f,                                       \
      "", "spatial filter tap weight exp2(-scale (plane distance / depth)^2): half weight at 1%"   \
      " of the depth")                                                                             \
    X(LUMEN_REFLECTION_SPATIAL_MAX_DISOCCLUSION_FRAMES, 2.0f,                                      \
      "frames", "while the history is younger than this the spatial filter doubles its taps,"      \
      " widens the normal lobe 4x, drops the luminance stop and tonemaps hard against fireflies")  \
    X(LUMEN_GLOBAL_SDF_RESOLUTION, 252,                                                            \
      "voxels", "voxels per axis of every level of the GI's global distance field (distance"       \
      " only, R8): 4 levels x 252^3 bytes, about 61 MiB; even, so the half-resolution coverage"    \
      " and the 2 x 2 x 2-voxel object grid cells divide it exactly")                              \
    X(LUMEN_GLOBAL_SDF_EXTENT, 50.0f,                                                              \
      "m", "extent of the GI's global distance field level 0, doubling per level: 19.8 cm voxels"  \
      " reaching 25 m from the camera at level 0, 200 m at the outermost")                         \
    X(LUMEN_SCENE_DIRECT_UPDATE_FACTOR, 32,                                                        \
      "", "card direct lighting update factor at the epic tier (high: 64): a frame relights the"   \
      " card tiles of a square of atlas / sqrt(this / the lighting update speed) texels on a"      \
      " side, about 1/32 of the atlas at speed 1 (lumen_scene::compute_lighting_tile_budget)")     \
    X(LUMEN_SCENE_RADIOSITY_UPDATE_FACTOR, 64,                                                     \
      "", "the radiosity's update factor the same way at the epic tier (high: 128): about 1/64"    \
      " of the atlas per frame at speed 1")                                                        \
    X(LUMEN_SCENE_LIGHTING_PRIORITY_DISTANCE, 25.0f,                                               \
      "m", "a card page's update speed is 1 / (1 + its distance to the nearest viewer / this):"    \
      " half speed 25 m out, the half extent of global SDF level 0")                               \
    X(LUMEN_SCENE_LIGHTING_FRUSTUM_MARGIN, 5.0f,                                                   \
      "m", "a card page within this of any viewer's frustum updates twice as fast")                \
    X(LUMEN_SCENE_LIGHTING_NEVER_LIT_FRAMES, 2048,                                                 \
      "frames", "the age a never-lit card page ranks with in the lighting buckets, 15 -"           \
      " log2(4 x age x speed): bucket 2 of 16 at speed 1; a larger age ranks new pages ahead of"   \
      " more overdue ones")                                                                        \
    X(LUMEN_SHORT_RANGE_AO_DOWNSAMPLE_FACTOR, 1,                                                   \
      "px", "the short-range AO is searched at every pixel at the epic tier; the high tier"        \
      " searches at 2: one pixel of each 2 x 2 block, taken in turn over four frames"              \
      " (lumen_short_range_ao.sh)")                                                                \
    X(LUMEN_SHORT_RANGE_AO_SLICE_COUNT, 2,                                                         \
      "slices", "screen slices of the horizon search per pixel, each searched on both sides;"      \
      " more slices cost more and leave less noise to the temporal")                               \
    X(LUMEN_SHORT_RANGE_AO_STEPS_PER_SLICE, 3,                                                     \
      "steps", "depth samples per slice direction, spaced quadratically: more near the pixel")     \
    X(LUMEN_SHORT_RANGE_AO_RADIUS_PROBE_TILES, 2.0f,                                               \
      "probe tiles", "search radius: twice the screen probe spacing (32 px at 16 px spacing),"     \
      " the occlusion detail the probe lattice cannot resolve")                                    \
    X(LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_DISTANCE, 0.3f,                                       \
      "ratio", "a sample whose depth differs from the pixel's by this share of the pixel's depth"  \
      " no longer raises the horizon; it fades out by (difference / (this x depth))^power")        \
    X(LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_POWER, 1.0f,                                          \
      "exponent", "exponent of that fade at the epic tier (high: 1.5,"                             \
      " lumen_pass::get_short_range_ao_layout)")                                                   \
    X(LUMEN_SHORT_RANGE_AO_MIN_VISIBILITY, 0.03f,                                                  \
      "ratio", "floor of the integrated visibility: no pixel goes below 3%")                       \
    X(LUMEN_SHORT_RANGE_AO_NEIGHBORHOOD_CLAMP_SCALE, 1.0f,                                         \
      "std devs", "the AO history is clamped to the mean +- this x the deviation of the 3 x 3"     \
      " neighbourhood of this frame's sample (lumen_short_range_ao_temporal.sh)")                  \
    X(LUMEN_SHORT_RANGE_AO_MAX_MULTIBOUNCE_ALBEDO, 0.5f,                                           \
      "albedo", "cap of the albedo the AO multi-bounce fit uses with the short-range AO"           \
      " (lighting.sh): the fit's gain grows with the albedo, so the cap limits how far bright"     \
      " surfaces lift their occlusion")                                                            \
    X(LUMEN_SHORT_RANGE_AO_UPSAMPLE_DEPTH_WEIGHT, 5000.0f,                                         \
      "1 / ratio^2", "a half-resolution AO sample counts for a pixel by exp2(-this x"              \
      " (plane distance / depth)^2), the plane distance of the pixel its search ran from: half"    \
      " weight at 1.4% of the depth")                                                              \
    X(LUMEN_SHORT_RANGE_AO_RECONSTRUCT_MIN_WEIGHT, 0.01f,                                          \
      "weight", "below this summed weight no half-resolution sample reconstructs the pixel; the"   \
      " history carries it (weight 1 / (1 + LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT N))")
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
