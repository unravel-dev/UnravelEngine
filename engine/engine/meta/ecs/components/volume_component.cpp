#include "volume_component.hpp"
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <engine/meta/core/math/vector.hpp>

namespace unravel
{

REFLECT_INLINE(volume_mode)
{
    entt::meta_factory<volume_mode>{}
        .type("volume_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volume_mode"},
            entt::attribute{"pretty_name", "Mode"},
        })
        .data<volume_mode::local>("local"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local"},
            entt::attribute{"pretty_name", "Local"},
            entt::attribute{"tooltip", "Volume uses bounds (extents)"},
        })
        .data<volume_mode::global>("global"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global"},
            entt::attribute{"pretty_name", "Global"},
            entt::attribute{"tooltip", "Volume affects camera everywhere (ignores bounds)"},
        });
}

REFLECT(volume_component)
{
    auto local_mode_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj)
    {
        auto* data = obj.try_cast<volume_component>();
        return data && data->mode == volume_mode::local;
    });

    entt::meta_factory<volume_component>{}
        .type("volume_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "volume_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Volume"},
        })
        .func<&component_meta<volume_component>::exists>("component_exists"_hs)
        .func<&component_meta<volume_component>::add>("component_add"_hs)
        .func<&component_meta<volume_component>::remove>("component_remove"_hs)
        .func<&component_meta<volume_component>::save>("component_save"_hs)
        .func<&component_meta<volume_component>::load>("component_load"_hs)
        .data<&volume_component::mode>("mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mode"},
            entt::attribute{"pretty_name", "Mode"},
            entt::attribute{"tooltip", "Local uses bounds, global affects everywhere"},
        })
        .data<&volume_component::priority>("priority"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "priority"},
            entt::attribute{"pretty_name", "Priority"},
            entt::attribute{"tooltip", "Higher priority wins when multiple volumes overlap"},
        })
        .data<&volume_component::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.05f},
            entt::attribute{"tooltip", "Influence multiplier"},
        })
        .data<&volume_component::blend_distance>("blend_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_distance"},
            entt::attribute{"pretty_name", "Blend Distance"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"tooltip", "Distance outside volume over which contribution ramps from 0 to 1"},
            entt::attribute{"predicate", local_mode_predicate_entt}
        })
        .data<&volume_component::extents>("extents"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "extents"},
            entt::attribute{"pretty_name", "Extents"},
            entt::attribute{"tooltip", "Half-extents for local box bounds"},
            entt::attribute{"predicate", local_mode_predicate_entt},
        });
}

SAVE(volume_component)
{
    try_save(ar, ser20::make_nvp("mode", static_cast<std::underlying_type_t<volume_mode>>(obj.mode)));
    try_save(ar, ser20::make_nvp("priority", obj.priority));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("blend_distance", obj.blend_distance));
    try_save(ar, ser20::make_nvp("extents", obj.extents));
}
SAVE_INSTANTIATE(volume_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(volume_component, ser20::oarchive_binary_t);

LOAD(volume_component)
{
    bool is_global = false;
    if(try_load(ar, ser20::make_nvp("is_global", is_global)))
    {
        obj.mode = is_global ? volume_mode::global : volume_mode::local;
    }
    else
    {
        std::underlying_type_t<volume_mode> mode_val = 0;
        try_load(ar, ser20::make_nvp("mode", mode_val));
        obj.mode = static_cast<volume_mode>(mode_val);
    }
    try_load(ar, ser20::make_nvp("priority", obj.priority));
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    try_load(ar, ser20::make_nvp("blend_distance", obj.blend_distance));
    try_load(ar, ser20::make_nvp("extents", obj.extents));
}
LOAD_INSTANTIATE(volume_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(volume_component, ser20::iarchive_binary_t);

} // namespace unravel
