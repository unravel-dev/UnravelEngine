#include "surface_cache_gi_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(surface_cache_gi_pass::surface_cache_gi_settings)
{
    using surface_cache_gi_settings = surface_cache_gi_pass::surface_cache_gi_settings;

    entt::meta_factory<surface_cache_gi_settings>{}
        .type("surface_cache_gi_pass::surface_cache_gi_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_cache_gi_pass::surface_cache_gi_settings"},
            entt::attribute{"pretty_name", "Surface Cache GI Settings"},
        })
        .data<&surface_cache_gi_settings::cache_blend>("cache_blend"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cache_blend"},
            entt::attribute{"pretty_name", "Cache Blend"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip",
                            "How strongly probe irradiance replaces skylight SH when confidence is high."},
        })
        .data<&surface_cache_gi_settings::ssil_near_field_weight>("ssil_near_field_weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ssil_near_field_weight"},
            entt::attribute{"pretty_name", "SSIL Near-Field Weight"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip",
                            "Near-field only: how much SSIL mixes on top of probe irradiance. "
                            "Keep low (0.2-0.35); mid-field GI comes from world probes."},
        })
        .data<&surface_cache_gi_settings::probe_near_extent>("probe_near_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_near_extent"},
            entt::attribute{"pretty_name", "Probe Near Extent"},
            entt::attribute{"min", 4.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip", "Near cascade size (m). Dense probes for rotation-stable local GI."},
        })
        .data<&surface_cache_gi_settings::probe_far_extent>("probe_far_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_far_extent"},
            entt::attribute{"pretty_name", "Probe Far Extent"},
            entt::attribute{"min", 16.0f},
            entt::attribute{"max", 200.0f},
            entt::attribute{"tooltip",
                            "Far cascade size (m). Keep large — compose confidence follows this "
                            "volume; too small causes lamp-like region pops when walking."},
        })
        .data<&surface_cache_gi_settings::probes_per_frame>("probes_per_frame"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probes_per_frame"},
            entt::attribute{"pretty_name", "Probes Per Frame"},
            entt::attribute{"min", 4},
            entt::attribute{"max", 256},
            entt::attribute{"tooltip", "Amortized probe update budget each frame."},
        })
        .data<&surface_cache_gi_settings::probe_history>("probe_history"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_history"},
            entt::attribute{"pretty_name", "Probe History"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.98f},
            entt::attribute{"tooltip", "Temporal hysteresis when blending probe irradiance."},
        })
        .data<&surface_cache_gi_settings::bounce_strength>("bounce_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_strength"},
            entt::attribute{"pretty_name", "Card Bounce Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "One card-space bounce into dirty atlas pages (multi-bounce v1)."},
        })
        .data<&surface_cache_gi_settings::enable_screen_project>("enable_screen_project"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_screen_project"},
            entt::attribute{"pretty_name", "Enable Screen Project"},
            entt::attribute{"tooltip",
                            "Debug/boost: also splat screen direct lighting into the atlas. "
                            "Off by default — card lighting is the primary world radiance path."},
        })
        .data<&surface_cache_gi_settings::max_card_distance>("max_card_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_card_distance"},
            entt::attribute{"pretty_name", "Max Card Distance"},
            entt::attribute{"min", 5.0f},
            entt::attribute{"max", 250.0f},
            entt::attribute{"tooltip",
                            "World radius around the camera for spawning/keeping static AABB cards. "
                            "Update policy is world-driven (not frustum-culled only)."},
        })
        .data<&surface_cache_gi_settings::card_thickness>("card_thickness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "card_thickness"},
            entt::attribute{"pretty_name", "Card Thickness"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Plane acceptance thickness for projecting/sampling cards."},
        })
        .data<&surface_cache_gi_settings::project_history>("project_history"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "project_history"},
            entt::attribute{"pretty_name", "Project History"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.99f},
            entt::attribute{"tooltip", "Temporal stickiness when writing material/radiance into atlas pages."},
        })
        .data<&surface_cache_gi_settings::stale_frames>("stale_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "stale_frames"},
            entt::attribute{"pretty_name", "Stale Frames"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 600},
            entt::attribute{"tooltip",
                            "Frames without a local refresh before amortized confidence decay starts."},
        })
        .data<&surface_cache_gi_settings::pages_per_frame>("pages_per_frame"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pages_per_frame"},
            entt::attribute{"pretty_name", "Pages Per Frame"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 64},
            entt::attribute{"tooltip", "Amortized card lighting / age budget each frame."},
        })
        .data<&surface_cache_gi_settings::min_face_area>("min_face_area"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_face_area"},
            entt::attribute{"pretty_name", "Min Face Area"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"tooltip", "Skip AABB faces smaller than this area (m^2) when spawning cards."},
        })
        .data<&surface_cache_gi_settings::seed_with_skylight>("seed_with_skylight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "seed_with_skylight"},
            entt::attribute{"pretty_name", "Seed With Skylight"},
            entt::attribute{"tooltip",
                            "Unused for card pages (cards are direct-lit only). Skylight still fills "
                            "compose when surface-cache confidence is 0."},
        })
        .data<&surface_cache_gi_settings::max_gather_distance>("max_gather_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_gather_distance"},
            entt::attribute{"pretty_name", "Max Gather Distance"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"tooltip",
                            "Max world distance for card-to-card / probe gather (indirect irradiance)."},
        })
        .data<&surface_cache_gi_settings::gather_intensity>("gather_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gather_intensity"},
            entt::attribute{"pretty_name", "Gather Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"tooltip",
                            "Scales final GI from the surface cache. 1.0 is the default balanced "
                            "look; values above ~1.5 get bright quickly."},
        })
        .data<&surface_cache_gi_settings::max_card_extent>("max_card_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_card_extent"},
            entt::attribute{"pretty_name", "Max Card Extent"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"tooltip",
                            "Subdivide large AABB faces into tiles no larger than this edge length. "
                            "Lower = fewer blocks / higher memory."},
        })
        .data<&surface_cache_gi_settings::sticky_distance>("sticky_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sticky_distance"},
            entt::attribute{"pretty_name", "Sticky Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 80.0f},
            entt::attribute{"tooltip",
                            "Extra world units beyond gather radius before a card drops out of "
                            "the GPU upload set (hysteresis)."},
        });
}

SAVE(surface_cache_gi_pass::surface_cache_gi_settings)
{
    try_save(ar, ser20::make_nvp("cache_blend", obj.cache_blend));
    try_save(ar, ser20::make_nvp("ssil_near_field_weight", obj.ssil_near_field_weight));
    try_save(ar, ser20::make_nvp("max_card_distance", obj.max_card_distance));
    try_save(ar, ser20::make_nvp("card_thickness", obj.card_thickness));
    try_save(ar, ser20::make_nvp("project_history", obj.project_history));
    try_save(ar, ser20::make_nvp("stale_frames", obj.stale_frames));
    try_save(ar, ser20::make_nvp("pages_per_frame", obj.pages_per_frame));
    try_save(ar, ser20::make_nvp("min_face_area", obj.min_face_area));
    try_save(ar, ser20::make_nvp("seed_with_skylight", obj.seed_with_skylight));
    try_save(ar, ser20::make_nvp("max_gather_distance", obj.max_gather_distance));
    try_save(ar, ser20::make_nvp("gather_intensity", obj.gather_intensity));
    try_save(ar, ser20::make_nvp("max_card_extent", obj.max_card_extent));
    try_save(ar, ser20::make_nvp("sticky_distance", obj.sticky_distance));
    try_save(ar, ser20::make_nvp("probe_near_extent", obj.probe_near_extent));
    try_save(ar, ser20::make_nvp("probe_far_extent", obj.probe_far_extent));
    try_save(ar, ser20::make_nvp("probes_per_frame", obj.probes_per_frame));
    try_save(ar, ser20::make_nvp("probe_history", obj.probe_history));
    try_save(ar, ser20::make_nvp("bounce_strength", obj.bounce_strength));
    try_save(ar, ser20::make_nvp("enable_screen_project", obj.enable_screen_project));
}
SAVE_INSTANTIATE(surface_cache_gi_pass::surface_cache_gi_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(surface_cache_gi_pass::surface_cache_gi_settings, ser20::oarchive_binary_t);

LOAD(surface_cache_gi_pass::surface_cache_gi_settings)
{
    try_load(ar, ser20::make_nvp("cache_blend", obj.cache_blend));
    try_load(ar, ser20::make_nvp("ssil_near_field_weight", obj.ssil_near_field_weight));
    try_load(ar, ser20::make_nvp("max_card_distance", obj.max_card_distance));
    try_load(ar, ser20::make_nvp("card_thickness", obj.card_thickness));
    try_load(ar, ser20::make_nvp("project_history", obj.project_history));
    try_load(ar, ser20::make_nvp("stale_frames", obj.stale_frames));
    try_load(ar, ser20::make_nvp("pages_per_frame", obj.pages_per_frame));
    try_load(ar, ser20::make_nvp("min_face_area", obj.min_face_area));
    try_load(ar, ser20::make_nvp("seed_with_skylight", obj.seed_with_skylight));
    try_load(ar, ser20::make_nvp("max_gather_distance", obj.max_gather_distance));
    try_load(ar, ser20::make_nvp("gather_intensity", obj.gather_intensity));
    try_load(ar, ser20::make_nvp("max_card_extent", obj.max_card_extent));
    // Legacy keys (ignored): sample_history, quality_preset, prefer_hardware_rt.
    float legacy_unused = 0.0f;
    int legacy_int = 0;
    bool legacy_bool = false;
    try_load(ar, ser20::make_nvp("sample_history", legacy_unused));
    try_load(ar, ser20::make_nvp("sticky_distance", obj.sticky_distance));
    try_load(ar, ser20::make_nvp("probe_near_extent", obj.probe_near_extent));
    try_load(ar, ser20::make_nvp("probe_far_extent", obj.probe_far_extent));
    try_load(ar, ser20::make_nvp("probes_per_frame", obj.probes_per_frame));
    try_load(ar, ser20::make_nvp("probe_history", obj.probe_history));
    try_load(ar, ser20::make_nvp("bounce_strength", obj.bounce_strength));
    try_load(ar, ser20::make_nvp("enable_screen_project", obj.enable_screen_project));
    try_load(ar, ser20::make_nvp("quality_preset", legacy_int));
    try_load(ar, ser20::make_nvp("prefer_hardware_rt", legacy_bool));
    (void)legacy_unused;
    (void)legacy_int;
    (void)legacy_bool;
}
LOAD_INSTANTIATE(surface_cache_gi_pass::surface_cache_gi_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(surface_cache_gi_pass::surface_cache_gi_settings, ser20::iarchive_binary_t);

REFLECT(surface_cache_gi_component)
{
    entt::meta_factory<surface_cache_gi_component>{}
        .type("surface_cache_gi_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_cache_gi_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Surface Cache GI"},
        })
        .func<&component_meta<surface_cache_gi_component>::exists>("component_exists"_hs)
        .func<&component_meta<surface_cache_gi_component>::add>("component_add"_hs)
        .func<&component_meta<surface_cache_gi_component>::remove>("component_remove"_hs)
        .func<&component_meta<surface_cache_gi_component>::save>("component_save"_hs)
        .func<&component_meta<surface_cache_gi_component>::load>("component_load"_hs)
        .data<&surface_cache_gi_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip",
                            "Enable world-space surface-cache GI for static geometry "
                            "(mesh cards + card lighting + cascade probe final gather). "
                            "Rotate-stable; SSIL is near-field only."},
        })
        .data<&surface_cache_gi_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(surface_cache_gi_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(surface_cache_gi_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(surface_cache_gi_component, ser20::oarchive_binary_t);

LOAD(surface_cache_gi_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(surface_cache_gi_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(surface_cache_gi_component, ser20::iarchive_binary_t);

} // namespace unravel
