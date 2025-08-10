#include "assao_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(assao_pass::settings)
{
    using settings = assao_pass::settings;

    rttr::registration::class_<settings>("assao_pass::settings")
        .constructor<>()(rttr::metadata("pretty_name", "SSAO Settings"))

        // radius: no explicit range (unbounded ≥ 0.0)
        .property("radius", &settings::radius)(rttr::metadata("pretty_name", "Radius"),
                                               rttr::metadata("tooltip",
                                                              "World (view) space size of the occlusion sphere.\n"
                                                              "Range: [0.0, ∞)"))

        // shadow_multiplier: [0.0, 5.0]
        .property("shadow_multiplier",
                  &settings::shadow_multiplier)(rttr::metadata("pretty_name", "Shadow Multiplier"),
                                                rttr::metadata("min", 0.0f),
                                                rttr::metadata("max", 5.0f),
                                                rttr::metadata("tooltip",
                                                               "Effect strength linear multiplier.\n"
                                                               "Range: [0.0, 5.0]"))

        // shadow_power: [0.5, 5.0]
        .property("shadow_power", &settings::shadow_power)(rttr::metadata("pretty_name", "Shadow Power"),
                                                           rttr::metadata("min", 0.5f),
                                                           rttr::metadata("max", 5.0f),
                                                           rttr::metadata("tooltip",
                                                                          "Effect strength power modifier.\n"
                                                                          "Range: [0.5, 5.0]"))

        // shadow_clamp: [0.0, 1.0]
        .property("shadow_clamp", &settings::shadow_clamp)(
            rttr::metadata("pretty_name", "Shadow Clamp"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("tooltip",
                           "Effect max limit (applied after multiplier but before blur).\n"
                           "Range: [0.0, 1.0]"))

        // horizon_angle_threshold: [0.0, 0.2]
        .property("horizon_angle_threshold", &settings::horizon_angle_threshold)(
            rttr::metadata("pretty_name", "Horizon Angle Threshold"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 0.2f),
            rttr::metadata("tooltip",
                           "Limits self-shadowing. Makes sampling cone narrower to avoid artifacts.\n"
                           "Range: [0.0, 0.2]"))

        // fade_out_from: no explicit range (unbounded ≥ 0.0)
        .property("fade_out_from", &settings::fade_out_from)(rttr::metadata("pretty_name", "Fade Out From"),
                                                             rttr::metadata("tooltip",
                                                                            "Distance to start fading out the effect.\n"
                                                                            "Range: [0.0, ∞)"))

        // fade_out_to: no explicit range (unbounded ≥ 0.0)
        .property("fade_out_to",
                  &settings::fade_out_to)(rttr::metadata("pretty_name", "Fade Out To"),
                                          rttr::metadata("tooltip",
                                                         "Distance at which the effect is fully faded out.\n"
                                                         "Range: [0.0, ∞)"))

        // quality_level: [-1, 3]
        .property("quality_level", &settings::quality_level)(rttr::metadata("pretty_name", "Quality Level"),
                                                             rttr::metadata("min", -1),
                                                             rttr::metadata("max", 3),
                                                             rttr::metadata("tooltip",
                                                                            "-1: Lowest (low, half-res checkerboard)\n"
                                                                            " 0: Low\n"
                                                                            " 1: Medium\n"
                                                                            " 2: High\n"
                                                                            " 3: Very High / Adaptive\n"
                                                                            "Range: [-1, 3]"))

        // adaptive_quality_limit: [0.0, 1.0]
        .property("adaptive_quality_limit", &settings::adaptive_quality_limit)(
            rttr::metadata("pretty_name", "Adaptive Q Limit"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 1.0f),
            rttr::metadata("tooltip",
                           "Adaptive quality limit (only for Quality Level 3).\n"
                           "Range: [0.0, 1.0]"))

        // blur_pass_count: [0, 6]
        .property("blur_pass_count", &settings::blur_pass_count)(
            rttr::metadata("pretty_name", "Blur Pass Count"),
            rttr::metadata("min", 0),
            rttr::metadata("max", 6),
            rttr::metadata("tooltip",
                           "Number of edge-sensitive blur passes.\n"
                           "Quality 0 uses a single 'dumb' blur pass instead of smart passes.\n"
                           "Range: [0, 6]"))

        // sharpness: [0.0, 1.0]
        .property("sharpness", &settings::sharpness)(rttr::metadata("pretty_name", "Sharpness"),
                                                     rttr::metadata("min", 0.0f),
                                                     rttr::metadata("max", 1.0f),
                                                     rttr::metadata("tooltip",
                                                                    "Sharpness (bleed over edges):\n"
                                                                    " 1.0 = Not at all\n"
                                                                    " 0.5 = Half-half\n"
                                                                    " 0.0 = Ignore edges entirely\n"
                                                                    "Range: [0.0, 1.0]"))

        // temporal_supersampling_angle_offset: [0.0, π]
        .property("temporal_supersampling_angle_offset", &settings::temporal_supersampling_angle_offset)(
            rttr::metadata("pretty_name", "Temporal SSAO Angle Offset"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 3.14159265358979323846f),
            rttr::metadata("tooltip",
                           "Rotate sampling kernel. If using temporal AA/supersampling, recommended:\n"
                           "  ((frame % 3) / 3.0 * π) or similar.\n"
                           "Range: [0.0, π]"))

        // temporal_supersampling_radius_offset: [0.0, 2.0]
        .property("temporal_supersampling_radius_offset", &settings::temporal_supersampling_radius_offset)(
            rttr::metadata("pretty_name", "Temporal SSAO Radius Offset"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 2.0f),
            rttr::metadata("tooltip",
                           "Scale sampling kernel. If using temporal AA/supersampling, recommended:\n"
                           "  (1.0 + (((frame % 3) - 1.0) / 3.0) * 0.1) or similar.\n"
                           "Range: [0.0, 2.0]"))

        // detail_shadow_strength: [0.0, 5.0]
        .property("detail_shadow_strength", &settings::detail_shadow_strength)(
            rttr::metadata("pretty_name", "Detail Shadow Strength"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("max", 5.0f),
            rttr::metadata("tooltip",
                           "High-res detail AO using neighboring depth pixels.\n"
                           "Adds detail but reduces temporal stability (adds aliasing).\n"
                           "Range: [0.0, 5.0]"))

        // generate_normals: no range (bool)
        .property("generate_normals", &settings::generate_normals)(
            rttr::metadata("pretty_name", "Generate Normals"),
            rttr::metadata("tooltip",
                           "If true, normals are generated from depth. Disable if precomputed normals are available."));

    // Register assao_pass::settings class with entt
    entt::meta_factory<settings>{}
        .type("assao_pass::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "SSAO Settings"},
        })
        .data<&settings::radius>("radius"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Radius"},
            entt::attribute{"tooltip", "World (view) space size of the occlusion sphere.\nRange: [0.0, ∞)"},
        })
        .data<&settings::shadow_multiplier>("shadow_multiplier"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Shadow Multiplier"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "Effect strength linear multiplier.\nRange: [0.0, 5.0]"},
        })
        .data<&settings::shadow_power>("shadow_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Shadow Power"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "Effect strength power modifier.\nRange: [0.5, 5.0]"},
        })
        .data<&settings::shadow_clamp>("shadow_clamp"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Shadow Clamp"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Effect max limit (applied after multiplier but before blur).\nRange: [0.0, 1.0]"},
        })
        .data<&settings::horizon_angle_threshold>("horizon_angle_threshold"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Horizon Angle Threshold"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip", "Limits self-shadowing. Makes sampling cone narrower to avoid artifacts.\nRange: [0.0, 0.2]"},
        })
        .data<&settings::fade_out_from>("fade_out_from"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Fade Out From"},
            entt::attribute{"tooltip", "Distance to start fading out the effect.\nRange: [0.0, ∞)"},
        })
        .data<&settings::fade_out_to>("fade_out_to"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Fade Out To"},
            entt::attribute{"tooltip", "Distance at which the effect is fully faded out.\nRange: [0.0, ∞)"},
        })
        .data<&settings::quality_level>("quality_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Quality Level"},
            entt::attribute{"min", -1},
            entt::attribute{"max", 3},
            entt::attribute{"tooltip", "-1: Lowest (low, half-res checkerboard)\n 0: Low\n 1: Medium\n 2: High\n 3: Very High / Adaptive\nRange: [-1, 3]"},
        })
        .data<&settings::adaptive_quality_limit>("adaptive_quality_limit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Adaptive Q Limit"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Adaptive quality limit (only for Quality Level 3).\nRange: [0.0, 1.0]"},
        })
        .data<&settings::blur_pass_count>("blur_pass_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Blur Pass Count"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 6},
            entt::attribute{"tooltip", "Number of edge-sensitive blur passes.\nQuality 0 uses a single 'dumb' blur pass instead of smart passes.\nRange: [0, 6]"},
        })
        .data<&settings::sharpness>("sharpness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Sharpness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Sharpness (bleed over edges):\n 1.0 = Not at all\n 0.5 = Half-half\n 0.0 = Ignore edges entirely\nRange: [0.0, 1.0]"},
        })
        .data<&settings::temporal_supersampling_angle_offset>("temporal_supersampling_angle_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Temporal SSAO Angle Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 3.14159265358979323846f},
            entt::attribute{"tooltip", "Rotate sampling kernel. If using temporal AA/supersampling, recommended:\n  ((frame % 3) / 3.0 * π) or similar.\nRange: [0.0, π]"},
        })
        .data<&settings::temporal_supersampling_radius_offset>("temporal_supersampling_radius_offset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Temporal SSAO Radius Offset"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Scale sampling kernel. If using temporal AA/supersampling, recommended:\n  (1.0 + (((frame % 3) - 1.0) / 3.0) * 0.1) or similar.\nRange: [0.0, 2.0]"},
        })
        .data<&settings::detail_shadow_strength>("detail_shadow_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Detail Shadow Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 5.0f},
            entt::attribute{"tooltip", "High-res detail AO using neighboring depth pixels.\nAdds detail but reduces temporal stability (adds aliasing).\nRange: [0.0, 5.0]"},
        })
        .data<&settings::generate_normals>("generate_normals"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Generate Normals"},
            entt::attribute{"tooltip", "If true, normals are generated from depth. Disable if precomputed normals are available."},
        });
}

SAVE_INLINE(assao_pass::settings)
{
    try_save(ar, ser20::make_nvp("radius", obj.radius));
    try_save(ar, ser20::make_nvp("shadow_multiplier", obj.shadow_multiplier));
    try_save(ar, ser20::make_nvp("shadow_power", obj.shadow_power));
    try_save(ar, ser20::make_nvp("shadow_clamp", obj.shadow_clamp));
    try_save(ar, ser20::make_nvp("horizon_angle_threshold", obj.horizon_angle_threshold));
    try_save(ar, ser20::make_nvp("fade_out_from", obj.fade_out_from));
    try_save(ar, ser20::make_nvp("fade_out_to", obj.fade_out_to));
    try_save(ar, ser20::make_nvp("quality_level", obj.quality_level));
    try_save(ar, ser20::make_nvp("adaptive_quality_limit", obj.adaptive_quality_limit));
    try_save(ar, ser20::make_nvp("blur_pass_count", obj.blur_pass_count));
    try_save(ar, ser20::make_nvp("sharpness", obj.sharpness));
    try_save(ar, ser20::make_nvp("temporal_supersampling_angle_offset", obj.temporal_supersampling_angle_offset));
    try_save(ar, ser20::make_nvp("temporal_supersampling_radius_offset", obj.temporal_supersampling_radius_offset));
    try_save(ar, ser20::make_nvp("detail_shadow_strength", obj.detail_shadow_strength));
    try_save(ar, ser20::make_nvp("generate_normals", obj.generate_normals));
}
SAVE_INSTANTIATE(assao_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(assao_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(assao_pass::settings)
{
    try_load(ar, ser20::make_nvp("radius", obj.radius));
    try_load(ar, ser20::make_nvp("shadow_multiplier", obj.shadow_multiplier));
    try_load(ar, ser20::make_nvp("shadow_power", obj.shadow_power));
    try_load(ar, ser20::make_nvp("shadow_clamp", obj.shadow_clamp));
    try_load(ar, ser20::make_nvp("horizon_angle_threshold", obj.horizon_angle_threshold));
    try_load(ar, ser20::make_nvp("fade_out_from", obj.fade_out_from));
    try_load(ar, ser20::make_nvp("fade_out_to", obj.fade_out_to));
    try_load(ar, ser20::make_nvp("quality_level", obj.quality_level));
    try_load(ar, ser20::make_nvp("adaptive_quality_limit", obj.adaptive_quality_limit));
    try_load(ar, ser20::make_nvp("blur_pass_count", obj.blur_pass_count));
    try_load(ar, ser20::make_nvp("sharpness", obj.sharpness));
    try_load(ar, ser20::make_nvp("temporal_supersampling_angle_offset", obj.temporal_supersampling_angle_offset));
    try_load(ar, ser20::make_nvp("temporal_supersampling_radius_offset", obj.temporal_supersampling_radius_offset));
    try_load(ar, ser20::make_nvp("detail_shadow_strength", obj.detail_shadow_strength));
    try_load(ar, ser20::make_nvp("generate_normals", obj.generate_normals));
}
LOAD_INSTANTIATE(assao_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(assao_pass::settings, ser20::iarchive_binary_t);

REFLECT(assao_component)
{
    rttr::registration::class_<assao_component>("assao_component")(rttr::metadata("category", "RENDERING"),
                                                                   rttr::metadata("pretty_name", "ASSAO"))
        .constructor<>()
        .method("component_exists", &component_exists<assao_component>)

        .property("enabled", &assao_component::enabled)(
            rttr::metadata("pretty_name", "Enabled"),
            rttr::metadata("tooltip", "Enable/disable ASSAO ambient occlusion"))
        .property("settings", &assao_component::settings)(rttr::metadata("pretty_name", "Settings"),
                                                          rttr::metadata("flattable", true));

    // Register assao_component class with entt
    entt::meta_factory<assao_component>{}
        .type("assao_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "ASSAO"},
        })
        .func<&component_exists<assao_component>>("component_exists"_hs)
        .data<&assao_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable ASSAO ambient occlusion"},
        })
        .data<&assao_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(assao_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(assao_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(assao_component, ser20::oarchive_binary_t);

LOAD(assao_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(assao_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(assao_component, ser20::iarchive_binary_t);
} // namespace unravel
