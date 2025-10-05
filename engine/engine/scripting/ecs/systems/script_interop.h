#pragma once
#include <math/math.h>
#include <monort/mono_pod_wrapper.h>
#include <hpp/small_vector.hpp>
#include <engine/ecs/ecs.h>
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_domain.h>
#include <monopp/mono_assembly.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/input/action_map/key.hpp>
#include <engine/engine.h>

namespace mono
{

template <typename T, size_t StaticCapacity>
struct mono_converter<hpp::small_vector<T, StaticCapacity>>
{
    using native_type = hpp::small_vector<T, StaticCapacity>;
    using managed_type = MonoObject*;

    static auto to_mono(const native_type& obj) -> managed_type
    {
        const auto& domain = mono_domain::get_current_domain();
        return mono_array<T>(domain, obj).get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        if(!obj)
        {
            return {};
        }
        return mono_array<T>(mono_object(obj)).template to_vector<native_type>();
    }
};


namespace managed_interface
{

struct vector2
{
    float x;
    float y;
};
template<>
auto converter::convert(const math::vec2& v) -> vector2;

template<>
auto converter::convert(const vector2& v) -> math::vec2;

struct vector3
{
    float x;
    float y;
    float z;
};
template<>
auto converter::convert(const math::vec3& v) -> vector3;

template<>
auto converter::convert(const vector3& v) -> math::vec3;

struct vector4
{
    float x;
    float y;
    float z;
    float w;
};
template<>
auto converter::convert(const math::vec4& v) -> vector4;
template<>
auto converter::convert(const vector4& v) -> math::vec4;

struct quaternion
{
    float x;
    float y;
    float z;
    float w;
};
template<>
auto converter::convert(const math::quat& q) -> quaternion;
template<>
auto converter::convert(const quaternion& q) -> math::quat;


struct color
{
    float r;
    float g;
    float b;
    float a;
};
template<>
auto converter::convert(const math::color& v) -> color;
template<>
auto converter::convert(const color& v) -> math::color;

struct bbox
{
    vector3 min{};
    vector3 max{};
};
template<>
auto converter::convert(const math::bbox& v) -> bbox;
template<>
auto converter::convert(const bbox& v) -> math::bbox;

struct raycast_hit
{
    entt::entity entity{};
    vector3 point{};
    vector3 normal{};
    float distance{};
};

struct ray
{
    vector3 origin{};
    vector3 direction{};
};

struct material_properties
{
    bool valid{};

    color base_color{1.0f, 1.0f, 1.0f, 1.0f};
    color emissive_color{};
    vector2 tiling{1.0f, 1.0f};
    float roughness{0.5};
    float metalness{};
    float bumpiness{1.0f};
};

struct ui_event_base
{
    std::intptr_t native_ptr{};
    std::string target_element_id;
    std::intptr_t target_element_ptr{};
    std::string current_element_id;
    std::intptr_t current_element_ptr{};
    int phase{};
    std::string event_type;
    
    // Note: Mouse and keyboard specific properties moved to derived structs
    // for better type safety and cleaner architecture
};

struct ui_pointer_event : ui_event_base
{
    // Generic pointer position (from RmlUi mouse_x, mouse_y parameters)
    float x{};
    float y{};
    
    // Button index (from RmlUi button parameter, -1 if not applicable)
    int button{-1};
    
    // Modifier keys (from RmlUi key modifier parameters)
    bool ctrl_key{};
    bool shift_key{};
    bool alt_key{};
    bool meta_key{};
    
    // Scroll deltas (from RmlUi wheel_delta_x, wheel_delta_y parameters)
    float delta_x{};
    float delta_y{};
};

struct ui_key_event : ui_event_base
{
    // Converted key code
    input::key_code key_code{};
    
    // Modifier keys (from RmlUi key modifier parameters)
    bool ctrl_key{};
    bool shift_key{};
    bool alt_key{};
    bool meta_key{};
};

struct ui_textinput_event : ui_event_base
{
    // Text input (from RmlUi text parameter)
    std::string text;
    
    // Modifier keys (from RmlUi key modifier parameters)
    bool ctrl_key{};
    bool shift_key{};
    bool alt_key{};
    bool meta_key{};
};




} // namespace managed_interface

register_basic_mono_converter_for_pod(math::vec2, managed_interface::vector2);
register_basic_mono_converter_for_pod(math::vec3, managed_interface::vector3);
register_basic_mono_converter_for_pod(math::vec4, managed_interface::vector4);
register_basic_mono_converter_for_pod(math::quat, managed_interface::quaternion);
register_basic_mono_converter_for_pod(math::color, managed_interface::color);

// Custom converter for ui_event_base since it contains std::string fields
template<>
struct mono_converter<managed_interface::ui_event_base>
{
    using native_type = managed_interface::ui_event_base;
    using managed_type = MonoObject*;

    static auto to_mono(const native_type& obj) -> managed_type
    {
        auto& ctx = unravel::engine::context();
        auto app_assembly = ctx.get_cached<unravel::script_system>().get_engine_assembly();
        auto type = app_assembly.get_type("Unravel.Core", "UIEventBase");
        auto instance = type.new_instance();
        mono::set_field_value(instance, "nativePtr", obj.native_ptr);
        mono::set_field_value(instance, "targetElementId", obj.target_element_id);
        mono::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
        mono::set_field_value(instance, "currentElementId", obj.current_element_id);
        mono::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
        mono::set_field_value(instance, "phase", obj.phase);
        mono::set_field_value(instance, "eventType", obj.event_type);
        // Note: Mouse and keyboard properties removed from base event
        return instance.get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        mono::mono_object object(obj);
        native_type data;
        mono::get_field_value(object, "nativePtr", data.native_ptr);
        mono::get_field_value(object, "targetElementId", data.target_element_id);
        mono::get_field_value(object, "targetElementPtr", data.target_element_ptr);
        mono::get_field_value(object, "currentElementId", data.current_element_id);
        mono::get_field_value(object, "currentElementPtr", data.current_element_ptr);
        mono::get_field_value(object, "phase", data.phase);
        mono::get_field_value(object, "eventType", data.event_type);
        // Note: Mouse and keyboard properties removed from base event
        return data;
    }
};

// Custom converter for ui_pointer_event
template<>
struct mono_converter<managed_interface::ui_pointer_event>
{
    using native_type = managed_interface::ui_pointer_event;
    using managed_type = MonoObject*;

    static auto to_mono(const native_type& obj) -> managed_type
    {
        auto& ctx = unravel::engine::context();
        auto app_assembly = ctx.get_cached<unravel::script_system>().get_engine_assembly();
        auto type = app_assembly.get_type("Unravel.Core", "UIPointerEvent");
        auto instance = type.new_instance();
        
        // Set base class fields
        mono::set_field_value(instance, "nativePtr", obj.native_ptr);
        mono::set_field_value(instance, "targetElementId", obj.target_element_id);
        mono::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
        mono::set_field_value(instance, "currentElementId", obj.current_element_id);
        mono::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
        mono::set_field_value(instance, "phase", obj.phase);
        mono::set_field_value(instance, "eventType", obj.event_type);
        
        // Set pointer-specific fields
        mono::set_field_value(instance, "x", obj.x);
        mono::set_field_value(instance, "y", obj.y);
        mono::set_field_value(instance, "button", obj.button);
        mono::set_field_value(instance, "ctrlKey", obj.ctrl_key);
        mono::set_field_value(instance, "shiftKey", obj.shift_key);
        mono::set_field_value(instance, "altKey", obj.alt_key);
        mono::set_field_value(instance, "metaKey", obj.meta_key);
        mono::set_field_value(instance, "deltaX", obj.delta_x);
        mono::set_field_value(instance, "deltaY", obj.delta_y);
        
        return instance.get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        // Implementation for C# to C++ conversion if needed
        mono::mono_object object(obj);
        native_type data;
        // Set base fields
        mono::get_field_value(object, "nativePtr", data.native_ptr);
        mono::get_field_value(object, "targetElementId", data.target_element_id);
        mono::get_field_value(object, "targetElementPtr", data.target_element_ptr);
        mono::get_field_value(object, "currentElementId", data.current_element_id);
        mono::get_field_value(object, "currentElementPtr", data.current_element_ptr);
        mono::get_field_value(object, "phase", data.phase);
        mono::get_field_value(object, "eventType", data.event_type);
        // Set pointer-specific fields
        mono::get_field_value(object, "x", data.x);
        mono::get_field_value(object, "y", data.y);
        mono::get_field_value(object, "button", data.button);
        mono::get_field_value(object, "ctrlKey", data.ctrl_key);
        mono::get_field_value(object, "shiftKey", data.shift_key);
        mono::get_field_value(object, "altKey", data.alt_key);
        mono::get_field_value(object, "metaKey", data.meta_key);
        mono::get_field_value(object, "deltaX", data.delta_x);
        mono::get_field_value(object, "deltaY", data.delta_y);
        return data;
    }
};

// Custom converter for ui_key_event
template<>
struct mono_converter<managed_interface::ui_key_event>
{
    using native_type = managed_interface::ui_key_event;
    using managed_type = MonoObject*;

    static auto to_mono(const native_type& obj) -> managed_type
    {
        auto& ctx = unravel::engine::context();
        auto app_assembly = ctx.get_cached<unravel::script_system>().get_engine_assembly();
        auto type = app_assembly.get_type("Unravel.Core", "UIKeyEvent");
        auto instance = type.new_instance();
        
        // Set base class fields
        mono::set_field_value(instance, "nativePtr", obj.native_ptr);
        mono::set_field_value(instance, "targetElementId", obj.target_element_id);
        mono::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
        mono::set_field_value(instance, "currentElementId", obj.current_element_id);
        mono::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
        mono::set_field_value(instance, "phase", obj.phase);
        mono::set_field_value(instance, "eventType", obj.event_type);
        
        // Set key-specific fields
        mono::set_field_value(instance, "keyCode", obj.key_code);
        mono::set_field_value(instance, "ctrlKey", obj.ctrl_key);
        mono::set_field_value(instance, "shiftKey", obj.shift_key);
        mono::set_field_value(instance, "altKey", obj.alt_key);
        mono::set_field_value(instance, "metaKey", obj.meta_key);
        
        return instance.get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        // Implementation for C# to C++ conversion if needed
        mono::mono_object object(obj);
        native_type data;
        // Set base fields
        mono::get_field_value(object, "nativePtr", data.native_ptr);
        mono::get_field_value(object, "targetElementId", data.target_element_id);
        mono::get_field_value(object, "targetElementPtr", data.target_element_ptr);
        mono::get_field_value(object, "currentElementId", data.current_element_id);
        mono::get_field_value(object, "currentElementPtr", data.current_element_ptr);
        mono::get_field_value(object, "phase", data.phase);
        mono::get_field_value(object, "eventType", data.event_type);
        // Set key-specific fields
        mono::get_field_value(object, "keyCode", data.key_code);
        mono::get_field_value(object, "ctrlKey", data.ctrl_key);
        mono::get_field_value(object, "shiftKey", data.shift_key);
        mono::get_field_value(object, "altKey", data.alt_key);
        mono::get_field_value(object, "metaKey", data.meta_key);
        return data;
    }
};

// Custom converter for ui_textinput_event
template<>
struct mono_converter<managed_interface::ui_textinput_event>
{
    using native_type = managed_interface::ui_textinput_event;
    using managed_type = MonoObject*;

    static auto to_mono(const native_type& obj) -> managed_type
    {
        auto& ctx = unravel::engine::context();
        auto app_assembly = ctx.get_cached<unravel::script_system>().get_engine_assembly();
        auto type = app_assembly.get_type("Unravel.Core", "UITextInputEvent");
        auto instance = type.new_instance();
        
        // Set base class fields
        mono::set_field_value(instance, "nativePtr", obj.native_ptr);
        mono::set_field_value(instance, "targetElementId", obj.target_element_id);
        mono::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
        mono::set_field_value(instance, "currentElementId", obj.current_element_id);
        mono::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
        mono::set_field_value(instance, "phase", obj.phase);
        mono::set_field_value(instance, "eventType", obj.event_type);
        
        // Set text input-specific fields
        mono::set_field_value(instance, "text", obj.text);
        mono::set_field_value(instance, "ctrlKey", obj.ctrl_key);
        mono::set_field_value(instance, "shiftKey", obj.shift_key);
        mono::set_field_value(instance, "altKey", obj.alt_key);
        mono::set_field_value(instance, "metaKey", obj.meta_key);
        
        return instance.get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        // Implementation for C# to C++ conversion if needed
        mono::mono_object object(obj);
        native_type data;
        // Set base fields
        mono::get_field_value(object, "nativePtr", data.native_ptr);
        mono::get_field_value(object, "targetElementId", data.target_element_id);
        mono::get_field_value(object, "targetElementPtr", data.target_element_ptr);
        mono::get_field_value(object, "currentElementId", data.current_element_id);
        mono::get_field_value(object, "currentElementPtr", data.current_element_ptr);
        mono::get_field_value(object, "phase", data.phase);
        mono::get_field_value(object, "eventType", data.event_type);
        // Set text input-specific fields
        mono::get_field_value(object, "text", data.text);
        mono::get_field_value(object, "ctrlKey", data.ctrl_key);
        mono::get_field_value(object, "shiftKey", data.shift_key);
        mono::get_field_value(object, "altKey", data.alt_key);
        mono::get_field_value(object, "metaKey", data.meta_key);
        return data;
    }
};


} // namespace mono

