#include "threader.h"

#include <base/platform/thread.hpp>
#include <logging/logging.h>
#include <engine/profiler/profiler.h>

#include <map>
namespace unravel
{
threader::threader()
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

    const size_t worker_count = platform::get_performance_core_count();
    pool = std::make_unique<tpp::thread_pool>(
        std::map<tpp::priority::category, size_t>{{tpp::priority::category::low, worker_count}});
}

auto threader::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto threader::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(pool)
    {
        pool.reset();
        tpp::shutdown();
    }

    return true;
}

void threader::process()
{
    tpp::this_thread::process();
}

} // namespace unravel
