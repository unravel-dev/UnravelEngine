#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <threadpp/thread_pool.h>
#include <threadpp/when_all_any.hpp>
#include <memory>

namespace unravel
{

struct threader
{
    threader();

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void process();

    std::unique_ptr<tpp::thread_pool> pool{};
};
} // namespace unravel
