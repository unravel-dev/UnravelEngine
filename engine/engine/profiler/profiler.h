#pragma once
#include <engine/engine_export.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <array>
#include <string>
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
        float time = 0.0f;              // Total accumulated time
        float min = 0.0f;               // Minimum time recorded
        float max = 0.0f;               // Maximum time recorded
        uint32_t samples = 0;           // Number of samples
        uint32_t samples_since_swap = 0; // Number of samples since last swap

        per_frame_data() = default;
        per_frame_data(float t) : time(t), min(t), max(t), samples(1), samples_since_swap(1)
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


        auto get_time() const -> float
        {
            return time;
        }

        /// @brief Get average time per sample
        auto get_avg() const -> float
        {
            return samples > 0 ? time / static_cast<float>(samples) : 0.0f;
        }

        /// @brief Get minimum time
        auto get_min() const -> float
        {
            return min;
        }

        /// @brief Get maximum time
        auto get_max() const -> float
        {
            return max;
        }

        /// @brief Get total time
        auto get_total() const -> float
        {
            return time;
        }

        /// @brief Get total sample count
        auto get_samples() const -> uint32_t
        {
            return samples;
        }

        /// @brief Get sample count since last swap (current frame)
        auto get_samples_since_swap() const -> uint32_t
        {
            return samples_since_swap;
        }

        /// @brief Add a new time sample and update statistics
        void add_sample(float t)
        {
            time += t;
            samples++;
            samples_since_swap++;
            
            // Update min/max
            if(samples == 1)
            {
                min = t;
                max = t;
            }
            else
            {
                if(t < min)
                {
                    min = t;
                }
                if(t > max)
                {
                    max = t;
                }
            }
        }

        /// @brief Reset all statistics to zero
        void reset()
        {
            time = 0.0f;

            samples_since_swap = 0;
        }
    };

    // Use std::string as key with std::less<> for transparent lookup
    // This allows lookup by string_view without allocation
    using record_data_t = std::map<std::string, per_frame_data, std::less<>>;

    /// @brief Add performance record with any string type
    /// @details Accepts string literals, std::string, string_view, etc.
    /// Uses transparent lookup to avoid allocations when the key already exists
    /// @param name Name for the performance record (any string-like type)
    /// @param time Time value in milliseconds
    void add_record(hpp::string_view name, float time)
    {
        add_record_internal(name, time);
    }

    void swap()
    {
        current_ = get_next_index();
        
        // Reset statistics for all entries but keep the keys
        // This avoids map reallocation and maintains the same profiling entries
        auto& per_frame = get_per_frame_data_write();
        for(auto& [name, data] : per_frame)
        {
            data.reset();
        }
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
    /// @brief Internal add_record implementation
    /// @details Uses transparent lookup with string_view to avoid allocations when key exists
    /// Only allocates std::string when inserting a new key
    void add_record_internal(hpp::string_view name, float time)
    {
        auto& per_frame = get_per_frame_data_write();

        // Use find for transparent lookup (avoids allocation if key exists)
        auto it = per_frame.find(name);
        if(it != per_frame.end())
        {
            // Key exists, update statistics without allocation
            it->second.add_sample(time);
        }
        else
        {
            // Key doesn't exist, insert new entry (allocates std::string)
            per_frame[std::string(name)] = per_frame_data(time);
        }
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
#define APP_SCOPE_PERF(name) const ::unravel::scope_perf_timer APP_SCOPE_PERF_UNIQUE_VAR(timer)(name, ::unravel::get_app_profiler())

} // namespace unravel
