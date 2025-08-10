#include "property_path_generator.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace unravel
{

void property_path_context::push_segment(const std::string& segment)
{
    // Directly sanitize here instead of calling private method
    path_segments_.push_back(segment);
}

void property_path_context::pop_segment()
{
    if (!path_segments_.empty())
    {
        path_segments_.pop_back();
    }
}

auto property_path_context::get_current_path() const -> std::string
{
    if (path_segments_.empty())
    {
        return "";
    }
    
    std::stringstream ss;
    for (size_t i = 0; i < path_segments_.size(); ++i)
    {
        // For array indices, don't add a dot separator
        if (i > 0 && path_segments_[i][0] != '[')
        {
            ss << "/";
        }
        ss << path_segments_[i];
    }
    
    return ss.str();
}

auto property_path_context::get_current_path_with_component_type() const -> std::string
{
    if (component_type_name_.empty())
    {
        return get_current_path();
    }
    return component_type_name_ + "/" + get_current_path();
}

void property_path_context::set_component_type(const std::string& type)
{
    component_type_name_ = type;
}

auto property_path_context::get_component_type_name() const -> std::string
{
    return component_type_name_;
}

void property_path_context::set_entity_uuid(const hpp::uuid& entity_uuid)
{
    entity_uuid_ = entity_uuid;
}

auto property_path_context::get_entity_uuid() const -> hpp::uuid
{
    return entity_uuid_;
}

} // namespace unravel