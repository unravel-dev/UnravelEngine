#include "prefab_component.hpp"
#include <engine/meta/assets/asset_handle.hpp>
#include <hpp/string_view.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/map.hpp>
#include <serialization/types/set.hpp>
#include <serialization/types/vector.hpp>

#include <engine/meta/ecs/entity.hpp>

namespace unravel
{

REFLECT(prefab_property_override_data)
{
    entt::meta_factory<prefab_property_override_data>{}
        .type("prefab_property_override_data"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_property_override_data"},
            entt::attribute{"pretty_name", "Prefab Property Override Data"},
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
    try_save(ar, ser20::make_nvp("instance_path", obj.instance_path));
    try_save(ar, ser20::make_nvp("entity_uuid", obj.entity_uuid));
    try_save(ar, ser20::make_nvp("component_path", obj.component_path));
    try_save(ar, ser20::make_nvp("pretty_component_path", obj.pretty_component_path));
}

namespace
{
/// The property @p type reflects as @p name.
auto find_statement_property(const entt::meta_type& type, const std::string& name) -> entt::meta_data
{
    for(auto [id, prop] : type.data())
    {
        if(entt::get_name(prop) == name)
        {
            return prop;
        }
    }
    return {};
}

/// "member/row" when @p name is, or was before a rename, a row of an object @p type holds as a flattable property,
/// whose rows are listed as the type's own; empty when no such member has it.
auto find_flattened_member_row(const entt::meta_type& type, const std::string& name) -> std::string
{
    for(auto [id, member] : type.data())
    {
        if(!entt::is_property_flattable(entt::meta_any{}, member))
        {
            continue;
        }
        auto row = find_statement_property(member.type(), name);
        if(!row)
        {
            row = entt::find_renamed_property(member.type(), name);
        }
        if(row)
        {
            return entt::get_name(member) + "/" + entt::get_name(row);
        }
    }
    return {};
}

/// "component/property..." as the component names the property now: one recorded under its former name is renamed,
/// and one that has become a row of a flattened member is read inside that member.
auto get_current_component_path(const std::string& component_path) -> std::string
{
    const auto component_end = component_path.find('/');
    if(component_end == std::string::npos)
    {
        return component_path;
    }
    const auto property_end = component_path.find_first_of("/[", component_end + 1);
    const auto component = hpp::string_view(component_path).substr(0, component_end);
    const std::string property(hpp::string_view(component_path).substr(component_end + 1, property_end - component_end - 1));
    const auto type = entt::resolve(entt::hashed_string::value(component.data(), component.size()));
    if(!type)
    {
        return component_path;
    }
    const auto rest = property_end == std::string::npos ? std::string() : component_path.substr(property_end);
    if(const auto renamed = entt::find_renamed_property(type, property))
    {
        return std::string(component) + "/" + entt::get_name(renamed) + rest;
    }
    if(find_statement_property(type, property))
    {
        return component_path;
    }
    const auto member_row = find_flattened_member_row(type, property);
    return member_row.empty() ? component_path : std::string(component) + "/" + member_row + rest;
}
} // namespace

LOAD_INLINE(prefab_property_override_data)
{
    // Absent in a record from before statements carried a path: about direct content.
    try_load(ar, ser20::make_nvp("instance_path", obj.instance_path));
    try_load(ar, ser20::make_nvp("entity_uuid", obj.entity_uuid));
    try_load(ar, ser20::make_nvp("component_path", obj.component_path));
    try_load(ar, ser20::make_nvp("pretty_component_path", obj.pretty_component_path));
    obj.component_path = get_current_component_path(obj.component_path);
}

SAVE_INLINE(prefab_statement_target)
{
    try_save(ar, ser20::make_nvp("instance_path", obj.instance_path));
    try_save(ar, ser20::make_nvp("id", obj.id));
}

LOAD_INLINE(prefab_statement_target)
{
    try_load(ar, ser20::make_nvp("instance_path", obj.instance_path));
    try_load(ar, ser20::make_nvp("id", obj.id));
}

SAVE(prefab_statements)
{
    try_save(ar, ser20::make_nvp("overrides", obj.overrides));
    try_save(ar, ser20::make_nvp("removed_entities", obj.removed_entities));
    try_save(ar, ser20::make_nvp("removed_instances", obj.removed_instances));
}
SAVE_INSTANTIATE(prefab_statements, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(prefab_statements, ser20::oarchive_binary_t);

namespace
{
/// The overrides @p stated stands for when the component has no property of the name it states and others hold what
/// that name stands for (their "replaces" attribute): one on each of them, as a whole. Empty when the component has
/// the property, or none holds it.
auto get_replacing_overrides(const prefab_property_override_data& stated) -> std::vector<prefab_property_override_data>
{
    std::vector<prefab_property_override_data> replacing;
    const auto component_end = stated.component_path.find('/');
    if(component_end == std::string::npos)
    {
        return replacing;
    }
    const auto property_end = stated.component_path.find_first_of("/[", component_end + 1);
    const std::string component = stated.component_path.substr(0, component_end);
    const std::string property = stated.component_path.substr(component_end + 1, property_end - component_end - 1);
    const auto type = entt::resolve(entt::hashed_string::value(component.data(), component.size()));
    if(!type || find_statement_property(type, property))
    {
        return replacing;
    }
    for(const auto& prop : entt::find_replacing_properties(type, property))
    {
        replacing.emplace_back(stated.instance_path,
                               stated.entity_uuid,
                               component + "/" + entt::get_name(prop),
                               entt::get_pretty_name(type) + "/" + entt::get_pretty_name(prop));
    }
    return replacing;
}

/// Restates the overrides of names other properties hold the values of, on those properties.
void restate_replaced_overrides(std::set<prefab_property_override_data>& overrides)
{
    std::vector<prefab_property_override_data> restated;
    for(auto it = overrides.begin(); it != overrides.end();)
    {
        auto replacing = get_replacing_overrides(*it);
        if(replacing.empty())
        {
            ++it;
            continue;
        }
        restated.insert(restated.end(), std::make_move_iterator(replacing.begin()), std::make_move_iterator(replacing.end()));
        it = overrides.erase(it);
    }
    overrides.insert(std::make_move_iterator(restated.begin()), std::make_move_iterator(restated.end()));
}
} // namespace

LOAD(prefab_statements)
{
    try_load(ar, ser20::make_nvp("overrides", obj.overrides));
    restate_replaced_overrides(obj.overrides);
    try_load(ar, ser20::make_nvp("removed_entities", obj.removed_entities));
    try_load(ar, ser20::make_nvp("removed_instances", obj.removed_instances));
}
LOAD_INSTANTIATE(prefab_statements, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(prefab_statements, ser20::iarchive_binary_t);

REFLECT(prefab_component)
{
    entt::meta_factory<prefab_component>{}
        .type("prefab_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_component"},
            entt::attribute{"category", "Basic"},
            entt::attribute{"pretty_name", "Prefab"},
        })
        .func<&component_meta<prefab_component>::exists>("component_exists"_hs)
        .func<&component_meta<prefab_component>::add>("component_add"_hs)
        .func<&component_meta<prefab_component>::remove>("component_remove"_hs)
        .func<&component_meta<prefab_component>::save>("component_save"_hs)
        .func<&component_meta<prefab_component>::load>("component_load"_hs)
        .data<&prefab_component::source>("source"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "source"},
            entt::attribute{"pretty_name", "Source"},
        })
        .data<nullptr, &prefab_component::instance_id>("instance_id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "instance_id"},
            entt::attribute{"pretty_name", "Instance Id"},
            entt::attribute{"tooltip", "Identifies which nested instance of the containing prefab this is."},
        });
}

SAVE(prefab_component)
{
    try_save(ar, ser20::make_nvp("source", obj.source));
    try_save(ar, ser20::make_nvp("instance_id", obj.instance_id));
    try_save(ar, ser20::make_nvp("instance_document", obj.instance_document));

    // The instance's own document's statements, as this scene or snapshot has them. Refreshed
    // by that document's own replay wherever it loads.
    try_save(ar, ser20::make_nvp("from_document", obj.from_document));

    // What was stated here. Into a prefab file it is folded into the document's own list by
    // the save (fold_document_statements) and the record carries an empty one - a file holds
    // no scene's statements. A scene or a clone stream keeps it as it is.
    const auto* save_ctx = try_get_save_context();
    const bool into_prefab = save_ctx != nullptr && save_ctx->is_saving_to_prefab();
    const prefab_statements empty;
    try_save(ar, ser20::make_nvp("local", into_prefab ? empty : obj.local));
}
SAVE_INSTANTIATE(prefab_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(prefab_component, ser20::oarchive_binary_t);

LOAD(prefab_component)
{
    try_load(ar, ser20::make_nvp("source", obj.source));

    auto* load_ctx = try_get_load_context();

    try_load(ar, ser20::make_nvp("instance_id", obj.instance_id));
    try_load(ar, ser20::make_nvp("instance_document", obj.instance_document));

    // The document's statements travel with the instance root: replaced by this record's
    // copy (a scene's, or a containing document's snapshot - which that document's own replay
    // refreshes next in the cascade).
    const bool has_statement_lists = try_load(ar, ser20::make_nvp("from_document", obj.from_document));

    // What was stated here is this scene's. A replay over an existing instance must not touch
    // it - the record's is the file's, which is empty, or an outer snapshot's, which is not this
    // scene's either. A fresh instantiate, a scene load or a clone takes the record's.
    const bool replaying = load_ctx != nullptr && load_ctx->is_updating_prefab();
    if(!replaying)
    {
        prefab_statements local;
        if(try_load(ar, ser20::make_nvp("local", local)))
        {
            obj.local = std::move(local);
        }
    }

    if(!has_statement_lists && load_ctx != nullptr)
    {
        // The released format: a flat override set and removed entities on an instance root,
        // all of it this scene's. Kept aside and converted once the document has loaded
        // (convert_legacy_override_state).
        legacy_override_state legacy;
        bool any = try_load(ar, ser20::make_nvp("property_overrides", legacy.property_overrides));
        restate_replaced_overrides(legacy.property_overrides);
        any |= try_load(ar, ser20::make_nvp("removed_entities", legacy.removed_entities));
        if(any)
        {
            if(const auto owner = obj.get_owner())
            {
                load_ctx->legacy_overrides[owner.entity()] = std::move(legacy);
            }
        }
    }
}
LOAD_INSTANTIATE(prefab_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(prefab_component, ser20::iarchive_binary_t);

REFLECT(prefab_id_component)
{
    entt::meta_factory<prefab_id_component>{}
        .type("prefab_id_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "prefab_id_component"},
            entt::attribute{"category", "Basic"},
            entt::attribute{"pretty_name", "Prefab Id"},
        })
        .func<&component_meta<prefab_id_component>::exists>("component_exists"_hs)
        .func<&component_meta<prefab_id_component>::add>("component_add"_hs)
        .func<&component_meta<prefab_id_component>::remove>("component_remove"_hs)
        .func<&component_meta<prefab_id_component>::save>("component_save"_hs)
        .func<&component_meta<prefab_id_component>::load>("component_load"_hs)
        .data<nullptr, &prefab_id_component::id>("id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "id"},
            entt::attribute{"pretty_name", "Id"},
            entt::attribute{"tooltip", "This is the unique id of the entity in the prefab."},
        });
}


SAVE(prefab_id_component)
{
    try_save(ar, ser20::make_nvp("id", obj.id));
    // Always written, never elided to "the file being written": an absent key costs a failed
    // lookup per entity on load, which is the one cost this path cannot afford.
    try_save(ar, ser20::make_nvp("document", obj.document));
}
SAVE_INSTANTIATE(prefab_id_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(prefab_id_component, ser20::oarchive_binary_t);

LOAD(prefab_id_component)
{
    try_load(ar, ser20::make_nvp("id", obj.id));

    // The document is identity, issued once: a record sets it only for an entity that has
    // none. A matched entity keeps its own - an outer document's snapshot can be stale about
    // it (an addition later applied into the nested asset is the nested asset's now), and
    // letting every replay restamp it would make the name flip with replay order. Absent only
    // in a file from before ids named their document: a fresh entity is left nil and
    // attributed by the legacy pass once the document has loaded.
    hpp::uuid document;
    if(!try_load(ar, ser20::make_nvp("document", document)))
    {
        if(auto* load_ctx = try_get_load_context())
        {
            load_ctx->saw_unqualified_ids = true;
        }
    }
    else if(obj.document.is_nil())
    {
        obj.document = document;
    }
}

LOAD_INSTANTIATE(prefab_id_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(prefab_id_component, ser20::iarchive_binary_t);

} // namespace unravel
