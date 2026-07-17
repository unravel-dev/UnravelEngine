#include "script_interop.h"
#include <engine/scripting/ecs/systems/script_system.h>

namespace dotnetpp_backend::managed_interface
{

template<>
auto converter::convert(const math::vec2& v) -> dotnetpp_backend::managed_interface::vector2
{
	return {v.x, v.y};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector2& v) -> math::vec2
{
	return {v.x, v.y};
}

template<>
auto converter::convert(const math::vec3& v) -> dotnetpp_backend::managed_interface::vector3
{
	return {v.x, v.y, v.z};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector3& v) -> math::vec3
{
	return {v.x, v.y, v.z};
}

template<>
auto converter::convert(const math::vec4& v) -> dotnetpp_backend::managed_interface::vector4
{
	return {v.x, v.y, v.z, v.w};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::vector4& v) -> math::vec4
{
	return {v.x, v.y, v.z, v.w};
}

template<>
auto converter::convert(const math::quat& q) -> dotnetpp_backend::managed_interface::quaternion
{
	return {q.x, q.y, q.z, q.w};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::quaternion& q) -> math::quat
{
	return math::quat::wxyz(q.w, q.x, q.y, q.z);
}

template<>
auto converter::convert(const math::color& v) -> dotnetpp_backend::managed_interface::color
{
	return {v.value.r, v.value.g, v.value.b, v.value.a};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::color& v) -> math::color
{
	return {v.r, v.g, v.b, v.a};
}

template<>
auto converter::convert(const math::bbox& v) -> dotnetpp_backend::managed_interface::bbox
{
	return {{v.min.x, v.min.y, v.min.z}, {v.max.x, v.max.y, v.max.z}};
}

template<>
auto converter::convert(const dotnetpp_backend::managed_interface::bbox& v) -> math::bbox
{
	return {{v.min.x, v.min.y, v.min.z}, {v.max.x, v.max.y, v.max.z}};
}

} // namespace managed_interface

auto dotnet_converter<dotnetpp_backend::managed_interface::ui_event_base>::create_instance(const std::string& namespace_name,
																				  const std::string& type_name)
	-> dotnet::object
{
	auto& ctx = unravel::engine::context();
	auto app_assembly = ctx.get_cached<unravel::script_system>().get_engine_assembly();
	auto type = app_assembly.get_type(namespace_name, type_name);
	return type.new_instance();
}
