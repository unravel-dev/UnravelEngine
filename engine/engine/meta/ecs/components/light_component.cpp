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

    auto clouds_enabled_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<skylight_component>();
        return data->get_mode() != skylight_component::sky_mode::skybox
            && data->get_cloud_mode() != skylight_component::cloud_mode::none;
    });

    auto volumetric_cloud_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto data = obj.try_cast<skylight_component>();
        return data->get_mode() != skylight_component::sky_mode::skybox
            && data->get_cloud_mode() == skylight_component::cloud_mode::volumetric;
    });

    // Register skylight_component::sky_mode enum with entt
    entt::meta_factory<skylight_component::sky_mode>{}
        .type("sky_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_mode"},
            entt::attribute{"pretty_name", "Sky Mode"},
        })
        // .data<skylight_component::sky_mode::reserved_0>("reserved_0"_hs)
        // .custom<entt::attributes>(entt::attributes{
        //     entt::attribute{"name", "standard"},
        //     entt::attribute{"pretty_name", "Standard"},
        // })
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
            entt::attribute{"tooltip", "Uniform ambient from Perez luminance (perez)."},
        })
        .data<skylight_component::irradiance_quality::normal_dependent>("normal_dependent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_dependent"},
            entt::attribute{"pretty_name", "Normal-Dependent"},
            entt::attribute{"tooltip", "Normal-dependent irradiance via spherical harmonics."},
        });

    entt::meta_factory<skylight_component::cloud_mode>{}
        .type("cloud_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_mode"},
            entt::attribute{"pretty_name", "Cloud Mode"},
        })
        .data<skylight_component::cloud_mode::none>("none"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "none"},
            entt::attribute{"pretty_name", "None"},
            entt::attribute{"tooltip", "No clouds rendered."},
        })
        .data<skylight_component::cloud_mode::flat>("flat"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "flat"},
            entt::attribute{"pretty_name", "Flat"},
            entt::attribute{"tooltip", "Flat projected clouds. Cheap single-sample scattering on a dome plane."},
        })
        .data<skylight_component::cloud_mode::volumetric>("volumetric"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volumetric"},
            entt::attribute{"pretty_name", "Volumetric"},
            entt::attribute{"tooltip", "Volumetric raymarched clouds. Full scattering at half resolution with temporal blending."},
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
        .data<&skylight_component::set_sky_brightness, &skylight_component::get_sky_brightness>("sky_brightness"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_brightness"},
            entt::attribute{"pretty_name", "Sky Brightness"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Brightness multiplier for sky and irradiance (1.0 = neutral)."},
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
        .data<&skylight_component::set_cloud_mode, &skylight_component::get_cloud_mode>("cloud_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_mode"},
            entt::attribute{"pretty_name", "Cloud Mode"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"tooltip", "Cloud rendering mode. None disables clouds, Flat uses cheap projected clouds, Volumetric uses full raymarched clouds."},
            entt::attribute{"predicate", dynamic_sky_predicate_entt},
        })
        .data<&skylight_component::set_cloud_coverage, &skylight_component::get_cloud_coverage>("cloud_coverage"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_coverage"},
            entt::attribute{"pretty_name", "Cloud Coverage"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Controls cloud density. 0.0 = clear sky, 1.0 = overcast. Higher values create more clouds by lowering the density threshold."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_cloud_base_altitude, &skylight_component::get_cloud_base_altitude>("cloud_base_altitude"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_base_altitude"},
            entt::attribute{"pretty_name", "Cloud Base Altitude"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 100.0f},
            entt::attribute{"max", 60000.0f},
            entt::attribute{"step", 500.0f},
            entt::attribute{"tooltip", "Cloud layer base altitude in world units. Volumetric: slab bottom. Flat: projection height."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_cloud_top_altitude, &skylight_component::get_cloud_top_altitude>("cloud_top_altitude"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_top_altitude"},
            entt::attribute{"pretty_name", "Cloud Top Altitude"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 200.0f},
            entt::attribute{"max", 80000.0f},
            entt::attribute{"step", 500.0f},
            entt::attribute{"tooltip", "Cloud layer top altitude in world units. Only used by volumetric clouds (slab top). Flat clouds ignore this."},
            entt::attribute{"predicate", volumetric_cloud_predicate_entt},
        })
        .data<&skylight_component::set_cloud_speed, &skylight_component::get_cloud_speed>("cloud_speed"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_speed"},
            entt::attribute{"pretty_name", "Cloud Speed (km/h)"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 200.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"tooltip", "Cloud wind speed in km/h. ~10 = gentle drift, ~50 = strong wind, ~100+ = storm."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_cloud_density, &skylight_component::get_cloud_density>("cloud_density"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_density"},
            entt::attribute{"pretty_name", "Cloud Density"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Cloud opacity multiplier. Higher values make clouds more opaque and visible."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_cloud_absorption, &skylight_component::get_cloud_absorption>("cloud_absorption"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_absorption"},
            entt::attribute{"pretty_name", "Cloud Absorption"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Beer-Lambert extinction coefficient. Controls how quickly light is absorbed passing through the cloud. Higher = more opaque."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_cloud_light_absorption, &skylight_component::get_cloud_light_absorption>("cloud_light_absorption"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cloud_light_absorption"},
            entt::attribute{"pretty_name", "Cloud Light Absorption"},
            entt::attribute{"group", "Clouds"},
            entt::attribute{"min", 0.01f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Self-shadow strength. Controls how much light is absorbed along the sun-facing direction. Higher = darker shadow side."},
            entt::attribute{"predicate", clouds_enabled_predicate_entt},
        })
        .data<&skylight_component::set_irradiance_intensity, &skylight_component::get_irradiance_intensity>("irradiance_intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_intensity"},
            entt::attribute{"pretty_name", "Irradiance Intensity"},
            entt::attribute{"group", "Irradiance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "Strength of indirect diffuse lighting from the sky."},
        })
        .data<&skylight_component::set_irradiance_tint, &skylight_component::get_irradiance_tint>("irradiance_tint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_tint"},
            entt::attribute{"pretty_name", "Irradiance Tint"},
            entt::attribute{"group", "Irradiance"},
            entt::attribute{"tooltip", "Optional tint override for irradiance (white = use sky-derived color)."},
        })
        .data<&skylight_component::set_irradiance_quality, &skylight_component::get_irradiance_quality>("irradiance_quality"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irradiance_quality"},
            entt::attribute{"pretty_name", "Irradiance Quality"},
            entt::attribute{"group", "Irradiance"},
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
    try_save(ar, ser20::make_nvp("sky_brightness", obj.get_sky_brightness()));
    try_save(ar, ser20::make_nvp("turbidity", obj.get_turbidity()));
    try_save(ar, ser20::make_nvp("cloud_mode", obj.get_cloud_mode()));
    try_save(ar, ser20::make_nvp("cloud_coverage", obj.get_cloud_coverage()));
    try_save(ar, ser20::make_nvp("cloud_base_altitude", obj.get_cloud_base_altitude()));
    try_save(ar, ser20::make_nvp("cloud_top_altitude", obj.get_cloud_top_altitude()));
    try_save(ar, ser20::make_nvp("cloud_speed", obj.get_cloud_speed()));
    try_save(ar, ser20::make_nvp("cloud_density", obj.get_cloud_density()));
    try_save(ar, ser20::make_nvp("cloud_absorption", obj.get_cloud_absorption()));
    try_save(ar, ser20::make_nvp("cloud_light_absorption", obj.get_cloud_light_absorption()));
    try_save(ar, ser20::make_nvp("irradiance_intensity", obj.get_irradiance_intensity()));
    try_save(ar, ser20::make_nvp("irradiance_tint", obj.get_irradiance_tint()));
    try_save(ar, ser20::make_nvp("irradiance_quality", obj.get_irradiance_quality()));
    try_save(ar, ser20::make_nvp("cubemap", obj.get_cubemap()));
}
SAVE_INSTANTIATE(skylight_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(skylight_component, ser20::oarchive_binary_t);

LOAD(skylight_component)
{
    skylight_component::sky_mode mode{skylight_component::sky_mode::perez};
    if(try_load(ar, ser20::make_nvp("mode", mode)))
    {
        obj.set_mode(mode);
    }

    float sky_brightness{1.0f};
    if(try_load(ar, ser20::make_nvp("sky_brightness", sky_brightness)))
    {
        obj.set_sky_brightness(sky_brightness);
    }

    float turbidity{};
    if(try_load(ar, ser20::make_nvp("turbidity", turbidity)))
    {
        obj.set_turbidity(turbidity);
    }

    skylight_component::cloud_mode cloud_mode{skylight_component::cloud_mode::volumetric};
    if(try_load(ar, ser20::make_nvp("cloud_mode", cloud_mode)))
    {
        obj.set_cloud_mode(cloud_mode);
    }

    float cloud_coverage{};
    if(try_load(ar, ser20::make_nvp("cloud_coverage", cloud_coverage)))
    {
        obj.set_cloud_coverage(cloud_coverage);
    }

    float cloud_base_altitude{};
    if(try_load(ar, ser20::make_nvp("cloud_base_altitude", cloud_base_altitude)))
    {
        obj.set_cloud_base_altitude(cloud_base_altitude);
    }

    float cloud_top_altitude{};
    if(try_load(ar, ser20::make_nvp("cloud_top_altitude", cloud_top_altitude)))
    {
        obj.set_cloud_top_altitude(cloud_top_altitude);
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

    float cloud_absorption{};
    if(try_load(ar, ser20::make_nvp("cloud_absorption", cloud_absorption)))
    {
        obj.set_cloud_absorption(cloud_absorption);
    }

    float cloud_light_absorption{};
    if(try_load(ar, ser20::make_nvp("cloud_light_absorption", cloud_light_absorption)))
    {
        obj.set_cloud_light_absorption(cloud_light_absorption);
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
