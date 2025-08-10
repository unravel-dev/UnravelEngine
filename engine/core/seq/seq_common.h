#pragma once
#include <chrono>
#include <functional>
#include <hpp/sentinel.hpp>
#include <hpp/type_name.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;

/**
 * @namespace seq
 * @brief Provides a sequence management framework with utilities for time management, interpolation, and inspection.
 */
namespace seq
{

/**
 * @typedef clock_t
 * @brief Alias for the steady clock used for timing actions.
 */
using clock_t = std::chrono::steady_clock;

/**
 * @typedef timepoint_t
 * @brief Represents a point in time using the steady clock.
 */
using timepoint_t = clock_t::time_point;

/**
 * @typedef duraiton_secs_t
 * @brief Represents a duration in seconds as a floating-point value.
 */
using duraiton_secs_t = std::chrono::duration<float>;

/**
 * @typedef duration_t
 * @brief Represents a duration in nanoseconds.
 */
using duration_t = std::chrono::nanoseconds;

/**
 * @typedef sentinel_t
 * @brief Alias for a sentinel object used for lifecycle management.
 */
using sentinel_t = hpp::sentinel;

/**
 * @typedef seq_id_t
 * @brief Represents a unique identifier for sequence actions.
 */
using seq_id_t = size_t;

/**
 * @typedef ease_t
 * @brief Represents an easing function for interpolation.
 * @param t A normalized time value (0 to 1).
 * @return The eased value.
 */
using ease_t = std::function<float(float)>;

/**
 * @typedef interpolate_t
 * @brief Represents a function for interpolating values.
 * @tparam T The type of value to interpolate.
 * @param start The starting value.
 * @param end The ending value.
 * @param t A normalized time value (0 to 1).
 * @param ease The easing function to apply.
 * @return The interpolated value.
 */
template<typename T>
using interpolate_t = std::function<T(const T&, const T&, float, const ease_t&)>;

/**
 * @enum state_t
 * @brief Represents the state of a sequence action.
 */
enum class state_t
{
    running,  ///< The action is running.
    paused,   ///< The action is paused.
    finished  ///< The action has finished.
};

/**
 * @struct seq_scope_policy
 * @brief Defines policies for scoping actions in a sequence.
 */
struct seq_scope_policy
{
    /**
     * @enum policy_t
     * @brief Represents the policy for scoping actions.
     */
    enum class policy_t
    {
        stacked,     ///< Actions share the same scope and stack behavior.
        independent  ///< Actions operate independently within their scope.
    };

    /**
     * @brief The name of the scope.
     */
    std::string scope;

    /**
     * @brief The scoping policy (default is stacked).
     */
    policy_t policy = policy_t::stacked;

    /**
     * @brief Default constructor.
     */
    seq_scope_policy() noexcept = default;

    /**
     * @brief Constructs a scope policy with a specified scope.
     * @param _scope The name of the scope.
     */
    seq_scope_policy(const char* _scope) noexcept : scope(_scope)
    {
    }

    /**
     * @brief Constructs a scope policy with a specified scope.
     * @param _scope The name of the scope as a string.
     */
    seq_scope_policy(const std::string& _scope) noexcept : scope(_scope)
    {
    }
};

/**
 * @struct seq_inspect_info
 * @brief Contains information for inspecting and debugging sequence actions.
 */
struct seq_inspect_info
{
    /// Shared pointer type.
    using ptr = std::shared_ptr<seq_inspect_info>;

           /// Weak pointer type.
    using weak_ptr = std::weak_ptr<seq_inspect_info>;

    /**
     * @brief The file name where the action was defined.
     */
    std::string file_name;

    /**
     * @brief The function name where the action was defined.
     */
    std::string function_name;

    /**
     * @brief The line number where the action was defined.
     */
    uint32_t line_number = 0;

    /**
     * @brief The column offset where the action was defined.
     */
    uint32_t column_offset = 0;

    /**
     * @brief The unique ID of the action.
     */
    seq_id_t id = 1;

    /**
     * @brief The speed multiplier for the action.
     */
    float speed_multiplier = 1.0f;

    /**
     * @brief Whether the action should stop when finished.
     */
    bool stop_when_finished = false;

    /**
     * @brief The current state of the action.
     */
    std::string state = "finished";

    /**
     * @brief The type of modification applied to the action.
     */
    std::string modified_type;

    /**
     * @brief The type of updater function used by the action.
     */
    std::string updater_type;

    /**
     * @brief The elapsed duration of the action.
     */
    duration_t elapsed{};

    /**
     * @brief The total duration of the action.
     */
    duration_t duration{};

    /**
     * @brief The progress of the action (0 to 1).
     */
    float progress{};

    /**
     * @brief The current value of the action.
     */
    std::string current_value;

    /**
     * @brief The beginning value of the action.
     */
    std::string begin_value;

    /**
     * @brief The ending value of the action.
     */
    std::string end_value;

    /**
     * @brief The easing function applied to the action.
     */
    ease_t ease_func;

    /**
     * @brief The child actions of this action.
     */
    std::vector<weak_ptr> children;
};

/**
 * @namespace inspector
 * @brief Provides utilities for inspecting and converting sequence-related types to strings.
 */
namespace inspector
{
namespace adl_helper
{
using std::to_string;

/**
 * @brief Converts a value to a string.
 * @tparam T The type of the value.
 * @param t The value to convert.
 * @return The string representation of the value.
 */
template<typename T>
auto as_string(const T& t) -> std::string
{
    return to_string(t);
}

/**
 * @brief Gets the type of a value as a string.
 * @tparam T The type of the value.
 * @param t The value.
 * @return The string representation of the type.
 */
template<typename T>
auto type_as_string(const T&) -> std::string
{
    return hpp::type_name_unqualified_str<T>();
}
} // namespace adl_helper

/**
 * @brief Converts a value to a string using ADL (Argument-Dependent Lookup).
 * @tparam T The type of the value.
 * @param t The value to convert.
 * @return The string representation of the value.
 */
template<typename T>
auto to_str(const T& t) -> std::string
{
    return adl_helper::as_string(t);
}

/**
 * @brief Specialization for converting state_t to a string.
 * @param t The state to convert.
 * @return The string representation of the state.
 */
template<>
inline auto to_str(const state_t& t) -> std::string
{
    if (t == state_t::running)
    {
        return "running";
    }
    if (t == state_t::paused)
    {
        return "paused";
    }
    return "finished";
}

/**
 * @brief Gets the type of a value as a string using ADL.
 * @tparam T The type of the value.
 * @param t The value.
 * @return The string representation of the type.
 */
template<typename T>
auto type_to_str(const T& t) -> std::string
{
    return adl_helper::type_as_string(t);
}

} // namespace inspector

} // namespace seq
