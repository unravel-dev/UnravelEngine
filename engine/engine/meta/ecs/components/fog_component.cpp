#include "fog_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

REFLECT_INLINE(fog_pass::settings)
{
    entt::meta_factory<volumetric_fog_quality>{}
        .type("volumetric_fog_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_fog_quality"},
            entt::attribute{"pretty_name", "Volumetric Fog Quality"},
        })
        .data<volumetric_fog_quality::low>("low"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "low"},
            entt::attribute{"pretty_name", "Low"},
        })
        .data<volumetric_fog_quality::medium>("medium"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "medium"},
            entt::attribute{"pretty_name", "Medium"},
        })
        .data<volumetric_fog_quality::high>("high"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "high"},
            entt::attribute{"pretty_name", "High"},
        })
        .data<volumetric_fog_quality::ultra>("ultra"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ultra"},
            entt::attribute{"pretty_name", "Ultra"},
        });

    entt::meta_factory<fog_pass::settings>{}
        .type("fog_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_settings"},
            entt::attribute{"pretty_name", "Fog Settings"},
        })
        .data<&fog_pass::settings::attenuation_distance>("attenuation_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "attenuation_distance"},
            entt::attribute{"pretty_name", "Attenuation Distance"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Density"},
            entt::attribute{"tooltip", "Metres of fog at the base height that dim what is behind it to about a third "
                                       "(1/e). Lower is denser fog."},
        })
        .data<&fog_pass::settings::base_height>("base_height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "base_height"},
            entt::attribute{"pretty_name", "Base Height"},
            entt::attribute{"step", 0.5f},
            entt::attribute{"group", "Density"},
            entt::attribute{"tooltip", "World height (m) below which the fog has its full density."},
        })
        .data<&fog_pass::settings::maximum_height>("maximum_height"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "maximum_height"},
            entt::attribute{"pretty_name", "Maximum Height"},
            entt::attribute{"step", 0.5f},
            entt::attribute{"group", "Density"},
            entt::attribute{"tooltip", "World height (m) where the fog has thinned to almost nothing (0.1% of its base "
                                       "density). It thins smoothly from the base height up to here."},
        })
        .data<&fog_pass::settings::start_distance>("start_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "start_distance"},
            entt::attribute{"pretty_name", "Start Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Density"},
            entt::attribute{"tooltip", "Distance (m) from the camera where the fog begins. Keeps nearby objects clear."},
        })
        .data<&fog_pass::settings::max_opacity>("max_opacity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_opacity"},
            entt::attribute{"pretty_name", "Max Opacity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Density"},
            entt::attribute{"tooltip", "The most the fog may hide. 1 lets distant objects vanish; lower keeps them "
                                       "visible through any amount of fog."},
        })
        .data<&fog_pass::settings::albedo>("albedo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "albedo"},
            entt::attribute{"pretty_name", "Albedo"},
            entt::attribute{"group", "Scattering"},
            entt::attribute{"tooltip", "Tint and brightness of the light the fog scatters. White is bright mist; "
                                       "dark is smoke that absorbs light."},
        })
        .data<&fog_pass::settings::anisotropy>("anisotropy"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "anisotropy"},
            entt::attribute{"pretty_name", "Anisotropy"},
            entt::attribute{"min", -0.95f},
            entt::attribute{"max", 0.95f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Scattering"},
            entt::attribute{"tooltip", "0 scatters sunlight evenly. Higher values make the fog glow around the sun "
                                       "when looking toward it; negative values when looking away from it."},
        })
        .data<&fog_pass::settings::sun_scattering>("sun_scattering"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sun_scattering"},
            entt::attribute{"pretty_name", "Sun Scattering"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Scattering"},
            entt::attribute{"tooltip", "Scale on the sunlight the fog scatters. 1 is physically based; the fog is "
                                       "not shadowed, so lower it where the sun should not reach."},
        })
        .data<&fog_pass::settings::ambient_scattering>("ambient_scattering"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ambient_scattering"},
            entt::attribute{"pretty_name", "Ambient Scattering"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"group", "Scattering"},
            entt::attribute{"tooltip", "Scale on the sky light the fog scatters. 1 is physically based; lower it "
                                       "indoors, where the sky does not reach."},
        })
        .data<&fog_pass::settings::sky_affect>("sky_affect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_affect"},
            entt::attribute{"pretty_name", "Sky Affect"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Sky"},
            entt::attribute{"tooltip", "How much the fog covers the sky. 0 leaves the sky and clouds clear."},
        })
        .data<&fog_pass::settings::sky_distance>("sky_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_distance"},
            entt::attribute{"pretty_name", "Sky Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 10.0f},
            entt::attribute{"group", "Sky"},
            entt::attribute{"tooltip", "Distance (m) the sky is fogged at, as if it were that far away. The fog thins "
                                       "with height, so the sky high above the fog stays clear."},
        })
        .data<&fog_pass::settings::volumetric>("volumetric"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric"},
            entt::attribute{"pretty_name", "Volumetric"},
            entt::attribute{"group", "Volumetric"},
            entt::attribute{"tooltip", "Light the fog near the camera through the sun's shadows and the clouds' shadow: "
                                       "light shafts, and dark fog in shade. Costs about a millisecond. Off where the "
                                       "project's feature limits or the GPU do not allow it."},
        })
        .data<&fog_pass::settings::volumetric_distance>("volumetric_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_distance"},
            entt::attribute{"pretty_name", "Volumetric Distance"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Volumetric"},
            entt::attribute{"tooltip", "How far (m) the volumetric fog reaches; the unshadowed fog continues past it. "
                                       "Longer spreads the same cells over more distance, so they get coarser."},
        })
        .data<&fog_pass::settings::volumetric_quality>("volumetric_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_quality"},
            entt::attribute{"pretty_name", "Volumetric Quality"},
            entt::attribute{"group", "Volumetric"},
            entt::attribute{"tooltip", "Cell grid of the volumetric fog: Low 72 rows, Medium 90, High 135, each 64 "
                                       "slices deep; Ultra 135 rows and 128 slices. Higher is sharper and costs more."},
        })
        .data<&fog_pass::settings::volumetric_slice_uniformity>("volumetric_slice_uniformity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_slice_uniformity"},
            entt::attribute{"pretty_name", "Slice Uniformity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Volumetric"},
            entt::attribute{"tooltip", "How the depth slices spread: 0 packs them near the camera for detail up close, "
                                       "1 spaces them evenly for detail far away."},
        })
        .data<&fog_pass::settings::volumetric_history>("volumetric_history"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric_history"},
            entt::attribute{"pretty_name", "Temporal History"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.98f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"group", "Volumetric"},
            entt::attribute{"tooltip", "Share of each cell's light kept from earlier frames. Higher is smoother; lower "
                                       "follows moving shadows faster but shimmers more."},
        });
}

SAVE_INLINE(fog_pass::settings)
{
    try_save(ar, ser20::make_nvp("attenuation_distance", obj.attenuation_distance));
    try_save(ar, ser20::make_nvp("base_height", obj.base_height));
    try_save(ar, ser20::make_nvp("maximum_height", obj.maximum_height));
    try_save(ar, ser20::make_nvp("start_distance", obj.start_distance));
    try_save(ar, ser20::make_nvp("max_opacity", obj.max_opacity));
    try_save(ar, ser20::make_nvp("albedo", obj.albedo));
    try_save(ar, ser20::make_nvp("anisotropy", obj.anisotropy));
    try_save(ar, ser20::make_nvp("sun_scattering", obj.sun_scattering));
    try_save(ar, ser20::make_nvp("ambient_scattering", obj.ambient_scattering));
    try_save(ar, ser20::make_nvp("sky_affect", obj.sky_affect));
    try_save(ar, ser20::make_nvp("sky_distance", obj.sky_distance));
    try_save(ar, ser20::make_nvp("volumetric", obj.volumetric));
    try_save(ar, ser20::make_nvp("volumetric_distance", obj.volumetric_distance));
    try_save(ar, ser20::make_nvp("volumetric_quality", obj.volumetric_quality));
    try_save(ar, ser20::make_nvp("volumetric_slice_uniformity", obj.volumetric_slice_uniformity));
    try_save(ar, ser20::make_nvp("volumetric_history", obj.volumetric_history));
}
SAVE_INSTANTIATE(fog_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(fog_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(fog_pass::settings)
{
    try_load(ar, ser20::make_nvp("attenuation_distance", obj.attenuation_distance));
    try_load(ar, ser20::make_nvp("base_height", obj.base_height));
    try_load(ar, ser20::make_nvp("maximum_height", obj.maximum_height));
    try_load(ar, ser20::make_nvp("start_distance", obj.start_distance));
    try_load(ar, ser20::make_nvp("max_opacity", obj.max_opacity));
    try_load(ar, ser20::make_nvp("albedo", obj.albedo));
    try_load(ar, ser20::make_nvp("anisotropy", obj.anisotropy));
    try_load(ar, ser20::make_nvp("sun_scattering", obj.sun_scattering));
    try_load(ar, ser20::make_nvp("ambient_scattering", obj.ambient_scattering));
    try_load(ar, ser20::make_nvp("sky_affect", obj.sky_affect));
    try_load(ar, ser20::make_nvp("sky_distance", obj.sky_distance));
    try_load(ar, ser20::make_nvp("volumetric", obj.volumetric));
    try_load(ar, ser20::make_nvp("volumetric_distance", obj.volumetric_distance));
    try_load(ar, ser20::make_nvp("volumetric_quality", obj.volumetric_quality));
    try_load(ar, ser20::make_nvp("volumetric_slice_uniformity", obj.volumetric_slice_uniformity));
    try_load(ar, ser20::make_nvp("volumetric_history", obj.volumetric_history));
}
LOAD_INSTANTIATE(fog_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(fog_pass::settings, ser20::iarchive_binary_t);

REFLECT(fog_component)
{
    entt::meta_factory<fog_component>{}
        .type("fog_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "fog_component"},
            entt::attribute{"category", "Rendering/Post Processing"},
            entt::attribute{"pretty_name", "Fog"},
        })
        .func<&component_meta<fog_component>::exists>("component_exists"_hs)
        .func<&component_meta<fog_component>::add>("component_add"_hs)
        .func<&component_meta<fog_component>::remove>("component_remove"_hs)
        .func<&component_meta<fog_component>::save>("component_save"_hs)
        .func<&component_meta<fog_component>::load>("component_load"_hs)
        .data<&fog_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable the height fog"},
        })
        .data<&fog_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(fog_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(fog_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(fog_component, ser20::oarchive_binary_t);

LOAD(fog_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(fog_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(fog_component, ser20::iarchive_binary_t);

} // namespace unravel
