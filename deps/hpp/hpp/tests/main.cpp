#include <hpp/type_traits.hpp>
#include <hpp/utility.hpp>

#include <iostream>

/// Defined in type_name_tests.cpp, where the type name checks live.
void run_type_name_tests();

int main()
{
	run_type_name_tests();

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
