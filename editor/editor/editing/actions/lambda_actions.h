#pragma once

#include "editing_action.h"
#include <functional>

namespace unravel
{

// Non-undoable lambda action
struct untracked_action_t : crtp_meta_type<untracked_action_t, editing_action_t>
{
    using action_t = std::function<void()>;
    action_t action{};

    untracked_action_t(action_t act);

    void do_action() override;
    void undo_action() override;
    auto is_undoable() const -> bool override;
};

// Non-undoable lambda action that is not a scene edit: it changes editor state - opens a project
// or a prefab, reloads scripts, selects, starts a rename, moves the editor camera - or writes a
// file, and leaves the content being edited alone. Running it marks neither the scene nor the
// prefab being edited as unsaved and does not rebuild reflection probes.
struct untracked_editor_state_action_t : untracked_action_t
{
    using untracked_action_t::untracked_action_t;

    auto modifies_scene_content() const -> bool override { return false; }
};

// Undoable lambda action with separate do/undo functions
struct tracked_lambda_action_t : crtp_meta_type<tracked_lambda_action_t, editing_action_t>
{
    using action_t = std::function<void()>;
    action_t do_func{};
    action_t undo_func{};

    tracked_lambda_action_t(action_t do_action, action_t undo_action);

    void do_action() override;
    void undo_action() override;
    auto is_undoable() const -> bool override;
};

} // namespace unravel
