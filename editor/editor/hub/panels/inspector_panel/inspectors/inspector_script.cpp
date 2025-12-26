#include "inspector_script.h"
#include "editor/imgui/integration/imgui.h"
#include "entt/core/any.hpp"
#include "inspectors.h"
#include <engine/assets/asset_manager.h>
#include <monopp/mono_field_invoker.h>
#include <engine/scripting/ecs/systems/script_interop.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <monopp/mono_property_invoker.h>
#include <monopp/mono_type_conversion.h>
#include <monopp/mono_list.h>
#include <type_traits>
#include <functional>

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
#include <stack>

namespace unravel
{

auto get_mono_type_stack() -> std::stack<mono::mono_type>&
{
    static std::stack<mono::mono_type> stack;
    return stack;
}

void push_mono_type(const mono::mono_type& type)
{
    get_mono_type_stack().push(type);
}

void pop_mono_type()
{
    get_mono_type_stack().pop();
}

auto get_current_mono_type() -> mono::mono_type
{
    if(get_mono_type_stack().empty())
    {
        return {};
    }
    return get_mono_type_stack().top();
}

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


auto get_header(const mono::mono_field& field) -> std::string
{
    auto attribs = field.get_attributes();
    auto header_attrib = find_attribute("HeaderAttribute", attribs);
    if(header_attrib.valid())
    {
        auto invoker = mono::make_field_invoker<std::string>(header_attrib.get_type(), "header");
        return invoker.get_value(header_attrib);
    }
    return "";
}
auto get_header(const mono::mono_property& property) -> std::string
{
    auto attribs = property.get_attributes();
    auto header_attrib = find_attribute("HeaderAttribute", attribs);
    if(header_attrib.valid())
    {
        auto invoker = mono::make_property_invoker<std::string>(header_attrib.get_type(), "header");
        return invoker.get_value(header_attrib);
    }
    return "";
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
    nested_proxy.impl->type_name = mutable_field.get_type().get_fullname();
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
    nested_proxy.impl->type_name = mutable_property.get_type().get_fullname();

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
    field_proxy.impl->type_name = script_proxy.get_type().get_fullname();

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
            if(!mono_obj.valid())
            {
                return false;
            }
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
    
    handle_proxy.impl->type_name = mutable_field.get_type().get_fullname();
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
    
    asset_proxy.impl->type_name = mutable_field.get_type().get_fullname();
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
  
        // Create a proxy that can get/set the value directly from/to the mono_object
        meta_any_proxy value_proxy;
        value_proxy.impl->type_name = obj.get_type().get_fullname();
        value_proxy.impl->get_name = [parent_proxy = obj_proxy]()
        {
            return parent_proxy.impl->get_name();
        };
        
        value_proxy.impl->getter = [parent_proxy = obj_proxy](entt::meta_any& result) mutable
        {
            entt::meta_any obj_var;
            if(parent_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                T value = mono::mono_converter<T>::from_mono(mono_obj.get_internal_ptr());
                result = entt::meta_any{std::in_place_type<T>, value};
                return true;
            }
            return false;
        };
        
        value_proxy.impl->setter = [parent_proxy = obj_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any obj_var;
            if(parent_proxy.impl->getter(obj_var) && obj_var)
            {
                auto mono_obj = obj_var.cast<mono::mono_object&>();
                if(value.try_cast<T>() )
                {
                    auto type = mono_obj.get_type();
                    if(type.is_string())
                    {
                        if(std::is_same<T, std::string>::value)
                        {
                            auto str = value.cast<std::string>();
                            auto new_mono_obj = mono::mono_object(mono::mono_converter<std::string>::to_mono(str));
                            obj_var = entt::meta_any{std::in_place_type<mono::mono_object>, new_mono_obj};

                            return parent_proxy.impl->setter(parent_proxy, obj_var, execution_count);

                        }
                    }
                    else
                    {
                        auto mono_value = mono::mono_converter<T>::to_mono(value.cast<T>());
                        mono_obj.box_value(mono_value, type);
                        obj_var = mono_obj;
                        return parent_proxy.impl->setter(parent_proxy, obj_var, execution_count);

                    }
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
    static auto inspect_object(rtti::context& ctx,
                               mono::mono_object& obj,
                               const meta_any_proxy& obj_proxy,
                               const var_info& info) -> inspect_result
    {
        inspect_result result;
        
        // Create a proxy that can get/set the handle value directly from/to the mono_object
        meta_any_proxy handle_proxy;
        handle_proxy.impl->type_name = obj.get_type().get_fullname();
        handle_proxy.impl->get_name = [obj_proxy]()
        {
            return obj_proxy.impl->get_name();
        };
        
        handle_proxy.impl->getter = [obj_proxy, &ctx](entt::meta_any& result) mutable
        {
            entt::meta_any obj_var;
            if(obj_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                
                // Unbox the Entity value from the mono_object
                // Entity is a value type in C#, so we need to unbox it
                auto obj_type = mono_obj.get_type();
                if(!obj_type.is_valuetype())
                {
                    return false;
                }
                auto entity = mono::mono_converter<entt::entity>::from_mono(mono_obj.get_internal_ptr());                
                // Convert entity to handle using the scene
                auto& ec = ctx.get_cached<ecs>();
                auto& scene = ec.get_scene();
                auto handle = scene.create_handle(entity);
                
                result = entt::meta_any{std::in_place_type<entt::handle>, handle};
                return true;
            }
            return false;
        };
        
        handle_proxy.impl->setter = [parent_proxy = obj_proxy, &ctx](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any obj_var;
            if(parent_proxy.impl->getter(obj_var) && obj_var)
            {
                auto mono_obj = obj_var.cast<mono::mono_object&>();
                if(value.try_cast<entt::handle>())
                {
                    auto handle = value.cast<entt::handle>();
                    entt::entity entity = handle ? handle.entity() : entt::null;
                    
                    // Box the entity value back into the mono_object
                    auto obj_type = mono_obj.get_type();
                    if(obj_type.is_valuetype())
                    {
                        auto mono_entity = mono::mono_converter<entt::entity>::to_mono(entity);
                        mono_obj.box_value(mono_entity, obj_type);
                        obj_var = mono_obj;
                        return parent_proxy.impl->setter(parent_proxy, obj_var, execution_count);
                    }
                }
            }
            return false;
        };
        
        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!handle_proxy.impl->getter(var))
        {
            return result;
        }
        
        // Use the existing inspection infrastructure
        result |= inspect_var(ctx, var, handle_proxy, info);
        
        return result;
    }

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
    static auto inspect_object(rtti::context& ctx,
                               mono::mono_object& obj,
                               const meta_any_proxy& obj_proxy,
                               const var_info& info) -> inspect_result
    {
        inspect_result result;
        
        // Create a proxy that can get/set the asset handle value directly from/to the mono_object
        meta_any_proxy asset_proxy;
        asset_proxy.impl->type_name = obj.get_type().get_fullname();
        asset_proxy.impl->get_name = [obj_proxy]()
        {
            return obj_proxy.impl->get_name();
        };
        
        asset_proxy.impl->getter = [obj_proxy, &ctx](entt::meta_any& result) mutable
        {
            entt::meta_any obj_var;
            if(obj_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                
                // The mono_object itself represents an asset handle (reference type)
                // Get the UID property from the asset handle object
                asset_handle<T> asset;
                if(mono_obj.valid())
                {
                    auto obj_type = mono_obj.get_type();
                    auto prop = obj_type.get_property("uid");
                    if(prop.get_internal_ptr())
                    {
                        auto uid_prop = mono::make_property_invoker<hpp::uuid>(prop);
                        auto uid = uid_prop.get_value(mono_obj);
                        
                        auto& am = ctx.get_cached<asset_manager>();
                        asset = am.get_asset<T>(uid);
                    }
                }
                
                result = entt::meta_any{std::in_place_type<asset_handle<T>>, asset};
                return true;
            }
            return false;
        };
        
        asset_proxy.impl->setter = [parent_proxy = obj_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any obj_var;
            if(parent_proxy.impl->getter(obj_var) && obj_var)
            {
                auto mono_obj = obj_var.cast<mono::mono_object&>();
                if(value.try_cast<asset_handle<T>>())
                {
                    auto asset = value.cast<asset_handle<T>>();
                    auto obj_type = mono_obj.get_type();

                    // Set the UID property on the asset handle object
                    if(!mono_obj.valid() && obj_type.valid())
                    {  
                        mono_obj = obj_type.new_instance();
                    }
                    
                    if(mono_obj.valid())
                    {
                        auto prop = obj_type.get_property("uid");
                        if(prop.get_internal_ptr())
                        {
                            auto uid_prop = mono::make_property_invoker<hpp::uuid>(prop);
                            uid_prop.set_value(mono_obj, asset ? asset.uid() : hpp::uuid{});
                            obj_var = mono_obj;
                            return parent_proxy.impl->setter(parent_proxy, obj_var, execution_count);
                        }
                    }
                }
            }
            return false;
        };
        
        // Get current value through the proxy for inspection
        entt::meta_any var;
        if(!asset_proxy.impl->getter(var))
        {
            return result;
        }
        
        // Use the existing inspection infrastructure
        result |= inspect_var(ctx, var, asset_proxy, info);
        
        return result;
    }

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
        bool is_list = collection_type.is_list();
        
        if (!is_array && !is_list)
        {
            return result;
        }
        
        
        meta_any_proxy collection_proxy;
        collection_proxy.impl->type_name = collection_type.get_fullname();
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
        collection_proxy.impl->getter = [obj_proxy, mutable_field, is_array, is_list](entt::meta_any& result) mutable
        {
            entt::meta_any obj_var;
            if(obj_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                auto collection_obj = mutable_field.get_value(mono_obj);


                if(is_array)
                {
                    auto array = mono::mono_array<mono::mono_object>(collection_obj).to_vector();
                    result = array;
                    return true;
                }
                if(is_list)
                {
                    auto list = mono::mono_list<mono::mono_object>(collection_obj).to_vector();
                    result = list;
                    return true;
                }

            }
            return false;
        };
        collection_proxy.impl->setter = [parent_proxy = obj_proxy, mutable_field, is_array, is_list](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
        {
            entt::meta_any obj_var;
            if(parent_proxy.impl->getter(obj_var) && obj_var)
            {
                auto& mono_obj = obj_var.cast<mono::mono_object&>();
                if(auto vec = value.try_cast<std::vector<mono::mono_object>>())
                {
                    if(is_array)
                    {
                        
                        auto element_type = mutable_field.get_type().get_element_type();

                        auto collection_obj = mutable_field.get_value(mono_obj);
                        auto collection = mono::mono_array<mono::mono_object>(collection_obj);
                        collection.set(*vec, element_type);

                        mutable_field.set_value(mono_obj, collection);
                    }
                    if(is_list)
                    {
                        auto element_type = mutable_field.get_type().get_element_type();

                        auto collection_obj = mutable_field.get_value(mono_obj);
                        auto collection = mono::mono_list<mono::mono_object>(collection_obj);
                        auto old_vec = collection.to_vector<std::vector<mono::mono_object>>();
                        collection.set(*vec, element_type);
                        mutable_field.set_value(mono_obj, collection);
                    }
                    return parent_proxy.impl->setter(parent_proxy, obj_var, execution_count);
                }
            }
            return false;
        };


        auto attribs = mutable_field.get_attributes();
        auto tooltip_attrib = find_attribute("TooltipAttribute", attribs);

        entt::attributes meta_attribs;
        meta_attribs["name"] = mutable_field.get_name();
        meta_attribs["pretty_name"] = mutable_field.get_name();
        meta_attribs["is_fixed_size_array"] = is_array;
        std::string tooltip;
        if(tooltip_attrib.valid())
        {
            auto invoker = mono::make_field_invoker<std::string>(tooltip_attrib.get_type(), "tooltip");
            tooltip = invoker.get_value(tooltip_attrib);
            meta_attribs["tooltip"] = tooltip;
        }
        auto custom = entt::make_custom<entt::attributes>(meta_attribs);
        {
            ImGui::PushID(mutable_field.get_name().c_str());
            
            if (is_array)
            {
                mono::mono_array<mono::mono_object> array(val);
                push_mono_type(array.get_element_type());
                auto vec = array.to_vector();
                entt::meta_any vec_var = entt::forward_as_meta(vec);
                result |= unravel::inspect_var(ctx, vec_var, collection_proxy, info, custom);

                pop_mono_type();

            }
            else if (is_list)
            {
                mono::mono_list<mono::mono_object> list(val);
                push_mono_type(list.get_element_type());
                auto vec = list.to_vector();
                entt::meta_any vec_var = entt::forward_as_meta(vec);
                result |= unravel::inspect_var(ctx, vec_var, collection_proxy, info, custom);
                pop_mono_type();
            }

            ImGui::PopID();
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

auto inspector_mono_object::inspect(rtti::context& ctx,
                                    entt::meta_any& var,
                                    const meta_any_proxy& var_proxy,
                                    const var_info& info,
                                    const entt::meta_custom& custom) -> inspect_result
{


    meta_any_proxy obj_proxy;
    obj_proxy.impl->type_name = entt::get_pretty_name(var.type());
    obj_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
    obj_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto data = var.cast<mono::mono_object&>();
            result = data;
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

    // auto& data =  var.cast<mono::mono_object&>();

    entt::meta_any vdata;
    if(!obj_proxy.impl->getter(vdata))
    {
        return {};
    }
    auto& data = vdata.cast<mono::mono_object&>();

    inspect_result result{};
    auto type = data.get_type();

    if(!type.valid())
    {
        type = get_current_mono_type();
    }

    if(!data.valid() && !type.valid())
    {
        // Fallback to unknown type display
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = true;
        std::string unknown_text;
        unknown_text = "null(object)";
        entt::meta_any unknown_var = entt::forward_as_meta(unknown_text);
        auto unknown_var_proxy = make_proxy(unknown_var);
        {
            property_layout layout(unknown_var_proxy.impl->get_name());
            result |= inspect_var(ctx, unknown_var, unknown_var_proxy, field_info);
        }
        return result;
    
    }

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
            {"Entity",  &mono_inspector<entt::handle>::inspect_object},
            {"Vector2", &mono_inspector<math::vec2>::inspect_object},
            {"Vector3", &mono_inspector<math::vec3>::inspect_object},
            {"Vector4", &mono_inspector<math::vec4>::inspect_object},
            {"Quaternion", &mono_inspector<math::quat>::inspect_object},
            {"Color", &mono_inspector<math::color>::inspect_object},
            {"LayerMask", &mono_inspector<layer_mask>::inspect_object},
            {"Texture",         &mono_inspector<asset_handle<gfx::texture>>::inspect_object},
            {"Material",        &mono_inspector<asset_handle<material>>::inspect_object},
            {"Mesh",            &mono_inspector<asset_handle<mesh>>::inspect_object},
            {"AnimationClip",   &mono_inspector<asset_handle<animation_clip>>::inspect_object},
            {"Prefab",          &mono_inspector<asset_handle<prefab>>::inspect_object},
            {"Scene",           &mono_inspector<asset_handle<scene_prefab>>::inspect_object},
            {"PhysicsMaterial", &mono_inspector<asset_handle<physics_material>>::inspect_object},
            {"AudioClip",       &mono_inspector<asset_handle<audio_clip>>::inspect_object},
            {"Font",            &mono_inspector<asset_handle<font>>::inspect_object},
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


    auto& script_sys = ctx.get_cached<script_system>();
    bool should_inspect = (type.is_serializable() || type.is_derived_from(script_sys.get_scriptable_component_base_type()));

    auto object_inspector = get_object_inspector(type.get_name());
    if(object_inspector)
    {
        result |= object_inspector(ctx, data, obj_proxy, info);
    }
    else if(should_inspect)
    {
        if(!data.valid())
        {
            // if(ImGui::Button("New"))
            // {
            //     auto new_data = type.new_instance();
            //     vdata = new_data;
            //     result.changed = obj_proxy.impl->setter(obj_proxy, vdata, 1);
            //     result.edit_finished = result.changed;
            //     return result;
            // }
            
            // ImGui::SameLine();
            // Object is null, show as read-only field
            var_info field_info;
            field_info.is_property = true;
            field_info.read_only = true;

            std::string null_text = "null (" + type.get_name() + ")";
            entt::meta_any null_var = entt::forward_as_meta(null_text);
            auto null_var_proxy = make_proxy(null_var);
            
            {
                result |= inspect_var(ctx, null_var, null_var_proxy, field_info);
            }

            return result;
        }

        // if(ImGui::Button("Null"))
        // {
        //     data.set_data(nullptr, type);
        //     result.changed = true;
        //     result.edit_finished = true;
        //     return result;
        // }

        // ImGui::SameLine();

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

            if(field.is_static() || field.has_attribute("HideAttribute"))
            {
                inspect_predicate = false;
            }


            if(inspect_predicate)
            {
                std::string header = get_header(field);
                if(!header.empty())
                {
                    ImGui::PushFont(ImGui::Font::Bold);
                    {
                        ImGui::NewLine();
                        ImGui::Text("%s", header.c_str());
                        ImGui::Spacing();
                    }
                    ImGui::PopFont();
                }
                

                const auto& field_type = field.get_type();

                auto field_inspector = get_field_inspector(field_type.get_name());

                auto& override_ctx = ctx.get_cached<prefab_override_context>();
                override_ctx.push_segment(field.get_name(), field.get_name());

                if(field_inspector)
                {
                    result |= field_inspector(ctx, data, obj_proxy, field, info);
                }
                else if(field_type.is_enum())
                {
                    auto enum_type = field_type.get_enum_base_type();
                    auto enum_inspector = get_enum_field_inspector(enum_type.get_name());
                    if(enum_inspector)
                    {
                        result |= enum_inspector(ctx, data, obj_proxy, field, info);
                    }
                }
                else if(field_type.is_array() || field_type.is_list())
                {
                    //Handle arrays and List<T> with add/remove support
                    result |= mono_inspector_collection::inspect_field(ctx, data, obj_proxy, field, info);
                }
                else if(field_type.is_serializable())
                {
                    // Recursively inspect serializable nested objects
                    auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                    auto nested_obj = invoker.get_value(data);
                    
                    if(nested_obj.valid())
                    {
                        auto nested_proxy = make_nested_object_proxy(obj_proxy, invoker);
                        result |= inspect_serializable_object(ctx, nested_obj, nested_proxy, field.get_name(), info);
                    }
                    else
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
                // else
                // {
                //     // Fallback to unknown type display
                //     var_info field_info;
                //     field_info.is_property = true;
                //     field_info.read_only = true;

                //     std::string unknown_text;

                //     try
                //     {
                //         auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                //         auto nested_obj = invoker.get_value(data);
                //         if(nested_obj.valid())
                //         {
                //             unknown_text = fmt::format("Unknown ({})", nested_obj.get_type().get_name());
                //         }
                //         else
                //         {
                //             unknown_text = "null (" + field_type.get_name() + ")";
                //         }
                //     }
                //     catch(const std::exception& e)
                //     {
                //         unknown_text = fmt::format("Unknown ({})", e.what());
                //     }

                    
                //     entt::meta_any unknown_var = entt::forward_as_meta(unknown_text);
                //     auto unknown_var_proxy = make_proxy(unknown_var);
                    
                //     {
                //         property_layout layout(field.get_name());
                //         result |= inspect_var(ctx, unknown_var, unknown_var_proxy, field_info);
                //     }
                // }

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

            if(prop.is_static() || prop.has_attribute("HideAttribute"))
            {
                inspect_predicate = false;
            }

            if(inspect_predicate)
            {
                std::string header = get_header(prop);
                if(!header.empty())
                {
                    ImGui::PushFont(ImGui::Font::Bold);
                    {
                        ImGui::NewLine();
                        ImGui::Text("%s", header.c_str());
                        ImGui::Spacing();
                    }
                    ImGui::PopFont();
                }
                const auto& prop_type = prop.get_type();

                auto property_inspector = get_property_inspector(prop_type.get_name());

                auto& override_ctx = ctx.get_cached<prefab_override_context>();
                override_ctx.push_segment(prop.get_name(), prop.get_name());

                if(property_inspector)
                {
                    result |= property_inspector(ctx, data, obj_proxy, prop, info);
                }
                else if(prop_type.is_enum())
                {
                    auto enum_type = prop_type.get_enum_base_type();
                    auto enum_inspector = get_enum_property_inspector(enum_type.get_name());
                    if(enum_inspector)
                    {
                        result |= enum_inspector(ctx, data, obj_proxy, prop, info);
                    }
                }
                else if(prop_type.is_array() || prop_type.is_list())
                {
                    // Handle arrays and List<T> with add/remove support
                    result |= mono_inspector_collection::inspect_property(ctx, data, obj_proxy, prop, info);
                }
                else if(prop_type.is_serializable())
                {
                    // Recursively inspect serializable nested objects
                    auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                    auto nested_obj = invoker.get_value(data);
                    
                    if(nested_obj.valid())
                    {
                        auto nested_proxy = make_nested_property_proxy(obj_proxy, invoker);
                        result |= inspect_serializable_object(ctx, nested_obj, nested_proxy, prop.get_name(), info);
                    }
                    else
                    {
                        // Object is null, show as read-only field
                        var_info field_info;
                        field_info.is_property = true;
                        field_info.read_only = true;

                        std::string null_text = "null (" + prop_type.get_name() + ")";
                        entt::meta_any null_var = entt::forward_as_meta(null_text);
                        auto null_var_proxy = make_proxy(null_var);
                        
                        {
                            property_layout layout(prop.get_name());
                            result |= inspect_var(ctx, null_var, null_var_proxy, field_info);
                        }
                    }
                }
                // else
                // {
                //     // Fallback to unknown type display
                //     var_info field_info;
                //     field_info.is_property = true;
                //     field_info.read_only = true;

                //     std::string unknown_text;

                //     try
                //     {
                //         auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                //         auto nested_obj = invoker.get_value(data);
                //         if(nested_obj.valid())
                //         {
                //             unknown_text = fmt::format("Unknown ({})", nested_obj.get_type().get_name());
                //         }
                //         else
                //         {
                //             unknown_text = "null (" + prop.get_type().get_name() + ")";
                //         }
                //     }
                //     catch(const std::exception& e)
                //     {
                //         unknown_text = fmt::format("Unknown ({})", e.what());
                //     }

                    
                //     entt::meta_any unknown_var = entt::forward_as_meta(unknown_text);
                //     auto unknown_var_proxy = make_proxy(unknown_var);
                    
                //     {
                //         property_layout layout(prop.get_name());
                //         result |= inspect_var(ctx, unknown_var, unknown_var_proxy, field_info);
                //     }
                // }

                override_ctx.pop_segment();
            }
            ImGui::PopReadonly();
        }
    }
    else
    {
        // // Object is null, show as read-only field
        var_info field_info;
        field_info.is_property = true;
        field_info.read_only = true;

        std::string null_text = "Unknown (" + type.get_name() + ")";
        entt::meta_any null_var = entt::forward_as_meta(null_text);
        auto null_var_proxy = make_proxy(null_var);
        
        {
            result |= inspect_var(ctx, null_var, null_var_proxy, field_info);
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
    obj_proxy.impl->type_name = entt::get_pretty_name(var.type());
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
