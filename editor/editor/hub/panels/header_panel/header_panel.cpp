#include "header_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"

#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/shortcuts.h>
#include <editor/system/project_manager.h>
#include <editor/system/version_manager.h>
#include <editor/assets/asset_watcher.h>

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/threading/threader.h>
#include <array>
#include <exception>
#include <simulation/simulation.h>
#include <version/version.h>

#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <engine/engine.h>


namespace unravel
{

namespace
{
auto get_debug_mode_size() -> float
{
    return 120.0f;
}
void draw_debug_mode()
{
    bool debugger_attached = script_system::is_debugger_attached();
    bool debug_mode = script_system::get_script_debug_mode();
    const char* modes[] = {ICON_MDI_BUG_CHECK " Debug", ICON_MDI_BUG " Release"};
    const char* debug_mode_preview = modes[int(!debug_mode)];
    ImGui::SetNextItemWidth(get_debug_mode_size());

    if(debugger_attached)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    }
    if(ImGui::BeginCombo("###DebugMode", debug_mode_preview))
    {
        if(ImGui::Selectable(modes[0]))
        {
            if(!debug_mode)
            {
                script_system::set_script_debug_mode(true);
                script_system::set_needs_recompile("app", true);
            }
        }
        ImGui::SetItemTooltipEx("%s",
                                "Debug mode enales C# debugging\n"
                                "but reduces C# performance.\n"
                                "Switching to Debug mode will recompile\n"
                                "and reload all scripts.");

        if(ImGui::Selectable(modes[1]))
        {
            if(debug_mode)
            {
                script_system::set_script_debug_mode(false);
                script_system::set_needs_recompile("app", true);
            }
        }
        ImGui::SetItemTooltipEx("%s",
                                "Release mode disables C# debugging\n"
                                "but improves C# performance.\n"
                                "Switching to Release mode will recompile\n"
                                "and reload all scripts.");

        ImGui::EndCombo();
    }

    const char* debug_mode_tooltip = debug_mode ? "Debugger Enabled" : "Debugger Disabled";

    ImGui::SetItemTooltipEx("%s", debug_mode_tooltip);
    if(debugger_attached)
    {
        ImGui::SetItemTooltipEx("%s", "Debugger Attached");
        ImGui::PopStyleColor();
    }
}
} // namespace

header_panel::header_panel(imgui_panels* parent) : parent_(parent)
{
}

void header_panel::draw_menubar_child(rtti::context& ctx)
{
    ImGuiWindowFlags header_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar;
    ImGui::BeginChild("HEADER_menubar", ImVec2(0, ImGui::GetFrameHeight()), false, header_flags);

    // Draw menu bar.
    if(ImGui::BeginMenuBar())
    {
        if(ImGui::BeginMenu("File"))
        {
            if(ImGui::MenuItem("New Scene", ImGui::GetKeyCombinationName(shortcuts::new_scene).c_str()))
            {
                editor_actions::new_scene(ctx);
            }

            if(ImGui::MenuItem("Open Scene", ImGui::GetKeyCombinationName(shortcuts::open_scene).c_str()))
            {
                editor_actions::open_scene(ctx);
            }

            if(ImGui::MenuItem("Save Scene...", ImGui::GetKeyCombinationName(shortcuts::save_scene).c_str()))
            {
                editor_actions::save_scene(ctx);
            }

            if(ImGui::MenuItem("Save Scene As", ImGui::GetKeyCombinationName(shortcuts::save_scene_as).c_str()))
            {
                editor_actions::save_scene_as(ctx);
            }

            if(ImGui::MenuItem("Restart Editor", nullptr))
            {
                editor_actions::restart_editor(ctx);
            }

            if(ImGui::MenuItem("Reload Project", nullptr))
            {
                editor_actions::reload_project(ctx);
            }

            if(ImGui::MenuItem("Close Project", nullptr))
            {
                editor_actions::close_project(ctx);
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Edit"))
        {
            auto& em = ctx.get_cached<editing_manager>();
            auto undo = em.can_undo() ? "Undo*" : "Undo";
            auto redo = em.can_redo() ? "Redo*" : "Redo";
            if(ImGui::MenuItem(undo, ImGui::GetKeyCombinationName(shortcuts::undo).c_str()))
            {
                em.undo();
            }

            if(ImGui::MenuItem(redo, ImGui::GetKeyCombinationName(shortcuts::redo).c_str()))
            {
                em.redo();
            }

            if(ImGui::MenuItem("Undo History", ImGui::GetKeyCombinationName(shortcuts::undo_history).c_str()))
            {
                parent_->get_undo_redo_panel().show(true);
            }

            if(ImGui::MenuItem("Editor Settings..."))
            {
                parent_->get_editor_settings_panel().show(true);
            }

            if(ImGui::MenuItem("Project Settings..."))
            {
                parent_->get_project_settings_panel().show(true, "{}");
            }

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Deploy"))
        {
            if(ImGui::MenuItem("Deploy Project"))
            {
                parent_->get_deploy_panel().show(true);
            }

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Build"))
        {
            if(ImGui::MenuItem(ICON_MDI_REFLECT_HORIZONTAL " Build Reflection Captures"))
            {
                const auto count = editor_actions::rebuild_reflection_probes(ctx, true);
                ImGui::PushNotification(ImGuiToast(ImGuiToastType_Info,
                                                   2000,
                                                   "Rebuilding %zu reflection probe(s)",
                                                   count));
            }
            ImGui::SetItemTooltip(
                "Force all reflection probes in loaded scenes to rebuild their cubemaps.\n"
                "Use after editing environment or static geometry.");

            ImGui::Separator();

            auto& em = ctx.get_cached<editing_manager>();
            if(ImGui::MenuItem("Auto Rebuild Reflection Probes", nullptr, em.auto_rebuild_reflection_probes))
            {
                em.auto_rebuild_reflection_probes = !em.auto_rebuild_reflection_probes;
            }
            ImGui::SetItemTooltip(
                "When enabled, reflection probes are automatically refreshed whenever the scene is\n"
                "modified (moving objects, editing materials, etc). Disable for large scenes where the\n"
                "background bakes become noticeable; use the Build menu to rebuild manually instead.");
            ImGui::Separator();
            if(ImGui::MenuItem("Migrate Prefabs"))
            {
                ImBox::ShowConfirmation(
                    "Migrate Prefabs?",
                    "Re-saves every prefab and scene in the project in the current format - nested\n"
                    "prefabs first, then the prefabs that contain them, then scenes. Files already in\n"
                    "the current format are rewritten unchanged.",
                    [](ImBox::ModalResult answer)
                    {
                        if(!ImBox::IsConfirmation(answer))
                        {
                            return;
                        }
                        auto& ctx = engine::context();
                        size_t total = 0;
                        const auto written = editor_actions::migrate_prefabs(ctx, &total);
                        ImGui::PushNotification(ImGuiToast(written == total ? ImGuiToastType_Success : ImGuiToastType_Warning,
                                                           4000,
                                                           "Migrated %zu of %zu prefab(s) and scene(s).",
                                                           written,
                                                           total));
                    });
            }
            ImGui::SetItemTooltip(
                "Re-save every prefab and scene in the project in the current format.\n"
                "Nested prefabs are written before the prefabs that contain them, scenes last.");
            if(ImGui::MenuItem("Migrate Color Spaces (Project)"))
            {
                editor_actions::migrate_texture_color_spaces("app:/");
            }
            ImGui::SetItemTooltip(
                "One-time: tag the textures referenced by material slots with their authored color\n"
                "space (base color / emissive = sRGB, data maps = linear) and recompile the changed ones.");
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Developer"))
        {
            auto& pm = ctx.get_cached<project_manager>();
            if(ImGui::MenuItem("Regenerate Agent Files", nullptr, false, pm.has_open_project()))
            {
                if(pm.regenerate_agent_files())
                {
                    ImGui::PushNotification(ImGuiToast(ImGuiToastType_Success,
                                                       3000,
                                                       "Regenerated UNRAVEL-AGENTS.md in the project root."));
                }
                else
                {
                    ImGui::PushNotification(ImGuiToast(ImGuiToastType_Error,
                                                       4000,
                                                       "Failed to regenerate agent files. Check the log."));
                }
            }
            ImGui::SetItemTooltip(
                "Overwrite UNRAVEL-AGENTS.md in the open project with the editor templates.\n"
                "Use after updating engine agent guidance, or to restore deleted files.");

            if(ImGui::BeginMenu("Assets"))
            {
                if(ImGui::MenuItem("Validate Prefab Graph"))
                {
                    const auto report = editor_actions::validate_prefab_graph(ctx);
                    if(report.is_valid())
                    {
                        ImGui::PushNotification(
                            ImGuiToast(ImGuiToastType_Success,
                                       4000,
                                       "Prefab graph is buildable.\n%zu assets, %zu with nesting, "
                                       "deepest nesting %zu.",
                                       report.asset_count,
                                       report.nesting_count,
                                       report.max_depth));
                    }
                    else
                    {
                        ImGui::PushNotification(ImGuiToast(ImGuiToastType_Error,
                                                           6000,
                                                           "%zu asset(s) take part in a prefab "
                                                           "nesting cycle. See the log.",
                                                           report.cyclic_ids.size()));
                    }
                }
                ImGui::SetItemTooltip(
                    "Check that every prefab and scene can be ordered for a build.\n"
                    "A prefab instancing another must be built after it, so a cycle -\n"
                    "A inside B inside A - has no valid order and is reported here.\n"
                    "Read-only: nothing is written.");

                if(ImGui::BeginMenu("Regenerate"))
                {
                    if(ImGui::MenuItem("Meta(Engine)"))
                    {
                        auto& am = ctx.get_cached<asset_watcher>();
                        am.recreate_meta_files(ctx, "engine:/");
                    }
                    if(ImGui::MenuItem("Meta(Editor)"))
                    {
                        auto& am = ctx.get_cached<asset_watcher>();
                        am.recreate_meta_files(ctx, "editor:/");
                    }
                    ImGui::EndMenu();
                }

                if(ImGui::BeginMenu("Recompile"))
                {
                    if(ImGui::BeginMenu("Shaders"))
                    {
                        if(ImGui::MenuItem("Shaders (Engine)"))
                        {
                            editor_actions::recompile_shaders("engine:/");
                        }

                        if(ImGui::MenuItem("Shaders (Editor)"))
                        {
                            editor_actions::recompile_shaders("editor:/");
                        }

                        if(ImGui::MenuItem("Shaders (Project)"))
                        {
                            editor_actions::recompile_shaders("app");
                        }
                        ImGui::EndMenu();
                    }
                    if(ImGui::BeginMenu("Textures"))
                    {
                        if(ImGui::MenuItem("Textures (Engine)"))
                        {
                            editor_actions::recompile_textures("engine:/");
                        }

                        if(ImGui::MenuItem("Textures (Editor)"))
                        {
                            editor_actions::recompile_textures("editor:/");
                        }

                        if(ImGui::MenuItem("Textures (Project)"))
                        {
                            editor_actions::recompile_textures("app:/");
                        }

                        ImGui::EndMenu();
                    }

                    if(ImGui::BeginMenu("Meshes"))
                    {
                        if(ImGui::MenuItem("Meshes (Engine)"))
                        {
                            editor_actions::recompile_meshes("engine:/");
                        }
                        if(ImGui::MenuItem("Meshes (Editor)"))
                        {
                            editor_actions::recompile_meshes("editor:/");
                        }
                        if(ImGui::MenuItem("Meshes (Project)"))
                        {
                            editor_actions::recompile_meshes("app:/");
                        }
                        ImGui::EndMenu();
                    }

                    if(ImGui::MenuItem("UI", ImGui::GetKeyCombinationName(shortcuts::recompile_ui).c_str()))
                    {
                        editor_actions::recompile_ui();
                    }


                    if(ImGui::BeginMenu("Scripts"))
                    {
                        if(ImGui::MenuItem("Scripts (Engine)"))
                        {
                            editor_actions::recompile_scripts("engine:/");
                        }

                        if(ImGui::MenuItem("Scripts (Editor)"))
                        {
                            editor_actions::recompile_scripts("editor:/");
                        }

                        if(ImGui::MenuItem("Scripts (Project)"))
                        {
                            editor_actions::recompile_scripts("app:/");
                        }
                        ImGui::EndMenu();
                    }

                    if(ImGui::BeginMenu("All"))
                    {
                        if(ImGui::MenuItem("All (Engine)"))
                        {
                            editor_actions::recompile_all("engine:/");
                        }
                        if(ImGui::MenuItem("All (Editor)"))
                        {
                            editor_actions::recompile_all("editor:/");
                        }
    
                        if(ImGui::MenuItem("All (Project)"))
                        {
                            editor_actions::recompile_all("app:/");
                        }
                        ImGui::EndMenu();
                    }

                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            

            if(ImGui::BeginMenu("Crash"))
            {
                if(ImGui::MenuItem("Abort"))
                {
                    std::abort();
                }
                if(ImGui::MenuItem("Terminate"))
                {
                    std::terminate();
                }
                if(ImGui::MenuItem("Segmentation Fault"))
                {
                    *(volatile int*)0 = 0;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Windows"))
        {
            if(ImGui::MenuItem("Style"))
            {
                parent_->get_style_panel().show(true);
            }
            if(ImGui::MenuItem("Layouts"))
            {
                parent_->get_layout_panel().focus();
            }
            if(ImGui::MenuItem("Animation"))
            {
                parent_->get_animation_panel().show(true);
            }
            if(ImGui::MenuItem("MCP Server"))
            {
                parent_->get_mcp_panel().show(true);
            }
            if(ImGui::MenuItem("Profiler"))
            {
                parent_->get_profiler_timeline_panel().show(true);
            }

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Help"))
        {
            if(ImGui::MenuItem("Check for Updates..."))
            {
                auto& vm = ctx.get_cached<version_manager>();
                vm.check_for_update_async();
            }
            ImGui::Separator();
            if(ImGui::MenuItem("About"))
            {
                show_about_window_ = true;
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if(!ImGui::IsAnyItemActive())
    {
        if(ImGui::IsCombinationKeyPressed(shortcuts::new_scene))
        {
            editor_actions::new_scene(ctx);
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::open_scene))
        {
            editor_actions::open_scene(ctx);
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::save_scene_as))
        {
            editor_actions::save_scene_as(ctx);
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::save_scene))
        {
            editor_actions::save_scene(ctx);
        }

        else if(ImGui::IsCombinationKeyPressed(shortcuts::redo, true))
        {
            ctx.get_cached<editing_manager>().redo();
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::redo_alt, true))
        {
            ctx.get_cached<editing_manager>().redo();
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::undo, true))
        {
            bool any_popup_open = ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopupId);
            if(!any_popup_open)
            {
                ctx.get_cached<editing_manager>().undo();
            }
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::undo_history))
        {
            bool any_popup_open = ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopupId);
            if(!any_popup_open)
            {
                parent_->get_undo_redo_panel().show(true);
            }
        }
        else if(ImGui::IsCombinationKeyPressed(shortcuts::recompile_ui))
        {
            editor_actions::recompile_ui();
        }
    }
    ImGui::EndChild();
}

void header_panel::draw_project_badge(rtti::context& ctx, 
                                      const ImVec2& window_pos,
                                      const ImVec2& window_size,
                                      float header_size,
                                      const ImVec2& item_spacing)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& play = ctx.get_cached<play_mode>();
    auto logo = fmt::format("{}", pm.get_name());
    auto logo_size = ImGui::CalcTextSize(logo.c_str());
    const float badge_h_pad = 30.0f;
    const float badge_taper = 12.0f;
    const float badge_width = logo_size.x + badge_h_pad * 2;
    const float badge_height = header_size * 0.5f - item_spacing.y;
    const ImVec2 badge_pos(window_pos.x + window_size.x * 0.5f - badge_width * 0.5f, window_pos.y);
    std::array<ImVec2, 5> points = {
        ImVec2(badge_pos.x, badge_pos.y),
        ImVec2(badge_pos.x + badge_taper, badge_pos.y + badge_height),
        ImVec2(badge_pos.x + badge_width - badge_taper, badge_pos.y + badge_height),
        ImVec2(badge_pos.x + badge_width, badge_pos.y),
        ImVec2(badge_pos.x, badge_pos.y)};
    ImU32 badge_color = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
    if(play.is_active())
    {
        badge_color = ImGui::GetColorU32(ImVec4(0.0f, 0.5f, 0.0f, 0.5f));
    }
    if(play.is_paused())
    {
        badge_color = ImGui::GetColorU32(ImVec4(0.6f, 0.3f, 0.0f, 0.5f));
    }
    ImGui::GetWindowDrawList()->AddConvexPolyFilled(points.data(), 5, badge_color);
    const ImVec2 text_pos(badge_pos.x + badge_width * 0.5f - logo_size.x * 0.5f,
                          badge_pos.y + (badge_height - logo_size.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), logo.c_str());
}

void header_panel::draw_left_zone(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    bool is_deploying = parent_->get_deploy_panel().is_deploying();
    ImGui::BeginDisabled(is_deploying);
    bool deploy_pressed = ImGui::Button(ICON_MDI_PACKAGE);
    ImGui::SetItemTooltipEx("%s", "Deploy and Run. For more control visit Deploy/Deploy Project menu.");
    ImGui::EndDisabled();
    if(deploy_pressed)
    {
        auto deploy_settings = pm.get_deploy_settings();
        deploy_settings.deploy_and_run = true;
        deploy_settings.deploy_dependencies = true;
        parent_->get_deploy_panel().deploy_and_run(ctx, deploy_settings);
    }
    ImGui::SameLine();
}

auto header_panel::calc_right_zone_width(const ImVec2& frame_padding, const ImVec2& item_spacing) -> float
{
    float speed_icon = ImGui::CalcTextSize(ICON_MDI_PLAY_SPEED).x;
    float slider = 100.0f;
    float reset_btn = ImGui::CalcTextSize(ICON_MDI_UNDO_VARIANT).x + frame_padding.x * 2;
    float vsync_box = ImGui::CalcTextSize("Vsync").x + frame_padding.x * 2 + ImGui::GetFrameHeight() + item_spacing.x;
    float max_fps_slider = 100.0f + item_spacing.x;
    float separator = item_spacing.x * 2 + 2.0f;
    return speed_icon + item_spacing.x + slider + item_spacing.x + reset_btn +
           separator + vsync_box + max_fps_slider + item_spacing.x;
}

auto header_panel::calc_center_zone_width(const ImVec2& frame_padding, const ImVec2& item_spacing) -> float
{
    float play_btns = ImGui::CalcTextSize(ICON_MDI_PLAY ICON_MDI_PAUSE ICON_MDI_SKIP_NEXT).x +
                      frame_padding.x * 6 + item_spacing.x * 2;
    float splash_checkbox = ImGui::CalcTextSize("Splash").x + frame_padding.x * 2 + ImGui::GetFrameHeight();
    float separator = item_spacing.x * 2 + 2.0f;
    float debug_mode = get_debug_mode_size();
    return play_btns + splash_checkbox + item_spacing.x + separator + debug_mode + item_spacing.x;
}

void header_panel::draw_center_zone(rtti::context& ctx)
{
    auto& play = ctx.get_cached<play_mode>();
    ImGuiKeyChord key_chord = shortcuts::play_toggle;
    bool play_pressed = ImGui::IsKeyChordPressed(key_chord);
    const bool has_errors = !editor_actions::can_enter_play(ctx) && !play.is_active();
    ImGui::BeginDisabled(has_errors);
    ImGui::BeginGroup();
    if(play.is_active())
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.47f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.58f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.12f, 1.0f));
    }
    play_pressed |= ImGui::Button(play.is_active() ? ICON_MDI_STOP : ICON_MDI_PLAY);
    if(play.is_active())
    {
        ImGui::PopStyleColor(3);
    }
    if(has_errors && !play.is_active())
    {
        play_pressed = false;
    }
    ImGui::SetItemTooltipEx("%s", ImGui::GetKeyChordName(key_chord));
    if(play_pressed)
    {
        editor_actions::toggle_play(ctx, play_splash_in_editor_);
    }
    ImGui::SameLine();

    // use local variable since set_play_paused can change it.
    bool is_paused = play.is_paused();
    if(is_paused)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.38f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.48f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.48f, 0.28f, 0.04f, 1.0f));
    }
    if(ImGui::Button(ICON_MDI_PAUSE))
    {
        editor_actions::set_play_paused(ctx, !is_paused);
    }
    if(is_paused)
    {
        ImGui::PopStyleColor(3);
    }
    ImGui::SameLine();
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if(ImGui::Button(ICON_MDI_SKIP_NEXT))
    {
        editor_actions::skip_play_frame(ctx);
    }
    ImGui::PopItemFlag();
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::BeginDisabled(play.is_active());
    draw_debug_mode();
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::EndDisabled();
    if(has_errors)
    {
        ImGui::SetItemTooltipEx("%s", "All compiler errors must be fixed before you can enter Play Mode!");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(play.is_active());
    ImGui::Checkbox("Splash", &play_splash_in_editor_);
    ImGui::SetItemTooltipEx("%s", "Allow splash on play; still requires splash enabled in project settings");
    ImGui::EndDisabled();
}

void header_panel::draw_right_zone(rtti::context& ctx)
{
    ImGui::BeginGroup();
    auto& sim = ctx.get_cached<simulation>();
    auto time_scale = sim.get_time_scale();
    ImGui::Text(ICON_MDI_PLAY_SPEED);
    ImGui::SetItemTooltipEx("%s", "Time scale");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if(ImGui::KnobSliderScalarT("###Time Scale", &time_scale, 0.0f, 3.0f))
    {
        sim.set_time_scale(time_scale);
    }
    ImGui::SetItemTooltipEx("%s", "Time scale");
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_UNDO_VARIANT))
    {
        sim.set_time_scale(1.0f);
    }
    ImGui::SetItemTooltipEx("Reset time scale to 1.0");
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    auto& rend = ctx.get_cached<renderer>();
    auto vsync = rend.get_vsync();
    if(ImGui::Checkbox("Vsync", &vsync))
    {
        rend.set_vsync(vsync);
    }
    ImGui::SameLine();
    int max_fps = static_cast<int>(sim.get_max_fps());
    ImGui::SetNextItemWidth(100.0f);
    const char* max_fps_fmt = (max_fps <= 0) ? "Uncapped" : "%d FPS";
    if(ImGui::KnobSliderScalarT("###Max FPS", &max_fps, 0, 240, max_fps_fmt, ImGuiSliderFlags_AlwaysClamp))
    {
        sim.set_max_fps(static_cast<uint32_t>(max_fps < 0 ? 0 : max_fps));
    }
    ImGui::SetItemTooltipEx("%s", "Max FPS (0 = uncapped)");
    ImGui::EndGroup();
}

void header_panel::draw_play_toolbar(rtti::context& ctx, float header_size)
{
    auto window_pos = ImGui::GetWindowPos();
    auto window_size = ImGui::GetWindowSize();
    const auto& style = ImGui::GetStyle();
    auto frame_padding = style.FramePadding;
    auto item_spacing = style.ItemSpacing;
    draw_project_badge(ctx, window_pos, window_size, header_size, item_spacing);
    draw_left_zone(ctx);
    float avail = ImGui::GetContentRegionAvail().x;
    float right_zone_width = calc_right_zone_width(frame_padding, item_spacing);
    float center_zone_width = calc_center_zone_width(frame_padding, item_spacing);
    ImGui::AlignedItem(0.5f, avail, center_zone_width,
                       [&]() -> void { draw_center_zone(ctx); });
    ImGui::SameLine();
    ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, right_zone_width,
                       [&]() -> void { draw_right_zone(ctx); });
}

void header_panel::on_frame_ui_render(rtti::context& ctx, float header_size)
{
    ImGuiWindowFlags header_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDecoration;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, header_size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    ImGui::SetNextWindowViewport(viewport->ID);

    bool open = ImGui::Begin("HEADER", nullptr, header_flags);

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    if(open)
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        // Draw a sep. child for the menu bar.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        draw_menubar_child(ctx);
        // ImGui::NewLine();
        draw_play_toolbar(ctx, header_size);
        ImGui::PopStyleColor();
    }

    ImGui::End();

    // Draw the about window (will only be visible if show_about_window_ is true)
    draw_about_window(ctx);

}

void header_panel::draw_about_window(rtti::context& ctx)
{
    if(!show_about_window_)
        return;

    if(!ImGui::IsPopupOpen("About Unravel Engine"))
    {
        ImGui::OpenPopup("About Unravel Engine");
    }

    auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize * 0.30f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal("About Unravel Engine",
                              &show_about_window_,
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        // Logo and title
        const float title_scale = 1.5f;
        ImGui::PushFont(GetFont(ImGui::Font::Bold),
                        GetFont(ImGui::Font::Bold)->LegacySize * title_scale); // Use default font
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Unravel Engine");
        ImGui::PopFont();

        // Version information
        ImGui::Text("Version %s", version::get_full().c_str());
        ImGui::Separator();

        // Engine description
        ImGui::TextWrapped("Unravel Engine is a modern, high-performance game engine designed for creating "
                           "interactive 3D and 2D applications. It features a component-based architecture, "
                           "powerful rendering capabilities, and an intuitive editor interface.");

        ImGui::Spacing();
        ImGui::Spacing();

        // Features
        const float section_scale = 1.2f;
        ImGui::PushFont(ImGui::GetFont(), ImGui::GetFont()->LegacySize * section_scale);
        ImGui::Text("Key Features");
        ImGui::PopFont();

        ImGui::Columns(2);
        ImGui::BulletText("Entity-Component-System");
        ImGui::BulletText("PBR Rendering");
        ImGui::BulletText("C# Scripting");
        ImGui::BulletText("Physics Integration");
        ImGui::NextColumn();
        ImGui::BulletText("Real-time Editor");
        ImGui::BulletText("Asset Management");
        ImGui::BulletText("Cross-platform Support");
        ImGui::BulletText("Extensible Architecture");
        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Spacing();

        // Build information
        ImGui::PushFont(ImGui::GetFont(), ImGui::GetFont()->LegacySize * section_scale);
        ImGui::Text("Build Information");
        ImGui::PopFont();

        ImGui::Text("Build Date: %s", __DATE__);
        ImGui::Text("Build Time: %s", __TIME__);
#ifdef _DEBUG
        ImGui::Text("Configuration: Debug");
#else
        ImGui::Text("Configuration: Release");
#endif

        ImGui::Spacing();
        ImGui::Spacing();

        // Copyright notice
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Copyright © %d. All rights reserved.", 2025);

        ImGui::EndPopup();
    }
}

} // namespace unravel
