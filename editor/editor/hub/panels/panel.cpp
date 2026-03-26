#include "panel.h"
#include "panels_defs.h"

#include <filesystem/filesystem.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <editor/editing/create_scene_modal.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/imgui/integration/imgui_messagebox.h>

#include <logging/logging.h>

namespace unravel
{

imgui_panels::imgui_panels()
{
    console_log_panel_ = std::make_shared<console_log_panel>(CONSOLE_VIEW);
    console_log_panel_->set_level(spdlog::level::trace);
    get_mutable_logging_container()->add_sink(console_log_panel_);

    header_panel_ = std::make_unique<header_panel>(this);
    footer_panel_ = std::make_unique<footer_panel>();
    cenral_dockspace_ = std::make_unique<dockspace>();

    content_browser_panel_ = std::make_unique<content_browser_panel>(this, CONTENT_VIEW);
    hierarchy_panel_ = std::make_unique<hierarchy_panel>(this, HIERARCHY_VIEW);
    inspector_panel_ = std::make_unique<inspector_panel>(this, INSPECTOR_VIEW);
    scene_panel_ = std::make_unique<scene_panel>(this, SCENE_VIEW);
    game_panel_ = std::make_unique<game_panel>(this, GAME_VIEW);
    profiler_timeline_panel_ = std::make_unique<profiler_timeline_panel>(this, PROFILER_VIEW);
    animation_panel_ = std::make_unique<animation_panel>(this);

    deploy_panel_ = std::make_unique<deploy_panel>(this);
    project_settings_panel_ = std::make_unique<project_settings_panel>(this);
    editor_settings_panel_ = std::make_unique<editor_settings_panel>(this);
    style_panel_ = std::make_unique<style_panel>(this);
    layout_panel_ = std::make_unique<layout_panel>(this, LAYOUTS_VIEW);
    undo_redo_panel_ = std::make_unique<undo_redo_panel>(this);
}

imgui_panels::~imgui_panels()
{
    get_mutable_logging_container()->remove_sink(console_log_panel_);
}

void imgui_panels::init(rtti::context& ctx)
{
    style_panel_->init(ctx);

    content_browser_panel_->init(ctx);
    hierarchy_panel_->init(ctx);
    inspector_panel_->init(ctx);
    scene_panel_->init(ctx);
    game_panel_->init(ctx);
    animation_panel_->init(ctx);

    auto layouts_dir = fs::resolve_protocol("editor:/settings/layouts");
    layout_manager_.init(layouts_dir);
}

void imgui_panels::deinit(rtti::context& ctx)
{
    content_browser_panel_->deinit(ctx);
    scene_panel_->deinit(ctx);
    game_panel_->deinit(ctx);
    inspector_panel_->deinit(ctx);
    animation_panel_->deinit(ctx);
}

void imgui_panels::on_frame_update(rtti::context& ctx, delta_t dt)
{
    scene_panel_->on_frame_update(ctx, dt);
    game_panel_->on_frame_update(ctx, dt);
}

void imgui_panels::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    scene_panel_->on_frame_before_render(ctx, dt);
    game_panel_->on_frame_before_render(ctx, dt);
}

void imgui_panels::on_frame_render(rtti::context& ctx, delta_t dt)
{
    scene_panel_->on_frame_render(ctx, dt);
    game_panel_->on_frame_render(ctx, dt);
}

void imgui_panels::on_frame_ui_render(rtti::context& ctx)
{
    auto footer_size = ImGui::GetFrameHeightWithSpacing();
    auto header_size = ImGui::GetFrameHeightWithSpacing() * 2;

    if(ImGui::IsCombinationKeyPressed(shortcuts::scene_fullscreen_toggle))
    {
        std::vector<panel_base*> panels =
        {
            scene_panel_.get(),
            game_panel_.get()
        };
        for(const auto& panel : panels)
        {
            if(panel->is_focused())
            {
                panel->toggle_fullscreen();
    
                if(panel->is_fullscreen())
                {
                    full_screen_panel_ = panel;
                }
                else
                {
                    full_screen_panel_ = nullptr;
                }
                panel->focus();
            }
        }
        
    }

    header_panel_->on_frame_ui_render(ctx, header_size);

    if(full_screen_panel_ && full_screen_panel_->is_fullscreen())
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 fullscreen_pos(viewport->WorkPos.x, viewport->WorkPos.y + header_size);
        const ImVec2 fullscreen_size(viewport->WorkSize.x, viewport->WorkSize.y - header_size - footer_size);
        ImGui::SetNextWindowPos(fullscreen_pos);
        ImGui::SetNextWindowSize(fullscreen_size);
        full_screen_panel_->on_frame_ui_render(ctx);
    }
    else
    {


        cenral_dockspace_->on_frame_ui_render(header_size, footer_size);

        hierarchy_panel_->on_frame_ui_render(ctx);

        inspector_panel_->on_frame_ui_render(ctx);

        profiler_timeline_panel_->on_frame_ui_render(ctx, PROFILER_VIEW);

        console_log_panel_->on_frame_ui_render(ctx);

        content_browser_panel_->on_frame_ui_render(ctx);

        scene_panel_->on_frame_ui_render(ctx);

        game_panel_->on_frame_ui_render(ctx);

        layout_panel_->on_frame_ui_render(ctx);

        animation_panel_->on_frame_ui_render(ctx, ANIMATION_VIEW);

        deploy_panel_->on_frame_ui_render(ctx, DEPLOY_VIEW);

        project_settings_panel_->on_frame_ui_render(ctx, PROJECT_SETTINGS_VIEW);

        editor_settings_panel_->on_frame_ui_render(ctx, EDITOR_SETTINGS_VIEW);
    }

    footer_panel_->on_frame_ui_render(ctx,
        footer_size,
        [&]()
        {
            console_log_panel_->draw_last_log_button();
        });
    cenral_dockspace_->execute_dock_builder_order_and_focus_workaround();


    // Draw the style picker window if visible
    style_panel_->on_frame_ui_render();
    
    // Draw the undo/redo panel if visible
    undo_redo_panel_->on_frame_ui_render(ctx);


    // Render toasts on top of everything, at the end of your code!
    // You should push style vars here
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f); // Round borders
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f)); // Background color
    ImGui::RenderNotifications(); // <-- Here we render all notifications
    ImBox::RenderMessageBoxes();
    create_scene_modal::render();
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);

    if(ImGui::IsKeyPressed(ImGuiKey_F11))
    {
        ImGuiToast toast(ImGuiToastType_Info, "Hello, world!");
        ImGui::PushNotification(toast);
    }
}

auto imgui_panels::get_deploy_panel() -> deploy_panel&
{
    return *deploy_panel_;
}

auto imgui_panels::get_project_settings_panel() -> project_settings_panel&
{
    return *project_settings_panel_;
}

auto imgui_panels::get_editor_settings_panel() -> editor_settings_panel&
{
    return *editor_settings_panel_;
}

auto imgui_panels::get_scene_panel() -> scene_panel&
{
    return *scene_panel_;
}

auto imgui_panels::get_game_panel() -> game_panel&
{
    return *game_panel_;
}

auto imgui_panels::get_console_log_panel() -> console_log_panel&
{
    return *console_log_panel_;
}

auto imgui_panels::get_style_panel() -> style_panel&
{
    return *style_panel_;
}

auto imgui_panels::get_dockspace() -> dockspace&
{
    return *cenral_dockspace_;
}

auto imgui_panels::get_animation_panel() -> animation_panel&
{
    return *animation_panel_;
}

auto imgui_panels::get_profiler_timeline_panel() -> profiler_timeline_panel&
{
    return *profiler_timeline_panel_;
}

auto imgui_panels::get_undo_redo_panel() -> undo_redo_panel&
{
    return *undo_redo_panel_;
}

void imgui_panels::set_external_drop_in_progress(bool in_progress)
{
    external_drop_data_.drop_in_progress = in_progress;
}

auto imgui_panels::get_external_drop_in_progress() const -> bool
{
    return external_drop_data_.drop_in_progress;
}

void imgui_panels::set_external_drop_position(ImVec2 pos)
{
    external_drop_data_.drop_position = pos;
}

auto imgui_panels::get_external_drop_position() const -> const ImVec2&
{
    return external_drop_data_.drop_position;
}

void imgui_panels::add_external_drop_file(const std::string& file)
{
    external_drop_data_.drop_files.emplace_back(file);
}

void imgui_panels::clear_external_drop_files()
{
    external_drop_data_.drop_files.clear();
}

auto imgui_panels::get_external_drop_files() const -> const std::vector<std::string>&
{
    return external_drop_data_.drop_files;
}

auto imgui_panels::get_layout_manager() -> layout_manager&
{
    return layout_manager_;
}

auto imgui_panels::get_layout_panel() -> layout_panel&
{
    return *layout_panel_;
}

} // namespace unravel
