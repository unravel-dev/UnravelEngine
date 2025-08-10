#pragma once

#include "string_view.hpp"

namespace hpp
{

template <typename T>
constexpr auto type_name() -> hpp::string_view;

template <>
constexpr auto type_name<void>() -> hpp::string_view
{
	return "void";
}

namespace detail
{

using type_name_prober = void;

template <typename T>
constexpr auto function_name() -> hpp::string_view
{
#if defined(__clang__)
	return __PRETTY_FUNCTION__;
#elif defined(__GNUC__) && !defined(__clang__)
#define __HPP_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if(__HPP_GCC_VERSION <= 90100)
	return __builtin_FUNCTION();
#else
	return __PRETTY_FUNCTION__;
#endif
#elif defined(_MSC_VER)
	return __FUNCSIG__;
#else
#error "No support for this compiler."
#endif
}

constexpr auto function_name_prefix_length() -> std::size_t
{
	return function_name<type_name_prober>().find(type_name<type_name_prober>());
}

constexpr auto function_name_suffix_length() -> std::size_t
{
	return function_name<type_name_prober>().length() - function_name_prefix_length() -
		   type_name<type_name_prober>().length();
}

} // namespace detail

template <typename T>
constexpr auto type_name() -> hpp::string_view
{
	constexpr auto wrapped_name = detail::function_name<T>();
	constexpr auto prefix_length = detail::function_name_prefix_length();
	constexpr auto suffix_length = detail::function_name_suffix_length();
	constexpr auto type_name_length = wrapped_name.length() - prefix_length - suffix_length;
	constexpr auto result = wrapped_name.substr(prefix_length, type_name_length);

	// remove struct/class declarations
	constexpr auto declaration_prefix_result = result.find(' ');
	constexpr auto declaration_prefix_length =
		declaration_prefix_result == hpp::string_view::npos ? 0 : declaration_prefix_result + 1;
	return result.substr(declaration_prefix_length);
}

template <typename T>
constexpr auto type_name_unqualified() -> hpp::string_view
{
	constexpr auto qualified_type_name = type_name<T>();

	constexpr auto first_open_tag = qualified_type_name.find_first_of("<", 1);
	constexpr auto tmp_type_name = qualified_type_name.substr(0, first_open_tag);
	constexpr auto last_double_colon = tmp_type_name.find_last_of("::");
	constexpr auto result = tmp_type_name.substr(last_double_colon + 1);

	return result;
}

template <typename T>
auto type_name_str() -> std::string
{
	return std::string(type_name<T>());
}

template <typename T>
auto type_name_unqualified_str() -> std::string
{
	return std::string(type_name_unqualified<T>());
}

template <typename T>
auto type_name_str(const T&) -> std::string
{
	return type_name_str<T>();
}

template <typename T>
auto type_name_unqualified_str(const T&) -> std::string
{
	return type_name_unqualified_str<T>();
}

} // namespace hpp
