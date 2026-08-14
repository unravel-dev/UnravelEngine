#include "gi_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

// `trace_resolution` is reflected once, by ssr_component.cpp. Registering it again would be a
// duplicate type in the meta registry, so this file only uses it as a field type.
//
// Phase 8 (tasks/gi_rewrite_plan.md): the cache block and the v1 gather fields are gone with
// their subsystems; try_load simply ignores their names in old scenes, so existing saves load
// clean with defaults for everything the collapse removed.

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
                            "Scales the gathered bounce light. Applies to the scene's own indirect "
                            "only; the sky/environment fallback keeps its calibrated brightness."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Internal resolution of the gather and its filter chain, as a fraction "
                            "of the frame; reflections trace at the same resolution. Indirect light "
                            "is low frequency, so Half loses little - the bilateral upsample "
                            "reconstructs full resolution."},
        })
        .data<&settings::probe_spacing>("probe_spacing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_spacing"},
            entt::attribute{"pretty_name", "Probe Spacing"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 32.0f},
            entt::attribute{"tooltip",
                            "Distance between screen probes, in full-resolution pixels. Lower means "
                            "denser probes and finer indirect detail; ray cost grows with the "
                            "inverse square."},
        })
        .data<&settings::enable_screen_trace>("enable_screen_trace"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_screen_trace"},
            entt::attribute{"pretty_name", "Screen Trace"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Hi-Z screen tier: gather rays march the depth pyramid first and commit "
                            "pixel-precise on-screen hits before the SDF answers."},
        })
        .data<&settings::probe_visibility_variance_gate>("probe_visibility_variance_gate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_visibility_variance_gate"},
            entt::attribute{"pretty_name", "Probe Visibility Variance Gate"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip",
                            "How statistically ambiguous a world probe's depth estimate must be "
                            "before its visibility is settled by marching the distance field "
                            "(std of the depth lobe, in probe spacings). 0 marches every probe: "
                            "maximum leak protection, slowest. Higher trusts the depth "
                            "statistics over a wider band: faster, with a wider leak margin "
                            "through silhouette gaps."},
        })
        .data<&settings::adaptive_probes>("adaptive_probes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "adaptive_probes"},
            entt::attribute{"pretty_name", "Adaptive Probes"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Probes on flat regions skip tracing and reconstruct from their "
                            "neighbours; geometry breaks keep full probe density. Flat scenes "
                            "trace a fraction of the rays for the same image."},
        })
        .data<&settings::probe_space_temporal>("probe_space_temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_space_temporal"},
            entt::attribute{"pretty_name", "Probe-Space Temporal"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Each probe traces 16 of 64 directions per frame and blends them "
                            "into that probe's own previous tile. A still camera keeps the same "
                            "origin for one window so the sphere fills, then walks to a new "
                            "Halton so blotches dissolve. Off traces all 64 every frame."},
        })
        .data<&settings::enable_reflections>("enable_reflections"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_reflections"},
            entt::attribute{"pretty_name", "Reflections"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "World-space specular tier under SSR: rough lobes from the world "
                            "probes, sharp ones traced - off-screen reflections SSR cannot see."},
        })
        .data<&settings::debug_view>("debug_view"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "debug_view"},
            entt::attribute{"pretty_name", "Debug View"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip",
                            "1 = ray tiers (green = screen commit, red = SDF hit, blue = "
                            "world-probe/sky completion, magenta = probe reconstructed from "
                            "neighbours). Session-only, not saved."},
        })
        .data<&settings::enable_temporal>("enable_temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal"},
            entt::attribute{"pretty_name", "Temporal"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"tooltip", "Full-resolution temporal accumulation (depth-rejection only)."},
        })
        .data<&settings::reflection_temporal_frames>("reflection_temporal_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_temporal_frames"},
            entt::attribute{"pretty_name", "Reflection Temporal Frames"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip",
                            "Stochastic reflection accumulation window; 0/1 turns the "
                            "reflection temporal off."},
        })
        .data<&settings::max_accum_frames>("max_accum_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_accum_frames"},
            entt::attribute{"pretty_name", "Max Accumulated Frames"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 4.0f},
            entt::attribute{"max", 96.0f},
            entt::attribute{"tooltip",
                            "Temporal history length in frames; the steady-state blend weight is "
                            "one over this. Higher is smoother but reacts slower to lighting "
                            "changes."},
        })
        .data<&settings::reprojection_tolerance>("reprojection_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reprojection_tolerance"},
            entt::attribute{"pretty_name", "Reprojection Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Relative depth error per unit view distance before history is cut."},
        })
        .data<&settings::enable_spatial_denoise>("enable_spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_spatial_denoise"},
            entt::attribute{"pretty_name", "Spatial Denoise"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"tooltip", "Variance-guided a-trous over the accumulated result."},
        })
        .data<&settings::denoise_passes>("denoise_passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_passes"},
            entt::attribute{"pretty_name", "Denoise Passes"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 6.0f},
            entt::attribute{"tooltip",
                            "A-trous filter passes; each doubles the filter's reach. More passes "
                            "smooth broader noise at some cost in fine lighting detail."},
        })
        .data<&settings::denoise_normal_power>("denoise_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_normal_power"},
            entt::attribute{"pretty_name", "Denoise Normal Power"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "How strictly the filter stops at normal differences. Higher keeps "
                            "corners and curvature crisp; lower blurs across them."},
        })
        .data<&settings::denoise_luma_phi>("denoise_luma_phi"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_luma_phi"},
            entt::attribute{"pretty_name", "Denoise Luma Phi"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "Brightness tolerance of the filter, scaled by the local variance "
                            "estimate. Higher smooths more aggressively across lighting contrast; "
                            "lower preserves shadow edges and highlights."},
        })
        .data<&settings::denoise_plane_tolerance>("denoise_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_plane_tolerance"},
            entt::attribute{"pretty_name", "Denoise Plane Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip",
                            "Depth tolerance as a fraction of view distance: filter taps lying off "
                            "the pixel's surface plane by more than this are rejected, keeping "
                            "light from bleeding across depth breaks."},
        })
        .data<&settings::denoise_low_count_boost>("denoise_low_count_boost"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_low_count_boost"},
            entt::attribute{"pretty_name", "Denoise Low Count Boost"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip",
                            "Extra smoothing for pixels with little temporal history "
                            "(disocclusions, fresh camera cuts); fades out as history accumulates."},
        })
        .data<&settings::enable_bilateral_upsample>("enable_bilateral_upsample"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_bilateral_upsample"},
            entt::attribute{"pretty_name", "Bilateral Upsample"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"tooltip", "Surface-aware reconstruction to full resolution."},
        })
        .data<&settings::upsample_normal_power>("upsample_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_normal_power"},
            entt::attribute{"pretty_name", "Upsample Normal Power"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip",
                            "How strictly upsample taps must agree with the pixel's normal. Higher "
                            "keeps indirect light from bleeding across edges during the low-res to "
                            "full-res reconstruction."},
        })
        .data<&settings::upsample_plane_tolerance>("upsample_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_plane_tolerance"},
            entt::attribute{"pretty_name", "Upsample Plane Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip",
                            "Depth tolerance for upsample taps as a fraction of view distance; "
                            "taps off the pixel's surface plane beyond this are rejected."},
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
                            "Build the cascade voxels in a compute dispatch instead of on the CPU. "
                            "Output is identical; the CPU path stalls the main thread whenever a "
                            "level rebuilds and exists as a diagnostic fallback."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"min", 16},
            entt::attribute{"max", 128},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Voxels per axis in every cascade level; memory and composition cost "
                            "scale with the cube. 128 is required for the world probes - other "
                            "values disable them and with them the whole gather. Changing it "
                            "rebuilds the cascade, which flickers for a few frames."},
        })
        .data<&settings::base_extent>("base_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "base_extent"},
            entt::attribute{"pretty_name", "Base Extent"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "World-space size of the finest level. With Level Scale this sets both "
                            "how fine the near field is and how far GI sees at all: total coverage is "
                            "base extent times level scale cubed. Rebuilds the cascade."},
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
                            "Size multiplier between consecutive levels. Doubling keeps the far "
                            "cascades fine enough that a floor does not appear to float -- a tracer "
                            "stops within a fraction of a VOXEL, so at 8 m voxels that error is "
                            "metres. Quadrupling buys range and costs exactly that. Rebuilds the "
                            "cascade."},
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
                            "Width of the cross-fade into the next level, in voxels of the level "
                            "fading out. Levels are composed independently so their isosurfaces sit "
                            "about a coarse voxel apart; the band has to be wider than that to hide "
                            "it. Zero restores a hard switch, which pops as the camera moves."},
        })
        .data<&settings::max_levels_per_update>("max_levels_per_update"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_levels_per_update"},
            entt::attribute{"pretty_name", "Levels Per Update"},
            entt::attribute{"min", 1},
            entt::attribute{"max", global_sdf_clipmap::level_count},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Cascade levels recomposed per frame at most - the budget that spreads "
                            "geometry changes over frames. Low values smooth the cost but let a "
                            "moved object keep occluding from its old position for a few frames; "
                            "high values react in fewer frames at a higher per-frame peak."},
        })
        .data<&settings::cull_composition>("cull_composition"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_composition"},
            entt::attribute{"pretty_name", "Cull Composition"},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Bin instances into a grid so each voxel tests only the ones within "
                            "reach, instead of every instance in the level. Output is identical "
                            "either way; disabling is only useful to diagnose composition issues."},
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
            entt::attribute{"tooltip", "The screen probe gather and its filter chain - resolution, "
                                       "probe density, temporal and denoise quality."},
        })
        .data<&gi_settings::clipmap>("clipmap"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clipmap"},
            entt::attribute{"pretty_name", "Cascade"},
            entt::attribute{"tooltip", "The world distance field every GI structure anchors to. "
                                       "Resolution 128 is required for the world probes."},
        });
}

SAVE_INLINE(gi_resolve_pass::settings)
{
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("probe_spacing", obj.probe_spacing));
    // debug_view is deliberately NOT saved: a scene must never load with a diagnostic view on.
    try_save(ar, ser20::make_nvp("enable_screen_trace", obj.enable_screen_trace));
    try_save(ar, ser20::make_nvp("probe_visibility_variance_gate", obj.probe_visibility_variance_gate));
    try_save(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
    try_save(ar, ser20::make_nvp("probe_space_temporal", obj.probe_space_temporal));
    try_save(ar, ser20::make_nvp("enable_reflections", obj.enable_reflections));
    try_save(ar, ser20::make_nvp("reflection_temporal_frames", obj.reflection_temporal_frames));
    try_save(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_save(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
    try_save(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_save(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_save(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_save(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_save(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_save(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_save(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_save(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_save(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_save(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
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
    try_load(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
    try_load(ar, ser20::make_nvp("probe_space_temporal", obj.probe_space_temporal));
    try_load(ar, ser20::make_nvp("enable_reflections", obj.enable_reflections));
    try_load(ar, ser20::make_nvp("reflection_temporal_frames", obj.reflection_temporal_frames));
    try_load(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_load(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
    try_load(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_load(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_load(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_load(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_load(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_load(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_load(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_load(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_load(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_load(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
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
            entt::attribute{"category", "RENDERING"},
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
            entt::attribute{"tooltip", "Enable/disable surface cache global illumination.\nWhen off the "
                                       "indirect consumer falls back to SSIL, then to the environment "
                                       "probe."},
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
