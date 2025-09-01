#include "undo_redo_stack.h"
#include "editor/imgui/integration/imgui_notify.h"

namespace unravel
{

void undo_redo_stack::push_if_undoable(std::shared_ptr<editing_action_t> action)
{
    // Only add to undo stack if the action is undoable
    if (action && action->is_undoable())
    {
        // Remove any actions after current_index (handles branching undo/redo history)
        if (current_index < actions.size())
        {
            actions.resize(current_index);
        }
        
        // Check if we can merge with the last action
        if (!actions.empty() && current_index > 0)
        {
            auto& last_action = actions[current_index - 1];

            auto type = action->get_meta_type();
            auto last_type = last_action->get_meta_type();  
            
            // Common merge checks: same type, valid operation_id, and same operation
            bool can_merge = last_action &&
                           (type == last_type) &&
                           action->merge_key  != 0 &&
                           action->merge_key == last_action->merge_key &&
                           action->is_mergeable(*last_action);
            
            if (can_merge)
            {
                // Merge the new action with the last one
                action->merge_with(*last_action);
                // Replace the last action with the merged one
                actions[current_index - 1] = std::move(action);
                return; // Don't increment current_index since we replaced, not added
            }
        }
        
        actions.emplace_back(std::move(action));
        current_index = actions.size(); // Point to the newly added action
    }
}

auto undo_redo_stack::can_undo() const -> bool 
{ 
    return current_index > 0 && !actions.empty(); 
}

auto undo_redo_stack::can_redo() const -> bool 
{ 
    return current_index < actions.size(); 
}

void undo_redo_stack::undo()
{
    if (can_undo())
    {
        current_index--;
        if (current_index < actions.size() && actions[current_index])
        {
            auto& action = actions[current_index];

            if(action->is_valid())
            {
                action->execution_count++;
                action->undo_action();
            }
            else
            {
                ImGui::PushNotification(ImGuiToast(ImGuiToastType_Warning, 1000,"Unable to Undo.\nMissing references."));
            }
            
        }
    }
}

void undo_redo_stack::redo()
{
    if (can_redo())
    {
        if (current_index < actions.size() && actions[current_index])
        {
            auto& action = actions[current_index];
            if(action->is_valid())
            {
                action->execution_count++;
                action->do_action();
            }
            else
            {
                ImGui::PushNotification(ImGuiToast(	ImGuiToastType_Warning, 1000,"Unable to Redo.\nMissing references."));
            }
        }
        current_index++;
    }
}

void undo_redo_stack::clear()
{
    actions.clear();
    current_index = 0;
}

} // namespace unravel
