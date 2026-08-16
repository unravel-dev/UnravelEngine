#include <hpp/type_index.hpp>
#include <hpp/type_name.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

/// Everything here is checked at compile time. The runtime part only prints the names so
/// that the canonical form of a new compiler can be eyeballed.

namespace test
{
struct my_struct
{
};

class my_class
{
};

enum my_enum
{
	my_enum_value
};

enum class my_enum_class
{
	value
};

union my_union
{
	int i;
};

/// Guards the whole-token rule in the substitution table: the "struct" and "class"
/// keywords must not be stripped out of the middle of an identifier.
struct structure
{
};

class classic
{
};

namespace inner
{
template <typename T>
struct tpl
{
};

template <typename T, typename U>
struct pair_tpl
{
};
} // namespace inner
} // namespace test

namespace
{
struct anon_struct
{
};
} // namespace

namespace
{

/// The invariant hpp::type_index depends on: distinct types must never share a name,
/// because the name is what gets hashed into the identity.
template <typename... Ts>
constexpr auto names_are_distinct() -> bool
{
	const hpp::string_view names[] = {hpp::type_name<Ts>()...};
	constexpr std::size_t count = sizeof...(Ts);
	for(std::size_t i = 0; i < count; ++i)
	{
		for(std::size_t j = i + 1; j < count; ++j)
		{
			if(names[i] == names[j])
			{
				return false;
			}
		}
	}
	return true;
}

constexpr auto contains(hpp::string_view haystack, hpp::string_view needle) -> bool
{
	return haystack.find(needle) != hpp::string_view::npos;
}

constexpr auto ends_with(hpp::string_view haystack, hpp::string_view needle) -> bool
{
	return haystack.size() >= needle.size() && haystack.substr(haystack.size() - needle.size()) == needle;
}

template <typename... Ts>
constexpr auto ids_are_distinct() -> bool
{
	const std::uint64_t hashes[] = {hpp::type_id_constexpr<Ts>().hash_code()...};
	constexpr std::size_t count = sizeof...(Ts);
	for(std::size_t i = 0; i < count; ++i)
	{
		for(std::size_t j = i + 1; j < count; ++j)
		{
			if(hashes[i] == hashes[j])
			{
				return false;
			}
		}
	}
	return true;
}

// --------------------------------------------------------------------------------------
// Builtins. These all used to collapse onto each other, because the old implementation
// cut everything up to the first space in order to drop msvc's "struct" / "class".
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<void>() == "void", "void");
static_assert(hpp::type_name<bool>() == "bool", "bool");
static_assert(hpp::type_name<char>() == "char", "char");
static_assert(hpp::type_name<signed char>() == "signed char", "signed char");
static_assert(hpp::type_name<unsigned char>() == "unsigned char", "unsigned char");
static_assert(hpp::type_name<short>() == "short", "short");
static_assert(hpp::type_name<unsigned short>() == "unsigned short", "unsigned short");
static_assert(hpp::type_name<int>() == "int", "int");
static_assert(hpp::type_name<unsigned int>() == "unsigned int", "unsigned int");
static_assert(hpp::type_name<long>() == "long", "long");
static_assert(hpp::type_name<unsigned long>() == "unsigned long", "unsigned long");
static_assert(hpp::type_name<long long>() == "long long", "long long");
static_assert(hpp::type_name<unsigned long long>() == "unsigned long long", "unsigned long long");
static_assert(hpp::type_name<float>() == "float", "float");
static_assert(hpp::type_name<double>() == "double", "double");
static_assert(hpp::type_name<long double>() == "long double", "long double");

static_assert(names_are_distinct<void,
								 bool,
								 char,
								 signed char,
								 unsigned char,
								 short,
								 unsigned short,
								 int,
								 unsigned int,
								 long,
								 unsigned long,
								 long long,
								 unsigned long long,
								 float,
								 double,
								 long double>(),
			  "every builtin must have its own name");

// --------------------------------------------------------------------------------------
// Pointers, references and cv qualifiers. Clang spells these "int *", gcc "int*" and msvc
// "int*"; all three have to land on the same string.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<int*>() == "int*", "pointer");
static_assert(hpp::type_name<int**>() == "int**", "pointer to pointer");
static_assert(hpp::type_name<void*>() == "void*", "void pointer");
static_assert(hpp::type_name<const int*>() == "const int*", "pointer to const");
static_assert(hpp::type_name<int&>() == "int&", "reference");
static_assert(hpp::type_name<const int&>() == "const int&", "reference to const");
static_assert(hpp::type_name<int&&>() == "int&&", "rvalue reference");
static_assert(hpp::type_name<test::my_struct*>() == "test::my_struct*", "pointer to class type");

static_assert(names_are_distinct<int,
								 int*,
								 int**,
								 void*,
								 const int*,
								 int&,
								 const int&,
								 int&&,
								 test::my_struct,
								 test::my_struct*>(),
			  "pointers must not collapse onto each other or onto their pointee");

// --------------------------------------------------------------------------------------
// Arrays and function types.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<int[4]>() == "int[4]", "array");
static_assert(hpp::type_name<void (*)(int)>() == "void(*)(int)", "function pointer");
static_assert(hpp::type_name<void (*)(int, char*)>() == "void(*)(int,char*)", "function pointer arguments");

// Clang drops the enclosing namespace of the class in a pointer to member, printing
// "void (my_struct::*)(int)" where gcc and msvc print "test::my_struct". That qualifier
// is simply absent from the signature, so the whole string cannot be pinned down here.
// What has to hold on every compiler is that msvc's calling convention is gone and the
// spacing is normalised.
using member_function_pointer = void (test::my_struct::*)(int);
static_assert(!contains(hpp::type_name<member_function_pointer>(), "__cdecl"), "calling convention dropped");
static_assert(!contains(hpp::type_name<member_function_pointer>(), " "), "member pointer carries no space");
static_assert(ends_with(hpp::type_name<member_function_pointer>(), "my_struct::*)(int)"), "member pointer shape");

static_assert(names_are_distinct<int[4], int[8], void (*)(int), void (*)(char), member_function_pointer>(),
			  "array extents and function signatures must survive");

// --------------------------------------------------------------------------------------
// Elaborated type specifiers. Msvc prints "struct" / "class" / "enum" / "union" in front
// of every user defined type, at every nesting level.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<test::my_struct>() == "test::my_struct", "struct keyword");
static_assert(hpp::type_name<test::my_class>() == "test::my_class", "class keyword");
static_assert(hpp::type_name<test::my_enum>() == "test::my_enum", "enum keyword");
static_assert(hpp::type_name<test::my_enum_class>() == "test::my_enum_class", "scoped enum keyword");
static_assert(hpp::type_name<test::my_union>() == "test::my_union", "union keyword");
static_assert(hpp::type_name<test::structure>() == "test::structure", "keyword prefix inside an identifier");
static_assert(hpp::type_name<test::classic>() == "test::classic", "keyword prefix inside an identifier");

// --------------------------------------------------------------------------------------
// Templates. The keywords have to go from the arguments too, and the separators have to
// be spelled the same way everywhere.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<test::inner::tpl<test::my_struct>>() == "test::inner::tpl<test::my_struct>",
			  "class type argument");
static_assert(hpp::type_name<test::inner::tpl<int*>>() == "test::inner::tpl<int*>", "pointer argument");
static_assert(hpp::type_name<test::inner::tpl<test::inner::tpl<int>>>() == "test::inner::tpl<test::inner::tpl<int>>",
			  "nested argument, no space between the closing angle brackets");
static_assert(hpp::type_name<test::inner::pair_tpl<int, test::my_struct>>() ==
				  "test::inner::pair_tpl<int,test::my_struct>",
			  "argument separator, no space after the comma");

static_assert(names_are_distinct<test::inner::tpl<int>,
								 test::inner::tpl<unsigned int>,
								 test::inner::tpl<int*>,
								 test::inner::tpl<const int*>,
								 test::inner::tpl<test::my_struct>,
								 test::inner::tpl<test::my_struct*>>(),
			  "template arguments must not collapse");

// --------------------------------------------------------------------------------------
// Anonymous namespaces. Three compilers, three spellings.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<anon_struct>() == "(anonymous)::anon_struct", "anonymous namespace");

// --------------------------------------------------------------------------------------
// Standard library types. Msvc prints defaulted template arguments and the others do not,
// so only the parts that can agree are pinned down here.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<std::vector<int>>().substr(0, 17) == "std::vector<int,s" ||
				  hpp::type_name<std::vector<int>>() == "std::vector<int>",
			  "std::vector is either bare or carries its allocator, never anything else");
static_assert(hpp::type_name_unqualified<std::vector<int>>() == "vector", "std::vector unqualified");
static_assert(hpp::type_name_unqualified<std::string>() == "basic_string",
			  "gcc's inline __cxx11 namespace must not leak into the unqualified name");

// --------------------------------------------------------------------------------------
// Unqualified names.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name_unqualified<int>() == "int", "builtin");
static_assert(hpp::type_name_unqualified<unsigned int>() == "unsigned int", "multi word builtin");
static_assert(hpp::type_name_unqualified<test::my_struct>() == "my_struct", "namespaces dropped");
static_assert(hpp::type_name_unqualified<test::inner::tpl<test::my_struct>>() == "tpl",
			  "template arguments dropped");
static_assert(hpp::type_name_unqualified<test::inner::pair_tpl<int, test::my_struct>>() == "pair_tpl",
			  "template arguments dropped");
static_assert(hpp::type_name_unqualified<anon_struct>() == "anon_struct", "anonymous namespace dropped");
static_assert(hpp::type_name_unqualified<test::my_struct*>() == "my_struct*", "pointer kept");
static_assert(hpp::type_name_unqualified<void (*)(int)>() == "void(*)(int)", "a declarator has no unqualified name");
static_assert(hpp::type_name_unqualified<member_function_pointer>() == hpp::type_name<member_function_pointer>(),
			  "the '::' inside a declarator qualifies the class, not the type");

// --------------------------------------------------------------------------------------
// Storage guarantees.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_name<test::my_struct>().data()[hpp::type_name<test::my_struct>().size()] == '\0',
			  "the qualified name is null terminated");
static_assert(hpp::type_name<int>().size() == 3, "the size excludes the terminator");

// --------------------------------------------------------------------------------------
// hpp::type_index rides on these names, so the identities have to be distinct too.
// --------------------------------------------------------------------------------------

static_assert(hpp::type_id_constexpr<test::my_struct>().name() == "test::my_struct", "type_index name");
static_assert(ids_are_distinct<int,
							   unsigned int,
							   long,
							   long long,
							   unsigned long long,
							   int*,
							   const int*,
							   void*,
							   test::my_struct,
							   test::my_struct*,
							   test::my_class,
							   test::inner::tpl<int>,
							   test::inner::tpl<unsigned int>>(),
			  "distinct types must hash to distinct type_index values");

template <typename T>
void print_name(const char* declared)
{
	std::cout << "  " << declared << "\n    -> '" << hpp::type_name_str<T>() << "'  ('"
			  << hpp::type_name_unqualified_str<T>() << "')" << std::endl;
}

} // namespace

#define PRINT_NAME(...) print_name<__VA_ARGS__>(#__VA_ARGS__)

void run_type_name_tests()
{
	std::cout << "type_name canonical form:" << std::endl;
	PRINT_NAME(unsigned int);
	PRINT_NAME(long long);
	PRINT_NAME(std::uint64_t);
	PRINT_NAME(int*);
	PRINT_NAME(const int*);
	PRINT_NAME(int[4]);
	PRINT_NAME(void (*)(int));
	PRINT_NAME(void (test::my_struct::*)(int));
	PRINT_NAME(int test::my_struct::*);
	PRINT_NAME(test::my_enum_class);
	PRINT_NAME(test::inner::tpl<int*>);
	PRINT_NAME(anon_struct);
	PRINT_NAME(std::vector<test::my_struct>);
	PRINT_NAME(std::string);
}
