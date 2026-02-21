#include "light_component.hpp"

#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/light.hpp>
#include <engine/meta/assets/asset_handle.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(light_component)
{
    entt::meta_factory<light_component>{}
        .type("light_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Light"},
        })
        .func<&component_meta<light_component>::exists>("component_exists"_hs)
        .func<&component_meta<light_component>::add>("component_add"_hs)
        .func<&component_meta<light_component>::save>("component_save"_hs)
        .func<&component_meta<light_component>::load>("component_load"_hs)
        .func<&component_meta<light_component>::remove>("component_remove"_hs)
        .data<&light_component::set_light, &light_component::get_light>("light"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light"},
            entt::attribute{"pretty_name", "Light"},
        });
}

SAVE(light_component)
{
    try_save(ar, ser20::make_nvp("light", obj.get_light()));
}
SAVE_INSTANTIATE(light_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(light_component, ser20::oarchive_binary_t);

LOAD(light_component)
{
    light l;
    try_load(ar, ser20::make_nvp("light", l));
    obj.set_light(l);
}
LOAD_INSTANTIATE(light_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(light_component, ser20::iarchive_binary_t);

REFLECT(skylight_component)
{            
    auto skybox_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<skylight_component>();
        return data->get_mode() == skylight_component::sky_mode::skybox;
    });


    auto dynamic_sky_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<skylight_component>();
        return data->get_mode() != skylight_component::sky_mode::skybox;
    });

    auto perez_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<skylight_component>();
        return data->get_mode() == skylight_component::sky_mode::perez;
    });

    // Register skylight_component::sky_mode enum with entt
    entt::meta_factory<skylight_component::sky_mode>{}
        .type("sky_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_mode"},
            entt::attribute{"pretty_name", "Sky Mode"},
        })
        .data<skylight_component::sky_mode::standard>("standard"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "standard"},
            entt::attribute{"pretty_name", "Standard"},
        })
        .data<skylight_component::sky_mode::perez>("perez"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "perez"},
            entt::attribute{"pretty_name", "Perez"},
        })
        .data<skylight_component::sky_mode::skybox>("skybox"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skybox"},
            entt::attribute{"pretty_name", "Skybox"},
        });

    // Register skylight_component::irradiance_quality enum with entt
    entt::meta_factory<skylight_component::irradiance_quality>{}
        .type("irradiance_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_quality"},
            entt::attribute{"pretty_name", "Irradiance Quality"},
        })
        .data<skylight_component::irradiance_quality::uniform>("uniform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "uniform"},
            entt::attribute{"pretty_name", "Uniform"},
            entt::attribute{"tooltip", "Uniform ambient from Perez luminance (perez, standard)."},
        })
        .data<skylight_component::irradiance_quality::normal_dependent>("normal_dependent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_dependent"},
            entt::attribute{"pretty_name", "Normal-Dependent"},
            entt::attribute{"tooltip", "Normal-dependent irradiance via spherical harmonics."},
        });

    // Register skylight_component class with entt
    entt::meta_factory<skylight_component>{}
        .type("skylight_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skylight_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Skylight"},
        })
        .func<&component_meta<skylight_component>::exists>("component_exists"_hs)
        .func<&component_meta<skylight_component>::add>("component_add"_hs)
        .func<&component_meta<skylight_component>::save>("component_save"_hs)
        .func<&component_meta<skylight_component>::load>("component_load"_hs)
        .func<&component_meta<skylight_component>::remove>("component_remove"_hs)
        .data<&skylight_component::set_mode, &skylight_component::get_mode>("mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mode"},
            entt::attribute{"pretty_name", "Mode"},
        })
        .data<&skylight_component::set_turbidity, &skylight_component::get_turbidity>("turbidity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "turbidity"},
            entt::attribute{"pretty_name", "Turbidity"},
            entt::attribute{"min", 1.9f},
            entt::attribute{"max", 10.0f},
            entt::attribute{"tooltip", "Adjusts the clarity of the atmosphere. Lower values (1.9) result in a clear, blue sky, while higher values (up to 10) create a hazy, diffused appearance with more scattering of light.."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt}, 
        })
        .data<&skylight_component::set_cloud_coverage, &skylight_component::get_cloud_coverage>("cloud_coverage"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_coverage"},
            entt::attribute{"pretty_name", "Cloud Coverage"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls cloud density. 0.0 = clear sky, 1.0 = overcast. Higher values create more clouds by lowering the density threshold."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt},
        })
        .data<&skylight_component::set_cloud_altitude, &skylight_component::get_cloud_altitude>("cloud_altitude"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_altitude"},
            entt::attribute{"pretty_name", "Cloud Altitude"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"tooltip", "Cloud layer altitude in world units. Higher values create smaller, more distant clouds."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt},
        })
        .data<&skylight_component::set_cloud_speed, &skylight_component::get_cloud_speed>("cloud_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_speed"},
            entt::attribute{"pretty_name", "Cloud Speed"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"tooltip", "Wind speed multiplier for cloud animation. Controls how fast clouds drift across the sky."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt},
        })
        .data<&skylight_component::set_cloud_density, &skylight_component::get_cloud_density>("cloud_density"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_density"},
            entt::attribute{"pretty_name", "Cloud Density"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Cloud opacity multiplier. Higher values make clouds more opaque and visible."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt},
        })
        .data<&skylight_component::set_irradiance_intensity, &skylight_component::get_irradiance_intensity>("irradiance_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_intensity"},
            entt::attribute{"pretty_name", "Irradiance Intensity"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Strength of indirect diffuse lighting from the sky."},
        })
        .data<&skylight_component::set_irradiance_tint, &skylight_component::get_irradiance_tint>("irradiance_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_tint"},
            entt::attribute{"pretty_name", "Irradiance Tint"},
            entt::attribute{"tooltip", "Optional tint override for irradiance (white = use sky-derived color)."},
        })
        .data<&skylight_component::set_irradiance_quality, &skylight_component::get_irradiance_quality>("irradiance_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_quality"},
            entt::attribute{"pretty_name", "IrradianceQuality"},
            entt::attribute{"tooltip", "Quality of irradiance for indirect diffuse lighting."},
        })
        .data<&skylight_component::set_cubemap, &skylight_component::get_cubemap>("cubemap"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cubemap"},
            entt::attribute{"pretty_name", "Cubemap"},
            entt::attribute{"predicate", skybox_predicate_entt}, 
        });
}

SAVE(skylight_component)
{
    try_save(ar, ser20::make_nvp("mode", obj.get_mode()));
    try_save(ar, ser20::make_nvp("turbidity", obj.get_turbidity()));
    try_save(ar, ser20::make_nvp("cloud_coverage", obj.get_cloud_coverage()));
    try_save(ar, ser20::make_nvp("cloud_altitude", obj.get_cloud_altitude()));
    try_save(ar, ser20::make_nvp("cloud_speed", obj.get_cloud_speed()));
    try_save(ar, ser20::make_nvp("cloud_density", obj.get_cloud_density()));
    try_save(ar, ser20::make_nvp("irradiance_intensity", obj.get_irradiance_intensity()));
    try_save(ar, ser20::make_nvp("irradiance_tint", obj.get_irradiance_tint()));
    try_save(ar, ser20::make_nvp("irradiance_quality", obj.get_irradiance_quality()));
    try_save(ar, ser20::make_nvp("cubemap", obj.get_cubemap()));
}
SAVE_INSTANTIATE(skylight_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(skylight_component, ser20::oarchive_binary_t);

LOAD(skylight_component)
{
    skylight_component::sky_mode mode;
    if(try_load(ar, ser20::make_nvp("mode", mode)))
    {
        obj.set_mode(mode);
    }

    float turbidity{};
    if(try_load(ar, ser20::make_nvp("turbidity", turbidity)))
    {
        obj.set_turbidity(turbidity);
    }

    float cloud_coverage{};
    if(try_load(ar, ser20::make_nvp("cloud_coverage", cloud_coverage)))
    {
        obj.set_cloud_coverage(cloud_coverage);
    }

    float cloud_altitude{};
    if(try_load(ar, ser20::make_nvp("cloud_altitude", cloud_altitude)))
    {
        obj.set_cloud_altitude(cloud_altitude);
    }

    float cloud_speed{};
    if(try_load(ar, ser20::make_nvp("cloud_speed", cloud_speed)))
    {
        obj.set_cloud_speed(cloud_speed);
    }

    float cloud_density{};
    if(try_load(ar, ser20::make_nvp("cloud_density", cloud_density)))
    {
        obj.set_cloud_density(cloud_density);
    }

    float irradiance_intensity{};
    if(try_load(ar, ser20::make_nvp("irradiance_intensity", irradiance_intensity)))
    {
        obj.set_irradiance_intensity(irradiance_intensity);
    }

    math::color irradiance_tint;
    if(try_load(ar, ser20::make_nvp("irradiance_tint", irradiance_tint)))
    {
        obj.set_irradiance_tint(irradiance_tint);
    }

    skylight_component::irradiance_quality irradiance_quality{skylight_component::irradiance_quality::uniform};
    if(try_load(ar, ser20::make_nvp("irradiance_quality", irradiance_quality)))
    {
        obj.set_irradiance_quality(irradiance_quality);
    }

    asset_handle<gfx::texture> cubemap;
    if(try_load(ar, ser20::make_nvp("cubemap", cubemap)))
    {
        obj.set_cubemap(cubemap);
    }
}
LOAD_INSTANTIATE(skylight_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(skylight_component, ser20::iarchive_binary_t);
} // namespace unravel
