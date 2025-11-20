#pragma once
#include <engine/engine_export.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <array>
#include <string>
#include <type_traits>
#include <algorithm>
#include <hpp/string_view.hpp>

namespace unravel
{

/// @brief Concept to ensure only string literals are accepted
/// @details A string literal is a const char array with fixed size known at compile time
template<typename T>
concept string_literal = std::is_array_v<std::remove_reference_t<T>> && 
                         std::is_same_v<std::remove_extent_t<std::remove_reference_t<T>>, const char>;

//-----------------------------------------------------------------------------
/// @brief Class for collecting and managing time-series sample data
/// @details Maintains a rolling buffer of samples with automatic statistics calculation
//-----------------------------------------------------------------------------
class sample_data
{
public:
    static constexpr uint32_t num_samples = 500;

    //-----------------------------------------------------------------------------
    /// @brief Constructor that initializes all samples to zero
    //-----------------------------------------------------------------------------
    sample_data()
    {
        reset(0.0f);
    }

    //-----------------------------------------------------------------------------
    /// @brief Reset all samples to a specific value
    /// @param value The value to reset all samples to
    //-----------------------------------------------------------------------------
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

    //-----------------------------------------------------------------------------
    /// @brief Add a new sample to the rolling buffer
    /// @details Automatically updates min, max, and average statistics using O(1) incremental updates
    /// Uses index tracking to maintain min/max without scanning
    /// @param value The new sample value
    //-----------------------------------------------------------------------------
    auto push_sample(float value) -> void
    {
        if(smart_init_samples_ > 0 && offset_ > smart_init_samples_)
        {
            reset(value);
            smart_init_samples_ = -1;
            return;
        }
        
        // Get the old value and index that will be replaced
        const float old_value = values_[offset_];
        const int32_t current_index = offset_;
        
        // Update sum incrementally (remove old, add new)
        sum_ = sum_ - old_value + value;
        
        // Store new value
        values_[offset_] = value;
        offset_ = (offset_ + 1) % static_cast<int32_t>(num_samples);
        
        // Update average from sum
        average_ = sum_ / static_cast<float>(num_samples);
        
        // Update min tracking
        if(min_index_ == current_index)
        {
            // We're replacing the current min, need to find new min
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
            // New value is the new minimum
            min_ = value;
            min_index_ = current_index;
        }
        
        // Update max tracking
        if(max_index_ == current_index)
        {
            // We're replacing the current max, need to find new max
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
            // New value is the new maximum
            max_ = value;
            max_index_ = current_index;
        }
    }

    //-----------------------------------------------------------------------------
    /// @brief Get the raw sample values array
    /// @return Pointer to the internal sample array
    //-----------------------------------------------------------------------------
    auto get_values() const -> const float*
    {
        return values_.data();
    }

    //-----------------------------------------------------------------------------
    /// @brief Get the current offset in the rolling buffer
    /// @return The current offset position
    //-----------------------------------------------------------------------------
    auto get_offset() const -> int32_t
    {
        return offset_;
    }

    //-----------------------------------------------------------------------------
    /// @brief Get the minimum value in the current sample set
    /// @return The minimum sample value
    //-----------------------------------------------------------------------------
    auto get_min() const -> float
    {
        return min_;
    }

    //-----------------------------------------------------------------------------
    /// @brief Get the maximum value in the current sample set
    /// @return The maximum sample value
    //-----------------------------------------------------------------------------
    auto get_max() const -> float
    {
        return max_;
    }

    //-----------------------------------------------------------------------------
    /// @brief Get the average value of the current sample set
    /// @return The average sample value
    //-----------------------------------------------------------------------------
    auto get_average() const -> float
    {
        return average_;
    }

private:
    static constexpr int32_t smart_init_samples_count = 20;
    
    int32_t offset_{0};
    std::array<float, num_samples> values_{};
    float min_{0.0f};
    float max_{0.0f};
    float average_{0.0f};
    float sum_{0.0f};        // Running sum for O(1) average calculation
    int32_t min_index_{0};   // Index of current minimum value
    int32_t max_index_{0};   // Index of current maximum value
    int32_t smart_init_samples_{-1};
};

class performance_profiler
{
public:
    struct per_frame_data
    {
        float time_since_swap = 0.0f;   // Time since last swap (current frame accumulation)
        uint32_t samples_since_swap = 0; // Number of samples since last swap (current frame)
        sample_data history;             // Rolling buffer of historical samples

        per_frame_data() = default;
        per_frame_data(float t) : time_since_swap(t), samples_since_swap(1)
        {
            history.push_sample(t);
        }

        operator float() const
        {
            return time_since_swap;
        }
        
        auto operator+=(float t) -> per_frame_data&
        {
            time_since_swap += t;
            return *this;
        }

        //-----------------------------------------------------------------------------
        /// @brief Get time accumulated since last swap (current frame)
        //-----------------------------------------------------------------------------
        auto get_time_since_swap() const -> float
        {
            return time_since_swap;
        }

        //-----------------------------------------------------------------------------
        /// @brief Get sample count since last swap (current frame)
        //-----------------------------------------------------------------------------
        auto get_samples_since_swap() const -> uint32_t
        {
            return samples_since_swap;
        }

        //-----------------------------------------------------------------------------
        /// @brief Get average time from historical samples
        //-----------------------------------------------------------------------------
        auto get_avg() const -> float
        {
            return history.get_average();
        }

        //-----------------------------------------------------------------------------
        /// @brief Get minimum time from historical samples
        //-----------------------------------------------------------------------------
        auto get_min() const -> float
        {
            return history.get_min();
        }

        //-----------------------------------------------------------------------------
        /// @brief Get maximum time from historical samples
        //-----------------------------------------------------------------------------
        auto get_max() const -> float
        {
            return history.get_max();
        }

        //-----------------------------------------------------------------------------
        /// @brief Get the rolling buffer of historical samples
        //-----------------------------------------------------------------------------
        auto get_history() const -> const sample_data&
        {
            return history;
        }

        //-----------------------------------------------------------------------------
        /// @brief Add a new time sample to current frame accumulation
        //-----------------------------------------------------------------------------
        void add_sample(float t)
        {
            time_since_swap += t;
            samples_since_swap++;
        }

        //-----------------------------------------------------------------------------
        /// @brief Reset current frame statistics and push accumulated time to history
        //-----------------------------------------------------------------------------
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
        // current_ = get_next_index();
        
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
        return per_frame_data_[current_];
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
