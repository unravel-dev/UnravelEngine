#pragma once
#include <context/context.hpp>

namespace unravel
{
struct version_manager
{
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void check_for_update_async();
};
} // namespace unravel
