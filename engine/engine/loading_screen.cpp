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

void loading_screen::begin_session(const std::string& title)
{
    is_session_active_ = true;
    session_title_ = title;
    session_start_ = clock::now();
    finished_modules_.clear();
    module_.clear();
    completed_ = 0;
    total_ = 0;
    step_.clear();
    item_.clear();

    APPLOG_TRACE("{}", title);
    notify_visualizer();
}

void loading_screen::end_session()
{
    if(!is_session_active_)
    {
        return;
    }
    finish_module();
    module_.clear();
    is_session_active_ = false;
    APPLOG_TRACE("{} took {:.2f} s", session_title_, std::chrono::duration<double>(session_elapsed()).count());
}

void loading_screen::begin_module(const std::string& module_name)
{
    finish_module();
    module_ = module_name;
    module_start_ = clock::now();
    completed_ = 0;
    total_ = 0;
    step_.clear();
    item_.clear();

    APPLOG_TRACE("Loading [{}]", module_);
    notify_visualizer();
}

void loading_screen::progress(size_t comp, size_t tot, const std::string& step, const std::string& item)
{
    completed_ = comp;
    total_ = tot;
    step_ = step;
    item_ = item;
    notify_visualizer();
}

void loading_screen::log(const std::string& message)
{
    APPLOG_TRACE("Loading [{}] {}", module_, message);
    step_ = message;
    item_.clear();
    notify_visualizer();
}

void loading_screen::refresh()
{
    notify_visualizer();
}

void loading_screen::fail(const std::string& message)
{
    failed_ = true;
    error_msg_ = message;

    APPLOG_CRITICAL("Loading [{}] {}", module_, message);

    if(on_fail_)
    {
        on_fail_(fmt::format("Loading [{}]", module_), message);
    }
}

auto loading_screen::check(bool result) -> bool
{
    if(!result && !failed_)
    {
        fail(fmt::format("Loading [{}] Failed", module_));
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

auto loading_screen::is_session_active() const -> bool
{
    return is_session_active_;
}

auto loading_screen::session_title() const -> const std::string&
{
    return session_title_;
}

auto loading_screen::session_elapsed() const -> clock::duration
{
    return clock::now() - session_start_;
}

auto loading_screen::finished_modules() const -> const std::vector<module_record>&
{
    return finished_modules_;
}

auto loading_screen::current_module() const -> const std::string&
{
    return module_;
}

auto loading_screen::module_elapsed() const -> clock::duration
{
    return clock::now() - module_start_;
}

auto loading_screen::completed() const -> size_t
{
    return completed_;
}

auto loading_screen::total() const -> size_t
{
    return total_;
}

auto loading_screen::current_step() const -> const std::string&
{
    return step_;
}

auto loading_screen::current_item() const -> const std::string&
{
    return item_;
}

void loading_screen::finish_module()
{
    if(module_.empty())
    {
        return;
    }
    finished_modules_.push_back({module_, clock::now() - module_start_});
}

void loading_screen::notify_visualizer()
{
    if(visualizer_ && is_session_active_)
    {
        visualizer_(*this);
    }
}

} // namespace unravel
