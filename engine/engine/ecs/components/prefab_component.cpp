#include "prefab_component.h"

#include <algorithm>
#include <string_view>
#include <uuid/uuid.h>

namespace unravel
{

namespace
{
/// Whether `child` is a property path strictly below `parent`: "a/b" below "a", "a[0]" below "a".
auto is_parent_path(const std::string& parent, const std::string& child) -> bool
{
    if(child.length() <= parent.length())
    {
        return false;
    }
    if(child.compare(0, parent.length(), parent) != 0)
    {
        return false;
    }
    const char next_char = child[parent.length()];
    return next_char == '/' || next_char == '[';
}

auto is_path_prefix(const std::vector<hpp::uuid>& prefix, const std::vector<hpp::uuid>& path) -> bool
{
    return path.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

auto strip_prefix(const std::vector<hpp::uuid>& prefix, const std::vector<hpp::uuid>& path) -> std::vector<hpp::uuid>
{
    return std::vector<hpp::uuid>(path.begin() + static_cast<std::ptrdiff_t>(prefix.size()), path.end());
}

auto prepend(const std::vector<hpp::uuid>& prefix, const std::vector<hpp::uuid>& path) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> out;
    out.reserve(prefix.size() + path.size());
    out.insert(out.end(), prefix.begin(), prefix.end());
    out.insert(out.end(), path.begin(), path.end());
    return out;
}

/// Copies the entries of one set that a predicate accepts, with their path transformed.
template<typename T, typename Accept, typename Transform>
void select_into(const std::set<T>& from, std::set<T>& into, Accept&& accept, Transform&& transform)
{
    for(const auto& entry : from)
    {
        if(!accept(entry.instance_path))
        {
            continue;
        }
        T copy = entry;
        copy.instance_path = transform(entry.instance_path);
        into.insert(std::move(copy));
    }
}

template<typename Accept, typename Transform>
auto select(const prefab_statements& from, Accept&& accept, Transform&& transform) -> prefab_statements
{
    prefab_statements out;
    select_into(from.overrides, out.overrides, accept, transform);
    select_into(from.removed_entities, out.removed_entities, accept, transform);
    select_into(from.removed_instances, out.removed_instances, accept, transform);
    return out;
}
} // namespace

// prefab_property_override_data

prefab_property_override_data::prefab_property_override_data(const hpp::uuid& uuid, const std::string& path)
    : entity_uuid(uuid), component_path(path), pretty_component_path(path)
{
}

prefab_property_override_data::prefab_property_override_data(const hpp::uuid& uuid,
                                                             const std::string& path,
                                                             const std::string& pretty_path)
    : entity_uuid(uuid), component_path(path), pretty_component_path(pretty_path)
{
}

prefab_property_override_data::prefab_property_override_data(std::vector<hpp::uuid> path,
                                                             const hpp::uuid& uuid,
                                                             const std::string& component,
                                                             const std::string& pretty_path)
    : instance_path(std::move(path)), entity_uuid(uuid), component_path(component), pretty_component_path(pretty_path)
{
}

auto prefab_property_override_data::operator==(const prefab_property_override_data& other) const -> bool
{
    return instance_path == other.instance_path && entity_uuid == other.entity_uuid &&
           component_path == other.component_path;
}

auto prefab_property_override_data::operator<(const prefab_property_override_data& other) const -> bool
{
    if(instance_path != other.instance_path)
    {
        return instance_path < other.instance_path;
    }
    if(entity_uuid != other.entity_uuid)
    {
        return entity_uuid < other.entity_uuid;
    }
    return component_path < other.component_path;
}

// prefab_statement_target

auto prefab_statement_target::operator==(const prefab_statement_target& other) const -> bool
{
    return instance_path == other.instance_path && id == other.id;
}

auto prefab_statement_target::operator<(const prefab_statement_target& other) const -> bool
{
    if(instance_path != other.instance_path)
    {
        return instance_path < other.instance_path;
    }
    return id < other.id;
}

// prefab_statements

auto prefab_statements::empty() const -> bool
{
    return overrides.empty() && removed_entities.empty() && removed_instances.empty();
}

void prefab_statements::clear()
{
    overrides.clear();
    removed_entities.clear();
    removed_instances.clear();
}

void prefab_statements::merge(const prefab_statements& other)
{
    overrides.insert(other.overrides.begin(), other.overrides.end());
    removed_entities.insert(other.removed_entities.begin(), other.removed_entities.end());
    removed_instances.insert(other.removed_instances.begin(), other.removed_instances.end());
}

void prefab_statements::add_override(const std::vector<hpp::uuid>& instance_path,
                                     const hpp::uuid& entity_uuid,
                                     const std::string& component_path,
                                     const std::string& pretty_component_path)
{
    std::vector<prefab_property_override_data> to_remove;
    auto it = overrides.lower_bound(prefab_property_override_data{instance_path, entity_uuid, std::string{}, std::string{}});
    for(; it != overrides.end() && it->instance_path == instance_path && it->entity_uuid == entity_uuid; ++it)
    {
        if(is_parent_path(it->component_path, component_path))
        {
            to_remove.push_back(*it);
        }
        else if(is_parent_path(component_path, it->component_path))
        {
            return;
        }
    }
    for(const auto& existing : to_remove)
    {
        overrides.erase(existing);
    }
    overrides.emplace(instance_path, entity_uuid, component_path, pretty_component_path);
}

void prefab_statements::remove_override(const std::vector<hpp::uuid>& instance_path,
                                        const hpp::uuid& entity_uuid,
                                        const std::string& component_path)
{
    overrides.erase(prefab_property_override_data{instance_path, entity_uuid, component_path, std::string{}});
}

auto prefab_statements::has_override(const std::vector<hpp::uuid>& instance_path,
                                     const hpp::uuid& entity_uuid,
                                     const std::string& component_path) const -> bool
{
    return overrides.contains(prefab_property_override_data{instance_path, entity_uuid, component_path, std::string{}});
}

auto prefab_statements::has_override_touching(const std::vector<hpp::uuid>& instance_path,
                                              const hpp::uuid& entity_uuid,
                                              std::string_view component_path) const -> bool
{
    if(overrides.empty())
    {
        return false;
    }

    // Below: the first entry at or after the path, for the same entity, that extends it.
    const auto it = overrides.lower_bound(
        prefab_property_override_data{instance_path, entity_uuid, std::string(component_path), std::string{}});
    if(it != overrides.end() && it->instance_path == instance_path && it->entity_uuid == entity_uuid)
    {
        const std::string_view candidate(it->component_path);
        if(candidate.starts_with(component_path) &&
           (candidate.size() == component_path.size() || candidate[component_path.size()] == '/' ||
            candidate[component_path.size()] == '['))
        {
            return true;
        }
    }

    // Above: every ancestor of the path.
    auto ancestor = component_path;
    while(true)
    {
        const auto separator = ancestor.rfind('/');
        if(separator == std::string_view::npos)
        {
            return false;
        }
        ancestor = ancestor.substr(0, separator);
        if(has_override(instance_path, entity_uuid, std::string(ancestor)))
        {
            return true;
        }
    }
}

auto prefab_statements::has_override_on_or_above(const std::vector<hpp::uuid>& instance_path,
                                                 const hpp::uuid& entity_uuid,
                                                 std::string_view component_path) const -> bool
{
    if(overrides.empty())
    {
        return false;
    }
    if(has_override(instance_path, entity_uuid, std::string(component_path)))
    {
        return true;
    }
    auto ancestor = component_path;
    while(true)
    {
        const auto separator = ancestor.rfind('/');
        if(separator == std::string_view::npos)
        {
            return false;
        }
        ancestor = ancestor.substr(0, separator);
        if(has_override(instance_path, entity_uuid, std::string(ancestor)))
        {
            return true;
        }
    }
}

void prefab_statements::remove_entity(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid)
{
    removed_entities.insert({instance_path, entity_uuid});
    auto it = overrides.lower_bound(prefab_property_override_data{instance_path, entity_uuid, std::string{}, std::string{}});
    while(it != overrides.end() && it->instance_path == instance_path && it->entity_uuid == entity_uuid)
    {
        it = overrides.erase(it);
    }
}

void prefab_statements::restore_entity(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid)
{
    removed_entities.erase({instance_path, entity_uuid});
}

auto prefab_statements::is_entity_removed(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& entity_uuid) const
    -> bool
{
    return removed_entities.count({instance_path, entity_uuid}) != 0u;
}

void prefab_statements::remove_instance(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id)
{
    removed_instances.insert({instance_path, instance_id});
}

void prefab_statements::restore_instance(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id)
{
    removed_instances.erase({instance_path, instance_id});
}

auto prefab_statements::is_instance_removed(const std::vector<hpp::uuid>& instance_path, const hpp::uuid& instance_id) const
    -> bool
{
    return removed_instances.count({instance_path, instance_id}) != 0u;
}

auto prefab_statements::rebased(const std::vector<hpp::uuid>& prefix) const -> prefab_statements
{
    return select(
        *this,
        [&prefix](const std::vector<hpp::uuid>& path) { return is_path_prefix(prefix, path); },
        [&prefix](const std::vector<hpp::uuid>& path) { return strip_prefix(prefix, path); });
}

auto prefab_statements::at(const std::vector<hpp::uuid>& prefix) const -> prefab_statements
{
    return select(
        *this,
        [&prefix](const std::vector<hpp::uuid>& path) { return path == prefix; },
        [](const std::vector<hpp::uuid>&) { return std::vector<hpp::uuid>{}; });
}

auto prefab_statements::direct() const -> prefab_statements
{
    return at({});
}

auto prefab_statements::nested_only() const -> prefab_statements
{
    return select(
        *this,
        [](const std::vector<hpp::uuid>& path) { return !path.empty(); },
        [](const std::vector<hpp::uuid>& path) { return path; });
}

auto prefab_statements::prefixed(const std::vector<hpp::uuid>& prefix) const -> prefab_statements
{
    return select(
        *this,
        [](const std::vector<hpp::uuid>&) { return true; },
        [&prefix](const std::vector<hpp::uuid>& path) { return prepend(prefix, path); });
}

// prefab_component

void prefab_component::clear_overrides()
{
    from_document.clear();
    local.clear();
}

void prefab_component::add_override(const hpp::uuid& entity_uuid, const std::string& component_path)
{
    local.overrides.emplace(std::vector<hpp::uuid>{}, entity_uuid, component_path, component_path);
}

void prefab_component::add_override(const hpp::uuid& entity_uuid,
                                    const std::string& component_path,
                                    const std::string& pretty_component_path)
{
    local.add_override({}, entity_uuid, component_path, pretty_component_path);
}

auto prefab_component::has_override(const hpp::uuid& entity_uuid, const std::string& component_path) const -> bool
{
    return local.has_override({}, entity_uuid, component_path);
}

void prefab_component::remove_override(const hpp::uuid& entity_uuid, const std::string& component_path)
{
    local.remove_override({}, entity_uuid, component_path);
}

void prefab_component::remove_entity(const hpp::uuid& entity_uuid)
{
    local.remove_entity({}, entity_uuid);
}

void prefab_component::remove_instance(const hpp::uuid& instance_id)
{
    local.remove_instance({}, instance_id);
}

auto prefab_component::is_instance_removed(const hpp::uuid& instance_id) const -> bool
{
    return local.is_instance_removed({}, instance_id);
}

auto prefab_component::has_override_touching(const hpp::uuid& entity_uuid, std::string_view component_path) const
    -> bool
{
    return local.has_override_touching({}, entity_uuid, component_path);
}

auto prefab_component::has_serialization_override(const std::string& serialization_path) const -> bool
{
    if(local.overrides.empty())
    {
        return false;
    }
    // Expected format: "entities/<entity_uuid>/components/<component_type>/<property_path>"
    const std::string_view path(serialization_path);
    const auto first = path.find('/');
    if(first == std::string_view::npos)
    {
        return false;
    }
    const auto second = path.find('/', first + 1);
    if(second == std::string_view::npos)
    {
        return false;
    }
    const auto third = path.find('/', second + 1);
    if(third == std::string_view::npos || third + 1 >= path.size())
    {
        return false;
    }
    if(!path.substr(0, first).starts_with("entities"))
    {
        return false;
    }
    if(!path.substr(second + 1, third - second - 1).starts_with("components"))
    {
        return false;
    }
    const auto uuid_opt = hpp::uuid::from_string(path.substr(first + 1, second - first - 1));
    if(!uuid_opt.has_value())
    {
        return false;
    }
    return has_override(uuid_opt.value(), std::string(path.substr(third + 1)));
}

} // namespace unravel
