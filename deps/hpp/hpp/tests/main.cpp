#include <hpp/type_traits.hpp>
#include <hpp/utility.hpp>
#include <hpp/type_name.hpp>
#include <hpp/type_index.hpp>

#include <iostream>

namespace test
{
struct my_struct
{

};
}

namespace test2
{
template<typename T>
struct my_struct2
{

};
}

int main()
{
    static_assert(hpp::type_name<int>() == "int", "not working");
    static_assert(hpp::type_name<test::my_struct>() == "test::my_struct", "not working");
	static_assert(hpp::type_name_unqualified<test::my_struct>() == "my_struct", "not working");
	static_assert(hpp::type_name_unqualified<test2::my_struct2<test::my_struct>>() == "my_struct2", "not working");
	static_assert(hpp::type_id_constexpr<test::my_struct>().name() == "test::my_struct", "not working");


	constexpr int i = 0;
	if_constexpr(i == 0)
	{
		std::cout << "case i == 0" << std::endl;
	}
	else_if_constexpr(i == 1)
	{
		std::cout << "case i == 1" << std::endl;
	}
	else_constexpr
	{
		std::cout << "case else" << std::endl;
	}
	end_if_constexpr;

	auto invokeable = [](int param) {
		std::cout << "invoked with " << param << std::endl;

		return param;
	};

	auto res = hpp::invoke(invokeable, 5);
	std::cout << "invoke returned " << res << std::endl;

	auto tup = std::make_tuple(6);

	auto res1 = hpp::apply(invokeable, tup);
	std::cout << "apply returned " << res1 << std::endl;

	return 0;
}
