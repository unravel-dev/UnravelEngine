#include "inspector_script.h"
#include "inspectors.h"
#include <engine/assets/asset_manager.h>
#include <engine/scripting/ecs/systems/script_interop.h>
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_property_invoker.h>
#include <monopp/mono_type_conversion.h>
#include <monopp/mono_list.h>
#include <type_traits>

#include <core/string_utils/utils.h>

#include <graphics/texture.h>

#include <engine/layers/layer_mask.h>
#include <engine/rendering/font.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>


#include <engine/animation/animation.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/physics/physics_material.h>

namespace unravel
{

auto find_attribute(const std::string& name, const std::vector<mono::mono_object>& attribs) -> mono::mono_object
{
    auto it = std::find_if(std::begin(attribs),
                           std::end(attribs),
                           [&](const mono::mono_object& obj)
                           {
                               return obj.get_type().get_name() == name;
                           });

    if(it != std::end(attribs))
    {
        return *it;
    }

    return {};
}

/**
 * @brief Checks if a mono type has the System.Serializable attribute
 * 
 * @param type The mono type to check
 * @return true if the type has the Serializable attribute, false otherwise
 */
auto is_serializable_type(const mono::mono_type& type) -> bool
{
    // auto attribs = type.get_attributes();
    // auto serializable_attrib = find_attribute("SerializableAttribute", attribs);
    // return serializable_attrib.valid();
    return true;
}

/**
 * @brief Checks if a mono type is a List<T>
 * 
 * @param type The mono type to check
 * @return true if the type is System.Collections.Generic.List<T>
 */
auto is_list_type(const mono::mono_type& type) -> bool
{
    if (!type.valid())
    {
        return false;
    }
    
    // Check if it's a generic List<T>
    auto fullname = type.get_fullname();
    return fullname.find("System.Collections.Generic.List<") == 0;
}

/**
 * @brief Gets the element type of a collection (array or List<T>)
 * 
 * @param type The collection type
 * @return The element type, or invalid type if not a collection
 */
auto get_collection_element_type(const mono::mono_type& type) -> mono::mono_type
{
    if (type.is_array())
    {
        return type.get_element_type();
    }
    else if (is_list_type(type))
    {
        // For List<T>, get the generic argument
        // List<T> has a property "Item" that returns T
        auto item_prop = type.get_property("Item");
        if (item_prop.get_internal_ptr())
        {
            return item_prop.get_type();
        }
    }
    
    return {};
}

/**
 * @brief Creates a proxy for a nested serializable object field
 * 
 * This creates a proxy that can access nested object fields while maintaining
 * proper property paths for the undo/redo system.
 */
template<typename Invoker>
auto make_nested_object_proxy(const meta_any_proxy& obj_proxy, const Invoker& mutable_field) -> meta_any_proxy
{
    meta_any_proxy nested_proxy;
    auto field_name = mutable_field.get_name();
    
    nested_proxy.impl->get_name = [obj_proxy, field_name]()
    {
        auto parent_name = obj_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return field_name;
        }
        return fmt::format("{}/{}", parent_name, field_name);
    };
    
    nested_proxy.impl->getter = [obj_proxy, field_name](entt::meta_any& result) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            
            // Recreate the invoker from the field name
            auto obj_type = mono_obj.get_type();
            auto field = obj_type.get_field(field_name);
            auto invoker = mono::make_field_invoker<mono::mono_object>(field);
            
            auto nested_obj = invoker.get_value(mono_obj);
            if(nested_obj.valid())
            {
                // Create an owned copy to avoid dangling references
                result = entt::meta_any{std::in_place_type<mono::mono_object>, nested_obj};
                return true;
            }
        }
        return false;
    };
    
    nested_proxy.impl->setter = [obj_proxy, field_name](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            if(value.try_cast<mono::mono_object>())
            {
                // Recreate the invoker from the field name
                auto obj_type = mono_obj.get_type();
                auto field = obj_type.get_field(field_name);
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                
                auto nested_obj = value.cast<mono::mono_object>();
                invoker.set_value(mono_obj, nested_obj);
                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
            }
        }
        return false;
    };
    
    return nested_proxy;
}

/**
 * @brief Creates a proxy for a nested serializable object property
 * 
 * This creates a proxy that can access nested object properties while maintaining
 * proper property paths for the undo/redo system.
 */
template<typename Invoker>
auto make_nested_property_proxy(const meta_any_proxy& obj_proxy, const Invoker& mutable_property) -> meta_any_proxy
{
    meta_any_proxy nested_proxy;
    auto prop_name = mutable_property.get_name();
    
    nested_proxy.impl->get_name = [obj_proxy, prop_name]()
    {
        auto parent_name = obj_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return prop_name;
        }
        return fmt::format("{}/{}", parent_name, prop_name);
    };
    
    nested_proxy.impl->getter = [obj_proxy, prop_name](entt::meta_any& result) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            
            // Recreate the invoker from the property name
            auto obj_type = mono_obj.get_type();
            auto property = obj_type.get_property(prop_name);
            auto invoker = mono::make_property_invoker<mono::mono_object>(property);
            
            auto nested_obj = invoker.get_value(mono_obj);
            if(nested_obj.valid())
            {
                // Create an owned copy to avoid dangling references
                result = entt::meta_any{std::in_place_type<mono::mono_object>, nested_obj};
                return true;
            }
        }
        return false;
    };
    
    nested_proxy.impl->setter = [obj_proxy, prop_name](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            if(value.try_cast<mono::mono_object>())
            {
                // Recreate the invoker from the property name
                auto obj_type = mono_obj.get_type();
                auto property = obj_type.get_property(prop_name);
                auto invoker = mono::make_property_invoker<mono::mono_object>(property);
                
                auto nested_obj = value.cast<mono::mono_object>();
                invoker.set_value(mono_obj, nested_obj);
                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
            }
        }
        return false;
    };
    
    return nested_proxy;
}

// Forward declaration for recursive serializable object inspection
auto inspect_serializable_object(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 const std::string& name,
                                 const var_info& info) -> inspect_result;

/**
 * @brief Proxy wrapper for mono field access that integrates with meta_any_proxy system
 * 
 * This allows script field changes to be properly recorded in the undo/redo system
 * by providing a bridge between mono field access and the engine's property system.
 */
template<typename T>
struct mono_field_proxy
{
    mono::mono_field field;
    std::string field_name;
    
    mono_field_proxy(mono::mono_field f) : field(f), field_name(f.get_name()) {}
    
    auto get_name() const -> std::string { return field_name; }
    
    auto get_value(mono::mono_object& obj) const -> T
    {
        auto invoker = mono::make_field_invoker<T>(field);
        return invoker.get_value(obj);
    }
    
    void set_value(mono::mono_object& obj, const T& value) const
    {
        auto invoker = mono::make_field_invoker<T>(field);
        invoker.set_value(obj, value);
    }
    
    auto get_attributes() const
    {
        return field.get_attributes();
    }
    
    auto get_type() const
    {
        return field.get_type();
    }
    
    auto is_readonly() const { return field.is_readonly(); }
    auto is_const() const { return field.is_const(); }
};

/**
 * @brief Proxy wrapper for mono property access that integrates with meta_any_proxy system
 * 
 * This allows script property changes to be properly recorded in the undo/redo system
 * by providing a bridge between mono property access and the engine's property system.
 */
template<typename T>
struct mono_property_proxy
{
    mono::mono_property property;
    std::string property_name;
    
    mono_property_proxy(mono::mono_property p) : property(p), property_name(p.get_name()) {}
    
    auto get_name() const -> std::string { return property_name; }
    
    auto get_value(mono::mono_object& obj) const -> T
    {
        auto invoker = mono::make_property_invoker<T>(property);
        return invoker.get_value(obj);
    }
    
    void set_value(mono::mono_object& obj, const T& value) const
    {
        auto invoker = mono::make_property_invoker<T>(property);
        invoker.set_value(obj, value);
    }
    
    auto get_attributes() const
    {
        return property.get_attributes();
    }
    
    auto get_type() const
    {
        return property.get_type();
    }
    
    auto is_readonly() const { return property.is_readonly(); }
};

/**
 * @brief Creates a meta_any_proxy that can access script fields through the proxy wrapper
 * 
 * This creates the bridge between script field access and the engine's undo/redo system.
 * The proxy stores lambdas that know how to navigate from the parent object to the specific field.
 */
template<typename T, typename ProxyType>
auto make_script_proxy(const meta_any_proxy& obj_proxy, const ProxyType& script_proxy) -> meta_any_proxy
{
    meta_any_proxy field_proxy;
    
    field_proxy.impl->get_name = [obj_proxy, script_proxy]()
    {
        auto parent_name = obj_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return script_proxy.get_name();
        }
        return fmt::format("{}/{}", parent_name, script_proxy.get_name());
    };
    
    field_proxy.impl->getter = [obj_proxy, script_proxy](entt::meta_any& result) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            auto field_value = script_proxy.get_value(mono_obj);
            // Create an owned copy to avoid dangling references
            result = entt::meta_any{std::in_place_type<T>, field_value};
            return true;
        }
        return false;
    };
    
    field_proxy.impl->setter = [obj_proxy, script_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            if(value.try_cast<T>())
            {
                script_proxy.set_value(mono_obj, value.cast<T>());
                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
            }
        }
        return false;
    };
    
    return field_proxy;
}

/**
 * @brief Creates a specialized proxy for entity handle fields/properties in script objects
 * 
 * This handles the conversion between mono entity fields/properties and engine entity handles,
 * avoiding capturing heavy invoker objects by storing only the field/property name.
 */
template<typename Invoker>
auto make_entity_handle_proxy(const meta_any_proxy& obj_proxy, const Invoker& mutable_field, rtti::context& ctx) -> meta_any_proxy
{
    meta_any_proxy handle_proxy;
    auto field_name = mutable_field.get_name();
    constexpr bool is_property = std::is_base_of<mono::mono_property, Invoker>::value;
    
    handle_proxy.impl->get_name = [obj_proxy, field_name]()
    {
        auto parent_name = obj_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return field_name;
        }
        return fmt::format("{}/{}", parent_name, field_name);
    };
    
    handle_proxy.impl->getter = [obj_proxy, field_name, &ctx](entt::meta_any& result) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            
            // Recreate the invoker from the field/property name
            auto obj_type = mono_obj.get_type();
            entt::entity entity = entt::null;
            if constexpr(is_property)
            {
                auto property = obj_type.get_property(field_name);
                auto invoker = mono::make_property_invoker<entt::entity>(property);
                entity = invoker.get_value(mono_obj);
            }
            else
            {
                auto field = obj_type.get_field(field_name);
                auto invoker = mono::make_field_invoker<entt::entity>(field);
                entity = invoker.get_value(mono_obj);
            }
            
            auto& ec = ctx.get_cached<ecs>();
            auto& scene = ec.get_scene();
            auto handle = scene.create_handle(entity);
            // Create an owned copy to avoid dangling references
            result = entt::meta_any{std::in_place_type<entt::handle>, handle};
            return true;
        }
        return false;
    };
    
    handle_proxy.impl->setter = [obj_proxy, field_name](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            if(value.try_cast<entt::handle>())
            {
                // Recreate the invoker from the field/property name
                auto obj_type = mono_obj.get_type();
                if constexpr(is_property)
                {
                    auto property = obj_type.get_property(field_name);
                    auto invoker = mono::make_property_invoker<entt::entity>(property);
                    auto handle = value.cast<entt::handle>();
                    invoker.set_value(mono_obj, handle.entity());
                }
                else
                {
                    auto field = obj_type.get_field(field_name);
                    auto invoker = mono::make_field_invoker<entt::entity>(field);
                    auto handle = value.cast<entt::handle>();
                    invoker.set_value(mono_obj, handle.entity());
                }
                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
            }
        }
        return false;
    };
    
    return handle_proxy;
}

/**
 * @brief Creates a specialized proxy for asset handle fields in script objects
 * 
 * This handles the conversion between mono asset handles and engine asset handles,
 * including UID management and asset manager integration.
 */
template<typename T, typename Invoker>
auto make_asset_handle_proxy(const meta_any_proxy& obj_proxy, const Invoker& mutable_field, rtti::context& ctx) -> meta_any_proxy
{
    meta_any_proxy asset_proxy;
    auto field_name = mutable_field.get_name();
    
    asset_proxy.impl->get_name = [obj_proxy, field_name]()
    {
        auto parent_name = obj_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return field_name;
        }
        return fmt::format("{}/{}", parent_name, field_name);
    };
    
    asset_proxy.impl->getter = [obj_proxy, field_name, &ctx](entt::meta_any& result) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            
            // Recreate the invoker from the field name
            auto obj_type = mono_obj.get_type();
            auto field = obj_type.get_field(field_name);
            auto invoker = mono::make_field_invoker<mono::mono_object>(field);
            
            auto val = invoker.get_value(mono_obj);
            
            // Convert mono asset handle to engine asset handle
            asset_handle<T> asset;
            if(val)
            {
                const auto& field_type = invoker.get_type();
                auto prop = field_type.get_property("uid");
                auto mutable_prop = mono::make_property_invoker<hpp::uuid>(prop);
                auto uid = mutable_prop.get_value(val);

                auto& am = ctx.get_cached<asset_manager>();
                asset = am.get_asset<T>(uid);
            }
            
            // Create an owned copy to avoid dangling references
            result = entt::meta_any{std::in_place_type<asset_handle<T>>, asset};
            return true;
        }
        return false;
    };
    
    asset_proxy.impl->setter = [obj_proxy, field_name](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any obj_var;
        if(obj_proxy.impl->getter(obj_var) && obj_var)
        {
            auto& mono_obj = obj_var.cast<mono::mono_object&>();
            if(value.try_cast<asset_handle<T>>())
            {
                // Recreate the invoker from the field name
                auto obj_type = mono_obj.get_type();
                auto field = obj_type.get_field(field_name);
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                
                auto asset = value.cast<asset_handle<T>>();
                const auto& field_type = invoker.get_type();
                
                auto val = invoker.get_value(mono_obj);
                if(asset && !val)
                {
                    val = field_type.new_instance();
                    invoker.set_value(mono_obj, val);
                }

                if(val)
                {
                    auto prop = field_type.get_property("uid");
                    auto mutable_prop = mono::make_property_invoker<hpp::uuid>(prop);
                    mutable_prop.set_value(val, asset.uid());
                }
                
                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
            }
        }
        return false;
    };
    
    return asset_proxy;
}

/**
 * @brief Creates a specialized proxy for individual array elements in script objects
 * 
 * This handles access to specific array indices while maintaining proper property paths.
 * Now receives the array proxy directly instead of the owner object proxy.
 */
template<typename T>
auto make_array_element_proxy(const meta_any_proxy& array_proxy, size_t index, const std::string& element_name) -> meta_any_proxy
{
    meta_any_proxy element_proxy;
    
    element_proxy.impl->get_name = [array_proxy, element_name]()
    {
        auto parent_name = array_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return element_name;
        }
        return fmt::format("{}/{}", parent_name, element_name);
    };
    
    element_proxy.impl->getter = [array_proxy, index](entt::meta_any& result) mutable
    {
        entt::meta_any array_var;
        if(array_proxy.impl->getter(array_var) && array_var)
        {
            auto& array_obj = array_var.cast<mono::mono_object&>();
            mono::mono_array<T> array(array_obj);
            
            if(index < array.size())
            {
                auto element_value = array.get(index);
                // Create an owned copy to avoid dangling references
                result = entt::meta_any{std::in_place_type<T>, element_value};
                return true;
            }
        }
        return false;
    };
    
    element_proxy.impl->setter = [array_proxy, index](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any array_var;
        if(array_proxy.impl->getter(array_var) && array_var)
        {
            auto& array_obj = array_var.cast<mono::mono_object&>();
            if(value.try_cast<T>())
            {
                mono::mono_array<T> array(array_obj);
                
                if(index < array.size())
                {
                    array.set(index, value.cast<T>());
                    // Note: mono arrays are reference types, so the change is already applied
                    // Create a mutable copy of array_proxy for the setter call
                    //auto mutable_array_proxy = array_proxy;
                    //return mutable_array_proxy.impl->setter(mutable_array_proxy, array_var, execution_count); 
                    return true;
                }
            }
        }
        return false;
    };
    
    return element_proxy;
}

/**
 * @brief Creates a specialized proxy for individual List<T> elements in script objects
 * 
 * This handles access to specific list indices while maintaining proper property paths.
 * Similar to make_array_element_proxy but uses mono_list<T> instead of mono_array<T>.
 * 
 * For mono_object specialization, handles conversion from primitive types to boxed mono_objects.
 */
template<typename T>
auto make_list_element_proxy(const meta_any_proxy& list_proxy, size_t index, const std::string& element_name) -> meta_any_proxy
{
    meta_any_proxy element_proxy;
    
    element_proxy.impl->get_name = [list_proxy, element_name]()
    {
        auto parent_name = list_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return element_name;
        }
        return fmt::format("{}/{}", parent_name, element_name);
    };
    
    element_proxy.impl->getter = [list_proxy, index](entt::meta_any& result) mutable
    {
        entt::meta_any list_var;
        if(list_proxy.impl->getter(list_var) && list_var)
        {
            auto& list_obj = list_var.cast<mono::mono_object&>();
            mono::mono_list<T> list(list_obj);
            
            if(index < list.size())
            {
                auto element_value = list.get(index);
                // Create an owned copy to avoid dangling references
                result = entt::meta_any{std::in_place_type<T>, element_value};
                return true;
            }
        }
        return false;
    };
    
    element_proxy.impl->setter = [list_proxy, index](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any list_var;
        if(list_proxy.impl->getter(list_var) && list_var)
        {
            auto& list_obj = list_var.cast<mono::mono_object&>();
            mono::mono_list<T> list(list_obj);
            
            if(index < list.size())
            {
                if(value.try_cast<T>())
                {
                    list.set(index, value.cast<T>());
                    // Note: mono lists are reference types, so the change is already applied
                    // Propagate the change up through the proxy chain
                    auto mutable_list_proxy = list_proxy;
                    return mutable_list_proxy.impl->setter(mutable_list_proxy, list_var, execution_count);
                }
            }
        }
        return false;
    };
    
    return element_proxy;
}

/**
 * @brief Specialization for mono_object elements that handles primitive type conversion
 * 
 * When a list contains mono_object elements but the actual element type might be a primitive,
 * this specialization handles converting primitive values to boxed mono_objects.
 */
template<>
auto make_list_element_proxy<mono::mono_object>(const meta_any_proxy& list_proxy, size_t index, const std::string& element_name) -> meta_any_proxy
{
    meta_any_proxy element_proxy;
    
    element_proxy.impl->get_name = [list_proxy, element_name]()
    {
        auto parent_name = list_proxy.impl->get_name();
        if(parent_name.empty())
        {
            return element_name;
        }
        return fmt::format("{}/{}", parent_name, element_name);
    };
    
    element_proxy.impl->getter = [list_proxy, index](entt::meta_any& result) mutable
    {
        entt::meta_any list_var;
        if(list_proxy.impl->getter(list_var) && list_var)
        {
            auto& list_obj = list_var.cast<mono::mono_object&>();
            mono::mono_list<mono::mono_object> list(list_obj);
            
            if(index < list.size())
            {
                auto element_value = list.get(index);
                // Create an owned copy to avoid dangling references
                result = entt::meta_any{std::in_place_type<mono::mono_object>, element_value};
                return true;
            }
        }
        return false;
    };
    
    element_proxy.impl->setter = [list_proxy, index](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any list_var;
        if(list_proxy.impl->getter(list_var) && list_var)
        {
            auto& list_obj = list_var.cast<mono::mono_object&>();
            mono::mono_list<mono::mono_object> list(list_obj);
            
            if(index < list.size())
            {
                mono::mono_object element_to_set;
                
                // Try to cast directly to mono_object first (already boxed)
                if(value.try_cast<mono::mono_object>())
                {
                    element_to_set = value.cast<mono::mono_object>();
                }
                else
                {
                    // Value is a primitive type, need to box it
                    // Get the element type from the list to know how to box
                    auto element = list.get(index);
                    if(element.valid())
                    {
                        const auto& element_type = element.get_type();
                        
                        // Try common primitive types and box them
                        if(value.try_cast<int32_t>())
                        {
                            int32_t val = value.cast<int32_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<float>())
                        {
                            float val = value.cast<float>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<double>())
                        {
                            double val = value.cast<double>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<bool>())
                        {
                            bool val = value.cast<bool>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<int64_t>())
                        {
                            int64_t val = value.cast<int64_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<uint32_t>())
                        {
                            uint32_t val = value.cast<uint32_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<uint64_t>())
                        {
                            uint64_t val = value.cast<uint64_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<int16_t>())
                        {
                            int16_t val = value.cast<int16_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<uint16_t>())
                        {
                            uint16_t val = value.cast<uint16_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<int8_t>())
                        {
                            int8_t val = value.cast<int8_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else if(value.try_cast<uint8_t>())
                        {
                            uint8_t val = value.cast<uint8_t>();
                            MonoDomain* domain = mono_domain_get();
                            MonoObject* boxed = mono_value_box(domain, element_type.get_internal_ptr(), &val);
                            element_to_set = mono::mono_object(boxed);
                        }
                        else
                        {
                            // Unknown type, can't convert
                            return false;
                        }
                    }
                    else
                    {
                        // Can't determine element type, can't convert
                        return false;
                    }
                }
                
                if(element_to_set.valid())
                {
                    list.set(index, element_to_set);
                    // Propagate the change up through the proxy chain
                    // auto mutable_list_proxy = list_proxy;
                    // return mutable_list_proxy.impl->setter(mutable_list_proxy, list_var, execution_count);
                    return true;
                }
            }
        }
        return false;
    };
    
    return element_proxy;
}

template<typename T>
struct mono_inspector
{
    /**
     * @brief Inspects a mono_object that directly represents a value of type T
     * 
     * This method is used when the mono_object itself is a value type (e.g., a boxed int32_t or math::vec2)
     * and should be inspected directly without going through fields/properties.
     * 
     * @param ctx The RTTI context
     * @param obj The mono_object to inspect (must be a value type representing T)
     * @param obj_proxy The proxy for the object
     * @param info Variable info
     * @return inspect_result The inspection result
     */
    static auto inspect_object(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                const var_info& info) -> inspect_result
    {
        inspect_result result;
        
        // Extract the value from the mono_object
        // For value types, we need to unbox the object
        const auto& obj_type = obj.get_type();
        if(!obj_type.is_valuetype())
        {
            // Not a value type, can't inspect directly
            return result;
        }
        
        // Use mono_converter to extract the value
        // T value = mono::mono_converter<T>::from_mono(obj.get_internal_ptr());
        
        // Create a proxy that can get/set the value directly from/to the mono_object
        meta_any_proxy value_proxy;
        value_proxy.impl->get_name = [obj_proxy]()
        {
            return obj_proxy.impl->get_name();
        };
        
        value_proxy.impl->getter = [obj_proxy](entt::meta_any& result) mutable
        {
            entt::meta_any obj_var;
            if(obj_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                T value = mono::mono_converter<T>::from_mono(mono_obj.get_internal_ptr());
                result = entt::meta_any{std::in_place_type<T>, value};
                return true;
            }
            return false;
        };
        
        value_proxy.impl->setter = [obj_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any obj_var;
            if(obj_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                if(value.try_cast<T>())
                {
                    T new_value = value.cast<T>();
                    // Update the mono_object by creating a new boxed object with the new value
                    const auto& obj_type = mono_obj.get_type();
                    // Create a new boxed object with the new value
                    MonoDomain* domain = mono_domain_get();
                    MonoObject* boxed = mono_value_box(domain, obj_type.get_internal_ptr(), &new_value);
                    mono_obj = mono::mono_object(boxed);
                    return obj_proxy.impl->setter(proxy, obj_var, execution_count);
                }
            }
            return false;
        };
        
        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!value_proxy.impl->getter(var))
        {
            return result;
        }
        
        // Use the existing inspection infrastructure
        result |= inspect_var(ctx, var, value_proxy, info);
        
        return result;
    }

    static auto inspect_field(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                mono::mono_field& field,
                                const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_field_invoker<T>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();

        // Use the new proxy system to enable undo/redo for script fields
        return inspect_invoker_with_proxy(ctx, obj, obj_proxy, field, field_info);
    }
    
    // New method that uses the proxy system for proper undo/redo support
    static auto inspect_invoker_with_proxy(rtti::context& ctx,
                                          mono::mono_object& obj,
                                          const meta_any_proxy& obj_proxy,
                                          mono::mono_field& field,
                                          const var_info& info) -> inspect_result
    {
        // Create script proxy wrapper
        mono_field_proxy<T> script_proxy(field);
        
        // Create meta_any_proxy that bridges to the script system
        auto field_proxy = make_script_proxy<T>(obj_proxy, script_proxy);
        
        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!field_proxy.impl->getter(var))
        {
            return {};
        }

        inspect_result result;

        // Extract attributes for custom metadata
        auto attribs = script_proxy.get_attributes();
        auto range_attrib = find_attribute("RangeAttribute", attribs);
        auto min_attrib = find_attribute("MinAttribute", attribs);
        auto max_attrib = find_attribute("MaxAttribute", attribs);
        auto step_attrib = find_attribute("StepAttribute", attribs);
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        entt::attributes meta_attribs;

        if(min_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(min_attrib.get_type(), "min");
            float min_value = invoker.get_value(min_attrib);
            meta_attribs["min"] = min_value;
        }

        if(range_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(range_attrib.get_type(), "min");
            float min_value = invoker.get_value(range_attrib);
            meta_attribs["min"] = min_value;
            
            auto max_invoker = mono::make_field_invoker<float>(range_attrib.get_type(), "max");
            float max_value = max_invoker.get_value(range_attrib);
            meta_attribs["max"] = max_value;
        }

        if(max_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(max_attrib.get_type(), "max");
            float max_value = invoker.get_value(max_attrib);
            meta_attribs["max"] = max_value;
        }

        if(step_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(step_attrib.get_type(), "step");
            float step_value = invoker.get_value(step_attrib);
            meta_attribs["step"] = step_value;
        }

        auto custom = entt::make_custom<entt::attributes>(meta_attribs);

        {
            property_layout layout(script_proxy.get_name(), tooltip);
            result |= inspect_var(ctx, var, field_proxy, info, custom);
        }

        return result;
    }

    static auto inspect_property(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                mono::mono_property& property,
                                const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_property_invoker<T>(property);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || property.is_readonly();

        // Use the new proxy system to enable undo/redo for script properties
        return inspect_property_with_proxy(ctx, obj, obj_proxy, property, field_info);
    }

    // New method that uses the proxy system for proper undo/redo support
    static auto inspect_property_with_proxy(rtti::context& ctx,
                                            mono::mono_object& obj,
                                            const meta_any_proxy& obj_proxy,
                                            mono::mono_property& property,
                                            const var_info& info) -> inspect_result
    {
        // Create script proxy wrapper
        mono_property_proxy<T> script_proxy(property);
        
        // Create meta_any_proxy that bridges to the script system
        auto prop_proxy = make_script_proxy<T>(obj_proxy, script_proxy);
        
        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!prop_proxy.impl->getter(var))
        {
            return {};
        }

        inspect_result result;

        // Extract attributes for custom metadata
        auto attribs = script_proxy.get_attributes();
        auto range_attrib = find_attribute("RangeAttribute", attribs);
        auto min_attrib = find_attribute("MinAttribute", attribs);
        auto max_attrib = find_attribute("MaxAttribute", attribs);
        auto step_attrib = find_attribute("StepAttribute", attribs);
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        entt::attributes meta_attribs;

        if(min_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(min_attrib.get_type(), "min");
            float min_value = invoker.get_value(min_attrib);
            meta_attribs["min"] = min_value;
        }

        if(range_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(range_attrib.get_type(), "min");
            float min_value = invoker.get_value(range_attrib);
            meta_attribs["min"] = min_value;
            
            auto max_invoker = mono::make_field_invoker<float>(range_attrib.get_type(), "max");
            float max_value = max_invoker.get_value(range_attrib);
            meta_attribs["max"] = max_value;
        }

        if(max_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(max_attrib.get_type(), "max");
            float max_value = invoker.get_value(max_attrib);
            meta_attribs["max"] = max_value;
        }

        if(step_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<float>(step_attrib.get_type(), "step");
            float step_value = invoker.get_value(step_attrib);
            meta_attribs["step"] = step_value;
        }

        auto custom = entt::make_custom<entt::attributes>(meta_attribs);

        {
            property_layout layout(script_proxy.get_name(), tooltip);
            result |= inspect_var(ctx, var, prop_proxy, info, custom);
        }

        return result;
    }
};

template<typename T>
struct mono_inspector_enum
{
    static auto value_to_name(T value, const std::vector<std::pair<T, std::string>>& mapping) -> const std::string&
    {
        for(const auto& kvp : mapping)
        {
            if(kvp.first == value)
            {
                return kvp.second;
            }
        }

        static const std::string empty;
        return empty;
    }

    static auto name_to_value(const std::string& name, const std::vector<std::pair<T, std::string>>& mapping) -> T
    {
        for(const auto& kvp : mapping)
        {
            if(kvp.second == name)
            {
                return kvp.first;
            }
        }

        return std::numeric_limits<T>::max();
    }

    template<typename Invoker>
    static auto inspect_invoker(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                const Invoker& mutable_field,
                                const std::vector<std::pair<T, std::string>>& mapping,
                                const var_info& info) -> inspect_result
    {
        auto val = mutable_field.get_value(obj);

        inspect_result result;

        auto attribs = mutable_field.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        auto current_name = value_to_name(val, mapping);

        std::vector<const char*> cstrings{};
        cstrings.reserve(mapping.size());

        int current_idx = 0;
        int i = 0;
        for(const auto& pair : mapping)
        {
            cstrings.push_back(pair.second.c_str());

            if(current_name == pair.second)
            {
                current_idx = i;
            }
            i++;
        }

        property_layout layout(mutable_field.get_name(), tooltip);

        if(info.read_only)
        {
            ImGui::LabelText("##enum", "%s", cstrings[current_idx]);
        }
        else
        {
            int listbox_item_size = static_cast<int>(cstrings.size());

            ImGuiComboFlags flags = 0;

            if(ImGui::BeginCombo("##enum", cstrings[current_idx], flags))
            {
                for(int n = 0; n < listbox_item_size; n++)
                {
                    const bool is_selected = (current_idx == n);

                    if(ImGui::Selectable(cstrings[n], is_selected))
                    {
                        current_idx = n;
                        result.changed = true;
                        result.edit_finished |= true;
                        val = name_to_value(cstrings[current_idx], mapping);
                    }

                    ImGui::DrawItemActivityOutline();

                    if(is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
            ImGui::DrawItemActivityOutline();
        }

        if(result.changed)
        {
            mutable_field.set_value(obj, val);
        }

        return result;
    }

    static auto inspect_field(rtti::context& ctx,
                              mono::mono_object& obj,
                              const meta_any_proxy& obj_proxy,
                              mono::mono_field& field,
                              const var_info& info) -> inspect_result
    {
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();

        const auto& field_type = field.get_type();

        auto mapping = field_type.get_enum_values<T>();

        // Use the new proxy system to enable undo/redo for enum script fields
        return inspect_enum_field_with_proxy(ctx, obj, obj_proxy, field, mapping, field_info);
    }
    
    // New method that uses the proxy system for proper undo/redo support for enum fields
    static auto inspect_enum_field_with_proxy(rtti::context& ctx,
                                              mono::mono_object& obj,
                                              const meta_any_proxy& obj_proxy,
                                              mono::mono_field& field,
                                              const std::vector<std::pair<T, std::string>>& mapping,
                                              const var_info& info) -> inspect_result
    {
        // Create script proxy wrapper
        mono_field_proxy<T> script_proxy(field);
        
        // Create meta_any_proxy that bridges to the script system
        auto field_proxy = make_script_proxy<T>(obj_proxy, script_proxy);
        
        // Get current value for display
        auto val = script_proxy.get_value(obj);

        inspect_result result;

        auto attribs = script_proxy.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        auto current_name = value_to_name(val, mapping);

        std::vector<const char*> cstrings{};
        cstrings.reserve(mapping.size());

        int current_idx = 0;
        int i = 0;
        for(const auto& pair : mapping)
        {
            cstrings.push_back(pair.second.c_str());

            if(current_name == pair.second)
            {
                current_idx = i;
            }
            i++;
        }

        property_layout layout(script_proxy.get_name(), tooltip);

        if(info.read_only)
        {
            ImGui::LabelText("##enum", "%s", cstrings[current_idx]);
        }
        else
        {
            int listbox_item_size = static_cast<int>(cstrings.size());

            ImGuiComboFlags flags = 0;

            if(ImGui::BeginCombo("##enum", cstrings[current_idx], flags))
            {
                for(int n = 0; n < listbox_item_size; n++)
                {
                    const bool is_selected = (current_idx == n);

                    if(ImGui::Selectable(cstrings[n], is_selected))
                    {
                        current_idx = n;
                        result.changed = true;
                        result.edit_finished |= true;
                        val = name_to_value(cstrings[current_idx], mapping);
                        
                        // Record the change using the proxy
                        entt::meta_any new_value = entt::forward_as_meta(val);
                        entt::meta_any old_value;
                        field_proxy.impl->getter(old_value);
                        
                        auto& override_ctx = ctx.get_cached<prefab_override_context>();
                        add_property_action(ctx, override_ctx, result, field_proxy, old_value, new_value, {});
                    }

                    ImGui::DrawItemActivityOutline();

                    if(is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
            ImGui::DrawItemActivityOutline();
        }

        return result;
    }

    static auto inspect_property(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 mono::mono_property& property,
                                 const var_info& info) -> inspect_result
    {
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || property.is_readonly();

        const auto& field_type = property.get_type();

        auto mapping = field_type.get_enum_values<T>();

        // Use the new proxy system to enable undo/redo for enum script properties
        return inspect_enum_property_with_proxy(ctx, obj, obj_proxy, property, mapping, field_info);
    }
    
    // New method that uses the proxy system for proper undo/redo support for enum properties
    static auto inspect_enum_property_with_proxy(rtti::context& ctx,
                                                 mono::mono_object& obj,
                                                 const meta_any_proxy& obj_proxy,
                                                 mono::mono_property& property,
                                                 const std::vector<std::pair<T, std::string>>& mapping,
                                                 const var_info& info) -> inspect_result
    {
        // Create script proxy wrapper
        mono_property_proxy<T> script_proxy(property);
        
        // Create meta_any_proxy that bridges to the script system
        auto prop_proxy = make_script_proxy<T>(obj_proxy, script_proxy);
        
        // Get current value for display
        auto val = script_proxy.get_value(obj);

        inspect_result result;

        auto attribs = script_proxy.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        auto current_name = value_to_name(val, mapping);

        std::vector<const char*> cstrings{};
        cstrings.reserve(mapping.size());

        int current_idx = 0;
        int i = 0;
        for(const auto& pair : mapping)
        {
            cstrings.push_back(pair.second.c_str());

            if(current_name == pair.second)
            {
                current_idx = i;
            }
            i++;
        }

        property_layout layout(script_proxy.get_name(), tooltip);

        if(info.read_only)
        {
            ImGui::LabelText("##enum", "%s", cstrings[current_idx]);
        }
        else
        {
            int listbox_item_size = static_cast<int>(cstrings.size());

            ImGuiComboFlags flags = 0;

            if(ImGui::BeginCombo("##enum", cstrings[current_idx], flags))
            {
                for(int n = 0; n < listbox_item_size; n++)
                {
                    const bool is_selected = (current_idx == n);

                    if(ImGui::Selectable(cstrings[n], is_selected))
                    {
                        current_idx = n;
                        result.changed = true;
                        result.edit_finished |= true;
                        val = name_to_value(cstrings[current_idx], mapping);
                        
                        // Record the change using the proxy
                        entt::meta_any new_value = entt::forward_as_meta(val);
                        entt::meta_any old_value;
                        prop_proxy.impl->getter(old_value);
                        
                        auto& override_ctx = ctx.get_cached<prefab_override_context>();
                        add_property_action(ctx, override_ctx, result, prop_proxy, old_value, new_value, {});
                    }

                    ImGui::DrawItemActivityOutline();

                    if(is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
            ImGui::DrawItemActivityOutline();
        }

        return result;
    }
};

template<>
struct mono_inspector<entt::handle>
{
    template<typename Invoker>
    static auto inspect_invoker(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                const Invoker& mutable_field,
                                const var_info& info) -> inspect_result
    {
        inspect_result result;

        auto attribs = mutable_field.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        // Use the helper function to create a clean entity handle proxy
        auto handle_proxy = make_entity_handle_proxy(obj_proxy, mutable_field, ctx);

        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!handle_proxy.impl->getter(var))
        {
            return {};
        }

        {
            property_layout layout(mutable_field.get_name(), tooltip);
            result |= inspect_var(ctx, var, handle_proxy, info);
        }

        return result;
    }

    static auto inspect_field(rtti::context& ctx,
                              mono::mono_object& obj,
                              const meta_any_proxy& obj_proxy,
                              mono::mono_field& field,
                              const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_field_invoker<entt::entity>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }

    static auto inspect_property(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 mono::mono_property& field,
                                 const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_property_invoker<entt::entity>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }
};

template<typename T>
struct mono_inspector<asset_handle<T>>
{
    template<typename Invoker>
    static auto inspect_invoker(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                const Invoker& mutable_field,
                                const var_info& info) -> inspect_result
    {
        inspect_result result;

        auto attribs = mutable_field.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
        }

        // Use the helper function to create a clean asset handle proxy
        auto asset_proxy = make_asset_handle_proxy<T>(obj_proxy, mutable_field, ctx);

        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!asset_proxy.impl->getter(var))
        {
            return {};
        }

        {
            property_layout layout(mutable_field.get_name(), tooltip);
            result |= inspect_var(ctx, var, asset_proxy, info);
        }

        return result;
    }

    static auto inspect_field(rtti::context& ctx,
                              mono::mono_object& obj,
                              const meta_any_proxy& obj_proxy,
                              mono::mono_field& field,
                              const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_field_invoker<mono::mono_object>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }

    static auto inspect_property(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 mono::mono_property& field,
                                 const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_property_invoker<mono::mono_object>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }
};

/**
 * @brief Inspector for collections (arrays and List<T>) with add/remove support
 */
struct mono_inspector_collection
{
    template<typename Invoker>
    static auto inspect_collection(rtti::context& ctx,
                                   mono::mono_object& obj,
                                   const meta_any_proxy& obj_proxy,
                                   const Invoker& mutable_field,
                                   const mono::mono_type& collection_type,
                                   const var_info& info) -> inspect_result
    {
        inspect_result result;
        
        auto val = mutable_field.get_value(obj);
        if (!val.valid())
        {
            return result;
        }
        
        bool is_array = collection_type.is_array();
        bool is_list = is_list_type(collection_type);
        
        if (!is_array && !is_list)
        {
            return result;
        }
        
        // Get collection size
        size_t collection_size = 0;
        if (is_array)
        {
            mono::mono_array<mono::mono_object> array(val);
            collection_size = array.size();
        }
        else if (is_list)
        {
            mono::mono_list<mono::mono_object> list(val);
            collection_size = list.size();

        }
        
        // Header with add/remove buttons
        ImGui::PushID(mutable_field.get_name().c_str());
        
        bool tree_open = ImGui::TreeNodeEx(mutable_field.get_name().c_str(), 
                                           ImGuiTreeNodeFlags_DefaultOpen,
                                           "%s [%zu]", 
                                           mutable_field.get_name().c_str(), 
                                           collection_size);
        
        // Add/Remove buttons (only for List<T>, arrays are fixed size)
        if (is_list && !info.read_only)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("+"))
            {
                mono::mono_list<mono::mono_object> list(val);
                list.add(collection_type.new_instance());
                result.changed = true;
                result.edit_finished = true;
            }
            
            if (collection_size > 0)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("-"))
                {
                    // Remove last element from list
                    auto remove_at_method = collection_type.get_method("RemoveAt", 1);
                    if (remove_at_method.valid())
                    {
                        auto remove_invoker = mono::make_method_invoker<void(int32_t)>(remove_at_method);
                        remove_invoker(val, static_cast<int32_t>(collection_size - 1));
                        result.changed = true;
                        result.edit_finished = true;
                    }
                }
            }
        }
        
        if (tree_open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
            
            // Inspect each element
            for (size_t i = 0; i < collection_size; ++i)
            {
                // Get element at index
                mono::mono_object element;
                if (is_array)
                {
                    mono::mono_array<mono::mono_object> array(val);
                    element = array.get(i);
                }
                else if (is_list)
                {
                    mono::mono_list<mono::mono_object> list(val);
                    element = list.get(i);
                }
                
                if (element.valid())
                {
                    // Create array/collection proxy first - create a proxy that points to the collection field
                    meta_any_proxy collection_proxy;
                    collection_proxy.impl->get_name = [obj_proxy, mutable_field]()
                    {
                        auto parent_name = obj_proxy.impl->get_name();
                        auto field_name = mutable_field.get_name();
                        if(parent_name.empty())
                        {
                            return field_name;
                        }
                        return fmt::format("{}/{}", parent_name, field_name);
                    };
                    collection_proxy.impl->getter = [obj_proxy, mutable_field](entt::meta_any& result) mutable
                    {
                        entt::meta_any obj_var;
                        if(obj_proxy.impl->getter(obj_var) && obj_var)
                        {
                            auto& mono_obj = obj_var.cast<mono::mono_object&>();
                            auto collection_obj = mutable_field.get_value(mono_obj);
                            result = entt::meta_any{std::in_place_type<mono::mono_object>, collection_obj};
                            return true;
                        }
                        return false;
                    };
                    collection_proxy.impl->setter = [obj_proxy, mutable_field](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
                    {
                        entt::meta_any obj_var;
                        if(obj_proxy.impl->getter(obj_var) && obj_var)
                        {
                            auto& mono_obj = obj_var.cast<mono::mono_object&>();
                            if(value.try_cast<mono::mono_object>())
                            {
                                mutable_field.set_value(mono_obj, value.cast<mono::mono_object>());
                                return obj_proxy.impl->setter(proxy, obj_var, execution_count);
                            }
                        }
                        return false;
                    };
                    
                    // Create element proxy using the collection proxy
                    std::string element_name = fmt::format("Element {}", i);
                    meta_any_proxy element_proxy;
                    if (is_array)
                    {
                        element_proxy = make_array_element_proxy<mono::mono_object>(collection_proxy, i, element_name);
                    }
                    else if (is_list)
                    {
                        element_proxy = make_list_element_proxy<mono::mono_object>(collection_proxy, i, element_name);
                    }
                    
                    // Inspect element
                    entt::meta_any element_var = entt::forward_as_meta(element);
                    
                    ImGui::PushID(static_cast<int>(i));
                    result |= unravel::inspect_var(ctx, element_var, element_proxy, info);
                    
                    // Remove button for List<T> elements
                    if (is_list && !info.read_only)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X"))
                        {
                            auto remove_at_method = collection_type.get_method("RemoveAt", 1);
                            if (remove_at_method.valid())
                            {
                                auto remove_invoker = mono::make_method_invoker<void(int32_t)>(remove_at_method);
                                remove_invoker(val, static_cast<int32_t>(i));
                                result.changed = true;
                                result.edit_finished = true;
                                ImGui::PopID();
                                break; // Exit loop after removal
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }
            
            ImGui::PopStyleVar();
            ImGui::TreePop();
        }
        
        ImGui::PopID();
        
        return result;
    }
    
    static auto inspect_field(rtti::context& ctx,
                             mono::mono_object& obj,
                             const meta_any_proxy& obj_proxy,
                             mono::mono_field& field,
                             const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_field_invoker<mono::mono_object>(field);
        auto field_type = field.get_type();
        
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();
        
        return inspect_collection(ctx, obj, obj_proxy, invoker, field_type, field_info);
    }
    
    static auto inspect_property(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                mono::mono_property& property,
                                const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_property_invoker<mono::mono_object>(property);
        auto prop_type = property.get_type();
        
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || property.is_readonly();
        
        return inspect_collection(ctx, obj, obj_proxy, invoker, prop_type, field_info);
    }
};

template<typename T>
struct mono_inspector<mono::mono_array<T>>
{
    template<typename Invoker>
    static auto inspect_invoker(rtti::context& ctx,
                                mono::mono_object& obj,
                                const meta_any_proxy& obj_proxy,
                                const Invoker& mutable_field,
                                const var_info& info) -> inspect_result
    {
        inspect_result result;

        auto val = mutable_field.get_value(obj);
        mono::mono_array<T> array(val);

        for(size_t i = 0; i < array.size(); ++i)
        {
            // Create array proxy first - create a proxy that points to the array field
            meta_any_proxy array_proxy;
            array_proxy.impl->get_name = [obj_proxy, mutable_field]()
            {
                auto parent_name = obj_proxy.impl->get_name();
                auto field_name = mutable_field.get_name();
                if(parent_name.empty())
                {
                    return field_name;
                }
                return fmt::format("{}/{}", parent_name, field_name);
            };
            array_proxy.impl->getter = [obj_proxy, mutable_field](entt::meta_any& result) mutable
            {
                entt::meta_any obj_var;
                if(obj_proxy.impl->getter(obj_var) && obj_var)
                {
                    auto& mono_obj = obj_var.cast<mono::mono_object&>();
                    auto array_obj = mutable_field.get_value(mono_obj);
                    result = entt::meta_any{std::in_place_type<mono::mono_object>, array_obj};
                    return true;
                }
                return false;
            };
            array_proxy.impl->setter = [obj_proxy, mutable_field](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
            {
                entt::meta_any obj_var;
                if(obj_proxy.impl->getter(obj_var) && obj_var)
                {
                    auto& mono_obj = obj_var.cast<mono::mono_object&>();
                    if(value.try_cast<mono::mono_object>())
                    {
                        mutable_field.set_value(mono_obj, value.cast<mono::mono_object>());
                        return obj_proxy.impl->setter(proxy, obj_var, execution_count);
                    }
                }
                return false;
            };
            
            // Create element proxy using the array proxy
            std::string element_name = fmt::format("Element {}", i);
            auto element_proxy = make_array_element_proxy<T>(array_proxy, i, element_name);

            // Get current value through the proxy for inspection
            entt::meta_any element;
            if(element_proxy.impl->getter(element))
            {
                result |= unravel::inspect_var(ctx, element, element_proxy, info);
            }
        }
        return result;
    }

    static auto inspect_field(rtti::context& ctx,
                              mono::mono_object& obj,
                              const meta_any_proxy& obj_proxy,
                              mono::mono_field& field,
                              const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_field_invoker<mono::mono_object>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly() || field.is_const();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }

    static auto inspect_property(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 mono::mono_property& field,
                                 const var_info& info) -> inspect_result
    {
        auto invoker = mono::make_property_invoker<mono::mono_object>(field);

        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = ImGui::IsReadonly() || info.read_only || field.is_readonly();

        return inspect_invoker(ctx, obj, obj_proxy, invoker, field_info);
    }
};

auto inspector_mono_object::inspect(rtti::context& ctx,
                                    entt::meta_any& var,
                                    const meta_any_proxy& var_proxy,
                                    const var_info& info,
                                    const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<mono::mono_object&>();

    inspect_result result{};

    const auto& type = data.get_type();
    using mono_object_inspector = std::function<inspect_result(rtti::context& ctx,
                                                                    mono::mono_object& obj,
                                                                    const meta_any_proxy& obj_proxy,
                                                                    const var_info& info)>;
    
    auto get_object_inspector = [](const std::string& type_name) -> const mono_object_inspector&
    {
        // clang-format off
        static std::map<std::string, mono_object_inspector> reg = {
            {"SByte",   &mono_inspector<int8_t>::inspect_object},
            {"Byte",    &mono_inspector<uint8_t>::inspect_object},
            {"Int16",   &mono_inspector<int16_t>::inspect_object},
            {"UInt16",  &mono_inspector<uint16_t>::inspect_object},
            {"Int32",   &mono_inspector<int32_t>::inspect_object},
            {"UInt32",  &mono_inspector<uint32_t>::inspect_object},
            {"Int64",   &mono_inspector<int64_t>::inspect_object},
            {"UInt64",  &mono_inspector<uint64_t>::inspect_object},
            {"Boolean", &mono_inspector<bool>::inspect_object},
            {"Single",  &mono_inspector<float>::inspect_object},
            {"Double",  &mono_inspector<double>::inspect_object},
            {"Char",    &mono_inspector<char16_t>::inspect_object},
            {"String",  &mono_inspector<std::string>::inspect_object},
            // {"Entity",  &mono_inspector<entt::handle>::inspect_object},
            {"Vector2", &mono_inspector<math::vec2>::inspect_object},
            {"Vector3", &mono_inspector<math::vec3>::inspect_object},
            {"Vector4", &mono_inspector<math::vec4>::inspect_object},
            {"Quaternion", &mono_inspector<math::quat>::inspect_object},
            {"Color", &mono_inspector<math::color>::inspect_object},
            {"LayerMask", &mono_inspector<layer_mask>::inspect_object},
            // {"Texture",         &mono_inspector<asset_handle<gfx::texture>>::inspect_object},
            // {"Material",        &mono_inspector<asset_handle<material>>::inspect_object},
            // {"Mesh",            &mono_inspector<asset_handle<mesh>>::inspect_object},
            // {"AnimationClip",   &mono_inspector<asset_handle<animation_clip>>::inspect_object},
            // {"Prefab",          &mono_inspector<asset_handle<prefab>>::inspect_object},
            // {"Scene",           &mono_inspector<asset_handle<scene_prefab>>::inspect_object},
            // {"PhysicsMaterial", &mono_inspector<asset_handle<physics_material>>::inspect_object},
            // {"AudioClip",       &mono_inspector<asset_handle<audio_clip>>::inspect_object},
            // {"Font",            &mono_inspector<asset_handle<font>>::inspect_object},
            // {"Color[]",       &mono_inspector<mono::mono_array<math::color>>::inspect_object},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_object_inspector empty;
        return empty;
    };



    using mono_field_inspector = std::function<inspect_result(rtti::context&,
                                                              mono::mono_object&,
                                                              const meta_any_proxy& obj_proxy,
                                                              mono::mono_field&,
                                                              const var_info&)>;

    auto get_field_inspector = [](const std::string& type_name) -> const mono_field_inspector&
    {
        // clang-format off
        static std::map<std::string, mono_field_inspector> reg = {
            {"SByte",   &mono_inspector<int8_t>::inspect_field},
            {"Byte",    &mono_inspector<uint8_t>::inspect_field},
            {"Int16",   &mono_inspector<int16_t>::inspect_field},
            {"UInt16",  &mono_inspector<uint16_t>::inspect_field},
            {"Int32",   &mono_inspector<int32_t>::inspect_field},
            {"UInt32",  &mono_inspector<uint32_t>::inspect_field},
            {"Int64",   &mono_inspector<int64_t>::inspect_field},
            {"UInt64",  &mono_inspector<uint64_t>::inspect_field},
            {"Boolean", &mono_inspector<bool>::inspect_field},
            {"Single",  &mono_inspector<float>::inspect_field},
            {"Double",  &mono_inspector<double>::inspect_field},
            {"Char",    &mono_inspector<char16_t>::inspect_field},
            {"String",  &mono_inspector<std::string>::inspect_field},
            {"Entity",  &mono_inspector<entt::handle>::inspect_field},
            {"Vector2", &mono_inspector<math::vec2>::inspect_field},
            {"Vector3", &mono_inspector<math::vec3>::inspect_field},
            {"Vector4", &mono_inspector<math::vec4>::inspect_field},
            {"Quaternion", &mono_inspector<math::quat>::inspect_field},
            {"Color", &mono_inspector<math::color>::inspect_field},
            {"LayerMask", &mono_inspector<layer_mask>::inspect_field},
            {"Texture",         &mono_inspector<asset_handle<gfx::texture>>::inspect_field},
            {"Material",        &mono_inspector<asset_handle<material>>::inspect_field},
            {"Mesh",            &mono_inspector<asset_handle<mesh>>::inspect_field},
            {"AnimationClip",   &mono_inspector<asset_handle<animation_clip>>::inspect_field},
            {"Prefab",          &mono_inspector<asset_handle<prefab>>::inspect_field},
            {"Scene",           &mono_inspector<asset_handle<scene_prefab>>::inspect_field},
            {"PhysicsMaterial", &mono_inspector<asset_handle<physics_material>>::inspect_field},
            {"AudioClip",       &mono_inspector<asset_handle<audio_clip>>::inspect_field},
            {"Font",            &mono_inspector<asset_handle<font>>::inspect_field},
            // {"Color[]",       &mono_inspector<mono::mono_array<math::color>>::inspect_field},

        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_field_inspector empty;
        return empty;
    };

    auto get_enum_field_inspector = [](const std::string& type_name) -> const mono_field_inspector&
    {
        // clang-format off
        static std::map<std::string, mono_field_inspector> reg = {
          {"SByte",   &mono_inspector_enum<int8_t>::inspect_field},
          {"Byte",    &mono_inspector_enum<uint8_t>::inspect_field},
          {"Int16",   &mono_inspector_enum<int16_t>::inspect_field},
          {"UInt16",  &mono_inspector_enum<uint16_t>::inspect_field},
          {"Int32",   &mono_inspector_enum<int32_t>::inspect_field},
          {"UInt32",  &mono_inspector_enum<uint32_t>::inspect_field},
          {"Int64",   &mono_inspector_enum<int64_t>::inspect_field},
          {"UInt64",  &mono_inspector_enum<uint64_t>::inspect_field},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_field_inspector empty;
        return empty;
    };

    auto object_inspector = get_object_inspector(type.get_name());
    if(object_inspector)
    {
        result |= object_inspector(ctx, data, var_proxy, info);
    }
    else
    {

        bool include_base = true;
        auto fields = type.get_fields(      include_base);
        for(auto& field : fields)
        {
            bool inspect_predicate = field.get_visibility() == mono::visibility::vis_public;

            ImGui::PushReadonly(!inspect_predicate);

            if(is_debug_view())
            {
                inspect_predicate = !field.is_backing_field();
            }

            if(field.is_static())
            {
                inspect_predicate = false;
            }


            if(inspect_predicate)
            {
                const auto& field_type = field.get_type();

                auto field_inspector = get_field_inspector(field_type.get_name());

                auto& override_ctx = ctx.get_cached<prefab_override_context>();
                override_ctx.push_segment(field.get_name(), field.get_name());

                if(field_inspector)
                {
                    result |= field_inspector(ctx, data, var_proxy, field, info);
                }
                else if(field_type.is_enum())
                {
                    auto enum_type = field_type.get_enum_base_type();
                    auto enum_inspector = get_enum_field_inspector(enum_type.get_name());
                    if(enum_inspector)
                    {
                        result |= enum_inspector(ctx, data, var_proxy, field, info);
                    }
                }
                // else if(field_type.is_array() || is_list_type(field_type))
                // {
                //     //Handle arrays and List<T> with add/remove support
                //     result |= mono_inspector_collection::inspect_field(ctx, data, var_proxy, field, info);
                // }
                else if(is_serializable_type(field_type))
                {
                    // Recursively inspect serializable nested objects
                    // auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                    // auto nested_obj = invoker.get_value(data);
                    
                    // if(nested_obj.valid())
                    // {
                    //     auto nested_proxy = make_nested_object_proxy(var_proxy, invoker);
                    //     result |= inspect_serializable_object(ctx, nested_obj, nested_proxy, field.get_name(), info);
                    // }
                    // else
                    {
                        // Object is null, show as read-only field
                        var_info field_info;
                        field_info.is_property = true;
                        field_info.read_only = true;

                        std::string null_text = "null (" + field_type.get_name() + ")";
                        entt::meta_any null_var = entt::forward_as_meta(null_text);
                        auto null_var_proxy = make_proxy(null_var);
                        
                        {
                            property_layout layout(field.get_name());
                            result |= inspect_var(ctx, null_var, null_var_proxy, field_info);
                        }
                    }
                }
                else
                {
                    // Fallback to unknown type display
                    var_info field_info;
                    field_info.is_property = true;
                    field_info.read_only = true;

                    std::string unknown_text;

                    try
                    {
                        auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                        auto nested_obj = invoker.get_value(data);
                        if(nested_obj.valid())
                        {
                            unknown_text = fmt::format("Unknown ({})", nested_obj.get_type().get_name());
                        }
                        else
                        {
                            unknown_text = "null (" + field_type.get_name() + ")";
                        }
                    }
                    catch(const std::exception& e)
                    {
                        unknown_text = fmt::format("Unknown ({})", e.what());
                    }

                    
                    entt::meta_any unknown_var = entt::forward_as_meta(unknown_text);
                    auto unknown_var_proxy = make_proxy(unknown_var);
                    
                    {
                        property_layout layout(field.get_name());
                        result |= inspect_var(ctx, unknown_var, unknown_var_proxy, field_info);
                    }
                }

                override_ctx.pop_segment();
            }
            ImGui::PopReadonly();
        }

        using mono_property_inspector = std::function<inspect_result(rtti::context&,
                                                                    mono::mono_object&,
                                                                    const meta_any_proxy& obj_proxy,
                                                                    mono::mono_property&,
                                                                    const var_info&)>;

        auto get_property_inspector = [](const std::string& type_name) -> const mono_property_inspector&
        {
            // clang-format off
            static std::map<std::string, mono_property_inspector> reg = {
                {"SByte",   &mono_inspector<int8_t>::inspect_property},
                {"Byte",    &mono_inspector<uint8_t>::inspect_property},
                {"Int16",   &mono_inspector<int16_t>::inspect_property},
                {"UInt16",  &mono_inspector<uint16_t>::inspect_property},
                {"Int32",   &mono_inspector<int32_t>::inspect_property},
                {"UInt32",  &mono_inspector<uint32_t>::inspect_property},
                {"Int64",   &mono_inspector<int64_t>::inspect_property},
                {"UInt64",  &mono_inspector<uint64_t>::inspect_property},
                {"Boolean", &mono_inspector<bool>::inspect_property},
                {"Single",  &mono_inspector<float>::inspect_property},
                {"Double",  &mono_inspector<double>::inspect_property},
                {"Char",    &mono_inspector<char16_t>::inspect_property},
                {"String",  &mono_inspector<std::string>::inspect_property},
                {"Entity",  &mono_inspector<entt::handle>::inspect_property},
                {"Vector2",  &mono_inspector<math::vec2>::inspect_property},
                {"Vector3",  &mono_inspector<math::vec3>::inspect_property},
                {"Vector4",  &mono_inspector<math::vec4>::inspect_property},
                {"Quaternion",  &mono_inspector<math::quat>::inspect_property},
                {"Color",  &mono_inspector<math::color>::inspect_property},
                {"LayerMask",  &mono_inspector<layer_mask>::inspect_property},
                {"Texture",         &mono_inspector<asset_handle<gfx::texture>>::inspect_property},
                {"Material",        &mono_inspector<asset_handle<material>>::inspect_property},
                {"Mesh",            &mono_inspector<asset_handle<mesh>>::inspect_property},
                {"AnimationClip",   &mono_inspector<asset_handle<animation_clip>>::inspect_property},
                {"Prefab",          &mono_inspector<asset_handle<prefab>>::inspect_property},
                {"Scene",           &mono_inspector<asset_handle<scene_prefab>>::inspect_property},
                {"PhysicsMaterial", &mono_inspector<asset_handle<physics_material>>::inspect_property},
                {"AudioClip",       &mono_inspector<asset_handle<audio_clip>>::inspect_property},
                {"Font",            &mono_inspector<asset_handle<font>>::inspect_property}

            };
            // clang-format on

            auto it = reg.find(type_name);
            if(it != reg.end())
            {
                return it->second;
            }
            static const mono_property_inspector empty;
            return empty;
        };

        auto get_enum_property_inspector = [](const std::string& type_name) -> const mono_property_inspector&
        {
            // clang-format off
            static std::map<std::string, mono_property_inspector> reg = {
            {"SByte",   &mono_inspector_enum<int8_t>::inspect_property},
            {"Byte",    &mono_inspector_enum<uint8_t>::inspect_property},
            {"Int16",   &mono_inspector_enum<int16_t>::inspect_property},
            {"UInt16",  &mono_inspector_enum<uint16_t>::inspect_property},
            {"Int32",   &mono_inspector_enum<int32_t>::inspect_property},
            {"UInt32",  &mono_inspector_enum<uint32_t>::inspect_property},
            {"Int64",   &mono_inspector_enum<int64_t>::inspect_property},
            {"UInt64",  &mono_inspector_enum<uint64_t>::inspect_property},
            };
            // clang-format on

            auto it = reg.find(type_name);
            if(it != reg.end())
            {
                return it->second;
            }
            static const mono_property_inspector empty;
            return empty;
        };

        auto properties = type.get_properties(include_base);
        for(auto& prop : properties)
        {
            bool inspect_predicate = prop.get_visibility() == mono::visibility::vis_public;
            ImGui::PushReadonly(!inspect_predicate);

            if(is_debug_view())
            {
                inspect_predicate = true;
            }

            if(prop.is_static())
            {
                inspect_predicate = false;
            }

            if(inspect_predicate)
            {
                const auto& prop_type = prop.get_type();

                auto property_inspector = get_property_inspector(prop_type.get_name());

                auto& override_ctx = ctx.get_cached<prefab_override_context>();
                override_ctx.push_segment(prop.get_name(), prop.get_name());

                if(property_inspector)
                {
                    result |= property_inspector(ctx, data, var_proxy, prop, info);
                }
                else if(prop_type.is_enum())
                {
                    auto enum_type = prop_type.get_enum_base_type();
                    auto enum_inspector = get_enum_property_inspector(enum_type.get_name());
                    if(enum_inspector)
                    {
                        result |= enum_inspector(ctx, data, var_proxy, prop, info);
                    }
                }
                // else if(prop_type.is_array() || is_list_type(prop_type))
                // {
                //     // Handle arrays and List<T> with add/remove support
                //     result |= mono_inspector_collection::inspect_property(ctx, data, var_proxy, prop, info);
                // }
                // else if(is_serializable_type(prop_type))
                // {
                //     // Recursively inspect serializable nested objects
                //     auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                //     auto nested_obj = invoker.get_value(data);
                    
                //     if(nested_obj.valid())
                //     {
                //         auto nested_proxy = make_nested_property_proxy(var_proxy, invoker);
                //         result |= inspect_serializable_object(ctx, nested_obj, nested_proxy, prop.get_name(), info);
                //     }
                //     else
                //     {
                //         // Object is null, show as read-only field
                //         var_info field_info;
                //         field_info.is_property = true;
                //         field_info.read_only = true;

                //         std::string null_text = "null (" + prop_type.get_name() + ")";
                //         entt::meta_any null_var = entt::forward_as_meta(null_text);
                //         auto null_var_proxy = make_proxy(null_var);
                        
                //         {
                //             property_layout layout(prop.get_name());
                //             result |= inspect_var(ctx, null_var, null_var_proxy, field_info);
                //         }
                //     }
                // }
                else
                {
                    // Fallback to unknown type display
                    var_info field_info;
                    field_info.is_property = true;
                    field_info.read_only = true;

                    std::string unknown_text;

                    try
                    {
                        auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                        auto nested_obj = invoker.get_value(data);
                        if(nested_obj.valid())
                        {
                            unknown_text = fmt::format("Unknown ({})", nested_obj.get_type().get_name());
                        }
                        else
                        {
                            unknown_text = "null (" + prop.get_type().get_name() + ")";
                        }
                    }
                    catch(const std::exception& e)
                    {
                        unknown_text = fmt::format("Unknown ({})", e.what());
                    }

                    
                    entt::meta_any unknown_var = entt::forward_as_meta(unknown_text);
                    auto unknown_var_proxy = make_proxy(unknown_var);
                    
                    {
                        property_layout layout(prop.get_name());
                        result |= inspect_var(ctx, unknown_var, unknown_var_proxy, field_info);
                    }
                }

                override_ctx.pop_segment();
            }
            ImGui::PopReadonly();
        }
    }

    return result;
}

auto inspector_mono_scoped_object::inspect(rtti::context& ctx,
                                           entt::meta_any& var,
                                           const meta_any_proxy& var_proxy,
                                           const var_info& info,
                                           const entt::meta_custom& custom) -> inspect_result
{
    meta_any_proxy obj_proxy;
    obj_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
    obj_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto& data = var.cast<mono::mono_scoped_object&>();
            auto& mono_obj = static_cast<mono::mono_object&>(data.object);
            result = entt::forward_as_meta(mono_obj);
            return true;
        }
        return false;
    };
    obj_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            var.assign(value);
            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
        }
        return false;
    };


    auto& data = var.cast<mono::mono_scoped_object&>();
    auto& mono_obj = static_cast<mono::mono_object&>(data.object);
    auto obj_var = entt::forward_as_meta(mono_obj);

    return inspector_mono_object::inspect(ctx, obj_var, obj_proxy, info, custom);
}

/**
 * @brief Recursively inspects a serializable object using the main inspector
 * 
 * This function creates a tree node and recursively calls the main inspector
 * to handle all fields and properties of a serializable object.
 */
auto inspect_serializable_object(rtti::context& ctx,
                                 mono::mono_object& obj,
                                 const meta_any_proxy& obj_proxy,
                                 const std::string& name,
                                 const var_info& info) -> inspect_result
{
    inspect_result result{};
    
    // Create a collapsible tree node for the nested object
    bool tree_open = ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    
    if(tree_open)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 8.0f);
        
        // Create a meta_any wrapper for the nested object and call the main inspector
        auto obj_var = entt::forward_as_meta(obj);
        inspector_mono_object inspector;
        result |= inspector.inspect(ctx, obj_var, obj_proxy, info, {});
        
        ImGui::PopStyleVar();
        ImGui::TreePop();
    }
    
    return result;
}

} // namespace unravel
