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

/// Managed Range<float> and Range<uint>. The engine's range<T> has constructors, which make the
/// Windows x64 ABI return it through memory, while managed code expects an 8-byte struct in a
/// register: internal calls take and return these plain structs instead.
struct float_range
{
	float min{};
	float max{};
};

struct uint_range
{
	uint32_t min{};
	uint32_t max{};
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
dotnet_register_converter_for_pod(math::bbox, dotnetpp_backend::managed_interface::bbox);

