#include "composite_action.h"

#include <algorithm>

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

    // Every pair must be the same kind of edit: a rename followed by a deactivation of the same
    // count is two steps, not one.
    for (size_t i = 0; i < sub_actions.size(); ++i)
    {
        const auto& current = sub_actions[i];
        const auto& prior = prev_composite_action.sub_actions[i];
        if (!current || !prior || current->get_meta_type() != prior->get_meta_type() ||
            !current->is_mergeable(*prior))
        {
            return false;
        }
    }
    return true;
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
    // first-vs-subsequent logic (e.g. create_entities_action_t restores from snapshot), and
    // is checked at its turn: what it acts on may exist again only once the steps before it ran.
    for(auto& action : sub_actions)
    {
        if(action && action->is_valid())
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
        if(*it && (*it)->is_valid())
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
    // A later step may act on an entity an earlier one creates, which is gone while the chain is
    // undone: the chain is valid when its first step is, and the others are checked at their turn.
    return !sub_actions.empty() && sub_actions.front() && sub_actions.front()->is_valid();
}

auto sequence_action_t::is_mergeable(const editing_action_t& /*previous*/) const -> bool
{
    return false;
}

auto sequence_action_t::is_undoable() const -> bool
{
    return undoable && (!first_run_done || !sub_actions.empty());
}

auto sequence_action_t::modifies_scene_content() const -> bool
{
    if(!first_run_done)
    {
        return true;
    }
    return std::any_of(sub_actions.begin(),
                       sub_actions.end(),
                       [](const std::shared_ptr<editing_action_t>& action)
                       {
                           return action && action->modifies_scene_content();
                       });
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
