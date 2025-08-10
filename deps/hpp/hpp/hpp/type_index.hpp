#ifndef HPP_TYPE_INDEX
#define HPP_TYPE_INDEX

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "type_name.hpp"
#include "crc.hpp"

namespace hpp
{

class type_index
{
public:
	constexpr auto operator==(const type_index& o) const noexcept -> bool;
	constexpr auto operator!=(const type_index& o) const noexcept -> bool;
	constexpr auto operator<(const type_index& o) const noexcept -> bool;
	constexpr auto operator>(const type_index& o) const noexcept -> bool;

	constexpr auto hash_code() const noexcept -> uint64_t;
	constexpr auto name() const noexcept -> hpp::string_view;

private:
	struct construct_t
	{
		uint64_t hash_code{};
		hpp::string_view name{};
		constexpr construct_t(uint64_t h, hpp::string_view n) noexcept : hash_code(h), name(n) {}
	};

	construct_t info_ = {0, ""};
	explicit constexpr type_index(construct_t info) noexcept;

	template<typename T>
	static constexpr auto get_constexpr() -> type_index;

	template<typename T>
	static auto get() -> const type_index&;

	template<typename T>
	friend auto type_id() -> const type_index&;
	template<typename T>
	friend constexpr auto type_id_constexpr() -> type_index;

	template<typename T>
	static constexpr auto construct() -> construct_t;
};

template<typename T>
constexpr auto type_index::construct() -> construct_t
{
	hpp::string_view name = hpp::type_name<T>();
	uint64_t hash_code = crc64(name.data(), name.size());
	return construct_t{hash_code, name};
}

template<typename T>
constexpr auto type_index::get_constexpr() -> type_index
{
	return type_index{construct<T>()};
}

template<typename T>
auto type_index::get() -> const type_index&
{
	static const type_index id = get_constexpr<T>();
	return id;
}

template<typename T>
auto type_id() -> const type_index&
{
	using type_t = typename std::remove_cv<T>::type;
	return type_index::get<type_t>();
}

template<typename T>
constexpr auto type_id_constexpr() -> type_index
{
	using type_t = typename std::remove_cv<T>::type;
	return type_index::get_constexpr<type_t>();
}

constexpr type_index::type_index(type_index::construct_t info) noexcept : info_{info}
{
}

constexpr auto type_index::operator==(const type_index& o) const noexcept -> bool
{
	return hash_code() == o.hash_code();
}

constexpr auto type_index::operator!=(const type_index& o) const noexcept -> bool
{
	return hash_code() != o.hash_code();
}

constexpr auto type_index::operator<(const type_index& o) const noexcept -> bool
{
	return hash_code() < o.hash_code();
}

constexpr auto type_index::operator>(const type_index& o) const noexcept -> bool
{
	return hash_code() > o.hash_code();
}

constexpr auto type_index::hash_code() const noexcept -> uint64_t
{
	return info_.hash_code;
}

constexpr auto type_index::name() const noexcept -> hpp::string_view
{
	return info_.name;
}

} // namespace hpp

#endif
