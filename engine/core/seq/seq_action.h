#pragma once
#include "seq_common.h"
#include <hpp/event.hpp>

/**
 * @namespace seq
 * @brief Provides a sequence-based action management system for defining, updating, and managing actions.
 */
namespace seq
{

/**
 * @struct seq_action
 * @brief Represents an action within the sequence management system. Contains lifecycle events and management
 * functions.
 */
struct seq_action
{
    friend struct seq_private;

    /**
     * @brief Type alias for an updater function that defines the action's behavior.
     * @param delta The time delta since the last update.
     * @param action Reference to the current action.
     * @return The current state of the action.
     */
    using updater_t = std::function<state_t(duration_t, seq_action&)>;

    /**
     * @brief Type alias for a creator function that generates an updater function for the action.
     * @return An updater function.
     */
    using creator_t = std::function<updater_t()>;

    seq_action() = default;
    ~seq_action() = default;

    /**
     * @brief Constructs a seq_action.
     * @param creator A function that creates an updater for the action.
     * @param duration The total duration of the action.
     * @param sentinel A sentinel value for internal state management.
     */
    seq_action(creator_t&& creator, duration_t duration, sentinel_t sentinel);

    seq_action(const seq_action&) = default;
    seq_action(seq_action&&) = default;

    auto operator=(const seq_action&) -> seq_action& = default;
    auto operator=(seq_action&&) -> seq_action& = default;

    //---------------------------------------------------------------------------------------
    /// @brief Event emitted when the action is started.
    //---------------------------------------------------------------------------------------
    hpp::event<void()> on_begin;

    //---------------------------------------------------------------------------------------
    /// @brief Event emitted every time the action value is updated.
    //---------------------------------------------------------------------------------------
    hpp::event<void()> on_step;

    //---------------------------------------------------------------------------------------
    /// @brief Event emitted every frame while the action is running.
    //---------------------------------------------------------------------------------------
    hpp::event<void()> on_update;

    //---------------------------------------------------------------------------------------
    /// @brief Event emitted when the action is finished. Not emitted if the action is stopped prematurely.
    //---------------------------------------------------------------------------------------
    hpp::event<void()> on_end;

    /**
     * @brief Gets a pointer to the action's inspection information.
     */
    seq_inspect_info::ptr info;

    /**
     * @brief Gets the unique ID of the action.
     * @return The unique ID of the action.
     */
    auto get_id() const -> seq_id_t;

    /**
     * @brief Converts the action to its unique ID.
     * @return The unique ID of the action.
     */
    operator seq_id_t() const;

    /**
     * @brief Checks if the action is valid.
     * @return True if the action is valid, false otherwise.
     */
    auto is_valid() const noexcept -> bool;

private:
    /**
     * @brief Updates the elapsed time of the action.
     * @param update_time The time to add to the elapsed duration.
     */
    void update_elapsed(duration_t update_time);

    /**
     * @brief Sets the elapsed duration of the action.
     * @param elapsed The elapsed time to set.
     */
    void set_elapsed(duration_t elapsed);

    /**
     * @brief Clamps the elapsed duration to the total duration of the action.
     */
    void clamp_elapsed();

    /**
     * @brief Starts the action, initializing its state and triggering the on_begin event.
     */
    void start();

    /**
     * @brief Stops the action, marking it as stopped.
     */
    void stop();

    /**
     * @brief Resumes the action.
     * @param key Optional key for managing pause states.
     * @param force If true, forces the action to resume.
     */
    void resume(const std::string& key = {}, bool force = false);

    /**
     * @brief Pauses the action.
     * @param key Optional key for managing pause states.
     */
    void pause(const std::string& key = {});

    /**
     * @brief Forcibly pauses the action with a specific key.
     * @param key The key used to forcibly pause the action.
     */
    void pause_forced(const std::string& key);

    /**
     * @brief Forcibly pauses the action without a key.
     */
    void pause_forced();

    /**
     * @brief Updates the state of the action with the given time delta.
     * @param delta The time delta to update with.
     * @return The current state of the action.
     */
    auto update(duration_t delta) -> state_t;

    /// Unique ID of the action.
    seq_id_t id_{};

    /// Creator function for generating the updater.
    creator_t creator_;

    /// Updater function for controlling the action's behavior.
    updater_t updater_;

    /// Key used to manage pause states.
    std::string pause_key_;

    /// Current state of the action.
    state_t state_ = state_t::finished;

    /// Time elapsed for the action.
    duration_t elapsed_{};

    /// Elapsed time without clamping to the total duration.
    duration_t elapsed_not_clamped_{};

    /// Total duration of the action.
    duration_t duration_{};

    /// Sentinel value for internal management.
    sentinel_t sentinel_;

    /// Indicates if the action should stop and finish after completing its current step.
    bool stop_and_finished_ = false;

    /// Indicates if the action should stop when finished.
    bool stop_when_finished_ = false;

    /// Speed multiplier for the action's progression.
    float speed_multiplier_ = 1.0f;
};

} // namespace seq
