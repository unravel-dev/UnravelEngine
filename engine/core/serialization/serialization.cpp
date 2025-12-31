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

auto path_context::push_segment(const std::string& segment) -> bool
{
    
    if(ignore_next_push)
    {
        ignore_next_push = false;
        return false;
    }

    if (recording_enabled)
    {
        path_segments.push_back(segment);
        return true;
    }
    return false;
}

void path_context::pop_segment()
{
    if (recording_enabled && !path_segments.empty())
    {
        if(path_segments.back().ends_with("]"))
        {
            int a = 0;
            a++;
        }
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
        // For array indices, don't add a separator
        if (i > 0 && path_segments[i][0] != '[')
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

path_segment_guard::path_segment_guard(const std::string& segment)
{
    push_segment(segment);
}

path_segment_guard::~path_segment_guard()
{
    pop_segment();
}


void path_segment_guard::push_segment(const std::string& segment)
{
    auto* ctx = get_path_context();
    if (ctx && ctx->is_recording())
    {
        was_pushed_ = ctx->push_segment(segment);
    }
}
void path_segment_guard::pop_segment()
{
    if(was_pushed_)
    {
        auto* ctx = get_path_context();
        if (ctx && ctx->is_recording())
        {
            ctx->pop_segment();
        }
    }
    was_pushed_ = false;
}


path_skip_segment_guard::path_skip_segment_guard(bool ignore_next_push)
{
    auto* ctx = get_path_context();
    if (ctx && ctx->is_recording())
    {
        ctx->ignore_next_push = ignore_next_push;
    }
}

path_skip_segment_guard::~path_skip_segment_guard()
{

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



void init(const init_data& data)
{
    if(data.warning_logger)
    {
        get_warning_logger() = data.warning_logger;
    }

    auto& context = ser20::get_vector_serialization_context();
    context.on_element_serialization_begin = [](size_t index)
    {
        std::string index_segment = "[" + std::to_string(index) + "]";
        // guard->push_segment(index_segment);
        auto* ctx = serialization::get_path_context();
        if (ctx && ctx->is_recording())
        {
            ctx->push_segment(index_segment);
        }
    };
    context.on_element_serialization_end = [](size_t index)
    {
        auto* ctx = serialization::get_path_context();
        if (ctx && ctx->is_recording())
        {
            ctx->pop_segment();
        }       
    };
    context.should_serialize_element = [](size_t index)
    {
        auto* ctx = serialization::get_path_context();
        if (ctx && ctx->is_recording())
        {
            return ctx->should_serialize_property(ctx->get_current_path());
        }
        return true;
    };
}
} // namespace serialization
