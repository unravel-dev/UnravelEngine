#pragma once
#include <context/context.hpp>

namespace unravel
{
struct version_manager
{
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;
};
} // namespace unravel
