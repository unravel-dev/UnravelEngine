#include "selection_action.h"
#include "../editing_manager.h"

namespace unravel
{
namespace
{
    auto convert_to_stable(const std::vector<entt::meta_any>& selection) -> std::vector<entt::meta_any>
    {
        std::vector<entt::meta_any> stable_selection;
        for(const auto& sel : selection)
        {
            if(sel.type() == entt::resolve<entt::handle>())
            {
                auto handle = sel.cast<entt::handle>();
                if(handle)
                {
                    stable_selection.emplace_back(entt::make_uhandle(handle));
                }
            }
            else
            {
                stable_selection.emplace_back(sel);
            }
        }
        return stable_selection;
    } 
    
    auto resolve_stable(const std::vector<entt::meta_any>& selection) -> std::vector<entt::meta_any>
    {
        std::vector<entt::meta_any> resolved_selection;
        for(const auto& sel : selection)
        {
            if(sel.type() == entt::resolve<entt::uhandle>())
            {
                auto uhandle = sel.cast<entt::uhandle>();
                auto resolved = uhandle.resolve();
                if(resolved)
                {
                    resolved_selection.emplace_back(resolved);
                }
            }
            else
            {
                resolved_selection.emplace_back(sel);
            }
        }
        return resolved_selection;
    }
}

selection_action_t::selection_action_t(editing_manager* mgr,
                                       const std::vector<entt::meta_any>& old_sel,
                                       const std::vector<entt::meta_any>& new_sel, bool is_select)
    : manager(mgr), old_selection(convert_to_stable(old_sel)), new_selection(convert_to_stable(new_sel)), is_select(is_select)
{
    name = is_select ? "Select" : "Unselect";
    undoable = true;

}

void selection_action_t::do_action()
{
    if (!manager)
    {
        return;
    }

    // Restore the new selection state using the internal method
    manager->restore_selection_impl(resolve_stable(new_selection));
}

void selection_action_t::undo_action()
{
    if (!manager)
    {
        return;
    }

    // Restore the old selection state using the internal method
    manager->restore_selection_impl(resolve_stable(old_selection));
}

auto selection_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& selection_action = static_cast<const selection_action_t&>(previous);
    if(selection_action.is_select != is_select)
    {
        return false;
    }

    // For sequential selection actions (like area picking), merge if:
    // The current action's old_selection matches the previous action's new_selection
    // This means the current action is continuing from where the previous one left off
    
    // Handle empty selections
    if(old_selection.empty() && selection_action.new_selection.empty())
    {
        return true;
    }

    if(old_selection.size() != selection_action.new_selection.size())
    {
        return false;
    }

    // Check if old_selection matches previous new_selection (order-independent comparison)
    // This handles the case where entities are selected in different orders
    for(const auto& old_item : old_selection)
    {
        bool found = false;
        for(const auto& prev_new_item : selection_action.new_selection)
        {
            if(old_item == prev_new_item)
            {
                found = true;
                break;
            }
        }
        if(!found)
        {
            return false;
        }
    }

    return true;
}

void selection_action_t::merge_with(const editing_action_t& previous)
{
    // When merging, keep the old_selection from the previous action
    // and use the new_selection from the current action (which is the latest state)
    const auto& selection_action = static_cast<const selection_action_t&>(previous);
    old_selection = selection_action.old_selection;
    // new_selection stays as is (it's the latest state)
}

auto selection_action_t::is_valid() const -> bool
{
    return manager != nullptr;
}

} // namespace unravel

