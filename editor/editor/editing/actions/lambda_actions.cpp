#include "lambda_actions.h"

namespace unravel
{

// Untracked Action Implementation
untracked_action_t::untracked_action_t(action_t act)
    : action(std::move(act))
{
}

void untracked_action_t::do_action()
{
    if (action)
    {
        action();
    }
}

void untracked_action_t::undo_action()
{
    // Non-undoable actions do nothing on undo
}

auto untracked_action_t::is_undoable() const -> bool
{
    return false; // This action type is not undoable
}

// Tracked Lambda Action Implementation
tracked_lambda_action_t::tracked_lambda_action_t(action_t do_action, action_t undo_action)
    : do_func(std::move(do_action)), undo_func(std::move(undo_action))
{
}

void tracked_lambda_action_t::do_action()
{
    if (do_func)
    {
        do_func();
    }
}

void tracked_lambda_action_t::undo_action()
{
    if (undo_func)
    {
        undo_func();
    }
}

auto tracked_lambda_action_t::is_undoable() const -> bool
{
    return true; // This action type is undoable
}

} // namespace unravel
