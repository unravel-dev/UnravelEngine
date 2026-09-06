#include "gtao_component.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

// `trace_resolution` is reflected once, by ssr_component.cpp.

REFLECT_INLINE(gtao_pass::settings)
{
    using settings = gtao_pass::settings;

    auto temporal_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        if(auto data = obj.try_cast<settings>())
        {
            return data->enable_temporal;
        }
        return false;
    });

    entt::meta_factory<settings>{}
        .type("gtao_pass::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gtao_pass::settings"},
            entt::attribute{"pretty_name", "GTAO Settings"},
        })
        .data<&settings::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "radius"},
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"tooltip", "World-space radius of the occlusion search. 4 m adds the room-scale term the GI's\nprobe lattice under-delivers; the search is capped on screen by Max Screen Radius, so\nlarger values mostly affect far geometry. 0.5-1 m for a contact-only term."},
        })
        .data<&settings::falloff_range>("falloff_range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "falloff_range"},
            entt::attribute{"pretty_name", "Falloff Range"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Portion of the radius over which an occluder's influence fades to zero. 0.3 keeps most\nof the radius at full weight; below 0.2 occluders pop when they cross the boundary."},
        })
        .data<&settings::max_screen_radius>("max_screen_radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_screen_radius"},
            entt::attribute{"pretty_name", "Max Screen Radius"},
            entt::attribute{"min", 0.05f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Longest horizon search as a fraction of the view height. Bounds the cost and the\nsample spacing of a wide radius (the world radius shrinks with it). 0.25 = contact\nscale, 0.4 = wide."},
        })
        .data<&settings::final_power>("final_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "final_power"},
            entt::attribute{"pretty_name", "Final Power"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "Power applied to the visibility. 1 = ground truth for the depth buffer; higher\ndarkens for taste (1.6 pairs with the wide radius, 2.2 with a contact-only one)."},
        })
        .data<&settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Blend between no occlusion (0) and the full visibility (1) at the lighting."},
        })
        .data<&settings::thin_occluder_compensation>("thin_occluder_compensation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "thin_occluder_compensation"},
            entt::attribute{"pretty_name", "Thin Occluder Compensation"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.9f},
            entt::attribute{"tooltip", "Lets horizons relax behind thin occluders (railings, foliage) instead of treating\nthem as walls. 0 = off."},
        })
        .data<&settings::quality_level>("quality_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "quality_level"},
            entt::attribute{"pretty_name", "Quality Level"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 3},
            entt::attribute{"tooltip", "0 = low (1 slice x 2 steps), 1 = medium (2 x 2), 2 = high (3 x 3), 3 = ultra (9 x 3)."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"tooltip", "Resolution the visibility is computed at. Half is the default for the wide-radius look\n(a quarter of the cost); Full for contact-scale reference quality."},
        })
        .data<&settings::denoise_passes>("denoise_passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_passes"},
            entt::attribute{"pretty_name", "Denoise Passes"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 3},
            entt::attribute{"tooltip", "Joint-bilateral (depth + normal) blur passes over the raw visibility. 0 disables."},
        })
        .data<&settings::enable_temporal>("enable_temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal"},
            entt::attribute{"pretty_name", "Enable Temporal"},
            entt::attribute{"tooltip", "Accumulate the visibility over frames (velocity-reprojected, neighbourhood-clamped)."},
        })
        .data<&settings::temporal_history>("temporal_history"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal_history"},
            entt::attribute{"pretty_name", "Temporal History"},
            entt::attribute{"predicate", temporal_predicate_entt},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.98f},
            entt::attribute{"tooltip", "History weight of the accumulation. Higher = smoother and slower to react."},
        })
        .data<&settings::temporal_depth_threshold>("temporal_depth_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "temporal_depth_threshold"},
            entt::attribute{"pretty_name", "Temporal Depth Threshold"},
            entt::attribute{"predicate", temporal_predicate_entt},
            entt::attribute{"min", 0.005f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip", "Relative view-depth tolerance of the disocclusion test. Lower rejects history\nmore eagerly (less ghosting, more noise on motion)."},
        })
        .data<&settings::bent_normal_strength>("bent_normal_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bent_normal_strength"},
            entt::attribute{"pretty_name", "Bent Normal Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "How far the diffuse lookups (the GI probes and the environment SH) follow the bent\nnormal - the mean unoccluded direction - instead of the geometric normal."},
        })
        .data<&settings::multi_bounce>("multi_bounce"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "multi_bounce"},
            entt::attribute{"pretty_name", "Multi-Bounce"},
            entt::attribute{"tooltip", "Brighten the diffuse occlusion on light albedos by the light a crevice gets back from\nits own walls. Off = single-bounce visibility."},
        })
        .data<&settings::generate_normals>("generate_normals"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "generate_normals"},
            entt::attribute{"pretty_name", "Generate Normals"},
            entt::attribute{"tooltip", "Generate the receiver normal from the depth buffer instead of the G-buffer shading normal."},
        })
        .data<&settings::normal_map_detail>("normal_map_detail"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_map_detail"},
            entt::attribute{"pretty_name", "Normal Map Detail"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Full-resolution detail term: the pixel's shading normal against the AO texel's bent cone,\nand the map's perturbation carried into the bent normal. Applies with generated normals or\nat reduced resolution, so the crevices are there either way. 0 = off."},
        });
}

SAVE_INLINE(gtao_pass::settings)
{
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("falloff_range", obj.falloff_range));
    try_save(ar, ser20::make_nvp("max_screen_radius", obj.max_screen_radius));
    try_save(ar, ser20::make_nvp("final_power", obj.final_power));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("thin_occluder_compensation", obj.thin_occluder_compensation));
    try_save(ar, ser20::make_nvp("quality_level", obj.quality_level));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_save(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_save(ar, ser20::make_nvp("temporal_history", obj.temporal_history));
    try_save(ar, ser20::make_nvp("temporal_depth_threshold", obj.temporal_depth_threshold));
    try_save(ar, ser20::make_nvp("bent_normal_strength", obj.bent_normal_strength));
    try_save(ar, ser20::make_nvp("multi_bounce", obj.multi_bounce));
    try_save(ar, ser20::make_nvp("generate_normals", obj.generate_normals));
    try_save(ar, ser20::make_nvp("normal_map_detail", obj.normal_map_detail));
}
SAVE_INSTANTIATE(gtao_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gtao_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(gtao_pass::settings)
{
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("falloff_range", obj.falloff_range));
    try_load(ar, ser20::make_nvp("max_screen_radius", obj.max_screen_radius));
    try_load(ar, ser20::make_nvp("final_power", obj.final_power));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("thin_occluder_compensation", obj.thin_occluder_compensation));
    try_load(ar, ser20::make_nvp("quality_level", obj.quality_level));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_load(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_load(ar, ser20::make_nvp("temporal_history", obj.temporal_history));
    try_load(ar, ser20::make_nvp("temporal_depth_threshold", obj.temporal_depth_threshold));
    try_load(ar, ser20::make_nvp("bent_normal_strength", obj.bent_normal_strength));
    try_load(ar, ser20::make_nvp("multi_bounce", obj.multi_bounce));
    try_load(ar, ser20::make_nvp("generate_normals", obj.generate_normals));
    try_load(ar, ser20::make_nvp("normal_map_detail", obj.normal_map_detail));
}
LOAD_INSTANTIATE(gtao_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gtao_pass::settings, ser20::iarchive_binary_t);

REFLECT(gtao_component)
{
    entt::meta_factory<gtao_component>{}
        .type("gtao_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gtao_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "GTAO"},
        })
        .func<&component_meta<gtao_component>::exists>("component_exists"_hs)
        .func<&component_meta<gtao_component>::add>("component_add"_hs)
        .func<&component_meta<gtao_component>::remove>("component_remove"_hs)
        .func<&component_meta<gtao_component>::save>("component_save"_hs)
        .func<&component_meta<gtao_component>::load>("component_load"_hs)
        .data<&gtao_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable Ground Truth Ambient Occlusion"},
        })
        .data<&gtao_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(gtao_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(gtao_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gtao_component, ser20::oarchive_binary_t);

LOAD(gtao_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(gtao_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gtao_component, ser20::iarchive_binary_t);

} // namespace unravel
