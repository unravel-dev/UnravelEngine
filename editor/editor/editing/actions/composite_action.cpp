#include "composite_action.h"

namespace unravel
{

void composite_action_t::add_sub_action(std::shared_ptr<editing_action_t> action)
{
    if (action)
    {

        if(!name.empty())
        {
            name += "/";
        }
       
        name += action->get_name();

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

void sequence_action_t::add_step(std::function<std::shared_ptr<editing_action_t>()> factory)
{
    if(factory)
    {
        step_factories.push_back(std::move(factory));
    }
}

void sequence_action_t::do_action()
{
    if(!first_run_done)
    {
        // First execution: invoke factories in order. Each factory runs AFTER the preceding
        // sub-action has executed, so it can reference handles/values produced upstream.
        for(auto& factory : step_factories)
        {
            if(!factory)
            {
                continue;
            }
            auto action = factory();
            if(!action)
            {
                continue;
            }
            action->do_action();
            sub_actions.push_back(std::move(action));
        }
        step_factories.clear();
        first_run_done = true;
        return;
    }

    // Redo path: replay captured sub-actions in order. Each sub-action owns its own
    // first-vs-subsequent logic (e.g. create_entities_action_t restores from snapshot).
    for(auto& action : sub_actions)
    {
        if(action)
        {
            action->do_action();
        }
    }
}

void sequence_action_t::undo_action()
{
    // Undo in reverse order so later steps are unwound before earlier ones.
    for(auto it = sub_actions.rbegin(); it != sub_actions.rend(); ++it)
    {
        if(*it)
        {
            (*it)->undo_action();
        }
    }
}

auto sequence_action_t::is_valid() const -> bool
{
    if(!first_run_done)
    {
        return !step_factories.empty();
    }
    if(sub_actions.empty())
    {
        return false;
    }
    for(const auto& action : sub_actions)
    {
        if(!action || !action->is_valid())
        {
            return false;
        }
    }
    return true;
}

auto sequence_action_t::is_mergeable(const editing_action_t& /*previous*/) const -> bool
{
    return false;
}

void sequence_action_t::draw_in_inspector(rtti::context& ctx)
{
    for(auto& action : sub_actions)
    {
        if(action)
        {
            action->draw_in_inspector(ctx);
        }
    }
}

} // namespace unravel
