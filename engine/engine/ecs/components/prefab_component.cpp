#include "prefab_component.h"
#include <sstream>
#include <uuid/uuid.h>
#include <string_utils/utils.h>

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
    property_overrides.clear();
    removed_entities.clear();
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

void prefab_component::remove_entity(const hpp::uuid& entity_uuid)
{
    removed_entities.insert(entity_uuid);

    for(auto& override : property_overrides)
    {
        if(override.entity_uuid == entity_uuid)
        {
            property_overrides.erase(override);
            return;
        }
    }
}


auto prefab_component::get_all_overrides() const -> const std::set<prefab_property_override_data>&
{
    return property_overrides;
}

auto prefab_component::has_serialization_override(const std::string& serialization_path) const -> bool
{
    // Parse serialization path: "entities/uuid/components/component_type/property_path"
    auto segments = string_utils::tokenize(serialization_path, "/");
    
    if (segments.size() < 4 || segments[0] != "entities" || segments[2] != "components")
    {
        return false;
    }
    
    // Extract UUID and component path
    std::string uuid_str = segments[1];
    auto uuid_opt = hpp::uuid::from_string(uuid_str);
    if (!uuid_opt.has_value())
    {
        return false;
    }
    
    // Build component path from remaining segments, skipping Script/script_components/ if present
    std::string component_path;
    bool first_segment = true;
    
    for (size_t i = 3; i < segments.size(); ++i)
    {
        // Skip "Script" and "script_components" segments entirely
        if (segments[i] == "Script" || segments[i] == "script_component" || segments[i] == "script_components")
        {
            continue;
        }
        
        if (!first_segment)
        {
            component_path += "/";
        }
        component_path += segments[i];
        first_segment = false;
    }
    
    if(has_override(uuid_opt.value(), component_path))
    {
        APPLOG_TRACE("Serialization override found for property: {}", serialization_path);
        return true;
    }
    return false;
}

} // namespace unravel