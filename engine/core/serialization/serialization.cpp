#include "serialization.h"
#include <atomic>
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

namespace
{
// Relaxed: these are diagnostic tallies, not synchronisation points.
std::atomic<uint64_t> failed_lookups{0};
std::atomic<uint64_t> thrown_lookups{0};
} // namespace

auto failed_lookup_count() -> uint64_t
{
    return failed_lookups.load(std::memory_order_relaxed);
}

auto thrown_lookup_count() -> uint64_t
{
    return thrown_lookups.load(std::memory_order_relaxed);
}

void reset_failed_lookup_count()
{
    failed_lookups.store(0, std::memory_order_relaxed);
    thrown_lookups.store(0, std::memory_order_relaxed);
}

void note_failed_lookup()
{
    failed_lookups.fetch_add(1, std::memory_order_relaxed);
}

void note_thrown_lookup()
{
    thrown_lookups.fetch_add(1, std::memory_order_relaxed);
}

namespace
{
thread_local output_format current_output_format = output_format::readable;
} // namespace

auto get_output_format() -> output_format
{
    return current_output_format;
}

scoped_output_format::scoped_output_format(output_format format) : previous_(current_output_format)
{
    current_output_format = format;
}

scoped_output_format::~scoped_output_format()
{
    current_output_format = previous_;
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
        // Array indices join without a separator, so "position" + "[0]" reads as
        // "position[0]". Matches the old rebuild's `i > 0 && segment[0] != '['`, including
        // its treatment of an empty segment (operator[](0) on an empty std::string yields
        // '\0', which is not '[', so a separator was added).
        if(!segment_ends_.empty() && (segment.empty() || segment.front() != '['))
        {
            path_ += '/';
        }
        path_ += segment;
        segment_ends_.push_back(path_.size());
        return true;
    }
    return false;
}

void path_context::pop_segment()
{
    if (recording_enabled && !segment_ends_.empty())
    {
        segment_ends_.pop_back();
        // Back to the end of the previous segment, which drops this segment and the
        // separator that was written before it in one step.
        path_.resize(segment_ends_.empty() ? 0u : segment_ends_.back());
    }
}

auto path_context::get_current_path() const -> const std::string&
{
    return path_;
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
    path_.clear();
    segment_ends_.clear();
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
