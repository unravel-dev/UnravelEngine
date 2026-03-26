#pragma once
#include <engine/engine_export.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <type_traits>
#include <hpp/string_view.hpp>
#include <base/platform/thread.hpp>

namespace unravel
{

// ============================================================================
// sample_data - Rolling statistics buffer
// ============================================================================

class sample_data
{
public:
    static constexpr uint32_t num_samples = 500;

    sample_data()
    {
        reset(0.0f);
    }

    auto reset(float value) -> void
    {
        offset_ = 0;
        std::fill(values_.begin(), values_.end(), value);
        min_ = value;
        max_ = value;
        average_ = value;
        sum_ = value * static_cast<float>(num_samples);
        min_index_ = 0;
        max_index_ = 0;
        smart_init_samples_ = smart_init_samples_count;
    }

    auto push_sample(float value) -> void
    {
        if(smart_init_samples_ > 0 && offset_ > smart_init_samples_)
        {
            reset(value);
            smart_init_samples_ = -1;
            return;
        }

        const float old_value = values_[offset_];
        const int32_t current_index = offset_;

        sum_ = sum_ - old_value + value;
        values_[offset_] = value;
        offset_ = (offset_ + 1) % static_cast<int32_t>(num_samples);
        average_ = sum_ / static_cast<float>(num_samples);

        if(min_index_ == current_index)
        {
            min_index_ = 0;
            min_ = values_[0];
            for(int32_t i = 1; i < static_cast<int32_t>(num_samples); ++i)
            {
                if(values_[i] < min_)
                {
                    min_ = values_[i];
                    min_index_ = i;
                }
            }
        }
        else if(value < min_)
        {
            min_ = value;
            min_index_ = current_index;
        }

        if(max_index_ == current_index)
        {
            max_index_ = 0;
            max_ = values_[0];
            for(int32_t i = 1; i < static_cast<int32_t>(num_samples); ++i)
            {
                if(values_[i] > max_)
                {
                    max_ = values_[i];
                    max_index_ = i;
                }
            }
        }
        else if(value > max_)
        {
            max_ = value;
            max_index_ = current_index;
        }
    }

    auto get_values() const -> const float* { return values_.data(); }
    auto get_offset() const -> int32_t { return offset_; }
    auto get_min() const -> float { return min_; }
    auto get_max() const -> float { return max_; }
    auto get_average() const -> float { return average_; }

private:
    static constexpr int32_t smart_init_samples_count = 20;

    int32_t offset_{0};
    std::array<float, num_samples> values_{};
    float min_{0.0f};
    float max_{0.0f};
    float average_{0.0f};
    float sum_{0.0f};
    int32_t min_index_{0};
    int32_t max_index_{0};
    int32_t smart_init_samples_{-1};
};

// ============================================================================
// Timeline profiler data structures
// ============================================================================

constexpr auto fnv1a_hash(const char* str) -> uint32_t
{
    uint32_t hash = 2166136261u;
    for(; *str; ++str)
    {
        hash ^= static_cast<uint32_t>(*str);
        hash *= 16777619u;
    }
    return hash;
}

/// @brief A single profiling event captured during a frame.
struct profile_event
{
    /// String literal from native scopes; null if the label is stored in name_owned.
    const char* name_literal{};
    std::string name_owned{};
    int64_t start_ns{0};
    int64_t end_ns{0};
    int64_t cpu_start_ns{0};
    int64_t cpu_end_ns{0};
    uint16_t depth{0};
    uint16_t thread_index{0};
    uint32_t color_hash{0};

    [[nodiscard]] auto name() const -> const char*
    {
        return name_literal ? name_literal : name_owned.c_str();
    }
};

/// @brief Fixed-size buffer of profile events for one frame on one thread.
struct thread_profile_buffer
{
    static constexpr uint32_t max_events = 32768;

    std::array<profile_event, max_events> events{};
    uint32_t count{0};
    uint16_t depth{0};
    uint16_t thread_index{0};

    void reset()
    {
        count = 0;
        depth = 0;
    }
};

/// @brief Double-buffered per-thread profiling state.
/// One buffer is being written by the owning thread while the other
/// is read by the main thread for display and aggregation.
struct thread_profile_data
{
    /// @brief Mirrored from @ref performance_profiler recording state: 1 while @c recording, else 0.
    /// Updated when recording toggles and when the thread registers. Hot path: relaxed load in profile_begin.
    std::atomic<uint8_t> capture_active{0};

    std::array<thread_profile_buffer, 2> buffers{};
    std::atomic<uint32_t> write_idx{0};

    auto write_buffer() -> thread_profile_buffer& { return buffers[write_idx.load(std::memory_order_relaxed)]; }
    auto read_buffer() -> thread_profile_buffer& { return buffers[1 - write_idx.load(std::memory_order_acquire)]; }

    void flip()
    {
        uint32_t old_idx = write_idx.load(std::memory_order_relaxed);
        write_idx.store(1 - old_idx, std::memory_order_release);
        write_buffer().reset();
    }
};

// ============================================================================
// Frame snapshot and recording state
// ============================================================================

/// @brief Compacted copy of one frame's profiling data across all threads.
struct frame_snapshot
{
    int64_t frame_start_ns{0};
    int64_t frame_end_ns{0};
    int64_t event_min_ns{0};
    int64_t event_max_ns{0};
    /// Managed heap used (e.g. Mono GC) sampled at capture; bytes.
    int64_t cpu_heap_used_bytes{0};
    /// Total GPU memory reported by bgfx at capture; bytes.
    int64_t gpu_memory_used_bytes{0};
    /// Process resident set (RSS / working set) at capture; bytes.
    int64_t process_resident_bytes{0};

    struct thread_snapshot
    {
        uint16_t thread_index{0};
        std::string name;
        std::vector<profile_event> events;
    };

    std::vector<thread_snapshot> threads;
};

enum class recording_state : uint8_t
{
    stopped,
    recording,
    paused
};

// ============================================================================
// performance_profiler
// ============================================================================

class performance_profiler
{
public:
    /// @brief Per-name aggregate timing data with rolling history.
    struct per_frame_data
    {
        float time_since_swap = 0.0f;
        uint32_t samples_since_swap = 0;
        sample_data history;

        per_frame_data() = default;
        per_frame_data(float t) : time_since_swap(t), samples_since_swap(1)
        {
            history.push_sample(t);
        }

        operator float() const { return time_since_swap; }

        auto operator+=(float t) -> per_frame_data&
        {
            time_since_swap += t;
            return *this;
        }

        auto get_time_since_swap() const -> float { return time_since_swap; }
        auto get_samples_since_swap() const -> uint32_t { return samples_since_swap; }
        auto get_avg() const -> float { return history.get_average(); }
        auto get_min() const -> float { return history.get_min(); }
        auto get_max() const -> float { return history.get_max(); }
        auto get_history() const -> const sample_data& { return history; }

        void add_sample(float t)
        {
            time_since_swap += t;
            samples_since_swap++;
        }

        void reset()
        {
            if(samples_since_swap > 0)
            {
                history.push_sample(time_since_swap);
            }
            samples_since_swap = 0;
            time_since_swap = 0.0f;
        }
    };

    using record_data_t = std::map<std::string, per_frame_data, std::less<>>;

    struct thread_info
    {
        std::string name;
        std::unique_ptr<thread_profile_data> data;
    };

    /// @brief Script / external pre-measured span on the **current** thread: same as
    /// profile_begin_owned + profile_end, then wall/CPU times set to [now - time_ms, now].
    /// Thread must already be registered (e.g. native code or prior ensure_thread_registered).
    void add_record(hpp::string_view name, float time_ms);

    /// @brief End-of-frame: flip buffers, compute aggregates, advance history.
    void swap();

    /// @brief Aggregate per-name data for the statistics panel.
    auto get_per_frame_data_read() const -> const record_data_t&;

    /// @brief Register the calling thread for timeline profiling.
    /// @return Pointer to the thread's profiling data (stable for the thread's lifetime).
    auto register_thread(const std::string& name) -> thread_profile_data*;

    /// @brief All registered threads and their profiling data.
    auto get_threads() const -> const std::vector<thread_info>&;

    /// @brief Frame boundary timestamps for the last completed frame.
    auto get_frame_start_ns() const -> int64_t;
    auto get_frame_end_ns() const -> int64_t;

    /// @brief Recording control.
    void set_recording_state(recording_state state);
    auto get_recording_state() const -> recording_state;

    /// @brief Number of captured frame snapshots available.
    auto get_frame_count() const -> uint32_t;

    /// @brief Access a captured frame snapshot.
    /// @param index 0 = oldest captured, get_frame_count()-1 = newest.
    auto get_frame_snapshot(uint32_t index) const -> const frame_snapshot*;

    /// @brief Selected frame index for UI inspection (-1 = live/latest).
    void set_selected_frame(int32_t index);
    auto get_selected_frame() const -> int32_t;

    /// @brief Clear all captured frame history.
    void clear_history();

    static constexpr uint32_t max_frame_history = 2000;

private:
    std::vector<thread_info> threads_;
    std::mutex registration_mutex_;

    record_data_t aggregate_data_;

    int64_t frame_start_ns_{0};
    int64_t frame_end_ns_{0};
    int64_t prev_frame_start_ns_{0};
    int64_t prev_frame_end_ns_{0};

    std::vector<frame_snapshot> frame_history_;
    uint32_t history_write_idx_{0};
    uint32_t history_count_{0};
    recording_state recording_state_{recording_state::stopped};
    int32_t selected_frame_{-1};

    auto register_thread_unlocked(const std::string& name) -> thread_profile_data*;

    void sync_capture_active_to_threads();

    void capture_frame_snapshot();
};

// ============================================================================
// Thread-local state and inline recording functions
// ============================================================================

inline thread_local thread_profile_data* t_profile_data = nullptr;

inline auto get_time_ns() -> int64_t
{
    using clock = std::chrono::high_resolution_clock;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch())
        .count();
}

/// @brief Register the current thread on first use. @a thread_name if non-null and non-empty is
/// stored as the lane label; otherwise "Thread-{id}" is used. Called at most once per thread (TLS).
auto ensure_thread_registered(const char* thread_name = nullptr) -> thread_profile_data*;

inline auto get_thread_profile_data() -> thread_profile_data*
{
    if(!t_profile_data) [[unlikely]]
    {
        t_profile_data = ensure_thread_registered(nullptr);
    }
    return t_profile_data;
}

/// @brief Resolve TLS data, registering with @a thread_name only if this is the first profiler use
/// on this OS thread (subsequent calls ignore @a thread_name).
inline auto get_thread_profile_data(const char* thread_name) -> thread_profile_data*
{
    if(!t_profile_data) [[unlikely]]
    {
        t_profile_data = ensure_thread_registered(thread_name);
    }
    return t_profile_data;
}

[[nodiscard]] inline auto thread_profile_should_capture(const thread_profile_data* data) -> bool
{
    return data->capture_active.load(std::memory_order_relaxed) != 0;
}

/// @brief Begin a profiling scope. Returns an event index for profile_end().
inline auto profile_begin(const char* name) -> uint32_t
{
    auto* data = get_thread_profile_data();
    if(!data) [[unlikely]]
    {
        return UINT32_MAX;
    }
    if(!thread_profile_should_capture(data)) [[unlikely]]
    {
        return UINT32_MAX;
    }

    auto& buf = data->write_buffer();
    if(buf.count >= thread_profile_buffer::max_events) [[unlikely]]
    {
        return UINT32_MAX;
    }

    uint32_t idx = buf.count++;
    auto& ev = buf.events[idx];
    ev.name_literal = name;
    ev.name_owned.clear();
    ev.start_ns = get_time_ns();
    ev.cpu_start_ns = platform::get_thread_cpu_time_ns();
    ev.end_ns = 0;
    ev.cpu_end_ns = 0;
    ev.depth = buf.depth++;
    ev.thread_index = buf.thread_index;
    ev.color_hash = fnv1a_hash(name);
    return idx;
}

/// @brief Same as profile_begin(@a name), but the first profiler call on this thread uses
/// @a thread_name for the timeline lane label (e.g. @c "Render" / @c "JobPool-3").
inline auto profile_begin(const char* name, const char* thread_name) -> uint32_t
{
    auto* data = get_thread_profile_data(thread_name);
    if(!data) [[unlikely]]
    {
        return UINT32_MAX;
    }
    if(!thread_profile_should_capture(data)) [[unlikely]]
    {
        return UINT32_MAX;
    }

    auto& buf = data->write_buffer();
    if(buf.count >= thread_profile_buffer::max_events) [[unlikely]]
    {
        return UINT32_MAX;
    }

    uint32_t idx = buf.count++;
    auto& ev = buf.events[idx];
    ev.name_literal = name;
    ev.name_owned.clear();
    ev.start_ns = get_time_ns();
    ev.cpu_start_ns = platform::get_thread_cpu_time_ns();
    ev.end_ns = 0;
    ev.cpu_end_ns = 0;
    ev.depth = buf.depth++;
    ev.thread_index = buf.thread_index;
    ev.color_hash = fnv1a_hash(name);
    return idx;
}

/// @brief Like profile_begin() but copies the label into name_owned (script / dynamic names).
inline auto profile_begin_owned(hpp::string_view name) -> uint32_t
{
    auto* data = get_thread_profile_data();
    if(!data) [[unlikely]]
    {
        return UINT32_MAX;
    }
    if(!thread_profile_should_capture(data)) [[unlikely]]
    {
        return UINT32_MAX;
    }

    auto& buf = data->write_buffer();
    if(buf.count >= thread_profile_buffer::max_events) [[unlikely]]
    {
        return UINT32_MAX;
    }

    uint32_t idx = buf.count++;
    auto& ev = buf.events[idx];
    ev.name_literal = nullptr;
    ev.name_owned = std::string(name);
    ev.start_ns = get_time_ns();
    ev.cpu_start_ns = platform::get_thread_cpu_time_ns();
    ev.end_ns = 0;
    ev.cpu_end_ns = 0;
    ev.depth = buf.depth++;
    ev.thread_index = buf.thread_index;
    ev.color_hash = fnv1a_hash(ev.name());
    return idx;
}

/// @brief profile_begin_owned(@a name) with optional lane label on first registration (see profile_begin two-arg).
inline auto profile_begin_owned(hpp::string_view name, const char* thread_name) -> uint32_t
{
    auto* data = get_thread_profile_data(thread_name);
    if(!data) [[unlikely]]
    {
        return UINT32_MAX;
    }
    if(!thread_profile_should_capture(data)) [[unlikely]]
    {
        return UINT32_MAX;
    }

    auto& buf = data->write_buffer();
    if(buf.count >= thread_profile_buffer::max_events) [[unlikely]]
    {
        return UINT32_MAX;
    }

    uint32_t idx = buf.count++;
    auto& ev = buf.events[idx];
    ev.name_literal = nullptr;
    ev.name_owned = std::string(name);
    ev.start_ns = get_time_ns();
    ev.cpu_start_ns = platform::get_thread_cpu_time_ns();
    ev.end_ns = 0;
    ev.cpu_end_ns = 0;
    ev.depth = buf.depth++;
    ev.thread_index = buf.thread_index;
    ev.color_hash = fnv1a_hash(ev.name());
    return idx;
}

/// @brief End a profiling scope started by profile_begin().
inline void profile_end(uint32_t idx)
{
    auto* data = get_thread_profile_data();
    if(!data || idx == UINT32_MAX) [[unlikely]]
    {
        return;
    }

    auto& buf = data->write_buffer();
    buf.events[idx].end_ns = get_time_ns();
    buf.events[idx].cpu_end_ns = platform::get_thread_cpu_time_ns();
    buf.depth--;
}

// ============================================================================
// RAII scope timer
// ============================================================================

/// @brief Concept to ensure only string literals (char arrays) are accepted.
/// With `const T&` parameter binding, T deduces as `char[N]`.
template<typename T>
concept string_literal = std::is_array_v<std::remove_reference_t<T>> &&
                         std::is_same_v<std::remove_const_t<std::remove_extent_t<std::remove_reference_t<T>>>, char>;

class scope_profile_timer
{
public:
    template<typename T>
        requires string_literal<T>
    explicit scope_profile_timer(const T& name)
        : index_(profile_begin(name))
    {
    }

    ~scope_profile_timer()
    {
        profile_end(index_);
    }

    scope_profile_timer(const scope_profile_timer&) = delete;
    scope_profile_timer(scope_profile_timer&&) = delete;
    auto operator=(const scope_profile_timer&) -> scope_profile_timer& = delete;
    auto operator=(scope_profile_timer&&) -> scope_profile_timer& = delete;

private:
    uint32_t index_;
};

class scope_profile_timer_owned
{
public:
    explicit scope_profile_timer_owned(hpp::string_view name)
        : index_(profile_begin_owned(name))
    {
    }

    ~scope_profile_timer_owned()
    {
        profile_end(index_);
    }

    scope_profile_timer_owned(const scope_profile_timer_owned&) = delete;
    scope_profile_timer_owned(scope_profile_timer_owned&&) = delete;
    auto operator=(const scope_profile_timer_owned&) -> scope_profile_timer_owned& = delete;
    auto operator=(scope_profile_timer_owned&&) -> scope_profile_timer_owned& = delete;

private:
    uint32_t index_;
};

/// @brief Like scope_profile_timer but registers the current OS thread with @a ThreadName on first use.
template<typename ScopeName, typename ThreadName>
    requires string_literal<ScopeName> && string_literal<ThreadName>
class scope_profile_timer_named_thread
{
public:
    explicit scope_profile_timer_named_thread(const ScopeName& scope_name, const ThreadName& thread_name)
        : index_(profile_begin(scope_name, thread_name))
    {
    }

    ~scope_profile_timer_named_thread()
    {
        profile_end(index_);
    }

    scope_profile_timer_named_thread(const scope_profile_timer_named_thread&) = delete;
    scope_profile_timer_named_thread(scope_profile_timer_named_thread&&) = delete;
    auto operator=(const scope_profile_timer_named_thread&) -> scope_profile_timer_named_thread& = delete;
    auto operator=(scope_profile_timer_named_thread&&) -> scope_profile_timer_named_thread& = delete;

private:
    uint32_t index_;
};

// ============================================================================
// Global accessor and macros
// ============================================================================

auto get_app_profiler() -> performance_profiler*;

#define APP_SCOPE_PERF_CONCATENATE_DETAIL(x, y) x##y
#define APP_SCOPE_PERF_CONCATENATE(x, y) APP_SCOPE_PERF_CONCATENATE_DETAIL(x, y)
#define APP_SCOPE_PERF_UNIQUE_VAR(prefix) APP_SCOPE_PERF_CONCATENATE(prefix, __LINE__)

/// @brief Create a scoped performance timer that records to the timeline profiler.
/// Only accepts string literals to ensure pointer lifetime safety.
#define APP_SCOPE_PERF(name_literal) \
    const ::unravel::scope_profile_timer APP_SCOPE_PERF_UNIQUE_VAR(timer)(name_literal)

/// @brief Create a scoped performance timer that records to the timeline profiler.
/// Only accepts string literals to ensure pointer lifetime safety.
#define APP_SCOPE_PERF_OWNED(name) \
    const ::unravel::scope_profile_timer_owned APP_SCOPE_PERF_UNIQUE_VAR(timer)(name)

/// @brief Scoped scope on a named thread lane (thread_name used only on first profiler use this thread).
#define APP_SCOPE_PERF_THREAD(scope_name_literal, thread_name_literal)                                           \
    const ::unravel::scope_profile_timer_named_thread APP_SCOPE_PERF_UNIQUE_VAR(timer_thread)(scope_name_literal, \
                                                                                              thread_name_literal)

} // namespace unravel
