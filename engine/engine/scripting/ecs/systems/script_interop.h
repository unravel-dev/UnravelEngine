#pragma once
#include <math/math.h>
#include <hpp/small_vector.hpp>
#include <engine/ecs/ecs.h>
#include <engine/input/action_map/key.hpp>
#include <engine/engine.h>
#include <dotnetpp/dotnetpp.h>
#include <dotnetpp/dotnet_managed.h>

namespace dotnetpp_backend
{
namespace managed_interface
{

struct vector2
{
	float x;
	float y;
};

struct vector3
{
	float x;
	float y;
	float z;
};

struct vector4
{
	float x;
	float y;
	float z;
	float w;
};

struct quaternion
{
	float x;
	float y;
	float z;
	float w;
};

struct color
{
	float r;
	float g;
	float b;
	float a;
};

struct bbox
{
	vector3 min{};
	vector3 max{};
};

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
};

struct ui_pointer_event : ui_event_base
{
	float x{};
	float y{};
	int button{-1};
	bool ctrl_key{};
	bool shift_key{};
	bool alt_key{};
	bool meta_key{};
	float delta_x{};
	float delta_y{};
};

struct ui_key_event : ui_event_base
{
	input::key_code key_code{};
	bool ctrl_key{};
	bool shift_key{};
	bool alt_key{};
	bool meta_key{};
};

struct ui_textinput_event : ui_event_base
{
	std::string text;
	bool ctrl_key{};
	bool shift_key{};
	bool alt_key{};
	bool meta_key{};
};

struct ui_slider_event : ui_event_base
{
	float value{};
	float min_value{};
	float max_value{};
	float step{};
};

struct ui_change_event : ui_event_base
{
	std::string value;
};

struct manifold_point
{
	vector3 point{};
	vector3 normal{};
	float distance{};
	float impulse{};
};

} // namespace managed_interface
} // namespace dotnetpp_backend

// Converter specializations are implemented in script_interop.cpp inside
// dotnetpp_backend::managed_interface (alias of dotnetpp_backend:: or clr::).
namespace dotnetpp_backend::managed_interface
{
template<>
auto converter::convert(const math::vec2& v) -> dotnetpp_backend::managed_interface::vector2;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector2& v) -> math::vec2;
template<>
auto converter::convert(const math::vec3& v) -> dotnetpp_backend::managed_interface::vector3;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector3& v) -> math::vec3;
template<>
auto converter::convert(const math::vec4& v) -> dotnetpp_backend::managed_interface::vector4;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector4& v) -> math::vec4;
template<>
auto converter::convert(const math::quat& q) -> dotnetpp_backend::managed_interface::quaternion;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::quaternion& q) -> math::quat;
template<>
auto converter::convert(const math::color& v) -> dotnetpp_backend::managed_interface::color;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::color& v) -> math::color;
template<>
auto converter::convert(const math::bbox& v) -> dotnetpp_backend::managed_interface::bbox;
template<>
auto converter::convert(const dotnetpp_backend::managed_interface::bbox& v) -> math::bbox;
}

template <typename T, size_t StaticCapacity>
struct dotnet_converter<hpp::small_vector<T, StaticCapacity>>
{
	using native_type = hpp::small_vector<T, StaticCapacity>;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		std::vector<T> vec(obj.begin(), obj.end());
		return dotnet_converter<std::vector<T>>::to_managed(vec);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		if(!obj)
		{
			return {};
		}
		auto vec = dotnet_converter<std::vector<T>>::from_managed(obj);
		return native_type(vec.begin(), vec.end());
	}
};

dotnet_register_converter_for_pod(math::vec2, dotnetpp_backend::managed_interface::vector2);
dotnet_register_converter_for_pod(math::vec3, dotnetpp_backend::managed_interface::vector3);
dotnet_register_converter_for_pod(math::vec4, dotnetpp_backend::managed_interface::vector4);
dotnet_register_converter_for_pod(math::quat, dotnetpp_backend::managed_interface::quaternion);
dotnet_register_converter_for_pod(math::color, dotnetpp_backend::managed_interface::color);

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_event_base;
	using managed_type = dotnet::managed_ptr;

	static auto create_instance(const std::string& namespace_name, const std::string& type_name) -> dotnet::object;

	static void to_managed_base(const native_type& obj, dotnet::object& instance)
	{
		dotnet::set_field_value(instance, "nativePtr", obj.native_ptr);
		dotnet::set_field_value(instance, "targetElementId", obj.target_element_id);
		dotnet::set_field_value(instance, "targetElementPtr", obj.target_element_ptr);
		dotnet::set_field_value(instance, "currentElementId", obj.current_element_id);
		dotnet::set_field_value(instance, "currentElementPtr", obj.current_element_ptr);
		dotnet::set_field_value(instance, "phase", obj.phase);
		dotnet::set_field_value(instance, "eventType", obj.event_type);
	}

	static void from_managed_base(const dotnet::object& object, native_type& data)
	{
		dotnet::get_field_value(object, "nativePtr", data.native_ptr);
		dotnet::get_field_value(object, "targetElementId", data.target_element_id);
		dotnet::get_field_value(object, "targetElementPtr", data.target_element_ptr);
		dotnet::get_field_value(object, "currentElementId", data.current_element_id);
		dotnet::get_field_value(object, "currentElementPtr", data.current_element_ptr);
		dotnet::get_field_value(object, "phase", data.phase);
		dotnet::get_field_value(object, "eventType", data.event_type);
	}

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UIEventBase");
		to_managed_base(obj, instance);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		return data;
	}
};

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_pointer_event>
	: dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_pointer_event;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UIPointerEvent");
		to_managed_base(obj, instance);
		dotnet::set_field_value(instance, "x", obj.x);
		dotnet::set_field_value(instance, "y", obj.y);
		dotnet::set_field_value(instance, "button", obj.button);
		dotnet::set_field_value(instance, "ctrlKey", obj.ctrl_key);
		dotnet::set_field_value(instance, "shiftKey", obj.shift_key);
		dotnet::set_field_value(instance, "altKey", obj.alt_key);
		dotnet::set_field_value(instance, "metaKey", obj.meta_key);
		dotnet::set_field_value(instance, "deltaX", obj.delta_x);
		dotnet::set_field_value(instance, "deltaY", obj.delta_y);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		dotnet::get_field_value(object, "x", data.x);
		dotnet::get_field_value(object, "y", data.y);
		dotnet::get_field_value(object, "button", data.button);
		dotnet::get_field_value(object, "ctrlKey", data.ctrl_key);
		dotnet::get_field_value(object, "shiftKey", data.shift_key);
		dotnet::get_field_value(object, "altKey", data.alt_key);
		dotnet::get_field_value(object, "metaKey", data.meta_key);
		dotnet::get_field_value(object, "deltaX", data.delta_x);
		dotnet::get_field_value(object, "deltaY", data.delta_y);
		return data;
	}
};

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_key_event>
	: dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_key_event;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UIKeyEvent");
		to_managed_base(obj, instance);
		dotnet::set_field_value(instance, "keyCode", obj.key_code);
		dotnet::set_field_value(instance, "ctrlKey", obj.ctrl_key);
		dotnet::set_field_value(instance, "shiftKey", obj.shift_key);
		dotnet::set_field_value(instance, "altKey", obj.alt_key);
		dotnet::set_field_value(instance, "metaKey", obj.meta_key);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		dotnet::get_field_value(object, "keyCode", data.key_code);
		dotnet::get_field_value(object, "ctrlKey", data.ctrl_key);
		dotnet::get_field_value(object, "shiftKey", data.shift_key);
		dotnet::get_field_value(object, "altKey", data.alt_key);
		dotnet::get_field_value(object, "metaKey", data.meta_key);
		return data;
	}
};

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_textinput_event>
	: dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_textinput_event;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UITextInputEvent");
		to_managed_base(obj, instance);
		dotnet::set_field_value(instance, "text", obj.text);
		dotnet::set_field_value(instance, "ctrlKey", obj.ctrl_key);
		dotnet::set_field_value(instance, "shiftKey", obj.shift_key);
		dotnet::set_field_value(instance, "altKey", obj.alt_key);
		dotnet::set_field_value(instance, "metaKey", obj.meta_key);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		dotnet::get_field_value(object, "text", data.text);
		dotnet::get_field_value(object, "ctrlKey", data.ctrl_key);
		dotnet::get_field_value(object, "shiftKey", data.shift_key);
		dotnet::get_field_value(object, "altKey", data.alt_key);
		dotnet::get_field_value(object, "metaKey", data.meta_key);
		return data;
	}
};

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_slider_event>
	: dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_slider_event;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UISliderEvent");
		to_managed_base(obj, instance);
		dotnet::set_field_value(instance, "value", obj.value);
		dotnet::set_field_value(instance, "minValue", obj.min_value);
		dotnet::set_field_value(instance, "maxValue", obj.max_value);
		dotnet::set_field_value(instance, "step", obj.step);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		dotnet::get_field_value(object, "value", data.value);
		dotnet::get_field_value(object, "minValue", data.min_value);
		dotnet::get_field_value(object, "maxValue", data.max_value);
		dotnet::get_field_value(object, "step", data.step);
		return data;
	}
};

template <>
struct dotnet_converter<dotnetpp_backend::managed_interface::ui_change_event>
	: dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>
{
	using native_type = dotnetpp_backend::managed_interface::ui_change_event;
	using managed_type = dotnet::managed_ptr;

	static auto to_managed(const native_type& obj) -> managed_type
	{
		auto instance = create_instance("Unravel.Core", "UIChangeEvent");
		to_managed_base(obj, instance);
		dotnet::set_field_value(instance, "value", obj.value);
		return dotnet::get_managed_ptr(instance);
	}

	static auto from_managed(const managed_type& obj) -> native_type
	{
		dotnet::object object(obj);
		native_type data;
		from_managed_base(object, data);
		dotnet::get_field_value(object, "value", data.value);
		return data;
	}
};
