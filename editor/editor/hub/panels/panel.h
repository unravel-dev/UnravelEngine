#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/texture.h>

#include "animation_panel/animation_panel.h"
#include "console_log_panel/console_log_panel.h"
#include "content_browser_panel/content_browser_panel.h"
#include "deploy_panel/deploy_panel.h"
#include "dockspace.h"
#include "editor_settings_panel/editor_settings_panel.h"
#include "footer_panel/footer_panel.h"
#include "game_panel/game_panel.h"
#include "header_panel/header_panel.h"
#include "hierarchy_panel/hierarchy_panel.h"
#include "inspector_panel/inspector_panel.h"
#include "project_settings_panel/project_settings_panel.h"
#include "scene_panel/scene_panel.h"
#include "statistics_panel/statistics_panel.h"
#include "style_panel/style_panel.h"

namespace unravel
{

class imgui_panels
{
public:
    imgui_panels();
    ~imgui_panels();

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_ui_render(rtti::context& ctx);

    auto get_deploy_panel() -> deploy_panel&;
    auto get_project_settings_panel() -> project_settings_panel&;
    auto get_editor_settings_panel() -> editor_settings_panel&;
    auto get_scene_panel() -> scene_panel&;
    auto get_game_panel() -> game_panel&;
    auto get_console_log_panel() -> console_log_panel&;
    auto get_style_panel() -> style_panel&;

    void set_external_drop_in_progress(bool in_progress);

    auto get_external_drop_in_progress() const -> bool;

    void set_external_drop_position(ImVec2 pos);

    auto get_external_drop_position() const -> const ImVec2&;

    void add_external_drop_file(const std::string& file);

    void clear_external_drop_files();

    auto get_external_drop_files() const -> const std::vector<std::string>&;

private:
    std::shared_ptr<console_log_panel> console_log_panel_;
    std::unique_ptr<content_browser_panel> content_browser_panel_;
    std::unique_ptr<hierarchy_panel> hierarchy_panel_;
    std::unique_ptr<inspector_panel> inspector_panel_;
    std::unique_ptr<scene_panel> scene_panel_;
    std::unique_ptr<game_panel> game_panel_;
    std::unique_ptr<statistics_panel> statistics_panel_;
    std::unique_ptr<header_panel> header_panel_;
    std::unique_ptr<footer_panel> footer_panel_;
    std::unique_ptr<deploy_panel> deploy_panel_;
    std::unique_ptr<project_settings_panel> project_settings_panel_;
    std::unique_ptr<editor_settings_panel> editor_settings_panel_;
    std::unique_ptr<style_panel> style_panel_;

    std::unique_ptr<animation_panel> animation_panel_;

    std::unique_ptr<dockspace> cenral_dockspace_;

    struct external_drop_data
    {
        bool drop_in_progress{};
        ImVec2 drop_position{};
        std::vector<std::string> drop_files{};
    } external_drop_data_;
};
} // namespace unravel
