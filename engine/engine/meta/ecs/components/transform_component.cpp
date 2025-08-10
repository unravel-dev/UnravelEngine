#include "transform_component.hpp"

#include "../../core/math/transform.hpp"
#include "../../core/math/vector.hpp"
#include "../entity.hpp"
#include "entt/meta/meta.hpp"
#include "glm/fwd.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(transform_component)
{
    auto invisible_predicate = rttr::property_predicate(
        [](rttr::instance& i)
        {
            return false;
        });

    auto get_translation = rttr::select_overload<const math::vec3&() const>(&math::transform::get_translation);
    auto set_translation = rttr::select_overload<void(const math::vec3&)>(&math::transform::set_translation);

    auto get_rotation = rttr::select_overload<const math::quat&() const>(&math::transform::get_rotation);
    auto set_rotation = rttr::select_overload<void(const math::quat&)>(&math::transform::set_rotation);

    auto get_scale = rttr::select_overload<const math::vec3&() const>(&math::transform::get_scale);
    auto set_scale = rttr::select_overload<void(const math::vec3&)>(&math::transform::set_scale);

    auto get_skew = rttr::select_overload<const math::vec3&() const>(&math::transform::get_skew);
    auto set_skew = rttr::select_overload<void(const math::vec3&)>(&math::transform::set_skew);

    rttr::registration::class_<math::transform>("transform")(rttr::metadata("pretty_name", "Transform"))
        .constructor<>()()
        .property("position", get_translation, set_translation)(rttr::metadata("pretty_name", "Position"))
        .property("rotation", get_rotation, set_rotation)(rttr::metadata("pretty_name", "Rotation"))
        .property("scale", get_scale, set_scale)(rttr::metadata("pretty_name", "Scale"))
        .property("skew", get_skew, set_skew)(rttr::metadata("pretty_name", "Skew"));

    rttr::registration::class_<transform_component>("transform_component")(rttr::metadata("category", "RENDERING"),
                                                                           rttr::metadata("pretty_name", "Transform"))
        .constructor<>()()
        .method("component_exists", &component_exists<transform_component>)
        .property("local_transform", &transform_component::get_transform_local, &transform_component::set_transform_local)(
            rttr::metadata("pretty_name", "Local"),
            rttr::metadata("tooltip",
                           "This is the local transformation.\n"
                           "It is relative to the parent."))
        .property("global_transform", &transform_component::get_transform_global, &transform_component::set_transform_global)(
            rttr::metadata("pretty_name", "Global"),
            rttr::metadata("tooltip",
                           "This is the global transformation.\n"
                           "Affected by parent transformation."))
        .property("active", &transform_component::is_active, &transform_component::set_active)(
            rttr::metadata("pretty_name", "Active"),
            rttr::metadata("tooltip",
                            "This is the active state."),
            rttr::metadata("predicate", invisible_predicate))
        ;

    // Register math::transform class
    entt::meta_factory<math::transform>{}
        .type("transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Transform"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_translation), 
              entt::overload<const math::vec3&() const>(&math::transform::get_translation)>("position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Position"},
        })
        .data<entt::overload<void(const math::quat&)>(&math::transform::set_rotation), 
              entt::overload<const math::quat&() const>(&math::transform::get_rotation)>("rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Rotation"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_scale), 
              entt::overload<const math::vec3&() const>(&math::transform::get_scale)>("scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Scale"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_skew), 
              entt::overload<const math::vec3&() const>(&math::transform::get_skew)>("skew"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Skew"},
        });

    // Register transform_component class
    entt::meta_factory<transform_component>{}
        .type("transform_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Transform"},
        })
        .func<&component_exists<transform_component>>("component_exists"_hs)
        .data<&transform_component::set_transform_local, &transform_component::get_transform_local>("local_transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Local"},
            entt::attribute{"tooltip", "This is the local transformation.\n"
                                                "It is relative to the parent."},
        })
        .data<&transform_component::set_transform_global, &transform_component::get_transform_global>("global_transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Global"},
            entt::attribute{"tooltip", "This is the global transformation.\n"
                                                "Affected by parent transformation."},
        })
        .data<&transform_component::set_active, &transform_component::is_active>("active"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"pretty_name", "Active"},
            entt::attribute{"tooltip", "This is the active state."},
            entt::attribute{"predicate", invisible_predicate},
        });
}

SAVE(transform_component)
{
    bool is_root = obj.get_owner().all_of<root_component>();

    try_save(ar, ser20::make_nvp("local_transform",  obj.get_transform_local()));
    try_save(ar, ser20::make_nvp("parent", is_root ? entt::handle{} : obj.get_parent()));
    try_save(ar, ser20::make_nvp("children", obj.get_children()));
    try_save(ar, ser20::make_nvp("active", obj.is_active()));
}
SAVE_INSTANTIATE(transform_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(transform_component, ser20::oarchive_binary_t);

LOAD(transform_component)
{
    math::transform local_transform = obj.get_transform_local();
    if(try_load(ar, ser20::make_nvp("local_transform", local_transform)))
    {
        obj.set_transform_local(local_transform);
    }

    entt::handle parent;
    try_load(ar, ser20::make_nvp("parent", parent));

    obj.set_parent(parent, false);

    // not really used but needed to preserve binary archive integrity
    std::vector<entt::handle> children;
    try_load(ar, ser20::make_nvp("children", children));

    bool active{true};
    if(try_load(ar, ser20::make_nvp("active", active)))
    {
        obj.set_active(active);
    }
}
LOAD_INSTANTIATE(transform_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(transform_component, ser20::iarchive_binary_t);

} // namespace unravel
