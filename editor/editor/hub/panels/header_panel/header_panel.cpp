#include "header_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "filesystem/filesystem.h"

#include <editor/editing/editing_manager.h>
#include <editor/system/project_manager.h>
#include <editor/shortcuts.h>

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/threading/threader.h>
#include <simulation/simulation.h>
#include <version/version.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

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

        if(ImGui::BeginMenu("Developer"))
        {
            if(ImGui::MenuItem("Crash"))
            {
                std::abort();
            }

            if(ImGui::MenuItem("Recompile Shaders"))
            {
                editor_actions::recompile_shaders();
            }

            if(ImGui::MenuItem("Recompile Textures"))
            {
                editor_actions::recompile_textures();
            }

            if(ImGui::MenuItem("Recompile Scripts"))
            {
                editor_actions::recompile_scripts();
            }

            if(ImGui::MenuItem("Recompile All"))
            {
                editor_actions::recompile_all();
            }

            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Windows"))
        {
            if(ImGui::MenuItem("Style"))
            {
                parent_->get_style_panel().show(true);
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Help"))
        {
            if(ImGui::MenuItem("About"))
            {
                show_about_window_ = true;
            }

            ImGui::EndMenu();
        }
       
        ImGui::EndMenuBar();
    }

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

    ImGui::EndChild();
}

void header_panel::draw_play_toolbar(rtti::context& ctx, float header_size)
{
    auto& ev = ctx.get_cached<events>();

    float width = ImGui::GetContentRegionAvail().x;

    auto window_pos = ImGui::GetWindowPos();
    auto window_size = ImGui::GetWindowSize();
    // Add a poly background for the logo.
    const ImVec2 logo_bounds = ImVec2(500, header_size * 0.5f);
    const ImVec2 logo_pos = ImVec2(window_pos.x + window_size.x * 0.5f - logo_bounds.x * 0.5f, window_pos.y);

    ImVec2 points[5] = {ImVec2(logo_pos.x, logo_pos.y),
                        ImVec2(logo_pos.x + 20, logo_pos.y + logo_bounds.y + 4),
                        ImVec2(logo_pos.x + logo_bounds.x - 20, logo_pos.y + logo_bounds.y + 4),
                        ImVec2(logo_pos.x + logo_bounds.x, logo_pos.y),
                        ImVec2(logo_pos.x, logo_pos.y)};

    const ImU32 poly_background = ImGui::GetColorU32(ImGuiCol_MenuBarBg);
    auto poly_background_border_color = poly_background;

    if(ev.is_playing)
    {
        poly_background_border_color = ImGui::GetColorU32(ImVec4(0.0f, 0.5f, 0.0f, 0.5f));
    }
    if(ev.is_paused)
    {
        poly_background_border_color = ImGui::GetColorU32(ImVec4(0.6f, 0.3f, 0.0f, 0.5f));
    }

    ImGui::GetWindowDrawList()->AddConvexPolyFilled(&points[0], 5, poly_background_border_color);
    // ImGui::GetWindowDrawList()->AddPolyline(&points[0], 4, poly_background_border_color, 0, 3);
    // ImGui::GetWindowDrawList()->AddRectFilledMultiColor(logo_pos,
    //                                                      logo_pos + logo_bounds,
    //                                                      poly_background_border_color,
    //                                                      poly_background_border_color,
    //                                                      poly_background,
    //                                                      poly_background);

    auto& pm = ctx.get_cached<project_manager>();
    auto logo = fmt::format("{}", pm.get_name());
    auto logo_size = ImGui::CalcTextSize(logo.c_str());
    // Add animated logo.
    const ImVec2 logo_min = ImVec2(logo_pos.x + logo_bounds.x * 0.5f - logo_size.x * 0.5f,
                                   logo_pos.y + (logo_bounds.y - logo_size.y) * 0.5f);
    const ImVec2 logo_max = ImVec2(logo_min.x + logo_size.x, logo_min.y + logo_size.y);
    auto logo_border_color = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddText(logo_min, logo_border_color, logo.c_str());

    const auto& style = ImGui::GetStyle();
    auto frame_padding = style.FramePadding;
    auto item_spacing = style.ItemSpacing;
    ImGui::AlignedItem(0.5f,
                       width,
                       ImGui::CalcTextSize(ICON_MDI_PLAY ICON_MDI_PAUSE ICON_MDI_SKIP_NEXT).x + frame_padding.x * 6 +
                           item_spacing.x * 3,
                       [&]()
                       {
                           ImGuiKeyChord key_chord = ev.is_playing ? shortcuts::play_toggle : shortcuts::play_toggle;
                           bool play_pressed = ImGui::IsKeyChordPressed(key_chord);

                           auto& scripting = ctx.get_cached<script_system>();
                           bool has_errors = scripting.has_compilation_errors();
                           ImGui::BeginDisabled(has_errors);
                           ImGui::BeginGroup();

                           play_pressed |= ImGui::Button(ev.is_playing ? ICON_MDI_STOP : ICON_MDI_PLAY);

                           if(has_errors)
                           {
                               play_pressed = false;
                           }
                           ImGui::SetItemTooltipEx("%s", ImGui::GetKeyChordName(key_chord));
                           if(play_pressed)
                           {
                               ev.toggle_play_mode(ctx);
                               ImGui::FocusWindow(ImGui::FindWindowByName(ev.is_playing ? GAME_VIEW : SCENE_VIEW));
                           }

                           ImGui::SameLine();
                           if(ImGui::Button(ICON_MDI_PAUSE))
                           {
                               bool was_playing = ev.is_playing;
                               ev.toggle_pause(ctx);
                           }

                           ImGui::SameLine();
                           ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                           if(ImGui::Button(ICON_MDI_SKIP_NEXT))
                           {
                               ev.skip_next_frame(ctx);
                           }
                           ImGui::PopItemFlag();
                           ImGui::SameLine();

                           ImGui::BeginDisabled(ev.is_playing);
                           draw_debug_mode();
                           ImGui::EndDisabled();
                           ImGui::SameLine();

                           auto& sim = ctx.get_cached<simulation>();

                           auto time_scale = sim.get_time_scale();
                           ImGui::SetNextItemWidth(100);
                           if(ImGui::SliderFloat("###Time Scale", &time_scale, 0.0f, 1.0f))
                           {
                               sim.set_time_scale(time_scale);
                           }
                           ImGui::SetItemTooltipEx("%s", "Time scale.");
                           ImGui::SameLine();
                           auto& rend = ctx.get_cached<renderer>();
                           auto vsync = rend.get_vsync();
                           if(ImGui::Checkbox("Vsync", &vsync))
                           {
                               rend.set_vsync(vsync);
                           }

                           ImGui::EndGroup();
                           ImGui::EndDisabled();

                           if(has_errors)
                           {
                               ImGui::SetItemTooltipEx(
                                   "%s",
                                   "All compiler errors must be fixed before you can enter Play Mode!");
                           }
                       });
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
        ImGui::NewLine();
        draw_play_toolbar(ctx, header_size);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    
    // Draw the about window (will only be visible if show_about_window_ is true)
    draw_about_window(ctx);
}

void header_panel::draw_about_window(rtti::context& ctx)
{
    if (!show_about_window_)
        return;

    if(!ImGui::IsPopupOpen("About Unravel Engine"))
    {
        ImGui::OpenPopup("About Unravel Engine");
    }

    auto viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize * 0.30f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkSize.x * 0.5f, viewport->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About Unravel Engine", &show_about_window_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize| ImGuiWindowFlags_NoMove))
    {
        // Logo and title
        const float title_scale = 1.5f;
        ImGui::PushFont(ImGui::Font::Bold); // Use default font
        ImGui::SetWindowFontScale(title_scale);
        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Unravel Engine");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();

        // Version information
        ImGui::Text("Version %s", version::get_full().c_str());
        ImGui::Separator();

        // Engine description
        ImGui::TextWrapped(
            "Unravel Engine is a modern, high-performance game engine designed for creating "
            "interactive 3D and 2D applications. It features a component-based architecture, "
            "powerful rendering capabilities, and an intuitive editor interface.");
        
        ImGui::Spacing();
        ImGui::Spacing();

        // Features
        const float section_scale = 1.2f;
        ImGui::SetWindowFontScale(section_scale);
        ImGui::Text("Key Features");
        ImGui::SetWindowFontScale(1.0f);
        
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
        ImGui::SetWindowFontScale(section_scale);
        ImGui::Text("Build Information");
        ImGui::SetWindowFontScale(1.0f);
        
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
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                          "Copyright © %d. All rights reserved.", 2025);


        ImGui::EndPopup();
    }
}

} // namespace unravel
