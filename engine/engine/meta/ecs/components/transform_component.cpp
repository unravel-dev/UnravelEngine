#include "transform_component.hpp"

#include "../../core/math/transform.hpp"
#include "../../core/math/vector.hpp"
#include "../entity.hpp"
#include "entt/meta/meta.hpp"
#include "entt/meta/resolve.hpp"
#include "glm/fwd.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(transform_component)
{
    auto invisible_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& i)
        {
            return false;
        });

    // The global transform only differs from the local one when the entity is parented;
    // for a root entity it is identical, so hide it to avoid a redundant editable block.
    auto has_parent_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& i) -> bool
        {
            const auto* comp = i.try_cast<transform_component>();
            return static_cast<bool>(comp && comp->get_parent());
        });
    // Register math::transform class
    entt::meta_factory<math::transform>{}
        .type("transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "transform"},
            entt::attribute{"pretty_name", "Transform"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_translation), 
              entt::overload<const math::vec3&() const>(&math::transform::get_translation)>("position"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "position"},
            entt::attribute{"pretty_name", "Position"},
        })
        .data<entt::overload<void(const math::quat&)>(&math::transform::set_rotation), 
              entt::overload<const math::quat&() const>(&math::transform::get_rotation)>("rotation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "rotation"},
            entt::attribute{"pretty_name", "Rotation"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_scale), 
              entt::overload<const math::vec3&() const>(&math::transform::get_scale)>("scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "scale"},
            entt::attribute{"pretty_name", "Scale"},
        })
        .data<entt::overload<void(const math::vec3&)>(&math::transform::set_skew), 
              entt::overload<const math::vec3&() const>(&math::transform::get_skew)>("skew"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "skew"},
            entt::attribute{"pretty_name", "Skew"},
        });

    // Register transform_component class
    entt::meta_factory<transform_component>{}
        .type("transform_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "transform_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Transform"},
        })
        .func<&component_meta<transform_component>::exists>("component_exists"_hs)
        .func<&component_meta<transform_component>::add>("component_add"_hs)
        .func<&component_meta<transform_component>::remove>("component_remove"_hs)
        .func<&component_meta<transform_component>::save>("component_save"_hs)
        .func<&component_meta<transform_component>::load>("component_load"_hs)
        .data<&transform_component::set_transform_local, &transform_component::get_transform_local>("local_transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "local_transform"},
            entt::attribute{"pretty_name", "Local"},
            entt::attribute{"tooltip", "This is the local transformation.\n"
                                                "It is relative to the parent."},
        })
        .data<&transform_component::set_transform_global, &transform_component::get_transform_global>("global_transform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_transform"},
            entt::attribute{"pretty_name", "Global"},
            entt::attribute{"tooltip", "This is the global transformation.\n"
                                                "Affected by parent transformation."},
            // Collapsed by default: it is a read-along view, secondary to the local transform.
            entt::attribute{"collapsed", true},
            // Only relevant for parented entities (root: global == local).
            entt::attribute{"predicate", has_parent_predicate_entt},
        })
        .data<&transform_component::set_active, &transform_component::is_active>("active"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "active"},
            entt::attribute{"pretty_name", "Active"},
            entt::attribute{"tooltip", "This is the active state."},
            entt::attribute{"predicate", invisible_predicate_entt},
        });

    // auto type = entt::resolve<transform_component>();
    // auto datas = type.data();
    // for(auto data : datas)
    // {
    //     APPLOG_TRACE("Data: {}", data.second.arity());
    // }
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

    // Applied only if the document carried a parent. A null handle that was *written* is a
    // real answer - it is how a root entity is encoded - but a parent key that is absent
    // means "not mentioned", and re-parenting to null on that would tear the entity out of
    // its hierarchy. Records can legitimately omit it: a suppressed prefab override, or a
    // sparse instance diff carrying only what the user changed.
    entt::handle parent;
    if(try_load(ar, ser20::make_nvp("parent", parent)))
    {
        obj.set_parent(parent, false);
    }

    // Read for its side effects, not its value. Resolving each child handle creates the
    // entity if this is the first mention of it and records it in the load context's
    // mappings, so the child's own record - which comes later, since the hierarchy is
    // flattened parents-first - resolves to the same entity instead of a second one.
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
