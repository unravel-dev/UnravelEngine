#include "reflection_probe.hpp"
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(reflection_probe)
{
    auto box_predicate_entt = entt::property_predicate(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<reflection_probe>();
            return data->type == probe_type::box;
        });
    auto sphere_predicate_entt = entt::property_predicate(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<reflection_probe>();
            return data->type == probe_type::sphere;
        });
    // EnTT meta registration mirroring RTTR
    entt::meta_factory<probe_type>{}
        .type("probe_type"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe_type"},  
            entt::attribute{"pretty_name", "Probe Type"},
        })
        .data<probe_type::box>("box"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "box"},
            entt::attribute{"pretty_name", "Box"} 
        })
        .data<probe_type::sphere>("sphere"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "sphere"},
            entt::attribute{"pretty_name", "Sphere"} 
        });

    entt::meta_factory<reflect_method>{}
        .type("reflect_method"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflect_method"},
            entt::attribute{"pretty_name", "Reflect Method"},
        })
        .data<reflect_method::environment>("environment"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "environment"},
            entt::attribute{"pretty_name", "Environment"} 
        })
        .data<reflect_method::static_only>("static_only"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "static_only"},
            entt::attribute{"pretty_name", "Static Only"} 
        });

    entt::meta_factory<reflection_probe::box>{}
        .type("box"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "box"},
            entt::attribute{"pretty_name", "Box"},
        })
        .data<&reflection_probe::box::extents>("extents"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "extents"},
            entt::attribute{"pretty_name", "Extents"} 
        })
        .data<&reflection_probe::box::transition_distance>("transition_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "transition_distance"},
            entt::attribute{"pretty_name", "Transition Distance"},
            entt::attribute{"min", 0.0f},
        });

    entt::meta_factory<reflection_probe::sphere>{}
        .type("sphere"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sphere"},
            entt::attribute{"pretty_name", "Sphere"},
        })
        .data<&reflection_probe::sphere::range>("range"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "range"},
            entt::attribute{"pretty_name", "Range"},
            entt::attribute{"min", 0.0f},
        });

    entt::meta_factory<reflection_probe>{}
        .type("reflection_probe"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_probe"},
            entt::attribute{"pretty_name", "Reflection Probe"},
        })
        .data<&reflection_probe::type>("type"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "type"},
            entt::attribute{"pretty_name", "Type"} 
        })
        .data<&reflection_probe::method>("method"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "method"},
            entt::attribute{"pretty_name", "Method"} 
        })
        .data<&reflection_probe::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 3.0f},
        })
        .data<&reflection_probe::box_data>("box_data"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "box_data"},
            entt::attribute{"pretty_name", "Box"},
            entt::attribute{"predicate", box_predicate_entt},
        })
        .data<&reflection_probe::sphere_data>("sphere_data"_hs)
        .custom<entt::attributes>(entt::attributes{ 
            entt::attribute{"name", "sphere_data"},
            entt::attribute{"pretty_name", "Sphere"},
            entt::attribute{"predicate", sphere_predicate_entt},
        });
}

SAVE(reflection_probe)
{
    try_save(ar, ser20::make_nvp("type", obj.type));
    try_save(ar, ser20::make_nvp("method", obj.method));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    if(obj.type == probe_type::box)
    {
        try_save(ar, ser20::make_nvp("extents", obj.box_data.extents));
        try_save(ar, ser20::make_nvp("transition_distance", obj.box_data.transition_distance));
    }
    else
    {
        try_save(ar, ser20::make_nvp("range", obj.sphere_data.range));
    }
}
SAVE_INSTANTIATE(reflection_probe, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(reflection_probe, ser20::oarchive_binary_t);

LOAD(reflection_probe)
{
    try_load(ar, ser20::make_nvp("type", obj.type));
    try_load(ar, ser20::make_nvp("method", obj.method));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    if(obj.type == probe_type::box)
    {
        try_load(ar, ser20::make_nvp("extents", obj.box_data.extents));
        try_load(ar, ser20::make_nvp("transition_distance", obj.box_data.transition_distance));
    }
    else
    {
        try_load(ar, ser20::make_nvp("range", obj.sphere_data.range));
    }
}
LOAD_INSTANTIATE(reflection_probe, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(reflection_probe, ser20::iarchive_binary_t);
} // namespace unravel
