#include "test_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(test_component)
{
    rttr::registration::class_<named_anim>("named_anim")(rttr::metadata("category", "BASIC"),
                                                         rttr::metadata("pretty_name", "Named Anim"))
        .constructor<>()()
        .property("name", &named_anim::name)
        .property("clip", &named_anim::clip);

    rttr::registration::class_<test_component>("test_component")(rttr::metadata("category", "BASIC"),
                                                                 rttr::metadata("pretty_name", "Test"))
        .constructor<>()()
        .method("component_exists", &component_exists<test_component>)
        .property("str", &test_component::str)
        .property("u8", &test_component::u8)
        .property("u8[restricted]", &test_component::u8)(rttr::metadata("min", 12), rttr::metadata("max", 200))
        .property("u16", &test_component::u16)
        .property("u32", &test_component::u32)
        .property("u64", &test_component::u64)
        .property("i8", &test_component::i8)
        .property("i16", &test_component::i16)
        .property("i32", &test_component::i32)
        .property("i64", &test_component::i64)
        .property("f", &test_component::f)
        .property("d", &test_component::d)
        .property("d[restricted]", &test_component::d)(rttr::metadata("min", 12.0), rttr::metadata("max", 200.0))
        .property("irange", &test_component::irange)
        .property("isize", &test_component::isize)
        .property("ipoint", &test_component::ipoint)
        .property("irect", &test_component::irect)
        .property("delta", &test_component::delta)
        .property("color", &test_component::color)
        .property("texture", &test_component::texture)
        .property("mat", &test_component::mat)
        .property("anim", &test_component::anim)
        .property("sequential", &test_component::sequential)
        .property("associative", &test_component::associative)
        .property("associative_mock", &test_component::associative_mock);

    // Register named_anim class with entt
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
