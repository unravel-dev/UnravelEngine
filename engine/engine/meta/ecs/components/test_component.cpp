#include "test_component.hpp"
#include "entt/meta/policy.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(test_component)
{
    entt::meta_factory<named_anim>{}
        .type("named_anim"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "named_anim"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Named Anim"},
        })
        .data<&named_anim::name>("name"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "name"},
        })
        .data<&named_anim::clip>("clip"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clip"},
        });

    // Register test_component class with entt (complete implementation)
    entt::meta_factory<test_component>{}
        .type("test_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "test_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Test"},
        })
        .func<&component_exists<test_component>>("component_exists"_hs)
        .data<&test_component::str>("str"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "str"},
        })
        .data<&test_component::u8>("u8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "u8"},
        })
        .data<&test_component::u16>("u16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "u16"},
        })
        .data<&test_component::u32>("u32"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "u32"},
        })
        .data<&test_component::u64>("u64"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "u64"},
        })
        .data<&test_component::i8>("i8"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "i8"},
        })
        .data<&test_component::i16>("i16"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "i16"},
        })
        .data<&test_component::i32>("i32"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "i32"},
        })
        .data<&test_component::i64>("i64"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "i64"},
        })
        .data<&test_component::f>("f"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "f"},
        })
        .data<&test_component::d>("d"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "d"},
        })
        .data<&test_component::irange>("irange"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irange"},
        })
        .data<&test_component::isize>("isize"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "isize"},
        })
        .data<&test_component::ipoint>("ipoint"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ipoint"},
        })
        .data<&test_component::irect>("irect"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "irect"},
        })
        .data<&test_component::delta>("delta"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "delta"},
        })
        .data<&test_component::color>("color"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "color"},
        })
        .data<&test_component::texture>("texture"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture"},
        })
        .data<&test_component::mat>("mat"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "mat"},
        })
        .data<&test_component::anim>("anim"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "anim"},
        })
        .data<&test_component::sequential>("sequential"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sequential"},
        })
        .data<&test_component::associative>("associative"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "associative"},
        })
        .data<&test_component::associative_mock>("associative_mock"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "associative_mock"},
        });
}

SAVE(test_component)
{
}
SAVE_INSTANTIATE(test_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(test_component, ser20::oarchive_binary_t);

LOAD(test_component)
{
}
LOAD_INSTANTIATE(test_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(test_component, ser20::iarchive_binary_t);

} // namespace unravel
