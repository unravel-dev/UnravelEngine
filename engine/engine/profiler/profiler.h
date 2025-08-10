#pragma once
#include <engine/engine_export.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <array>
#include <type_traits>
#include <concepts>
#include <hpp/string_view.hpp>

namespace unravel
{

/// @brief Concept to ensure only string literals are accepted
/// @details A string literal is a const char array with fixed size known at compile time
template<typename T>
concept string_literal = std::is_array_v<std::remove_reference_t<T>> && 
                         std::is_same_v<std::remove_extent_t<std::remove_reference_t<T>>, const char>;

class performance_profiler
{
public:
    struct per_frame_data
    {
        float time = 0.0f;
        uint32_t samples = 0;

        per_frame_data() = default;
        per_frame_data(float t) : time(t)
        {
        }

        operator float() const
        {
            return time;
        }
        auto operator+=(float t) -> per_frame_data&
        {
            time += t;
            return *this;
        }
    };

    using record_data_t = std::map<hpp::string_view, per_frame_data>;

    /// @brief Add performance record using string literal only
    /// @details Only accepts string literals to ensure lifetime safety since we store non-owning pointers
    /// @param name String literal name for the performance record
    /// @param time Time value in milliseconds
    template<typename T>
    void add_record(T&& name, float time)
    {
        static_assert(string_literal<T>, 
                     "ERROR: add_record() only accepts string literals for memory safety. "
                     "Use: add_record(\"literal_name\", time) instead of add_record(variable_name, time)");
        add_record_internal(name, time);
    }

    void swap()
    {
        current_ = get_next_index();
        get_per_frame_data_write().clear();
    }

    auto get_per_frame_data_read() const -> const record_data_t&
    {
        return per_frame_data_[get_next_index()];
    }

    auto get_per_frame_data_write() -> record_data_t&
    {
        return per_frame_data_[current_];
    }

private:
    /// @brief Internal add_record for use by scope_perf_timer
    /// @details This is safe to use internally since scope_perf_timer validates string literals at construction
    void add_record_internal(const char* name, float time)
    {
        auto& per_frame = get_per_frame_data_write();

        auto& data = per_frame[name];
        data.time += time;
        data.samples++;
    }

    auto get_next_index() const -> int
    {
        return (current_ + 1) % per_frame_data_.size();
    }

    std::array<record_data_t, 2> per_frame_data_;
    int current_{0};

    // Allow scope_perf_timer to access the internal add_record method
    friend class scope_perf_timer;
};

class scope_perf_timer
{
public:
    using clock_t = std::chrono::high_resolution_clock;
    using timepoint_t = clock_t::time_point;
    using duration_t = std::chrono::duration<float, std::milli>;

    /// @brief Constructor that only accepts string literals for safety
    /// @details Only accepts string literals to ensure lifetime safety since we store non-owning pointers
    /// @param name String literal name for the performance timer
    /// @param profiler Pointer to the performance profiler instance
    template<typename T>
    scope_perf_timer(T&& name, performance_profiler* profiler) 
        : name_(name), profiler_(profiler)
    {
        static_assert(string_literal<T>, 
                     "ERROR: scope_perf_timer only accepts string literals for memory safety. "
                     "Use: scope_perf_timer(\"literal_name\", profiler) instead of scope_perf_timer(variable_name, profiler)");
    }

    ~scope_perf_timer()
    {
        auto end = clock_t::now();
        auto time = std::chrono::duration_cast<duration_t>(end - start_);

        profiler_->add_record_internal(name_, time.count());
    }

private:

    const char* name_;
    performance_profiler* profiler_{};
    timepoint_t start_ = clock_t::now();
};

auto get_app_profiler() -> performance_profiler*;

// Helper macros to concatenate tokens
#define APP_SCOPE_PERF_CONCATENATE_DETAIL(x, y) x##y
#define APP_SCOPE_PERF_CONCATENATE(x, y) APP_SCOPE_PERF_CONCATENATE_DETAIL(x, y)

// Macro to create a unique variable name
#define APP_SCOPE_PERF_UNIQUE_VAR(prefix) APP_SCOPE_PERF_CONCATENATE(prefix, __LINE__)

/// @brief Create a scoped performance timer that only accepts string literals
/// @details This macro creates a performance timer that automatically measures the scope duration.
/// Only string literals are accepted to ensure memory safety since names are stored as non-owning pointers.
/// @param name String literal name for the performance measurement
/// 
/// Example usage:
/// @code
/// void my_function() {
///     APP_SCOPE_PERF("my_function_performance"); // [OK] Valid - string literal
///     // ... function code ...
/// }
/// 
/// // This would cause a compile error with clear message:
/// // const char* name = "dynamic_name";
/// // APP_SCOPE_PERF(name); // [ERROR] Clear static_assert message about string literals
/// @endcode
#define APP_SCOPE_PERF(name) const scope_perf_timer APP_SCOPE_PERF_UNIQUE_VAR(timer)(name, get_app_profiler())

} // namespace unravel
