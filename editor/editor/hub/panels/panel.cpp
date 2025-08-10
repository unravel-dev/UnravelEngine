#include "panel.h"
#include "panels_defs.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <editor/imgui/integration/imgui_notify.h>

#include <logging/logging.h>

namespace unravel
{

imgui_panels::imgui_panels()
{
    console_log_panel_ = std::make_shared<console_log_panel>();
    console_log_panel_->set_level(spdlog::level::trace);
    get_mutable_logging_container()->add_sink(console_log_panel_);

    header_panel_ = std::make_unique<header_panel>(this);
    footer_panel_ = std::make_unique<footer_panel>();
    cenral_dockspace_ = std::make_unique<dockspace>();

    content_browser_panel_ = std::make_unique<content_browser_panel>(this);
    hierarchy_panel_ = std::make_unique<hierarchy_panel>(this);
    inspector_panel_ = std::make_unique<inspector_panel>(this);
    scene_panel_ = std::make_unique<scene_panel>(this);
    game_panel_ = std::make_unique<game_panel>();
    statistics_panel_ = std::make_unique<statistics_panel>();
    animation_panel_ = std::make_unique<animation_panel>(this);

    deploy_panel_ = std::make_unique<deploy_panel>(this);
    project_settings_panel_ = std::make_unique<project_settings_panel>(this);
    editor_settings_panel_ = std::make_unique<editor_settings_panel>(this);
    style_panel_ = std::make_unique<style_panel>(this);
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
    statistics_panel_->init(ctx);
    animation_panel_->init(ctx);
}

void imgui_panels::deinit(rtti::context& ctx)
{
    content_browser_panel_->deinit(ctx);
    scene_panel_->deinit(ctx);
    game_panel_->deinit(ctx);
    inspector_panel_->deinit(ctx);
    statistics_panel_->deinit(ctx);
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
    auto header_size = ImGui::GetFrameHeightWithSpacing() * 3;

    header_panel_->on_frame_ui_render(ctx, header_size);

    cenral_dockspace_->on_frame_ui_render(header_size, footer_size);

    hierarchy_panel_->on_frame_ui_render(ctx, HIERARCHY_VIEW);

    inspector_panel_->on_frame_ui_render(ctx, INSPECTOR_VIEW);

    statistics_panel_->on_frame_ui_render(ctx, STATISTICS_VIEW);

    console_log_panel_->on_frame_ui_render(ctx, CONSOLE_VIEW);

    content_browser_panel_->on_frame_ui_render(ctx, CONTENT_VIEW);

    scene_panel_->on_frame_ui_render(ctx, SCENE_VIEW);

    game_panel_->on_frame_ui_render(ctx, GAME_VIEW);

    animation_panel_->on_frame_ui_render(ctx, ANIMATION_VIEW);

    deploy_panel_->on_frame_ui_render(ctx, DEPLOY_VIEW);

    project_settings_panel_->on_frame_ui_render(ctx, PROJECT_SETTINGS_VIEW);

    editor_settings_panel_->on_frame_ui_render(ctx, EDITOR_SETTINGS_VIEW);

    footer_panel_->on_frame_ui_render(ctx,
                                      footer_size,
                                      [&]()
                                      {
                                          console_log_panel_->draw_last_log_button();
                                      });
    cenral_dockspace_->execute_dock_builder_order_and_focus_workaround();

    // Draw the style picker window if visible
    style_panel_->on_frame_ui_render();


    // Render toasts on top of everything, at the end of your code!
    // You should push style vars here
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f); // Round borders
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(43.f / 255.f, 43.f / 255.f, 43.f / 255.f, 100.f / 255.f)); // Background color
    ImGui::RenderNotifications(); // <-- Here we render all notifications
    ImGui::PopStyleVar(1); // Don't forget to Pop()
    ImGui::PopStyleColor(1);
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

} // namespace unravel
