#pragma once
#include "seq_core.h"
#include "seq_manager.h"
#include <hpp/source_location.hpp>

/**
 * @namespace seq
 * @brief Provides a sequence-based action management system for controlling and scheduling actions.
 */
namespace seq
{
/**
 * @brief Starts a new action.
 * @param action The action to be started.
 * @param scope_policy The policy for the scope of the action (default is empty).
 * @param location The source location where this function is called (default is the current location).
 * @return A unique ID representing the started action.
 */
auto start(seq_action action,
           const seq_scope_policy& scope_policy = {},
           hpp::source_location location = hpp::source_location::current()) -> seq_id_t;

/**
 * @brief Stops the action associated with the given ID.
 * @param id The ID of the action to stop.
 */
void stop(seq_id_t id);

/**
 * @brief Pauses the action associated with the given ID.
 * @param id The ID of the action to pause.
 */
void pause(seq_id_t id);

/**
 * @brief Resumes the action associated with the given ID.
 * @param id The ID of the action to resume.
 */
void resume(seq_id_t id);

/**
 * @brief Marks the action to stop when it finishes.
 * @param id The ID of the action to modify.
 */
void stop_when_finished(seq_id_t id);

/**
 * @brief Stops the action after a specified duration.
 * @param id The ID of the action to stop.
 * @param finish_after The duration to wait before stopping (default is 0ms).
 */
void stop_and_finish(seq_id_t id, duration_t finish_after = 0ms);

/**
 * @brief Updates the state of all actions with a time delta.
 * @param delta The time delta to update with.
 */
void update(duration_t delta);

/**
 * @brief Updates the state of all actions with a time delta in seconds.
 * @param delta The time delta in seconds.
 */
void update(duraiton_secs_t delta);

/**
 * @brief Shuts down the action management system, stopping all actions.
 */
void shutdown();

/**
 * @brief Checks if the action is stopping.
 * @param id The ID of the action to check.
 * @return True if the action is stopping, false otherwise.
 */
auto is_stopping(seq_id_t id) -> bool;

/**
 * @brief Checks if the action is running.
 * @param id The ID of the action to check.
 * @return True if the action is running, false otherwise.
 */
auto is_running(seq_id_t id) -> bool;

/**
 * @brief Checks if the action is paused.
 * @param id The ID of the action to check.
 * @return True if the action is paused, false otherwise.
 */
auto is_paused(seq_id_t id) -> bool;

/**
 * @brief Checks if the action has finished.
 * @param id The ID of the action to check.
 * @return True if the action has finished, false otherwise.
 */
auto is_finished(seq_id_t id) -> bool;

/**
 * @brief Checks if there is an action associated with the given scope ID.
 * @param scope_id The scope ID to check.
 * @return True if an action exists with the given scope ID, false otherwise.
 */
auto has_action_with_scope(const std::string& scope_id) -> bool;

/**
 * @brief Sets the speed multiplier for an action.
 * @param id The ID of the action.
 * @param speed_multiplier The speed multiplier to set (default is 1.0f).
 */
void set_speed_multiplier(seq_id_t id, float speed_multiplier = 1.0f);

/**
 * @brief Gets the speed multiplier of an action.
 * @param id The ID of the action.
 * @return The current speed multiplier.
 */
auto get_speed_multiplier(seq_id_t id) -> float;

/**
 * @brief Gets the elapsed duration of an action.
 * @param id The ID of the action.
 * @return The elapsed duration.
 */
auto get_elapsed(seq_id_t id) -> duration_t;

/**
 * @brief Sets the elapsed duration of an action.
 * @param id The ID of the action.
 * @param duration The elapsed duration to set.
 */
void set_elapsed(seq_id_t id, duration_t duration);

/**
 * @brief Updates the elapsed duration of a specific action.
 * @param id The ID of the action.
 * @param duration The duration to update with.
 */
void update(seq_id_t id, duration_t duration);

/**
 * @brief Gets the total duration of an action.
 * @param id The ID of the action.
 * @return The total duration.
 */
auto get_duration(seq_id_t id) -> duration_t;

/**
 * @brief Gets the overflow duration of an action.
 * @param id The ID of the action.
 * @return The overflow duration.
 */
auto get_overflow(seq_id_t id) -> duration_t;

/**
 * @brief Gets the percentage completion of an action.
 * @param id The ID of the action.
 * @return The percentage completion as a float.
 */
auto get_percent(seq_id_t id) -> float;

namespace manager
{
/**
 * @brief Pushes a sequence manager to the stack.
 * @param mgr The manager to push.
 */
void push(seq_manager& mgr);

/**
 * @brief Pops the top sequence manager from the stack.
 */
void pop();
} // namespace manager

namespace scope
{
/**
 * @brief Pushes a new scope to the stack.
 * @param scope The name of the scope to push.
 */
void push(const std::string& scope);

/**
 * @brief Closes the specified scope.
 * @param scope The name of the scope to close.
 */
void close(const std::string& scope);

/**
 * @brief Pops the current scope from the stack.
 */
void pop();

/**
 * @brief Clears all scopes from the stack.
 */
void clear();

/**
 * @brief Gets the name of the current scope.
 * @return The name of the current scope.
 */
auto get_current() -> const std::string&;

/**
 * @brief Stops all actions within the specified scope.
 * @param scope The name of the scope.
 */
void stop_all(const std::string& scope);

/**
 * @brief Pauses all actions within the specified scope.
 * @param scope The name of the scope.
 */
void pause_all(const std::string& scope);

/**
 * @brief Resumes all actions within the specified scope.
 * @param scope The name of the scope.
 */
void resume_all(const std::string& scope);

/**
 * @brief Pauses all actions within the specified scope and key.
 * @param scope The name of the scope.
 * @param key The key associated with the actions.
 */
void pause_all(const std::string& scope, const std::string& key);

/**
 * @brief Resumes all actions within the specified scope and key.
 * @param scope The name of the scope.
 * @param key The key associated with the actions.
 */
void resume_all(const std::string& scope, const std::string& key);

/**
 * @brief Stops and finishes all actions within the specified scope.
 * @param scope The name of the scope.
 */
void stop_and_finish_all(const std::string& scope);

/**
 * @brief Marks all actions within the specified scope to stop when they finish.
 * @param scope The name of the scope.
 */
void stop_when_finished_all(const std::string& scope);
} // namespace scope

} // namespace seq
