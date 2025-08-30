#include "composite_action.h"

namespace unravel
{

void composite_action_t::add_sub_action(std::shared_ptr<editing_action_t> action)
{
    if (action)
    {
        sub_actions.push_back(std::move(action));
    }
}

void composite_action_t::do_action()
{
    for (auto& action : sub_actions)
    {
        if (action)
        {
            action->do_action();
        }
    }
}

void composite_action_t::undo_action()
{
    // Undo in reverse order
    for (auto it = sub_actions.rbegin(); it != sub_actions.rend(); ++it)
    {
        if (*it)
        {
            (*it)->undo_action();
        }
    }
}

auto composite_action_t::is_valid() const -> bool
{
    for (auto& action : sub_actions)
    {
        if (!action->is_valid())
        {
            return false;
        }
    }
    return true;
}

auto composite_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    // Default: composite actions with same type can try to merge their sub-actions
    const auto& prev_composite_action = static_cast<const composite_action_t&>(previous);

    if(sub_actions.size() != prev_composite_action.sub_actions.size())
    {
        return false;
    }

    bool all_sub_actions_can_merge = true;
    for (size_t i = 0; i < sub_actions.size() && i < prev_composite_action.sub_actions.size(); ++i)
    {
        if (sub_actions[i] && prev_composite_action.sub_actions[i] &&
            sub_actions[i]->get_meta_type() == prev_composite_action.sub_actions[i]->get_meta_type())
        {
            if(!sub_actions[i]->is_mergeable(*prev_composite_action.sub_actions[i]))
            {
                all_sub_actions_can_merge = false;
                break;
            }
        }
    }

    return all_sub_actions_can_merge;
}

void composite_action_t::merge_with(const editing_action_t& previous)
{
    // Default: no merging for composite actions (derived classes should implement)

    const auto& prev_composite_action = static_cast<const composite_action_t&>(previous);
    
    // Merge corresponding sub-actions
    for (size_t i = 0; i < sub_actions.size() && i < prev_composite_action.sub_actions.size(); ++i)
    {
        if (sub_actions[i] && prev_composite_action.sub_actions[i] &&
            sub_actions[i]->get_meta_type() == prev_composite_action.sub_actions[i]->get_meta_type())
        {
            sub_actions[i]->merge_with(*prev_composite_action.sub_actions[i]);
        }
    }
}

void composite_action_t::draw_in_inspector(rtti::context& ctx)
{
    for (auto& action : sub_actions)
    {
        action->draw_in_inspector(ctx);
    }
}

} // namespace unravel
