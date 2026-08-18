#include "prefab_component.h"
#include <sstream>
#include <string_view>
#include <uuid/uuid.h>
#include <string_view>

namespace unravel
{

// prefab_property_override_data implementation

prefab_property_override_data::prefab_property_override_data(const hpp::uuid& uuid, const std::string& path)
    : entity_uuid(uuid), component_path(path), pretty_component_path(path)
{
}

prefab_property_override_data::prefab_property_override_data(const hpp::uuid& uuid, const std::string& path, const std::string& pretty_path)
    : entity_uuid(uuid), component_path(path), pretty_component_path(pretty_path)
{
}

auto prefab_property_override_data::operator==(const prefab_property_override_data& other) const -> bool
{
    return entity_uuid == other.entity_uuid && component_path == other.component_path;
}

auto prefab_property_override_data::operator<(const prefab_property_override_data& other) const -> bool
{
    if (entity_uuid != other.entity_uuid)
    {
        return entity_uuid < other.entity_uuid;
    }
    return component_path < other.component_path;
}

// prefab_component implementation
void prefab_component::clear_overrides()
{
    // The inherited half goes too. Reverting means "put this instance back the way its asset
    // has it", and an override stated by the document that contains this instance is not this
    // instance's to keep - though it will be stated again the next time that document is
    // replayed, which is correct: it is that document's answer, not this one's.
    property_overrides.clear();
    inherited_overrides.clear();
    removed_entities.clear();
    removed_instances.clear();
}

void prefab_component::add_override(const hpp::uuid& entity_uuid, const std::string& component_path)
{
    property_overrides.emplace(entity_uuid, component_path);
}

void prefab_component::add_override(const hpp::uuid& entity_uuid, const std::string& component_path, const std::string& pretty_component_path)
{
    // Helper function to check if one path is a parent of another
    auto is_parent_path = [](const std::string& parent, const std::string& child) -> bool 
    {
        if (child.length() <= parent.length())
        {
            return false;
        }
        if (child.substr(0, parent.length()) != parent)
        {
            return false;
        }
        char next_char = child[parent.length()];
        return next_char == '/' || next_char == '[';
    };
    
    // Remove any existing overrides that are parents or children of this path for the same entity
    std::vector<prefab_property_override_data> to_remove;
    for (const auto& existing_override : get_all_overrides())
    {
        // Only consider overrides for the same entity
        if (existing_override.entity_uuid != entity_uuid)
        {
            continue;
        }
        
        // Check if the existing override is a parent of the new one
        if (is_parent_path(existing_override.component_path, component_path))
        {
            to_remove.push_back(existing_override);
        }
        // Check if the new override is a parent of existing ones
        else if (is_parent_path(component_path, existing_override.component_path))
        {
            // Don't add the new override, the existing one is more specific
            return;
        }
    }
    
    // Remove the parent overrides
    for (const auto& override_to_remove : to_remove)
    {
        remove_override(override_to_remove.entity_uuid, override_to_remove.component_path);
    }
    property_overrides.emplace(entity_uuid, component_path, pretty_component_path);
}

auto prefab_component::has_override(const hpp::uuid& entity_uuid, const std::string& component_path) const -> bool
{
    return property_overrides.contains(prefab_property_override_data{entity_uuid, component_path});
}

void prefab_component::remove_override(const hpp::uuid& entity_uuid, const std::string& component_path)
{
    property_overrides.erase(prefab_property_override_data{entity_uuid, component_path});
}

void prefab_component::remove_instance(const hpp::uuid& instance_id)
{
    removed_instances.insert(instance_id);
}

auto prefab_component::is_instance_removed(const hpp::uuid& instance_id) const -> bool
{
    return removed_instances.count(instance_id) != 0u;
}

void prefab_component::remove_entity(const hpp::uuid& entity_uuid)
{
    removed_entities.insert(entity_uuid);

    // operator< orders by (entity_uuid, component_path), so every override belonging to one
    // entity is a contiguous range and an empty component_path sorts before all of them.
    //
    // Erasing through the range-for would be undefined: std::set::erase invalidates the
    // iterator to the erased element, which is the one the loop then increments.
    const auto erase_entity_range = [&entity_uuid](std::set<prefab_property_override_data>& overrides)
    {
        auto it = overrides.lower_bound(prefab_property_override_data{entity_uuid, std::string{}});
        while(it != overrides.end() && it->entity_uuid == entity_uuid)
        {
            it = overrides.erase(it);
        }
    };

    erase_entity_range(property_overrides);

    // The inherited half too. An entity that is gone cannot still be attributed to the prefab
    // that contains this instance, and leaving it there would make the next resync compute the
    // local set by subtracting entries nothing holds.
    erase_entity_range(inherited_overrides);
}


auto prefab_component::get_all_overrides() const -> const std::set<prefab_property_override_data>&
{
    return property_overrides;
}

auto prefab_component::has_override_touching(const hpp::uuid& entity_uuid,
                                             std::string_view component_path) const -> bool
{
    if(property_overrides.empty())
    {
        return false;
    }

    // Below: ordered by (entity_uuid, component_path), so anything sharing the prefix sorts
    // immediately after it - if the first entry not less than it does not share it, none do.
    const auto it =
        property_overrides.lower_bound(prefab_property_override_data{entity_uuid, std::string(component_path)});
    if(it != property_overrides.end() && it->entity_uuid == entity_uuid)
    {
        const std::string_view candidate(it->component_path);
        if(candidate.starts_with(component_path) &&
           (candidate.size() == component_path.size() || candidate[component_path.size()] == '/'))
        {
            return true;
        }
    }

    // Above: an override on a whole property covers everything inside it. Walked segment by
    // segment rather than searched, since a property path is only a few deep.
    auto ancestor = component_path;
    while(true)
    {
        const auto separator = ancestor.rfind('/');
        if(separator == std::string_view::npos)
        {
            return false;
        }

        ancestor = ancestor.substr(0, separator);
        if(has_override(entity_uuid, std::string(ancestor)))
        {
            return true;
        }
    }
}

auto prefab_component::has_serialization_override(const std::string& serialization_path) const -> bool
{
    // Called once per property of every entity during a prefab resync, so it runs tens of
    // times per entity and overwhelmingly answers "no". Nothing below allocates until the
    // path has actually been recognised as addressing an overridable property.

    // Nothing is overridden: the common case by a wide margin, and the whole answer.
    if(property_overrides.empty())
    {
        return false;
    }

    // Split "entities[i]/<uuid>/components/<component_path>" on its first three
    // separators. Previously this tokenized the whole path into a vector<string> and then
    // rejoined the tail into another string, for every property.
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
        // No component path after "components/" - the old tokenize produced fewer than
        // the four segments it required and bailed out here too.
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