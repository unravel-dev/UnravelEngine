#include "inspector_particle_emitter.h"
#include "editor/hub/panels/inspector_panel/inspectors/inspector.h"
#include "inspectors.h"

namespace unravel
{
namespace
{

/// Status colors: stopped, paused, playing, and the warning of a full emitter.
const ImVec4 k_stopped_color(0.8f, 0.4f, 0.4f, 1.0f);
const ImVec4 k_paused_color(0.8f, 0.8f, 0.4f, 1.0f);
const ImVec4 k_playing_color(0.4f, 0.8f, 0.4f, 1.0f);
const ImVec4 k_full_color(0.9f, 0.6f, 0.3f, 1.0f);

/// Whether the playback buttons drive the emitters in the entity's children too. One choice for every emitter
/// inspected: an effect is usually a root emitter with its parts below it.
bool g_with_child_emitters = true;

/// A playback button: disabled when it would do nothing. True when it was pressed.
auto draw_playback_button(const char* label, bool is_enabled, const char* tooltip) -> bool
{
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tooltip);
    ImGui::SameLine();
    return is_pressed;
}

void draw_status(const particle_emitter_component& data)
{
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    if(!data.is_playing())
    {
        ImGui::TextColored(k_stopped_color, "Stopped");
    }
    else if(data.is_paused())
    {
        ImGui::TextColored(k_paused_color, "Paused");
    }
    else
    {
        ImGui::TextColored(k_playing_color, "Playing");
    }
    // Live particles against the capacity: a full emitter skips the particles it cannot hold.
    const uint32_t live = data.get_num_particles();
    const uint32_t capacity = data.get_max_particles();
    ImGui::SameLine();
    ImGui::Text("Particles: %u / %u", live, capacity);
    if(capacity > 0 && live >= capacity)
    {
        ImGui::SameLine();
        ImGui::TextColored(k_full_color, "(full)");
        ImGui::SetItemTooltip("The emitter is at its capacity and skips new particles; raise Max Particles or lower the rates.");
    }
}

/// Play, Pause / Resume, Stop and Clear in one row, on this emitter and, with the toggle, its child emitters.
auto draw_playback(particle_emitter_component& data) -> bool
{
    const bool is_playing = data.is_playing();
    const bool is_paused = data.is_paused();
    const bool with_children = g_with_child_emitters;
    bool is_changed = false;
    if(draw_playback_button("Play", !is_playing || is_paused, "Start emitting, from the start of the cycle when stopped."))
    {
        data.play();
        if(with_children)
        {
            data.play_child_emitters();
        }
        is_changed = true;
    }
    if(is_paused)
    {
        if(draw_playback_button("Resume", true, "Continue from where the particles froze."))
        {
            data.resume();
            if(with_children)
            {
                data.resume_child_emitters();
            }
            is_changed = true;
        }
    }
    else if(draw_playback_button("Pause", is_playing, "Freeze the particles where they are; they stay visible."))
    {
        data.pause();
        if(with_children)
        {
            data.pause_child_emitters();
        }
        is_changed = true;
    }
    if(draw_playback_button("Stop", is_playing, "Stop emitting; the particles alive live out their lives."))
    {
        data.stop();
        if(with_children)
        {
            data.stop_child_emitters();
        }
        is_changed = true;
    }
    if(draw_playback_button("Clear", true, "Stop emitting and remove every particle at once."))
    {
        data.stop_and_reset();
        if(with_children)
        {
            data.stop_and_reset_child_emitters();
        }
        is_changed = true;
    }
    ImGui::Checkbox("With Child Emitters", &g_with_child_emitters);
    ImGui::SetItemTooltip("The buttons drive every emitter in the entity's children too (the hierarchy below it; "
                          "sub emitters fire on their own).");
    return is_changed;
}

} // namespace

auto inspector_particle_emitter_component::inspect(rtti::context& ctx,
                                                   entt::meta_any& var,
                                                   const meta_any_proxy& var_proxy,
                                                   const var_info& info,
                                                   const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result;
    auto& data = var.cast<particle_emitter_component&>();
    draw_status(data);
    result.changed |= draw_playback(data);
    ImGui::Spacing();
    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);
    return result;
}
} // namespace unravel
