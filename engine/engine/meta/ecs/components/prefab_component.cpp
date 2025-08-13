#include "prefab_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/set.hpp>

namespace unravel
{

REFLECT(prefab_property_override_data)
{
    rttr::registration::class_<prefab_property_override_data>("prefab_property_override_data")
        .constructor<>()()
        .constructor<const hpp::uuid&, const std::string&>()()
        .constructor<const hpp::uuid&, const std::string&, const std::string&>()()
        .property("entity_uuid", &prefab_property_override_data::entity_uuid)(rttr::metadata("pretty_name", "Entity UUID"))
        .property("component_path", &prefab_property_override_data::component_path)(rttr::metadata("pretty_name", "Component Path"))
        .property("pretty_component_path", &prefab_property_override_data::pretty_component_path)(rttr::metadata("pretty_name", "Pretty Component Path"))
        ;

    // Register prefab_property_override_data class with entt
    entt::meta_factory<prefab_property_override_data>{}
        .type("prefab_property_override_data"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_property_override_data"},
        })
        .data<&prefab_property_override_data::entity_uuid>("entity_uuid"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entity_uuid"},
            entt::attribute{"pretty_name", "Entity UUID"},
        })
        .data<&prefab_property_override_data::component_path>("component_path"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "component_path"},
            entt::attribute{"pretty_name", "Component Path"},
        })
        .data<&prefab_property_override_data::pretty_component_path>("pretty_component_path"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pretty_component_path"},
            entt::attribute{"pretty_name", "Pretty Component Path"},
        });
}

SAVE_INLINE(prefab_property_override_data)
{
    try_save(ar, ser20::make_nvp("entity_uuid", obj.entity_uuid));
    try_save(ar, ser20::make_nvp("component_path", obj.component_path));
    try_save(ar, ser20::make_nvp("pretty_component_path", obj.pretty_component_path));
}
LOAD_INLINE(prefab_property_override_data)
{
    try_load(ar, ser20::make_nvp("entity_uuid", obj.entity_uuid));
    try_load(ar, ser20::make_nvp("component_path", obj.component_path));
    try_load(ar, ser20::make_nvp("pretty_component_path", obj.pretty_component_path));
}

REFLECT(prefab_component)
{
    rttr::registration::class_<prefab_component>("prefab_component")(rttr::metadata("category", "BASIC"),
                                                                     rttr::metadata("pretty_name", "Prefab"))
        .constructor<>()()
        .method("component_exists", &component_exists<prefab_component>)

        .property("source", &prefab_component::source)(rttr::metadata("pretty_name", "Source"))
        .property("property_overrides", &prefab_component::property_overrides)(rttr::metadata("pretty_name", "Property Overrides"))
        .property("removed_entities", &prefab_component::removed_entities)(rttr::metadata("pretty_name", "Removed Entities"))
        ;

    // Register prefab_component class with entt
    entt::meta_factory<prefab_component>{}
        .type("prefab_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Prefab"},
        })
        .func<&component_exists<prefab_component>>("component_exists"_hs)
        .data<&prefab_component::source>("source"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "source"},
            entt::attribute{"pretty_name", "Source"},
        })
        .data<&prefab_component::property_overrides>("property_overrides"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "property_overrides"},
            entt::attribute{"pretty_name", "Property Overrides"},
        })
        .data<&prefab_component::removed_entities>("removed_entities"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "removed_entities"},
            entt::attribute{"pretty_name", "Removed Entities"},
        });
}

SAVE(prefab_component)
{
    try_save(ar, ser20::make_nvp("source", obj.source));
    try_save(ar, ser20::make_nvp("property_overrides", obj.property_overrides));
    try_save(ar, ser20::make_nvp("removed_entities", obj.removed_entities));
}
SAVE_INSTANTIATE(prefab_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(prefab_component, ser20::oarchive_binary_t);

LOAD(prefab_component)
{
    try_load(ar, ser20::make_nvp("source", obj.source));
    try_load(ar, ser20::make_nvp("property_overrides", obj.property_overrides));
    try_load(ar, ser20::make_nvp("removed_entities", obj.removed_entities));
}
LOAD_INSTANTIATE(prefab_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(prefab_component, ser20::iarchive_binary_t);

REFLECT(prefab_id_component)
{
    rttr::registration::class_<prefab_id_component>("prefab_id_component")(rttr::metadata("category", "BASIC"),
                                                             rttr::metadata("pretty_name", "Prefab Id"))
        .constructor<>()()
        .method("component_exists", &component_exists<prefab_id_component>)

        .property_readonly("id", &prefab_id_component::id)(rttr::metadata("pretty_name", "Id"),
                                                    rttr::metadata("tooltip", "This is the unique id of the entity in the prefab."));

    // Register prefab_id_component class with entt
    entt::meta_factory<prefab_id_component>{}
        .type("prefab_id_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_id_component"},
            entt::attribute{"category", "BASIC"},
            entt::attribute{"pretty_name", "Prefab Id"},
        })
        .func<&component_exists<prefab_id_component>>("component_exists"_hs)
        .data<nullptr, &prefab_id_component::id>("id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "id"},
            entt::attribute{"pretty_name", "Id"},
            entt::attribute{"tooltip", "This is the unique id of the entity in the prefab."},
        });
}


SAVE(prefab_id_component)
{
    try_save(ar, ser20::make_nvp("id", hpp::to_string(obj.id)));
}
SAVE_INSTANTIATE(prefab_id_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(prefab_id_component, ser20::oarchive_binary_t);

LOAD(prefab_id_component)
{
    std::string suuid;

    if(try_load(ar, ser20::make_nvp("id", suuid)))
    {
        auto id = hpp::uuid::from_string(suuid);
        obj.id = id.value_or(hpp::uuid{});
    }
}

LOAD_INSTANTIATE(prefab_id_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(prefab_id_component, ser20::iarchive_binary_t);

} // namespace unravel
