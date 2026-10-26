#include "fog_volume_component.hpp"
#include <engine/meta/ecs/components/component_disables.h>
#include <engine/meta/core/math/vector.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(fog_pass::volume_settings)
{
    entt::meta_factory<fog_volume_shape>{}
        .type("fog_volume_shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_volume_shape"},
            entt::attribute{"pretty_name", "Fog Volume Shape"},
        })
        .data<fog_volume_shape::box>("box"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "box"},
            entt::attribute{"pretty_name", "Box"},
        })
        .data<fog_volume_shape::ellipsoid>("ellipsoid"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ellipsoid"},
            entt::attribute{"pretty_name", "Ellipsoid"},
        });

    entt::meta_factory<fog_volume_mode>{}
        .type("fog_volume_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_volume_mode"},
            entt::attribute{"pretty_name", "Fog Volume Mode"},
        })
        .data<fog_volume_mode::add>("add"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "add"},
            entt::attribute{"pretty_name", "Add"},
        })
        .data<fog_volume_mode::subtract>("subtract"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "subtract"},
            entt::attribute{"pretty_name", "Subtract"},
        });

    entt::meta_factory<fog_pass::volume_settings>{}
        .type("fog_volume_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_volume_settings"},
            entt::attribute{"pretty_name", "Fog Volume Settings"},
        })
        .data<&fog_pass::volume_settings::shape>("shape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shape"},
            entt::attribute{"pretty_name", "Shape"},
            entt::attribute{"tooltip", "A box, or an ellipsoid inside the same extents."},
        })
        .data<&fog_pass::volume_settings::mode>("mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mode"},
            entt::attribute{"pretty_name", "Mode"},
            entt::attribute{"tooltip", "Add fills the shape with fog. Subtract clears the fog inside it, for example to "
                                       "keep a room free of the outdoor fog (volumetric fog only)."},
        })
        .data<&fog_pass::volume_settings::extents>("extents"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "extents"},
            entt::attribute{"pretty_name", "Extents"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"tooltip", "Half size (m) of the box, or the ellipsoid's radii, along the entity's axes. The "
                                       "entity's scale stretches them."},
        })
        .data<&fog_pass::volume_settings::attenuation_distance>("attenuation_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "attenuation_distance"},
            entt::attribute{"pretty_name", "Attenuation Distance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Metres of the volume's fog that dim what is behind it to about a third (1/e). "
                                       "Lower is denser."},
        })
        .data<&fog_pass::volume_settings::albedo>("albedo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "albedo"},
            entt::attribute{"pretty_name", "Albedo"},
            entt::attribute{"tooltip", "Tint and brightness of the light the volume's fog scatters. White is bright "
                                       "mist; dark is smoke."},
        })
        .data<&fog_pass::volume_settings::edge_fade>("edge_fade"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "edge_fade"},
            entt::attribute{"pretty_name", "Edge Fade"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Share of the shape over which the fog fades in from its surface. 0 is a hard "
                                       "edge, 1 fades from the centre."},
        })
        .data<&fog_pass::volume_settings::noise_strength>("noise_strength"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "noise_strength"},
            entt::attribute{"pretty_name", "Noise Strength"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "How much noise breaks the fog up into wisps. 0 is uniform. Volumetric fog only."},
        })
        .data<&fog_pass::volume_settings::noise_size>("noise_size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "noise_size"},
            entt::attribute{"pretty_name", "Noise Size"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"tooltip", "Size (m) of the noise's wisps."},
        });
}

SAVE_INLINE(fog_pass::volume_settings)
{
    try_save(ar, ser20::make_nvp("shape", obj.shape));
    try_save(ar, ser20::make_nvp("mode", obj.mode));
    try_save(ar, ser20::make_nvp("extents", obj.extents));
    try_save(ar, ser20::make_nvp("attenuation_distance", obj.attenuation_distance));
    try_save(ar, ser20::make_nvp("albedo", obj.albedo));
    try_save(ar, ser20::make_nvp("edge_fade", obj.edge_fade));
    try_save(ar, ser20::make_nvp("noise_strength", obj.noise_strength));
    try_save(ar, ser20::make_nvp("noise_size", obj.noise_size));
}
SAVE_INSTANTIATE(fog_pass::volume_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(fog_pass::volume_settings, ser20::oarchive_binary_t);

LOAD_INLINE(fog_pass::volume_settings)
{
    try_load(ar, ser20::make_nvp("shape", obj.shape));
    try_load(ar, ser20::make_nvp("mode", obj.mode));
    try_load(ar, ser20::make_nvp("extents", obj.extents));
    try_load(ar, ser20::make_nvp("attenuation_distance", obj.attenuation_distance));
    try_load(ar, ser20::make_nvp("albedo", obj.albedo));
    try_load(ar, ser20::make_nvp("edge_fade", obj.edge_fade));
    try_load(ar, ser20::make_nvp("noise_strength", obj.noise_strength));
    try_load(ar, ser20::make_nvp("noise_size", obj.noise_size));
}
LOAD_INSTANTIATE(fog_pass::volume_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(fog_pass::volume_settings, ser20::iarchive_binary_t);

REFLECT(fog_volume_component)
{
    entt::meta_factory<fog_volume_component>{}
        .type("fog_volume_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_volume_component"},
            entt::attribute{COMPONENT_RENDER_FEATURE_ATTRIBUTE, render_feature::volumetric_fog},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Fog Volume"},
        })
        .func<&component_meta<fog_volume_component>::exists>("component_exists"_hs)
        .func<&component_meta<fog_volume_component>::add>("component_add"_hs)
        .func<&component_meta<fog_volume_component>::remove>("component_remove"_hs)
        .func<&component_meta<fog_volume_component>::save>("component_save"_hs)
        .func<&component_meta<fog_volume_component>::load>("component_load"_hs)
        .data<&fog_volume_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable the fog volume"},
        })
        .data<&fog_volume_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(fog_volume_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(fog_volume_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(fog_volume_component, ser20::oarchive_binary_t);

LOAD(fog_volume_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(fog_volume_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(fog_volume_component, ser20::iarchive_binary_t);

} // namespace unravel
