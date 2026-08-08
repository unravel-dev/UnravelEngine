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
            entt::attribute{"tooltip", "Artistic multiplier on the scene bounce; the environment fallback keeps probe intensity."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip", "Indirect diffuse is low frequency; half resolution costs little."},
        })
        .data<&settings::probe_spacing>("probe_spacing"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_spacing"},
            entt::attribute{"pretty_name", "Probe Spacing"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 32.0f},
            entt::attribute{"tooltip", "Probe tile edge in full-resolution pixels."},
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
                            "1 = ray tiers (green = screen commit, red = SDF, blue = completion). "
                            "Session-only, not saved."},
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
            entt::attribute{"tooltip", "Steady-state blend weight is one over this."},
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
        })
        .data<&settings::denoise_normal_power>("denoise_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_normal_power"},
            entt::attribute{"pretty_name", "Denoise Normal Power"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
        })
        .data<&settings::denoise_luma_phi>("denoise_luma_phi"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_luma_phi"},
            entt::attribute{"pretty_name", "Denoise Luma Phi"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
        })
        .data<&settings::denoise_plane_tolerance>("denoise_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_plane_tolerance"},
            entt::attribute{"pretty_name", "Denoise Plane Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
        })
        .data<&settings::denoise_low_count_boost>("denoise_low_count_boost"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_low_count_boost"},
            entt::attribute{"pretty_name", "Denoise Low Count Boost"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 64.0f},
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
        })
        .data<&settings::upsample_plane_tolerance>("upsample_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_plane_tolerance"},
            entt::attribute{"pretty_name", "Upsample Plane Tolerance"},
            entt::attribute{"group", "Filtering"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
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
                            "The CPU composer blocks the main thread for milliseconds whenever the "
                            "camera moves far enough to re-snap a level, which lands as a stutter; "
                            "measured at 4.20 ms wall against 0.42 ms plus ~0.5 ms of GPU. Output is "
                            "identical -- pinned byte for byte by a parity test -- so this is a cost "
                            "switch, not a quality one. Turn it off to compare, or if a backend "
                            "misbehaves."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"min", 16},
            entt::attribute{"max", 128},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Voxels per axis in every cascade level. Memory and composition work are "
                            "both CUBIC in this: 128 is four times the spatial detail and eight times "
                            "the cost of 64. It was held at 64 because the CPU composer could not "
                            "afford more; on the GPU 128 is reachable. Changing it rebuilds the "
                            "cascade, so it flickers for a few frames."},
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
                            "Cascade levels recomposed per frame at most. Composing touches every "
                            "voxel of a level, so rebuilding all four in the frame something moved is "
                            "a visible hitch. The cost of budgeting is that a moved object keeps "
                            "occluding from its old position for a few frames. Raise it now that "
                            "composition is on the GPU and cheaper."},
        })
        .data<&settings::cull_composition>("cull_composition"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_composition"},
            entt::attribute{"pretty_name", "Cull Composition"},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Bin instances so a voxel tests only the ones that can reach it, instead "
                            "of every instance the level overlaps. Pure acceleration -- composing with "
                            "it off produces byte-identical voxels, which a test asserts. Present so "
                            "that comparison can be made; leave it on."},
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
            entt::attribute{"tooltip", "The screen probe gather and its filter chain. Everything "
                                       "else is constant-driven (gi_constants)."},
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
    try_save(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
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
    try_load(ar, ser20::make_nvp("adaptive_probes", obj.adaptive_probes));
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
