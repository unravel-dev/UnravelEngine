#pragma once
#include "seq_action.h"
#include <set>
#include <unordered_map>

/**
 * @namespace seq
 * @brief Provides a sequence-based action management framework.
 */
namespace seq
{

/**
 * @struct seq_manager
 * @brief Manages and coordinates multiple sequence actions with scoping, pausing, and updating capabilities.
 */
struct seq_manager
{
    /**
     * @struct seq_info
     * @brief Stores information about an action, including its state, depth, and associated scopes.
     */
    struct seq_info
    {
        /**
         * @brief The action being managed.
         */
        seq_action action;

        /**
         * @brief The depth of the action in a sequence hierarchy.
         */
        uint32_t depth = 0;

        /**
         * @brief The scopes associated with the action.
         */
        std::vector<std::string> scopes;
    };

    /**
     * @brief Alias for the collection of actions managed by the seq_manager.
     */
    using action_collection_t = std::map<seq_id_t, seq_info>;

    /**
     * @brief Starts a new action and associates it with the specified scope policy.
     * @param action The action to start.
     * @param scope_policy The scoping policy for the action (default is empty).
     * @return The unique ID of the started action.
     */
    seq_id_t start(seq_action action, const seq_scope_policy& scope_policy = {});

    /**
     * @brief Stops the action associated with the specified ID.
     * @param id The ID of the action to stop.
     */
    void stop(seq_id_t id);

    /**
     * @brief Stops all actions within the specified scope.
     * @param scope The name of the scope (default is all scopes).
     */
    void stop_all(const std::string& scope = {});

    /**
     * @brief Pauses the action associated with the specified ID.
     * @param id The ID of the action to pause.
     */
    void pause(seq_id_t id);

    /**
     * @brief Pauses all actions within the specified scope and key.
     * @param scope The name of the scope (default is all scopes).
     * @param key The key associated with the actions (default is all keys).
     */
    void pause_all(const std::string& scope = {}, const std::string& key = {});

    /**
     * @brief Resumes the action associated with the specified ID.
     * @param id The ID of the action to resume.
     */
    void resume(seq_id_t id);

    /**
     * @brief Resumes all actions within the specified scope and key.
     * @param scope The name of the scope (default is all scopes).
     * @param key The key associated with the actions (default is all keys).
     */
    void resume_all(const std::string& scope = {}, const std::string& key = {});

    /**
     * @brief Marks an action to stop when it finishes.
     * @param id The ID of the action to mark.
     */
    void stop_when_finished(seq_id_t id);

    /**
     * @brief Marks all actions in the specified scope to stop when they finish.
     * @param scope The name of the scope (default is all scopes).
     */
    void stop_when_finished_all(const std::string& scope = {});

    /**
     * @brief Stops an action and ensures it completes after the specified duration.
     * @param id The ID of the action to stop.
     * @param finish_after The duration to wait before stopping (default is 0ms).
     */
    void stop_and_finish(seq_id_t id, duration_t finish_after = 0ms);

    /**
     * @brief Stops all actions in the specified scope and ensures they complete.
     * @param scope The name of the scope (default is all scopes).
     */
    void stop_and_finish_all(const std::string& scope = {});

    /**
     * @brief Checks if an action is stopping.
     * @param id The ID of the action to check.
     * @return True if the action is stopping, false otherwise.
     */
    auto is_stopping(seq_id_t id) const -> bool;

    /**
     * @brief Checks if an action is running.
     * @param id The ID of the action to check.
     * @return True if the action is running, false otherwise.
     */
    auto is_running(seq_id_t id) const -> bool;

    /**
     * @brief Checks if an action is paused.
     * @param id The ID of the action to check.
     * @return True if the action is paused, false otherwise.
     */
    auto is_paused(seq_id_t id) const -> bool;

    /**
     * @brief Checks if an action has finished.
     * @param id The ID of the action to check.
     * @return True if the action has finished, false otherwise.
     */
    auto is_finished(seq_id_t id) const -> bool;

    /**
     * @brief Checks if there is any action associated with the specified scope ID.
     * @param scope_id The scope ID to check.
     * @return True if an action exists within the scope, false otherwise.
     */
    bool has_action_with_scope(const std::string& scope_id);

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
     * @brief Gets the elapsed time of an action.
     * @param id The ID of the action.
     * @return The elapsed duration.
     */
    auto get_elapsed(seq_id_t id) const -> duration_t;

    /**
     * @brief Gets the total duration of an action.
     * @param id The ID of the action.
     * @return The total duration.
     */
    auto get_duration(seq_id_t id) const -> duration_t;

    /**
     * @brief Gets the overflow time of an action.
     * @param id The ID of the action.
     * @return The overflow duration.
     */
    auto get_overflow(seq_id_t id) const -> duration_t;

    /**
     * @brief Updates the elapsed time of a specific action.
     * @param id The ID of the action.
     * @param delta The time delta to apply.
     */
    void update(seq_id_t id, duration_t delta);

    /**
     * @brief Sets the elapsed time of an action (use with caution).
     * @param id The ID of the action.
     * @param elapsed The elapsed time to set.
     */
    void set_elapsed(seq_id_t id, duration_t elapsed);

    /**
     * @brief Updates all managed actions with a time delta.
     * @param delta The time delta to apply.
     */
    void update(duration_t delta);

    /**
     * @brief Pushes a scope onto the scope stack.
     * @param scope The name of the scope to push.
     */
    void push_scope(const std::string& scope);

    /**
     * @brief Closes a scope, removing it from the stack.
     * @param scope The name of the scope to close.
     */
    void close_scope(const std::string& scope);

    /**
     * @brief Pops the current scope from the scope stack.
     */
    void pop_scope();

    /**
     * @brief Clears all scopes from the scope stack.
     */
    void clear_scopes();

    /**
     * @brief Gets the current scope from the scope stack.
     * @return The name of the current scope.
     */
    auto get_current_scope() const -> const std::string&;

    /**
     * @brief Gets the list of all active scopes.
     * @return A reference to the list of active scopes.
     */
    auto get_scopes() const -> const std::vector<std::string>&;

    /**
     * @brief Gets the collection of all managed actions.
     * @return A reference to the collection of managed actions.
     */
    auto get_actions() const -> const action_collection_t&;

private:
    /**
     * @brief Starts a specific action.
     * @param info The information about the action to start.
     */
    void start_action(seq_info& info);

    /**
     * @brief Gets the IDs of all managed actions.
     * @return A vector of action IDs.
     */
    auto get_ids() const -> std::vector<seq_id_t>;
    /**
     * @brief The stack of active scopes.
     */
    std::vector<std::string> scopes_;

    /**
     * @brief The index of the current scope.
     */
    int32_t current_scope_idx_ = -1;

    /**
     * @brief The set of paused scopes with associated keys.
     */
    std::set<std::pair<std::string, std::string>> paused_scopes_;

    /**
     * @brief The collection of active actions.
     */
    action_collection_t actions_;

    /**
     * @brief The collection of actions pending execution.
     */
    action_collection_t pending_actions_;
};

} // namespace seq
