#include "header_panel.h"
#include "../panel.h"
#include "../panel_toolbar.h"
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
constexpr ImU32 HEADER_PLAYING_COLOR = IM_COL32(46, 125, 50, 255);
constexpr ImU32 HEADER_PAUSED_COLOR = IM_COL32(178, 106, 20, 255);
constexpr ImU32 HEADER_DEBUGGER_ATTACHED_COLOR = IM_COL32(90, 220, 90, 255);
constexpr float HEADER_SLIDER_WIDTH = 100.0f;
constexpr float HEADER_TIME_SCALE_MAX = 3.0f;
constexpr int HEADER_MAX_FPS_LIMIT = 240;

/// Switching the mode recompiles and reloads every script, so only a real change goes through.
void set_script_debug_mode(bool is_debug_mode)
{
    if(script_system::get_script_debug_mode() == is_debug_mode)
    {
        return;
    }
    script_system::set_script_debug_mode(is_debug_mode);
    script_system::set_needs_recompile("app", true);
}

void draw_script_mode_dropdown()
{
    const bool is_debugger_attached = script_system::is_debugger_attached();
    const bool is_debug_mode = script_system::get_script_debug_mode();
    const char* text = is_debug_mode ? ICON_MDI_BUG_CHECK " Debug" : ICON_MDI_BUG " Release";
    const char* state = is_debug_mode ? "Debugger Enabled" : "Debugger Disabled";
    const char* tooltip = is_debugger_attached ? "Debugger Attached" : state;
    const ImU32 text_color = is_debugger_attached ? HEADER_DEBUGGER_ATTACHED_COLOR : 0;
    if(!panel_toolbar::begin_dropdown("##script_mode", text, tooltip, text_color))
    {
        return;
    }
    ImGui::SeparatorText("Script Mode");
    if(ImGui::MenuItem(ICON_MDI_BUG_CHECK " Debug", nullptr, is_debug_mode))
    {
        set_script_debug_mode(true);
    }
    ImGui::SetItemTooltipEx("%s",
                            "Debug mode enables C# debugging\n"
                            "but reduces C# performance.\n"
                            "Switching to Debug mode will recompile\n"
                            "and reload all scripts.");
    if(ImGui::MenuItem(ICON_MDI_BUG " Release", nullptr, !is_debug_mode))
    {
        set_script_debug_mode(false);
    }
    ImGui::SetItemTooltipEx("%s",
                            "Release mode disables C# debugging\n"
                            "but improves C# performance.\n"
                            "Switching to Release mode will recompile\n"
                            "and reload all scripts.");
    panel_toolbar::end_dropdown();
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
                about_window_.open();
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

void header_panel::draw_project_badge(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& play = ctx.get_cached<play_mode>();
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    auto logo = fmt::format("{}", pm.get_name());
    auto logo_size = ImGui::CalcTextSize(logo.c_str());
    const float badge_h_pad = 30.0f;
    const float badge_taper = 12.0f;
    const float badge_width = logo_size.x + badge_h_pad * 2;
    // The badge hangs from the top edge over the menu bar row.
    const float badge_height = ImGui::GetFrameHeight();
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

void header_panel::draw_deploy_button(rtti::context& ctx)
{
    const bool is_deploying = parent_->get_deploy_panel().is_deploying();
    ImGui::BeginDisabled(is_deploying);
    const bool is_pressed =
        panel_toolbar::button("##deploy",
                              ICON_MDI_PACKAGE,
                              "Deploy and Run. For more control visit Deploy/Deploy Project menu.");
    ImGui::EndDisabled();
    if(!is_pressed)
    {
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    auto deploy_settings = pm.get_deploy_settings();
    deploy_settings.deploy_and_run = true;
    deploy_settings.deploy_dependencies = true;
    parent_->get_deploy_panel().deploy_and_run(ctx, deploy_settings);
}

void header_panel::draw_transport_controls(rtti::context& ctx)
{
    auto& play = ctx.get_cached<play_mode>();
    const ImGuiKeyChord play_chord = shortcuts::play_toggle;
    // Compile errors keep the editor out of play mode, but never inside it.
    const bool has_errors = !editor_actions::can_enter_play(ctx) && !play.is_active();
    // The controls are one group, so the reason they are disabled has a whole area to show up on.
    panel_toolbar::begin_group();
    ImGui::BeginDisabled(has_errors);
    const char* play_icon = play.is_active() ? ICON_MDI_STOP : ICON_MDI_PLAY;
    const bool is_play_clicked = panel_toolbar::toggle("##play",
                                                       play_icon,
                                                       play.is_active(),
                                                       ImGui::GetKeyChordName(play_chord),
                                                       nullptr,
                                                       HEADER_PLAYING_COLOR);
    // A local: pausing changes the state the next toggle would read.
    const bool is_paused = play.is_paused();
    if(panel_toolbar::toggle("##pause", ICON_MDI_PAUSE, is_paused, "Pause", nullptr, HEADER_PAUSED_COLOR))
    {
        editor_actions::set_play_paused(ctx, !is_paused);
    }
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if(panel_toolbar::button("##step", ICON_MDI_SKIP_NEXT, "Step one frame"))
    {
        editor_actions::skip_play_frame(ctx);
    }
    ImGui::PopItemFlag();
    ImGui::EndDisabled();
    panel_toolbar::end_group();
    if(has_errors && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("%s", "All compiler errors must be fixed before you can enter Play Mode!");
    }
    const bool is_play_pressed = is_play_clicked || ImGui::IsKeyChordPressed(play_chord);
    if(is_play_pressed && !has_errors)
    {
        editor_actions::toggle_play(ctx, play_splash_in_editor_);
    }
}

void header_panel::draw_play_options(rtti::context& ctx)
{
    auto& play = ctx.get_cached<play_mode>();
    // Both apply to the next play session, so they are locked during one.
    ImGui::BeginDisabled(play.is_active());
    draw_script_mode_dropdown();
    if(panel_toolbar::toggle("##splash",
                             "Splash",
                             play_splash_in_editor_,
                             "Allow splash on play; still requires splash enabled in project settings"))
    {
        play_splash_in_editor_ = !play_splash_in_editor_;
    }
    ImGui::EndDisabled();
}

void header_panel::draw_time_scale(rtti::context& ctx)
{
    auto& sim = ctx.get_cached<simulation>();
    float time_scale = sim.get_time_scale();
    panel_toolbar::label(ICON_MDI_PLAY_SPEED);
    ImGui::SetItemTooltipEx("%s", "Time scale");
    panel_toolbar::begin_field(HEADER_SLIDER_WIDTH);
    if(ImGui::KnobSliderScalarT("###Time Scale", &time_scale, 0.0f, HEADER_TIME_SCALE_MAX))
    {
        sim.set_time_scale(time_scale);
    }
    ImGui::SetItemTooltipEx("%s", "Time scale");
    panel_toolbar::end_field();
    if(panel_toolbar::button("##reset_time_scale", ICON_MDI_UNDO_VARIANT, "Reset time scale to 1.0"))
    {
        sim.set_time_scale(1.0f);
    }
}

void header_panel::draw_frame_pacing(rtti::context& ctx)
{
    auto& rend = ctx.get_cached<renderer>();
    auto& sim = ctx.get_cached<simulation>();
    const bool is_vsync_on = rend.get_vsync();
    if(panel_toolbar::toggle("##vsync", "VSync", is_vsync_on, "Wait for the display before presenting a frame"))
    {
        rend.set_vsync(!is_vsync_on);
    }
    int max_fps = static_cast<int>(sim.get_max_fps());
    const char* max_fps_format = (max_fps <= 0) ? "Uncapped" : "%d FPS";
    panel_toolbar::begin_field(HEADER_SLIDER_WIDTH);
    if(ImGui::KnobSliderScalarT("###Max FPS", &max_fps, 0, HEADER_MAX_FPS_LIMIT, max_fps_format, ImGuiSliderFlags_AlwaysClamp))
    {
        sim.set_max_fps(static_cast<uint32_t>(max_fps < 0 ? 0 : max_fps));
    }
    ImGui::SetItemTooltipEx("%s", "Max FPS (0 = uncapped)");
    panel_toolbar::end_field();
}

void header_panel::draw_play_toolbar(rtti::context& ctx)
{
    draw_project_badge(ctx);
    if(panel_toolbar::begin_strip("##header_toolbar", panel_toolbar::strip_style::flat))
    {
        draw_deploy_button(ctx);
        panel_toolbar::align_center();
        draw_transport_controls(ctx);
        panel_toolbar::separator();
        draw_play_options(ctx);
        panel_toolbar::align_right();
        draw_time_scale(ctx);
        panel_toolbar::separator();
        draw_frame_pacing(ctx);
    }
    panel_toolbar::end_strip();
}

auto header_panel::calc_height() -> float
{
    return ImGui::GetFrameHeight() + panel_toolbar::get_strip_height();
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
        // The play toolbar follows flush. calc_height() is the two rows and nothing between them,
        // so the item spacing under the menu bar would push the toolbar down and crop its bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
        draw_play_toolbar(ctx);
        ImGui::PopStyleColor();
    }

    ImGui::End();

    about_window_.draw(ctx);

}

} // namespace unravel
