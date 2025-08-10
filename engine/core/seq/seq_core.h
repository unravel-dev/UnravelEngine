#pragma once
#include "seq_common.h"
#include "seq_ease.h"
#include "seq_private.h"
#include <vector>

/**
 * @namespace seq
 * @brief Provides functions for creating and managing sequence-based animations and transitions.
 */
namespace seq
{

/**
 * @brief Creates an action to change an object from one value to another over a specified duration.
 * @tparam T The type of the object being modified.
 * @param object The object to modify.
 * @param begin The starting value.
 * @param end The ending value.
 * @param duration The duration of the change.
 * @param sentinel A sentinel for managing the action lifecycle.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_from_to(T& object,
                    const std::decay_t<T>& begin,
                    const std::decay_t<T>& end,
                    const duration_t& duration,
                    const sentinel_t& sentinel,
                    const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates an action to change a shared object from one value to another over a specified duration.
 * @tparam T The type of the shared object being modified.
 * @param object The shared object to modify.
 * @param begin The starting value.
 * @param end The ending value.
 * @param duration The duration of the change.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_from_to(const std::shared_ptr<T>& object,
                    const std::decay_t<T>& begin,
                    const std::decay_t<T>& end,
                    const duration_t& duration,
                    const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates an action to change an object to a specified value over a specified duration.
 * @tparam T The type of the object being modified.
 * @param object The object to modify.
 * @param end The target value.
 * @param duration The duration of the change.
 * @param sentinel A sentinel for managing the action lifecycle.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_to(T& object,
               const std::decay_t<T>& end,
               const duration_t& duration,
               const sentinel_t& sentinel,
               const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates an action to change a shared object to a specified value over a specified duration.
 * @tparam T The type of the shared object being modified.
 * @param object The shared object to modify.
 * @param end The target value.
 * @param duration The duration of the change.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_to(const std::shared_ptr<T>& object,
               const std::decay_t<T>& end,
               const duration_t& duration,
               const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates an action to change an object by a specified amount over a specified duration.
 * @tparam T The type of the object being modified.
 * @param object The object to modify.
 * @param amount The amount to change by.
 * @param duration The duration of the change.
 * @param sentinel A sentinel for managing the action lifecycle.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_by(T& object,
               const std::decay_t<T>& amount,
               const duration_t& duration,
               const sentinel_t& sentinel,
               const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates an action to change a shared object by a specified amount over a specified duration.
 * @tparam T The type of the shared object being modified.
 * @param object The shared object to modify.
 * @param amount The amount to change by.
 * @param duration The duration of the change.
 * @param ease_func The easing function to apply (default is linear easing).
 * @return A seq_action representing the change operation.
 */
template<typename T>
auto change_by(const std::shared_ptr<T>& object,
               const std::decay_t<T>& amount,
               const duration_t& duration,
               const ease_t& ease_func = ease::linear) -> seq_action;

/**
 * @brief Creates a sequential action that executes a list of actions one after another.
 * @param actions The list of actions to execute sequentially.
 * @param sentinel A sentinel for managing the action lifecycle (default is eternal).
 * @return A seq_action representing the sequence.
 */
auto sequence(const std::vector<seq_action>& actions, const sentinel_t& sentinel = hpp::eternal_sentinel())
    -> seq_action;

/**
 * @brief Creates a precise sequential action that executes a list of actions with exact timing.
 * @param actions The list of actions to execute sequentially.
 * @param sentinel A sentinel for managing the action lifecycle (default is eternal).
 * @return A seq_action representing the precise sequence.
 */
auto sequence_precise(const std::vector<seq_action>& actions, const sentinel_t& sentinel = hpp::eternal_sentinel())
    -> seq_action;

/**
 * @brief Creates a sequential action with two or more actions.
 * @tparam Args Additional actions.
 * @param t1 The first action.
 * @param t2 The second action.
 * @param actions Additional actions to include in the sequence.
 * @return A seq_action representing the sequence.
 */
template<typename... Args>
auto sequence(const seq_action& t1, const seq_action& t2, Args&&... actions) -> seq_action
{
    return sequence(std::vector<seq_action>{t1, t2, std::forward<Args>(actions)...});
}

/**
 * @brief Creates a precise sequential action with two or more actions.
 * @tparam Args Additional actions.
 * @param t1 The first action.
 * @param t2 The second action.
 * @param actions Additional actions to include in the sequence.
 * @return A seq_action representing the precise sequence.
 */
template<typename... Args>
auto sequence_precise(const seq_action& t1, const seq_action& t2, Args&&... actions) -> seq_action
{
    return sequence_precise(std::vector<seq_action>{t1, t2, std::forward<Args>(actions)...});
}

/**
 * @brief Creates a simultaneous action that executes a list of actions together.
 * @param actions The list of actions to execute together.
 * @param sentinel A sentinel for managing the action lifecycle (default is eternal).
 * @return A seq_action representing the simultaneous action.
 */
auto together(const std::vector<seq_action>& actions, const sentinel_t& sentinel = hpp::eternal_sentinel())
    -> seq_action;

/**
 * @brief Creates a simultaneous action with two or more actions.
 * @tparam Args Additional actions.
 * @param t1 The first action.
 * @param t2 The second action.
 * @param actions Additional actions to include in the simultaneous action.
 * @return A seq_action representing the simultaneous action.
 */
template<typename... Args>
auto together(const seq_action& t1, const seq_action& t2, Args&&... actions) -> seq_action
{
    return together(std::vector<seq_action>{t1, t2, std::forward<Args>(actions)...});
}

/**
 * @brief Creates a delay action.
 * @param duration The duration of the delay.
 * @param sentinel A sentinel for managing the action lifecycle (default is eternal).
 * @return A seq_action representing the delay.
 */
auto delay(const duration_t& duration, const sentinel_t& sentinel = hpp::eternal_sentinel()) -> seq_action;

/**
 * @brief Repeats an action a specified number of times.
 * @param action The action to repeat.
 * @param times The number of times to repeat the action (default is infinite if 0).
 * @return A seq_action representing the repeated action.
 */
auto repeat(const seq_action& action, size_t times = 0) -> seq_action;

/**
 * @brief Repeats an action a specified number of times with a sentinel.
 * @param action The action to repeat.
 * @param sentinel A sentinel for managing the action lifecycle.
 * @param times The number of times to repeat the action (default is infinite if 0).
 * @return A seq_action representing the repeated action.
 */
auto repeat(const seq_action& action, const sentinel_t& sentinel, size_t times = 0) -> seq_action;

/**
 * @brief Precisely repeats an action a specified number of times.
 * @param action The action to repeat.
 * @param times The number of times to repeat the action (default is infinite if 0).
 * @return A seq_action representing the precise repeated action.
 */
auto repeat_precise(const seq_action& action, size_t times = 0) -> seq_action;

/**
 * @brief Precisely repeats an action a specified number of times with a sentinel.
 * @param action The action to repeat.
 * @param sentinel A sentinel for managing the action lifecycle.
 * @param times The number of times to repeat the action (default is infinite if 0).
 * @return A seq_action representing the precise repeated action.
 */
auto repeat_precise(const seq_action& action, const sentinel_t& sentinel, size_t times = 0) -> seq_action;

} // namespace seq

#include "seq_core.hpp"
