#include "gi_component.hpp"

#include <engine/rendering/gi/gi_constants.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

// `trace_resolution` is reflected once, by ssr_component.cpp. Registering it again would be a
// duplicate type in the meta registry, so this file only uses it as a field type.

REFLECT_INLINE(gi_resolve_pass::settings)
{
    using settings = gi_resolve_pass::settings;

    entt::meta_factory<settings>{}
        .type("gi_resolve_pass::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_resolve_pass::settings"},
            entt::attribute{"pretty_name", "Gather"},
        })
        .data<&settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"group", "Energy"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"tooltip",
                            "Multiplier on the traced indirect diffuse light and the probe-lit "
                            "rough reflections. 1 is physically based. The environment probe "
                            "that fills in where the gather finds no result is not scaled."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Internal resolution of the GI gather, its filters and the GI "
                            "reflections, relative to the output. Indirect lighting is low "
                            "frequency: Half loses little detail and the bilateral upsample "
                            "restores full-resolution edges. Lower settings are faster but "
                            "softer."},
        })
        .data<&settings::probe_spacing>("probe_spacing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_spacing"},
            entt::attribute{"pretty_name", "Probe Spacing"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 48.0f},
            entt::attribute{"tooltip",
                            "Distance between screen probes, in output pixels. Lower values "
                            "place probes more densely for finer indirect detail and contact "
                            "shading; the trace cost grows with the inverse square of the "
                            "spacing."},
        })
        .data<&settings::enable_screen_trace>("enable_screen_trace"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_screen_trace"},
            entt::attribute{"pretty_name", "Screen Trace"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Marches gather rays through the depth buffer first and takes their "
                            "on-screen hits exactly; the distance field answers the rest. Adds "
                            "detail from visible geometry that the coarse distance field cannot "
                            "represent. Off traces the distance field only."},
        })
        .data<&settings::probe_visibility_variance_gate>("probe_visibility_variance_gate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_visibility_variance_gate"},
            entt::attribute{"pretty_name", "Probe Visibility Variance Gate"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip",
                            "How uncertain a world probe's depth estimate may be before its "
                            "visibility is verified by marching the distance field, as the "
                            "standard deviation of the estimate in probe spacings. Lower values "
                            "march more probes: less light leaking through thin geometry, at a "
                            "higher cost. 0 marches every probe."},
        })
        .data<&settings::probe_filter_passes>("probe_filter_passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_filter_passes"},
            entt::attribute{"pretty_name", "Probe Filter Passes"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"tooltip",
                            "Spatial filter passes over the screen-probe radiance before it is "
                            "converted to irradiance. Each pass blends every direction with "
                            "neighbouring probes on the same surface, reducing per-probe noise "
                            "and the blotches it causes in motion. More passes also soften "
                            "small-scale indirect shadowing."},
        })
        .data<&settings::adaptive_probes>("adaptive_probes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptive_probes"},
            entt::attribute{"pretty_name", "Adaptive Probes"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Probes on flat, continuous surfaces skip tracing and are "
                            "interpolated from their traced neighbours; probes at depth or "
                            "orientation changes always trace. Flat areas trace only a quarter "
                            "of their probes. Off traces every probe."},
        })
        .data<&settings::adaptive_rays>("adaptive_rays"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptive_rays"},
            entt::attribute{"pretty_name", "Adaptive Rays"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Allocates each probe's rays by importance: directions that carried "
                            "bright light in the previous frame are traced individually, dim "
                            "directions in groups of four with one wider ray. Cheaper, with "
                            "slightly more noise in dim directions. Off traces every direction "
                            "individually."},
        })
        .data<&settings::reprojected_firefly_reference>("reprojected_firefly_reference"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reprojected_firefly_reference"},
            entt::attribute{"pretty_name", "Reprojected Firefly Reference"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Reference for the firefly clamp, which limits each new probe sample "
                            "to a multiple of recently observed radiance.\n"
                            "Off: the larger of the same screen texel's previous value and the "
                            "reprojected probe's own radiance.\n"
                            "On: the reprojected probe's radiance only, so the limit follows the "
                            "surface rather than the screen during camera motion; probes without "
                            "reprojected history are not clamped."},
        })
        .data<&settings::world_probe_jitter>("world_probe_jitter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_probe_jitter"},
            entt::attribute{"pretty_name", "World Probe Jitter"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Jitters world-probe rays within their direction cell and "
                            "accumulates each probe as a running average over several seconds. "
                            "Removes the fixed per-probe bias that shows as blotches on walls "
                            "lit by small emitters, but probes visibly re-converge after the "
                            "probe grid scrolls with a moving camera. Off uses fixed ray "
                            "directions: biased, but settled immediately."},
        })
        .data<&settings::enable_reflections>("enable_reflections"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_reflections"},
            entt::attribute{"pretty_name", "Reflections"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "World-space reflections beneath SSR: rough surfaces reflect the "
                            "probe lighting, glossy and mirror-like surfaces trace rays through "
                            "the screen and then the distance field. Supplies the off-screen "
                            "reflections SSR cannot resolve."},
        })
        .data<&settings::reflection_finder_resumes>("reflection_finder_resumes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_finder_resumes"},
            entt::attribute{"pretty_name", "Reflection Finder Resumes"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", float(gi::GI_REFLECTION_FINDER_RESUMES_MIN)},
            entt::attribute{"max", float(gi::GI_REFLECTION_FINDER_RESUMES_MAX)},
            entt::attribute{"tooltip",
                            "How many times a distant reflection ray may continue past an object "
                            "it only grazed before it is shaded as a hit. Higher values remove "
                            "more of the thin false outlines along distant grazing surfaces, at "
                            "a higher trace cost."},
        })

        .data<&settings::enable_temporal>("enable_temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal"},
            entt::attribute{"pretty_name", "Temporal"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"tooltip",
                            "Accumulates the gathered indirect light over frames, reprojected "
                            "with motion vectors and discarded where depth disagrees. Removes "
                            "most of the per-frame noise; off shows the raw gather and is "
                            "intended for diagnosis."},
        })
        .data<&settings::reflection_temporal_frames>("reflection_temporal_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_temporal_frames"},
            entt::attribute{"pretty_name", "Reflection Temporal Frames"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip",
                            "Number of frames the GI reflection rays are averaged over. Longer "
                            "windows give smoother glossy reflections but react more slowly to "
                            "change. 0 or 1 disables the reflection temporal filter."},
        })
        .data<&settings::temporal_slow_frames>("temporal_slow_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal_slow_frames"},
            entt::attribute{"pretty_name", "Temporal Slow Frames"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip",
                            "History length, in frames, of the indirect-light accumulation where "
                            "lighting is stable. Longer windows smooth more residual flicker. "
                            "Detected lighting changes still fall back to a short 8-frame "
                            "window, so responsiveness is kept."},
        })
        .data<&settings::denoise_converged_early_out>("denoise_converged_early_out"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_converged_early_out"},
            entt::attribute{"pretty_name", "Denoise Converged Early-Out"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"tooltip",
                            "Skips the spatial denoise on screen tiles whose temporal "
                            "accumulation has converged with low noise, saving most of the "
                            "denoise cost while the view is still. Requires Temporal."},
        })
        .data<&settings::reprojection_tolerance>("reprojection_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reprojection_tolerance"},
            entt::attribute{"pretty_name", "Reprojection Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip",
                            "Depth mismatch allowed between a pixel and its reprojected history "
                            "before the history is discarded, relative to view distance. Lower "
                            "values reject history sooner (less ghosting, more noise in motion); "
                            "higher values keep more history."},
        })
        .data<&settings::enable_spatial_denoise>("enable_spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_spatial_denoise"},
            entt::attribute{"pretty_name", "Spatial Denoise"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"tooltip",
                            "Edge-aware a-trous filter over the accumulated indirect light, "
                            "guided by depth, normals and the local noise estimate. Removes the "
                            "noise the temporal accumulation leaves behind."},
        })
        .data<&settings::denoise_passes>("denoise_passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_passes"},
            entt::attribute{"pretty_name", "Denoise Passes"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 6.0f},
            entt::attribute{"tooltip",
                            "Number of a-trous iterations; each doubles the filter footprint. "
                            "More passes remove coarser noise at the cost of fine lighting "
                            "detail and GPU time. 0 disables the filter."},
        })
        .data<&settings::denoise_normal_power>("denoise_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_normal_power"},
            entt::attribute{"pretty_name", "Denoise Normal Power"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "Exponent on the normal agreement between a pixel and its filter "
                            "taps. Higher values stop the filter at smaller normal changes, "
                            "keeping corners and curvature crisp; lower values blur across them."},
        })
        .data<&settings::denoise_luma_phi>("denoise_luma_phi"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_luma_phi"},
            entt::attribute{"pretty_name", "Denoise Luma Phi"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "Brightness tolerance of the filter, in multiples of the pixel's "
                            "estimated noise. Higher values smooth across stronger lighting "
                            "contrast; lower values preserve shadow edges and highlights. "
                            "Requires Temporal, which provides the noise estimate."},
        })
        .data<&settings::denoise_plane_tolerance>("denoise_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_plane_tolerance"},
            entt::attribute{"pretty_name", "Denoise Plane Tolerance"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip",
                            "How far a filter tap may lie off the pixel's surface plane, as a "
                            "fraction of view distance, before its weight falls off. Keeps light "
                            "from bleeding across depth discontinuities; lower is stricter."},
        })
        .data<&settings::denoise_low_count_boost>("denoise_low_count_boost"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_low_count_boost"},
            entt::attribute{"pretty_name", "Denoise Low Count Boost"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip",
                            "Extra brightness tolerance for pixels with little temporal history, "
                            "such as disocclusions and camera cuts, so they are smoothed harder. "
                            "The tolerance is widened by this value over the frames accumulated "
                            "and returns to normal once the history reaches this length."},
        })
        .data<&settings::denoise_luma_floor>("denoise_luma_floor"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_luma_floor"},
            entt::attribute{"pretty_name", "Denoise Luma Floor"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip",
                            "Minimum brightness tolerance of the filter, as a fraction of each "
                            "pixel's own luminance. Keeps smoothing the faint low-contrast "
                            "blotches that remain after the temporal accumulation has converged; "
                            "0 relies on the noise estimate alone."},
        })
        .data<&settings::enable_bilateral_upsample>("enable_bilateral_upsample"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_bilateral_upsample"},
            entt::attribute{"pretty_name", "Bilateral Upsample"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"tooltip",
                            "Reconstructs the output resolution from the trace resolution, "
                            "guided by full-resolution depth and normals so indirect light stays "
                            "on the correct side of edges. Runs only when Trace Resolution is "
                            "below Full; off uses the low-resolution result as is."},
        })
        .data<&settings::upsample_normal_power>("upsample_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_normal_power"},
            entt::attribute{"pretty_name", "Upsample Normal Power"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "Exponent on the normal agreement between a pixel and the "
                            "low-resolution samples it is reconstructed from. Higher values keep "
                            "indirect light from bleeding across edges and creases."},
        })
        .data<&settings::upsample_plane_tolerance>("upsample_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_plane_tolerance"},
            entt::attribute{"pretty_name", "Upsample Plane Tolerance"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip",
                            "How far a low-resolution sample may lie off the pixel's surface "
                            "plane, as a fraction of view distance, before its weight falls off. "
                            "Lower values keep depth edges sharper."},
        })
        .data<&settings::hold_at_rest>("hold_at_rest"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "hold_at_rest"},
            entt::attribute{"pretty_name", "Hold At Rest"},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Pauses GI tracing while the view is at rest: once the camera, "
                            "exposure, lights and scene have been still long enough to converge, "
                            "the gather and the reflection trace reuse their last converged "
                            "result. Any tracked change resumes tracing from it. Changes the "
                            "renderer does not track, such as particles and animated materials, "
                            "are not picked up while paused."},
        });
}

REFLECT_INLINE(global_sdf_clipmap::settings)
{
    using settings = global_sdf_clipmap::settings;

    entt::meta_factory<settings>{}
        .type("global_sdf_clipmap::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_sdf_clipmap::settings"},
            entt::attribute{"pretty_name", "Cascade"},
        })
        .data<&settings::compose_on_gpu>("compose_on_gpu"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compose_on_gpu"},
            entt::attribute{"pretty_name", "Compose On GPU"},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Builds the distance-field cascade in a GPU compute pass. The CPU "
                            "path produces identical voxels but stalls the main thread whenever "
                            "a level rebuilds; it is kept as a diagnostic reference."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"min", 16},
            entt::attribute{"max", 128},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Voxels per axis in each cascade level; memory and rebuild cost grow "
                            "with the cube. The world probes require 128: other values disable "
                            "them, and with them the GI gather. Changing it rebuilds the "
                            "cascade, which flickers for a few frames."},
        })
        .data<&settings::base_extent>("base_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "base_extent"},
            entt::attribute{"pretty_name", "Base Extent"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "World-space size of the finest cascade level, in metres. With Level "
                            "Scale it sets both the near-field precision and the total reach: "
                            "the coarsest level spans Base Extent x Level Scale^3. Changing it "
                            "rebuilds the cascade."},
        })
        .data<&settings::level_scale>("level_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "level_scale"},
            entt::attribute{"pretty_name", "Level Scale"},
            entt::attribute{"min", 1.5f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "Size ratio between consecutive cascade levels. Ray hits are "
                            "accurate to a fraction of a voxel, so coarse far levels make "
                            "distant surfaces appear offset: 2 keeps them fine, larger ratios "
                            "extend the reach at that cost. Changing it rebuilds the cascade."},
        })
        .data<&settings::blend_voxels>("blend_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_voxels"},
            entt::attribute{"pretty_name", "Level Blend Band"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "Width of the cross-fade into the next cascade level, in voxels of "
                            "the finer level. Levels are built independently, so their surfaces "
                            "can differ by about one coarse voxel; the band must be wider than "
                            "that to hide the seam. 0 switches levels abruptly, which pops as "
                            "the camera moves."},
        })
        .data<&settings::max_levels_per_update>("max_levels_per_update"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_levels_per_update"},
            entt::attribute{"pretty_name", "Levels Per Update"},
            entt::attribute{"min", 1},
            entt::attribute{"max", global_sdf_clipmap::level_count},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Maximum number of cascade levels rebuilt per frame. Lower values "
                            "spread the rebuild cost over frames but let a moved object occlude "
                            "from its old position for a few frames; higher values react sooner "
                            "with larger frame-time spikes."},
        })
        .data<&settings::cull_composition>("cull_composition"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_composition"},
            entt::attribute{"pretty_name", "Cull Composition"},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Bins instances into a grid so each voxel tests only the instances "
                            "within reach. The output is identical either way; disable only to "
                            "diagnose composition issues."},
        });
}

REFLECT_INLINE(gi_settings)
{
    entt::meta_factory<gi_settings>{}
        .type("gi_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings"},
            entt::attribute{"pretty_name", "Settings"},
        })
        .data<&gi_settings::resolve>("resolve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolve"},
            entt::attribute{"pretty_name", "Gather"},
            entt::attribute{"tooltip",
                            "Screen-probe gather and its filter chain: resolution, probe "
                            "density, temporal accumulation and denoising."},
        })
        .data<&gi_settings::clipmap>("clipmap"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clipmap"},
            entt::attribute{"pretty_name", "Cascade"},
            entt::attribute{"tooltip",
                            "World-space distance-field cascade that all GI tracing runs "
                            "against. Resolution 128 is required for the world probes."},
        });
}

SAVE_INLINE(gi_resolve_pass::settings)
{
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("probe_spacing", obj.probe_spacing));
    try_save(ar, ser20::make_nvp("enable_screen_trace", obj.enable_screen_trace));
    try_save(ar, ser20::make_nvp("probe_visibility_variance_gate", obj.probe_visibility_variance_gate));
    try_save(ar, ser20::make_nvp("probe_filter_passes", obj.probe_filter_passes));
    try_save(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
    try_save(ar, ser20::make_nvp("adaptive_rays", obj.adaptive_rays));
    try_save(ar, ser20::make_nvp("reprojected_firefly_reference", obj.reprojected_firefly_reference));
    try_save(ar, ser20::make_nvp("world_probe_jitter", obj.world_probe_jitter));
    try_save(ar, ser20::make_nvp("enable_reflections", obj.enable_reflections));
    try_save(ar, ser20::make_nvp("reflection_temporal_frames", obj.reflection_temporal_frames));
    try_save(ar, ser20::make_nvp("reflection_finder_resumes", obj.reflection_finder_resumes));
    try_save(ar, ser20::make_nvp("denoise_converged_early_out", obj.denoise_converged_early_out));
    try_save(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_save(ar, ser20::make_nvp("temporal_slow_frames", obj.temporal_slow_frames));
    try_save(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_save(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_save(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_save(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_save(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_save(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_save(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_save(ar, ser20::make_nvp("denoise_luma_floor", obj.denoise_luma_floor));
    try_save(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_save(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_save(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
    try_save(ar, ser20::make_nvp("hold_at_rest", obj.hold_at_rest));
}
SAVE_INSTANTIATE(gi_resolve_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_resolve_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_resolve_pass::settings)
{
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("probe_spacing", obj.probe_spacing));
    try_load(ar, ser20::make_nvp("enable_screen_trace", obj.enable_screen_trace));
    try_load(ar, ser20::make_nvp("probe_visibility_variance_gate", obj.probe_visibility_variance_gate));
    try_load(ar, ser20::make_nvp("probe_filter_passes", obj.probe_filter_passes));
    try_load(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
    try_load(ar, ser20::make_nvp("adaptive_rays", obj.adaptive_rays));
    try_load(ar, ser20::make_nvp("reprojected_firefly_reference", obj.reprojected_firefly_reference));
    try_load(ar, ser20::make_nvp("world_probe_jitter", obj.world_probe_jitter));
    try_load(ar, ser20::make_nvp("enable_reflections", obj.enable_reflections));
    try_load(ar, ser20::make_nvp("reflection_temporal_frames", obj.reflection_temporal_frames));
    try_load(ar, ser20::make_nvp("reflection_finder_resumes", obj.reflection_finder_resumes));
    try_load(ar, ser20::make_nvp("denoise_converged_early_out", obj.denoise_converged_early_out));
    try_load(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_load(ar, ser20::make_nvp("temporal_slow_frames", obj.temporal_slow_frames));
    try_load(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_load(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_load(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_load(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_load(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_load(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_load(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_load(ar, ser20::make_nvp("denoise_luma_floor", obj.denoise_luma_floor));
    try_load(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_load(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_load(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
    try_load(ar, ser20::make_nvp("hold_at_rest", obj.hold_at_rest));
}
LOAD_INSTANTIATE(gi_resolve_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_resolve_pass::settings, ser20::iarchive_binary_t);

SAVE_INLINE(global_sdf_clipmap::settings)
{
    try_save(ar, ser20::make_nvp("compose_on_gpu", obj.compose_on_gpu));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("base_extent", obj.base_extent));
    try_save(ar, ser20::make_nvp("level_scale", obj.level_scale));
    try_save(ar, ser20::make_nvp("blend_voxels", obj.blend_voxels));
    try_save(ar, ser20::make_nvp("max_levels_per_update", obj.max_levels_per_update));
    try_save(ar, ser20::make_nvp("cull_composition", obj.cull_composition));
}
SAVE_INSTANTIATE(global_sdf_clipmap::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(global_sdf_clipmap::settings, ser20::oarchive_binary_t);

LOAD_INLINE(global_sdf_clipmap::settings)
{
    try_load(ar, ser20::make_nvp("compose_on_gpu", obj.compose_on_gpu));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("base_extent", obj.base_extent));
    try_load(ar, ser20::make_nvp("level_scale", obj.level_scale));
    try_load(ar, ser20::make_nvp("blend_voxels", obj.blend_voxels));
    try_load(ar, ser20::make_nvp("max_levels_per_update", obj.max_levels_per_update));
    try_load(ar, ser20::make_nvp("cull_composition", obj.cull_composition));
}
LOAD_INSTANTIATE(global_sdf_clipmap::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(global_sdf_clipmap::settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings)
{
    try_save(ar, ser20::make_nvp("resolve", obj.resolve));
    try_save(ar, ser20::make_nvp("clipmap", obj.clipmap));
}
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings)
{
    try_load(ar, ser20::make_nvp("resolve", obj.resolve));
    try_load(ar, ser20::make_nvp("clipmap", obj.clipmap));
}
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_binary_t);

// --- Reflection + Serialization: gi_component ---

REFLECT(gi_component)
{
    entt::meta_factory<gi_component>{}
        .type("gi_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_component"},
            entt::attribute{"category", "Rendering/Post Processing"},
            entt::attribute{"pretty_name", "Global Illumination"},
        })
        .func<&component_meta<gi_component>::exists>("component_exists"_hs)
        .func<&component_meta<gi_component>::add>("component_add"_hs)
        .func<&component_meta<gi_component>::remove>("component_remove"_hs)
        .func<&component_meta<gi_component>::save>("component_save"_hs)
        .func<&component_meta<gi_component>::load>("component_load"_hs)
        .data<&gi_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip",
                            "Enables global illumination: multi-bounce indirect diffuse lighting "
                            "and, with Reflections on, world-space reflections, traced against "
                            "the scene's distance field.\n"
                            "When off, indirect diffuse falls back to SSIL when present, "
                            "otherwise to the environment probe."},
        })
        .data<&gi_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(gi_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(gi_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_component, ser20::oarchive_binary_t);

LOAD(gi_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(gi_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_component, ser20::iarchive_binary_t);

} // namespace unravel
