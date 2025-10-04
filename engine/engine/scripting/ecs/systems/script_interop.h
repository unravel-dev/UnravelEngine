#pragma once
#include <math/math.h>
#include <monort/mono_pod_wrapper.h>
#include <hpp/small_vector.hpp>
#include <engine/ecs/ecs.h>
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_domain.h>
#include <monopp/mono_assembly.h>
#include <engine/scripting/ecs/systems/script_system.h>
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
    std::string target_element_id{};
    std::intptr_t target_element_ptr{};
    std::string current_element_id{};
    std::intptr_t current_element_ptr{};
    int phase{};
    std::string event_type{};
    
    // Mouse event data
    float mouse_x{};
    float mouse_y{};
    
    // Keyboard event data
    int key_code{};
};

struct pointer_event : ui_event_base
{
    float x{};
    float y{};
};

struct key_event : ui_event_base
{
    int key_code{};
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
        mono::set_field_value(instance, "targetElementId", obj.target_element_id);
        mono::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
        mono::set_field_value(instance, "currentElementId", obj.current_element_id);
        mono::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
        mono::set_field_value(instance, "phase", obj.phase);
        mono::set_field_value(instance, "eventType", obj.event_type);
        mono::set_field_value(instance, "mouseX", obj.mouse_x);
        mono::set_field_value(instance, "mouseY", obj.mouse_y);
        mono::set_field_value(instance, "keyCode", obj.key_code);
        return instance.get_internal_ptr();
    }

    static auto from_mono(const managed_type& obj) -> native_type
    {
        mono::mono_object object(obj);
        native_type data;
        mono::get_field_value(object, "targetElementId", data.target_element_id);
        mono::get_field_value(object, "targetElementPtr", data.target_element_ptr);
        mono::get_field_value(object, "currentElementId", data.current_element_id);
        mono::get_field_value(object, "currentElementPtr", data.current_element_ptr);
        mono::get_field_value(object, "phase", data.phase);
        mono::get_field_value(object, "eventType", data.event_type);
        mono::get_field_value(object, "mouseX", data.mouse_x);
        mono::get_field_value(object, "mouseY", data.mouse_y);
        mono::get_field_value(object, "keyCode", data.key_code);
        return data;
    }
};


} // namespace mono

