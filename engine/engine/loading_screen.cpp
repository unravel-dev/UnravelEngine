#include "loading_screen.h"

#include <logging/logging.h>

namespace unravel
{

void loading_screen::set_visualizer(visualizer_fn fn)
{
    visualizer_ = std::move(fn);
}

void loading_screen::set_on_fail(on_fail_fn fn)
{
    on_fail_ = std::move(fn);
}

void loading_screen::begin_module(const std::string& module_name)
{
    module_ = fmt::format("Loading [{}]", module_name);
    completed_ = 0;
    total_ = 0;
    current_job_ = {};

    APPLOG_TRACE("{}", module_);
    notify_visualizer();
}

void loading_screen::progress(size_t comp, size_t tot, const std::string& job)
{
    completed_ = comp;
    total_ = tot;
    current_job_ = job;
    notify_visualizer();
}

void loading_screen::log(const std::string& message)
{
    APPLOG_TRACE("{} {}", module_, message);
    current_job_ = message;
    notify_visualizer();
}

void loading_screen::fail(const std::string& message)
{
    failed_ = true;
    error_msg_ = message;

    APPLOG_CRITICAL("{} {}", module_, message);

    if(on_fail_)
    {
        on_fail_(module_, message);
    }
}

auto loading_screen::check(bool result) -> bool
{
    if(!result && !failed_)
    {
        fail(module_ + " Failed");
    }
    return result;
}

auto loading_screen::has_failed() const -> bool
{
    return failed_;
}

auto loading_screen::error_message() const -> const std::string&
{
    return error_msg_;
}

auto loading_screen::current_module() const -> const std::string&
{
    return module_;
}

auto loading_screen::completed() const -> size_t
{
    return completed_;
}

auto loading_screen::total() const -> size_t
{
    return total_;
}

auto loading_screen::current_job() const -> const std::string&
{
    return current_job_;
}

void loading_screen::notify_visualizer()
{
    if(visualizer_)
    {
        visualizer_(*this);
    }
}

} // namespace unravel
