#pragma once

#include "string_view.hpp"

#include <cstddef>
#include <string>

/// @file
/// Compile time type names.
///
/// The compiler is asked for its own spelling of a type through __PRETTY_FUNCTION__ /
/// __FUNCSIG__, and that spelling is then rewritten into one canonical form so that the
/// same type yields the same string on clang, gcc and msvc. The canonical form:
///
///  - carries no elaborated type specifier: "struct foo" and "class foo" are both "foo",
///    at every nesting level, including inside template argument lists;
///  - carries no msvc calling convention or pointer decoration: "void(__cdecl *)(int)"
///    is "void(*)(int)";
///  - spells the builtin integers the way C++ does: "__int64" and "long long int" are
///    both "long long", "long unsigned int" is "unsigned long";
///  - keeps a space only where it separates two identifiers ("unsigned int", "const
///    int"), and drops every space that merely decorates punctuation, so "int *",
///    "vector<a, b>" and "> >" become "int*", "vector<a,b>" and ">>";
///  - spells every anonymous namespace "(anonymous)".
///
/// Three things cannot be repaired here, because the information is already gone by the
/// time the signature reaches this header. All three are stable within one toolchain, so
/// names stay usable as identities; they just are not byte identical across toolchains.
///
///  - msvc prints defaulted template arguments and the others do not, so std::vector<int>
///    is "std::vector<int,std::allocator<int>>" on msvc and "std::vector<int>" elsewhere.
///    Undoing that would require knowing each template's defaults.
///  - clang omits the enclosing namespace of the class in a pointer to member, printing
///    "void(my_struct::*)(int)" where gcc and msvc print "test::my_struct".
///  - msvc omits the space in a pointer to member data altogether, printing
///    "inttest::my_struct::*". Putting it back would mean guessing where the type ends.
///
/// The result has static storage duration and is null terminated, so both the view and
/// its data() outlive any caller.

namespace hpp
{

namespace detail
{

/// The type used to discover the compiler specific prefix and suffix wrapped around the
/// type inside the function signature. Its printed form must be known up front, which is
/// why it is void: every supported compiler spells it exactly "void".
using type_name_prober = void;

constexpr auto type_name_prober_name() -> hpp::string_view
{
	return "void";
}

template <typename T>
constexpr auto function_name() -> hpp::string_view
{
#if defined(__clang__) || defined(__GNUC__)
	return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
	return __FUNCSIG__;
#else
#error "hpp::type_name: no support for this compiler."
#endif
}

constexpr auto function_name_prefix_length() -> std::size_t
{
	return function_name<type_name_prober>().find(type_name_prober_name());
}

constexpr auto function_name_suffix_length() -> std::size_t
{
	return function_name<type_name_prober>().length() - function_name_prefix_length() -
		   type_name_prober_name().length();
}

/// The type exactly as the compiler spells it, before normalisation.
template <typename T>
constexpr auto raw_type_name() -> hpp::string_view
{
	static_assert(function_name_prefix_length() != hpp::string_view::npos,
				  "hpp::type_name: the probe type is missing from the compiler signature, so the "
				  "signature layout is not the one this header expects.");
	// Each of these costs a constexpr walk over a string literal, so none is called twice.
	const hpp::string_view signature = function_name<T>();
	const std::size_t prefix = function_name_prefix_length();
	const std::size_t suffix = function_name_suffix_length();
	return signature.substr(prefix, signature.length() - prefix - suffix);
}

constexpr auto is_identifier_char(char c) -> bool
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

constexpr auto is_space_char(char c) -> bool
{
	return c == ' ' || c == '\t';
}

/// A source phrase and what it becomes. An empty replacement erases the phrase.
struct name_substitution
{
	hpp::string_view pattern;
	hpp::string_view replacement;
};

/// Whether @p pattern occurs at @p pos as a whole token, so that "structure" is not read
/// as "struct" and "test::classic" is not read as "test::class".
constexpr auto matches_at(hpp::string_view src, std::size_t pos, hpp::string_view pattern) -> bool
{
	if(pattern.size() == 0 || pos + pattern.size() > src.size())
	{
		return false;
	}
	for(std::size_t i = 0; i < pattern.size(); ++i)
	{
		if(src[pos + i] != pattern[i])
		{
			return false;
		}
	}
	if(is_identifier_char(pattern[0]) && pos > 0 && is_identifier_char(src[pos - 1]))
	{
		return false;
	}
	const std::size_t end = pos + pattern.size();
	if(is_identifier_char(pattern[pattern.size() - 1]) && end < src.size() && is_identifier_char(src[end]))
	{
		return false;
	}
	return true;
}

/// Cheap rejection so the substitution table is only scanned where a phrase can begin.
/// Must stay in sync with the first character of every pattern below.
constexpr auto can_begin_substitution(char c) -> bool
{
	return c == 'l' || c == 's' || c == 'c' || c == 'e' || c == 'u' || c == '_' || c == '(' || c == '{' || c == '`';
}

/// Number of phrases in the table below. The table is held in a struct, rather than built
/// inside the lookup, so that it is constructed once per name instead of once per
/// character: every entry costs a constexpr strlen to build, and paying that per character
/// dominated the compile time of this header.
enum : std::size_t
{
	substitution_count = 22
};

struct substitution_table
{
	name_substitution entries[substitution_count];
};

/// The first match wins, so entries are kept longest first: "long long int" has to be
/// tried before "long int", or gcc's spelling of long long would come out as "long long".
constexpr auto make_substitution_table() -> substitution_table
{
	return substitution_table{{
		{"long long unsigned int", "unsigned long long"},
		{"(anonymous namespace)", "(anonymous)"},
		{"`anonymous-namespace'", "(anonymous)"},
		{"short unsigned int", "unsigned short"},
		{"long unsigned int", "unsigned long"},
		{"std::__cxx11::", "std::"},
		{"long long int", "long long"},
		{"__vectorcall", ""},
		{"{anonymous}", "(anonymous)"},
		{"__thiscall", ""},
		{"__restrict", ""},
		{"__fastcall", ""},
		{"short int", "short"},
		{"__stdcall", ""},
		{"long int", "long"},
		{"__int64", "long long"},
		{"__cdecl", ""},
		{"__ptr64", ""},
		{"struct", ""},
		{"class", ""},
		{"union", ""},
		{"enum", ""},
	}};
}

// A phrase added above without growing substitution_count would be silently dropped,
// because the array would simply be zero filled past the entries that fit.
static_assert(make_substitution_table().entries[substitution_count - 1].pattern.size() != 0,
			  "substitution_count does not match the number of phrases in make_substitution_table");

/// The phrase starting at @p pos, or an empty substitution when there is none.
constexpr auto find_substitution(const substitution_table& table, hpp::string_view src, std::size_t pos)
	-> name_substitution
{
	for(std::size_t i = 0; i < substitution_count; ++i)
	{
		// The first character is compared here rather than inside matches_at: it rejects
		// almost every entry, and during constant evaluation a call costs far more than a
		// comparison.
		if(table.entries[i].pattern[0] == src[pos] && matches_at(src, pos, table.entries[i].pattern))
		{
			return table.entries[i];
		}
	}
	return name_substitution{hpp::string_view{}, hpp::string_view{}};
}

/// Appends to a buffer and remembers how much was written and what came last, which is
/// what the whitespace rule below needs in order to look backwards.
class name_writer
{
public:
	constexpr explicit name_writer(char* out) : out_(out)
	{
	}

	constexpr void put(char c)
	{
		out_[size_] = c;
		++size_;
		last_ = c;
	}

	constexpr void put(hpp::string_view text)
	{
		for(std::size_t i = 0; i < text.size(); ++i)
		{
			put(text[i]);
		}
	}

	constexpr auto size() const -> std::size_t
	{
		return size_;
	}

	/// The last character written, or '\0' when nothing has been written yet.
	constexpr auto last() const -> char
	{
		return last_;
	}

private:
	char* out_{nullptr};
	std::size_t size_{0};
	char last_{'\0'};
};

/// Rewrites @p raw into its canonical spelling at @p out, which must have room for
/// type_name_capacity chars, and returns the length written.
constexpr auto normalize_type_name(hpp::string_view raw, char* out) -> std::size_t
{
	const substitution_table table = make_substitution_table();
	name_writer writer(out);
	std::size_t i = 0;
	while(i < raw.size())
	{
		const name_substitution sub =
			can_begin_substitution(raw[i]) ? find_substitution(table, raw, i) : name_substitution{{}, {}};
		if(sub.pattern.size() != 0)
		{
			writer.put(sub.replacement);
			i += sub.pattern.size();
		}
		else if(is_space_char(raw[i]))
		{
			std::size_t next = i;
			while(next < raw.size() && is_space_char(raw[next]))
			{
				++next;
			}
			// A space survives only between two identifiers ("unsigned int"). Every space
			// that just decorates punctuation ("int *", "> >", ", ") is noise the compilers
			// disagree about, so it is dropped.
			if(next < raw.size() && is_identifier_char(writer.last()) && is_identifier_char(raw[next]))
			{
				writer.put(' ');
			}
			i = next;
		}
		else
		{
			writer.put(raw[i]);
			++i;
		}
	}
	return writer.size();
}

template <std::size_t N>
struct name_buffer
{
	char data[N];
	std::size_t size;
};

/// Upper bound on the normalised length, so that the name can be produced in a single
/// pass. Every substitution shrinks the text except "__int64" -> "long long", which turns
/// seven characters into nine, so the result never exceeds nine sevenths of the input.
/// The two extra chars cover the rounding and the terminator. Overshooting costs a few
/// bytes of static data; undershooting cannot go unnoticed, because writing past the
/// array during constant evaluation is an error rather than undefined behaviour.
template <typename T>
constexpr auto type_name_capacity() -> std::size_t
{
	return raw_type_name<T>().size() * 9 / 7 + 2;
}

template <typename T>
using type_name_buffer = name_buffer<type_name_capacity<T>()>;

/// The buffer is value initialised and never filled to the end, so the zero left just
/// past the name survives and data() is a valid C string.
template <typename T>
constexpr auto make_type_name_buffer() -> type_name_buffer<T>
{
	type_name_buffer<T> buffer{};
	buffer.size = normalize_type_name(raw_type_name<T>(), buffer.data);
	return buffer;
}

/// Static storage for the normalised name, so that the views handed out stay valid for
/// the lifetime of the program.
template <typename T>
struct type_name_holder
{
	static constexpr type_name_buffer<T> value = make_type_name_buffer<T>();
};

#if !defined(__cpp_inline_variables)
template <typename T>
constexpr type_name_buffer<T> type_name_holder<T>::value;
#endif

/// Position of the '<' opening the template argument list, or npos. The search starts at
/// index 1 because some compilers spell closure types "<lambda_0>", where the leading '<'
/// belongs to the name itself.
constexpr auto template_arguments_start(hpp::string_view name) -> std::size_t
{
	return name.find('<', 1);
}

/// Position where the unqualified name begins, that is just past the last "::" that still
/// qualifies the type itself. A '(' opens a declarator, as in "void(test::my_struct::*)(int)",
/// and the "::" inside one qualifies the pointed to class rather than the type, so the
/// search stops there and such a type keeps its full name. As above, index 0 is skipped
/// because "(anonymous)" opens with a '(' and is an ordinary namespace component.
constexpr auto unqualified_start(hpp::string_view name) -> std::size_t
{
	const std::size_t qualified_end = name.find('(', 1);
	const std::size_t separator = name.substr(0, qualified_end).rfind("::");
	return separator == hpp::string_view::npos ? std::size_t(0) : separator + 2;
}

} // namespace detail

/// The fully qualified type name, in the canonical spelling described at the top of this
/// file. The view is null terminated and has static storage duration.
template <typename T>
constexpr auto type_name() noexcept -> hpp::string_view
{
	return hpp::string_view(detail::type_name_holder<T>::value.data, detail::type_name_holder<T>::value.size);
}

/// The type name without its namespaces and without its template argument list, so
/// test2::my_struct2<test::my_struct> is "my_struct2". Not null terminated.
template <typename T>
constexpr auto type_name_unqualified() noexcept -> hpp::string_view
{
	constexpr auto qualified = type_name<T>();
	constexpr auto without_arguments = qualified.substr(0, detail::template_arguments_start(qualified));
	return without_arguments.substr(detail::unqualified_start(without_arguments));
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

/// Names the static type of the argument. A base class reference names the base, not the
/// dynamic type; use typeid for that.
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
