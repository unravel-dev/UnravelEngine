#include "tests_helper.h"

std::mt19937& helper::details::rd_gen()
{
    static std::random_device rd;
    static std::mt19937 generator(rd());
    return generator;
}
