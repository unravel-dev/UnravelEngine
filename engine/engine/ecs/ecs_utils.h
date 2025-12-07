#pragma once
#include "components/transform_component.h"
#include <functional>

namespace unravel
{

/**
 * @brief Iterates through all children (recursively) and calls the callback for each entity that has the specified component.
 * @tparam ComponentType The type of component to search for.
 * @param parent The parent entity handle to start iteration from.
 * @param callback A function that receives a reference to the component.
 */
template<typename ComponentType>
void for_each_component_in_children(entt::handle parent, std::function<void(ComponentType&)> callback)
{
    if(!parent.valid())
    {
        return;
    }
    
    // Check if parent has transform_component to get children
    if(!parent.all_of<transform_component>())
    {
        return;
    }
    
    const auto& children = parent.get<transform_component>().get_children();
    for(const auto& child : children)
    {
        // Check if child has the component
        if(child.all_of<ComponentType>())
        {
            auto& component = child.get<ComponentType>();
            callback(component);
        }
        
        // Recurse into children
        for_each_component_in_children<ComponentType>(child, callback);
    }
}

} // namespace unravel
