#include "inspector_particle_emitter.h"
#include "editor/hub/panels/inspector_panel/inspectors/inspector.h"
#include "inspectors.h"

namespace unravel
{
auto inspector_particle_emitter_component::inspect(rtti::context& ctx,
                                                   entt::meta_any& var,
                                                   const meta_any_proxy& var_proxy,
                                                   const var_info& info,
                                                   const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result;
    auto& data = var.cast<particle_emitter_component&>();
    

    // Current state display
    const bool is_playing = data.is_playing();
    const bool is_paused = data.is_paused();
    
    // Status text
    ImGui::Text("Status: ");
    ImGui::SameLine();
    if(!is_playing)
    {
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Stopped");
    }
    else if(is_paused)
    {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "Paused");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Playing");
    }
    
    ImGui::Spacing();
    
    // Control buttons
    
    // Play button
    if(!is_playing || is_paused)
    {
        if(ImGui::Button("Play"))
        {
            data.play();
            result.changed = true;
        }

        ImGui::SetItemTooltip("Start particle emission and simulation");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Play");
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    // Pause/Resume button
    if(is_playing && !is_paused)
    {
        if(ImGui::Button("Pause"))
        {
            data.pause();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Pause particle simulation (particles remain visible)");
    }
    else if(is_playing && is_paused)
    {
        if(ImGui::Button("Resume"))
        {
            data.resume();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Resume particle simulation from paused state");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Pause");
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    // Stop button
    if(is_playing)
    {
        if(ImGui::Button("Stop"))
        {
            data.stop();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Stop emission and clear all particles");

        ImGui::SameLine();
        if(ImGui::Button("Stop and Reset"))
        {
            data.stop_and_reset();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Stop emission and clear all particles");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Stop");
        ImGui::SameLine();
        ImGui::Button("Stop and Reset");
        ImGui::EndDisabled();
    }




    if(!is_playing || is_paused)
    {
        if(ImGui::Button("Play(S)"))
        {
            data.play();
            data.play_sub_emitters();
            result.changed = true;
        }

        ImGui::SetItemTooltip("Start particle emission and simulation (with sub emitters)");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Play(S)");
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    // Pause/Resume button
    if(is_playing && !is_paused)
    {
        if(ImGui::Button("Pause(S)"))
        {
            data.pause();
            data.pause_sub_emitters();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Pause particle simulation (particles remain visible) (with sub emitters)");
    }
    else if(is_playing && is_paused)
    {
        if(ImGui::Button("Resume(S)"))
        {
            data.resume();
            data.resume_sub_emitters();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Resume particle simulation from paused state (with sub emitters)");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Pause(S)");
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    // Stop button
    if(is_playing)
    {
        if(ImGui::Button("Stop(S)"))
        {
            data.stop();
            data.stop_sub_emitters();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Stop emission and clear all particles (with sub emitters)");

        ImGui::SameLine();
        if(ImGui::Button("Stop and Reset(S)"))
        {
            data.stop_and_reset();
            data.stop_and_reset_sub_emitters();
            result.changed = true;
        }
        ImGui::SetItemTooltip("Stop emission and clear all particles (with sub emitters)");
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Stop(S)");
        ImGui::SameLine();
        ImGui::Button("Stop and Reset(S)");
        ImGui::EndDisabled();
    }

    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);
    return result;
}
} // namespace unravel
