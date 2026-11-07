#include "threader.h"

#include <base/platform/thread.hpp>
#include <concurrency/threading_mode.h>
#include <logging/logging.h>
#include <engine/profiler/profiler.h>

#include <chrono>
#include <map>
namespace unravel
{
namespace
{
/// How long each frame runs the queued jobs no one waits for, in the single-threaded mode. Loads that the
/// frame waits for run in that wait instead; this only moves the rest along without stalling the frame.
constexpr auto PENDING_JOBS_FRAME_BUDGET = std::chrono::milliseconds(4);
/// Runs the queue until it is empty, the jobs the queued ones add included.
constexpr auto UNLIMITED_BUDGET = std::chrono::steady_clock::duration::max();
} // namespace

threader::threader(cmd_line::parser& parser)
{
    tpp::init_data data{};
    data.set_thread_name = [](const std::string& name)
    {
        platform::set_thread_name(name.c_str());
        get_thread_profile_data(name.c_str());
    };

    data.log_info = [](const std::string& msg)
    {
        APPLOG_INFO(msg);
    };
    data.log_error = [](const std::string& msg)
    {
        APPLOG_ERROR(msg);
    };

    tpp::init(data);
    is_tpp_initialized_ = true;

    parser.set_optional<bool>("",
                              "single-threaded",
                              false,
                              "Run all engine work on the main thread, with no worker threads, as the browser does.");
}

auto threader::init(rtti::context& ctx, const cmd_line::parser& parser) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    bool is_single_threaded_requested = false;
    parser.try_get("single-threaded", is_single_threaded_requested);
    if(is_single_threaded_requested)
    {
        set_single_threaded(true);
    }
    const size_t worker_count = is_single_threaded() ? 0 : platform::get_performance_core_count();
    pool = std::make_unique<tpp::thread_pool>(
        std::map<tpp::priority::category, size_t>{{tpp::priority::category::low, worker_count}});
    if(is_single_threaded())
    {
        APPLOG_INFO("Threading: single-threaded, every job runs on the main thread.");
    }
    return true;
}

auto threader::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    pool.reset();
    if(is_tpp_initialized_)
    {
        tpp::shutdown();
        is_tpp_initialized_ = false;
    }

    return true;
}

void threader::run_queued_jobs()
{
    if(pool && pool->get_workers_count() == 0)
    {
        pool->run_pending(UNLIMITED_BUDGET);
    }
}

void threader::process()
{
    if(pool && pool->get_workers_count() == 0)
    {
        pool->run_pending(PENDING_JOBS_FRAME_BUDGET);
    }
    tpp::this_thread::process();
}

} // namespace unravel
