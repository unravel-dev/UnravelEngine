#pragma once
#include "seq_action.h"

/**
 * @namespace seq
 * @brief Provides a sequence-based action management framework.
 */
namespace seq
{

/**
 * @struct seq_private
 * @brief Provides internal utilities for managing `seq_action` objects. These methods allow for direct control over
 * actions' state, progression, and other properties.
 */
struct seq_private
{
    /**
     * @brief Starts a given action.
     * @param action The action to start.
     */
    static void start(seq_action& action)
    {
        action.start();
    }

    /**
     * @brief Stops a given action.
     * @param action The action to stop.
     */
    static void stop(seq_action& action)
    {
        action.stop();
    }

    /**
     * @brief Marks a given action as stopped and finished.
     * @param action The action to mark.
     */
    static void stop_and_finished(seq_action& action)
    {
        action.stop_and_finished_ = true;
    }

    /**
     * @brief Marks a given action to stop when finished.
     * @param action The action to mark.
     */
    static void stop_when_finished(seq_action& action)
    {
        action.stop_when_finished_ = true;
    }

    /**
     * @brief Resumes a given action, optionally forcing the resume.
     * @param action The action to resume.
     * @param force If true, forces the action to resume (default is false).
     */
    static void resume(seq_action& action, bool force = false)
    {
        action.resume({}, force);
    }

    /**
     * @brief Resumes a given action with a specific key.
     * @param action The action to resume.
     * @param key The key associated with the action.
     */
    static void resume(seq_action& action, const std::string& key)
    {
        action.resume(key);
    }

    /**
     * @brief Pauses a given action.
     * @param action The action to pause.
     */
    static void pause(seq_action& action)
    {
        action.pause();
    }

    /**
     * @brief Pauses a given action with a specific key.
     * @param action The action to pause.
     * @param key The key associated with the action.
     */
    static void pause(seq_action& action, const std::string& key)
    {
        action.pause(key);
    }

    /**
     * @brief Forcibly pauses a given action.
     * @param action The action to forcibly pause.
     */
    static void pause_forced(seq_action& action)
    {
        action.pause_forced();
    }

    /**
     * @brief Forcibly pauses a given action with a specific key.
     * @param action The action to forcibly pause.
     * @param key The key associated with the action.
     */
    static void pause_forced(seq_action& action, const std::string& key)
    {
        action.pause_forced(key);
    }

    /**
     * @brief Sets the speed multiplier for a given action.
     * @param action The action to modify.
     * @param speed_multiplier The speed multiplier to set (clamped between 0.0f and 100.0f).
     */
    static void set_speed_multiplier(seq_action& action, float speed_multiplier)
    {
        speed_multiplier = std::min<float>(100.0f, speed_multiplier);
        speed_multiplier = std::max<float>(0.0f, speed_multiplier);
        action.speed_multiplier_ = speed_multiplier;
    }

    /**
     * @brief Gets the speed multiplier of a given action.
     * @param action The action to query.
     * @return The current speed multiplier of the action.
     */
    static auto get_speed_multiplier(const seq_action& action) -> float
    {
        return action.speed_multiplier_;
    }

    /**
     * @brief Gets the current state of a given action.
     * @param action The action to query.
     * @return The current state of the action.
     */
    static auto get_state(const seq_action& action) -> state_t
    {
        return action.state_;
    }

    /**
     * @brief Checks if the action is requested to stop when finished.
     * @param action The action to query.
     * @return True if the action is requested to stop when finished, false otherwise.
     */
    static auto is_stop_when_finished_requested(const seq_action& action) -> bool
    {
        return action.stop_when_finished_;
    }

    /**
     * @brief Checks if the action is requested to stop and finish.
     * @param action The action to query.
     * @return True if the action is requested to stop and finish, false otherwise.
     */
    static auto is_stop_and_finished_requested(const seq_action& action) -> bool
    {
        return action.stop_and_finished_;
    }

    /**
     * @brief Checks if the action is currently running.
     * @param action The action to query.
     * @return True if the action is running, false otherwise.
     */
    static auto is_running(const seq_action& action) -> bool
    {
        return action.state_ == state_t::running;
    }

    /**
     * @brief Checks if the action is currently paused.
     * @param action The action to query.
     * @return True if the action is paused, false otherwise.
     */
    static auto is_paused(const seq_action& action) -> bool
    {
        return action.state_ == state_t::paused;
    }

    /**
     * @brief Checks if the action is finished.
     * @param action The action to query.
     * @return True if the action is finished, false otherwise.
     */
    static auto is_finished(const seq_action& action) -> bool
    {
        return action.state_ == state_t::finished;
    }

    /**
     * @brief Updates a given action with a time delta.
     * @param action The action to update.
     * @param delta The time delta to apply.
     * @return The current state of the action after the update.
     */
    static auto update(seq_action& action, duration_t delta) -> state_t
    {
        return action.update(delta);
    }

    /**
     * @brief Sets the elapsed time of a given action.
     * @param self The action to modify.
     * @param elapsed The elapsed time to set.
     */
    static void set_elapsed(seq_action& self, duration_t elapsed)
    {
        self.set_elapsed(elapsed);
    }

    /**
     * @brief Updates the elapsed time of a given action.
     * @param self The action to modify.
     * @param update_time The time to add to the elapsed duration.
     */
    static void update_elapsed(seq_action& self, duration_t update_time)
    {
        self.update_elapsed(update_time);
    }

    /**
     * @brief Gets the elapsed time of a given action.
     * @param self The action to query.
     * @return The elapsed time of the action.
     */
    static auto get_elapsed(const seq_action& self) -> duration_t
    {
        return self.elapsed_;
    }

    /**
     * @brief Gets the total duration of a given action.
     * @param self The action to query.
     * @return The total duration of the action.
     */
    static auto get_duration(const seq_action& self) -> duration_t
    {
        return self.duration_;
    }

    /**
     * @brief Computes the overflow duration of a given action if it exceeds its total duration.
     * @param self The action to query.
     * @return The overflow duration, or zero if the action has not overflowed.
     */
    static auto get_overflow(const seq_action& self) -> duration_t
    {
        auto duration = get_duration(self);
        auto elapsed_not_clamped = self.elapsed_not_clamped_;

        if(duration > duration_t::zero() && duration <= elapsed_not_clamped)
        {
            return elapsed_not_clamped - duration;
        }

        return {};
    }
};

} // namespace seq
