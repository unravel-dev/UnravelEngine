#include "serialization.h"
#include <sstream>

namespace serialization
{
namespace
{
auto get_warning_logger() -> log_callback_t&
{
    static log_callback_t logger;
    return logger;
}

// Path tracking implementation
thread_local path_context* current_path_context = nullptr;

} // namespace
void set_warning_logger(const std::function<void(const std::string&, const hpp::source_location& loc)>& logger)
{
    get_warning_logger() = logger;
}
void log_warning(const std::string& log_msg, const hpp::source_location& loc)
{
    auto& logger = get_warning_logger();
    if(logger)
        logger(log_msg, loc);
}

auto get_path_context() -> path_context*
{
    return current_path_context;
}

void set_path_context(path_context* ctx)
{
    current_path_context = ctx;
}

void path_context::push_segment(const std::string& segment, bool ignore_next)
{
    if (recording_enabled)
    {
        if(!ignore_next)
        {
            if(ignore_next_push)
            {
                ignore_next_push = false;
            }
            else
            {
                path_segments.push_back(segment);
            }
        }
    }
}

void path_context::pop_segment()
{
    if (recording_enabled && !path_segments.empty())
    {
        path_segments.pop_back();
    }
}

auto path_context::get_current_path() const -> std::string
{
    if (path_segments.empty())
        return "";
    
    std::stringstream ss;
    for (size_t i = 0; i < path_segments.size(); ++i)
    {
        if (i > 0)
            ss << "/";
        ss << path_segments[i];
    }
    return ss.str();
}

void path_context::enable_recording()
{
    recording_enabled = true;
}

void path_context::disable_recording()
{
    recording_enabled = false;
}

auto path_context::is_recording() const -> bool
{
    return recording_enabled;
}

void path_context::clear()
{
    path_segments.clear();
    recording_enabled = false;
}

path_segment_guard::path_segment_guard(const std::string& segment, bool ignore_next_push)
{
    auto* ctx = get_path_context();
    if (ctx && ctx->is_recording())
    {
        ctx->push_segment(segment);
        was_pushed_ = true;
    }
}

path_segment_guard::~path_segment_guard()
{
    if (was_pushed_)
    {
        auto* ctx = get_path_context();
        if (ctx)
        {
            ctx->pop_segment();
        }
    }
}

auto get_current_deserialization_path() -> std::string
{
    auto* ctx = get_path_context();
    if (ctx && ctx->is_recording())
    {
        return ctx->get_current_path();
    }
    return "";
}

} // namespace serialization
