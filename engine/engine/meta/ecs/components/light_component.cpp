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
    rttr::registration::class_<light_component>("light_component")(rttr::metadata("category", "LIGHTING"),
                                                                   rttr::metadata("pretty_name", "Light"))
        .constructor<>()
        .method("component_exists", &component_exists<light_component>)

        .property("light", &light_component::get_light, &light_component::set_light)(
            rttr::metadata("pretty_name", "Light"));

    // Register light_component class with entt
    entt::meta_factory<light_component>{}
        .type("light_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "light_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Light"},
        })
        .func<&component_exists<light_component>>("component_exists"_hs)
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
    rttr::registration::enumeration<skylight_component::sky_mode>("sky_mode")(
        rttr::value("Standard", skylight_component::sky_mode::standard),
        rttr::value("Perez", skylight_component::sky_mode::perez),
        rttr::value("Skybox", skylight_component::sky_mode::skybox));

    auto skybox_predicate = rttr::property_predicate([](rttr::instance& obj)
    {
        auto data = obj.try_convert<skylight_component>();
        return data->get_mode() == skylight_component::sky_mode::skybox;
    });


    auto dynamic_sky_predicate = rttr::property_predicate([](rttr::instance& obj)
    {
        auto data = obj.try_convert<skylight_component>();
        return data->get_mode() != skylight_component::sky_mode::skybox;
    });


    rttr::registration::class_<skylight_component>("skylight_component")(rttr::metadata("category", "LIGHTING"),
                                                                         rttr::metadata("pretty_name", "Skylight"))
        .constructor<>()
        .property("mode", &skylight_component::get_mode, &skylight_component::set_mode)(
            rttr::metadata("pretty_name", "Mode"))
        .property("turbidity", &skylight_component::get_turbidity, &skylight_component::set_turbidity)(
            rttr::metadata("predicate", dynamic_sky_predicate),
            rttr::metadata("pretty_name", "Turbidity"),
            rttr::metadata("min", 1.9f),
            rttr::metadata("max", 10.0f),
            rttr::metadata(
                "tooltip",
                "Adjusts the clarity of the atmosphere. Lower values (1.9) result in a clear, blue sky, while higher "
                "values (up to 10) create a hazy, diffused appearance with more scattering of light.."))
        .property("cubemap", &skylight_component::get_cubemap, &skylight_component::set_cubemap)(
            rttr::metadata("pretty_name", "Cubemap"),
            rttr::metadata("predicate", skybox_predicate));


            
    auto skybox_predicate_entt = entt::property_predicate([](entt::meta_handle& obj)
    {
        auto data = obj->try_cast<skylight_component>();
        return data->get_mode() == skylight_component::sky_mode::skybox;
    });


    auto dynamic_sky_predicate_entt = entt::property_predicate([](entt::meta_handle& obj)
    {
        auto data = obj->try_cast<skylight_component>();
        return data->get_mode() != skylight_component::sky_mode::skybox;
    });

    // Register skylight_component::sky_mode enum with entt
    entt::meta_factory<skylight_component::sky_mode>{}
        .type("sky_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sky_mode"},
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

    // Register skylight_component class with entt
    entt::meta_factory<skylight_component>{}
        .type("skylight_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skylight_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Skylight"},
        })
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

    asset_handle<gfx::texture> cubemap;
    if(try_load(ar, ser20::make_nvp("cubemap", cubemap)))
    {
        obj.set_cubemap(cubemap);
    }
}
LOAD_INSTANTIATE(skylight_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(skylight_component, ser20::iarchive_binary_t);
} // namespace unravel
