#include "undo_redo_panel.h"
#include "../panel.h"

#include <editor/editing/editing_manager.h>
#include <editor/shortcuts.h>

#include <imgui/imgui.h>
#include <array>

namespace unravel
{

undo_redo_panel::undo_redo_panel(imgui_panels* parent) : parent_(parent)
{
}

void undo_redo_panel::show(bool show)
{
    visible_ = show;
}

void undo_redo_panel::on_frame_ui_render(rtti::context& ctx)
{
    if (!visible_)
    {
        return;
    }

    if (ImGui::Begin("Undo History", &visible_))
    {
        auto& editing_mgr = ctx.get_cached<editing_manager>();
        const auto& undo_stack = editing_mgr.undo_stack;
        

        auto undo = editing_mgr.can_undo() ? "Undo*" : "Undo";
        auto redo = editing_mgr.can_redo() ? "Redo*" : "Redo";
        // Action buttons
        if (ImGui::Button(undo) && editing_mgr.can_undo())
        {
            editing_mgr.undo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button(redo) && editing_mgr.can_redo())
        {
            editing_mgr.redo();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Clear Stack"))
        {
            editing_mgr.undo_stack.clear();
        }
        
        ImGui::Separator();

        if (undo_stack.actions.empty())
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(No actions in stack)");
        }
        else
        {
            // Add option to jump to latest state (after all actions) - at top since it's most recent
            bool is_at_latest_state = (undo_stack.current_index == undo_stack.actions.size());
            ImGui::PushStyleColor(ImGuiCol_Text, is_at_latest_state ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            
            if (ImGui::Selectable(is_at_latest_state ? ICON_MDI_ARROW_RIGHT " [Latest State]" : ICON_MDI_FAST_FORWARD " [Latest State]", is_at_latest_state))
            {
                // Redo everything to get to latest state
                while (editing_mgr.can_redo())
                {
                    editing_mgr.redo();
                }
            }
            
            if (ImGui::IsItemHovered())
            {
                ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, 800.0f));
                ImGui::BeginTooltip();
                ImGui::Text("Latest State (all actions executed)");
                if (!is_at_latest_state)
                {
                    size_t redo_steps = undo_stack.actions.size() - undo_stack.current_index;
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), 
                                     "Click to redo %zu step%s to latest state", 
                                     redo_steps, 
                                     redo_steps == 1 ? "" : "s");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "← Current Position");
                }
                ImGui::EndTooltip();
            }
            
            ImGui::PopStyleColor();
            
            // Draw separator
            ImGui::Separator();
            
            // Draw the stack with visual indicators (in reverse order - most recent first)
            for (size_t idx = 0; idx < undo_stack.actions.size(); ++idx)
            {
                // Reverse the index to draw from newest to oldest
                size_t i = undo_stack.actions.size() - 1 - idx;
                const auto& action = undo_stack.actions[i];
                
                // Determine the status of this action
                bool is_executed = i < undo_stack.current_index;
                bool is_current = (i == undo_stack.current_index - 1) && undo_stack.current_index > 0;
                
                // Only check validity for actions adjacent to current position
                bool is_adjacent_to_current = (i == undo_stack.current_index - 1) || (i == undo_stack.current_index);
                bool is_invalid = is_adjacent_to_current && !action->is_valid();
                
                // Choose colors based on status
                ImVec4 text_color;
                const char* status_icon = "";

                if(is_invalid)
                {
                    text_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red for invalid
                    status_icon = ICON_MDI_ALERT " ";
                }
                else
                {
                    if (is_current)
                    {
                        text_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green for current
                        status_icon = ICON_MDI_ARROW_RIGHT " ";
                    }
                    else if (is_executed)
                    {
                        text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White for executed
                        status_icon = ICON_MDI_CHECK " ";
                    }
                    else
                    {
                        text_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray for future
                        status_icon = ICON_MDI_CLOCK_OUTLINE " ";
                    }
                }
                // Make the action clickable/selectable
                ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                
                // Use Selectable to make it clickable with hover effects
                bool is_selected = is_current;
        
                auto selectable_label = fmt::format("{} [{}] {}", status_icon, i, action->get_name());
                
                if (ImGui::Selectable(selectable_label.c_str(), is_selected))
                {
                    // Calculate target position: clicking on action i means we want current_index to be i+1
                    size_t target_index = i + 1;
                    
                    // Execute undo/redo operations to reach the target position
                    while (undo_stack.current_index != target_index)
                    {
                        if (undo_stack.current_index > target_index)
                        {
                            // Need to undo
                            if (editing_mgr.can_undo())
                            {
                                editing_mgr.undo();
                            }
                            else
                            {
                                break; // Safety break
                            }
                        }
                        else
                        {
                            // Need to redo
                            if (editing_mgr.can_redo())
                            {
                                editing_mgr.redo();
                            }
                            else
                            {
                                break; // Safety break
                            }
                        }
                    }
                }
                
                ImGui::PopStyleColor();
                
                // Add tooltip with additional info and click instructions
                if (ImGui::IsItemHovered())
                {   
                    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, 800.0f));
                    ImGui::BeginTooltip();
                    if(is_invalid)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Invalid Action due to missing dependencies.");
                    }
                    else
                    {
                        ImGui::Text("Action: %s", action->get_name().c_str());
                        ImGui::Text("Status: %s", is_executed ? "Executed" : "Not Executed");
                        ImGui::Text("Undoable: %s", action->is_undoable() ? "Yes" : "No");

                        action->draw_in_inspector(ctx);
                    }
                    // Add click instruction based on current state
                    size_t target_index = i + 1;
                    if (target_index != undo_stack.current_index)
                    {
                        ImGui::Separator();
                        if (target_index < undo_stack.current_index)
                        {
                            size_t undo_steps = undo_stack.current_index - target_index;
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), 
                                             "Click to undo %zu step%s", 
                                             undo_steps, 
                                             undo_steps == 1 ? "" : "s");
                        }
                        else
                        {
                            size_t redo_steps = target_index - undo_stack.current_index;
                            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), 
                                             "Click to redo %zu step%s", 
                                             redo_steps, 
                                             redo_steps == 1 ? "" : "s");
                        }
                    }
                    else if (is_current)
                    {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s Current Position", ICON_MDI_ARROW_LEFT);
                    }
                    
                    ImGui::EndTooltip();
                }
            }
            
            // Draw separator
            ImGui::Separator();
            
            // Add option to jump to initial state (before any actions) - at bottom since it's oldest
            bool is_at_initial_state = (undo_stack.current_index == 0);
            ImGui::PushStyleColor(ImGuiCol_Text, is_at_initial_state ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            
            if (ImGui::Selectable(is_at_initial_state ? ICON_MDI_ARROW_RIGHT " [Initial State]" : ICON_MDI_RESTORE " [Initial State]", is_at_initial_state))
            {
                // Undo everything to get back to initial state
                while (editing_mgr.can_undo())
                {
                    editing_mgr.undo();
                }
            }
            
            if (ImGui::IsItemHovered())
            {
                ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, 800.0f));
                ImGui::BeginTooltip();
                ImGui::Text("Initial State (no actions executed)");
                if (!is_at_initial_state)
                {
                    size_t undo_steps = undo_stack.current_index;
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), 
                                     "Click to undo %zu step%s to initial state", 
                                     undo_steps, 
                                     undo_steps == 1 ? "" : "s");
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s Current Position", ICON_MDI_ARROW_LEFT);
                }
                ImGui::EndTooltip();
            }
            
            ImGui::PopStyleColor();
            
            // Add visual indicator for the current position
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "%s",
                               fmt::format("Current Position: {} / {}", undo_stack.current_index, undo_stack.actions.size()).c_str());
           
            
            // Progress bar showing position in stack
            if (!undo_stack.actions.empty())
            {
                float progress = static_cast<float>(undo_stack.current_index) / static_cast<float>(undo_stack.actions.size());
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");
            }
        }
    }
    ImGui::End();
}

} // namespace unravel
