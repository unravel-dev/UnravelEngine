#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <cmd_line/parser.h>
#include <context/context.hpp>
#include <threadpp/thread_pool.h>
#include <threadpp/when_all_any.hpp>
#include <memory>

namespace unravel
{

struct threader
{
    threader(cmd_line::parser& parser);

    /// Turns the single-threaded mode on when the command line asks for it (always on in the browser) and starts
    /// the job pool: one worker per performance core, or none in that mode.
    auto init(rtti::context& ctx, const cmd_line::parser& parser) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /// Runs the main thread's queued calls. In the single-threaded mode it first runs the pool's queued jobs for
    /// part of the frame, since no worker ever takes them.
    void process();

    /// Null until init.
    std::unique_ptr<tpp::thread_pool> pool{};

private:
    /// Whether tpp::init ran and deinit still has to shut tpp down; tpp counts its inits.
    bool is_tpp_initialized_{false};
};
} // namespace unravel
