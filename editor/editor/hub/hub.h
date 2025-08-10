#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <ospp/event.h>

#include "panels/panel.h"

namespace unravel
{

class hub
{
public:
    hub(rtti::context& ctx);
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void open_project_settings(rtti::context& ctx, const std::string& hint);
private:
    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_frame_before_render(rtti::context& ctx, delta_t dt);
    void on_frame_render(rtti::context& ctx, delta_t dt);
    void on_frame_ui_render(rtti::context& ctx, delta_t dt);
    void on_play_begin(rtti::context& ctx);
    void on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version);
    void on_os_event(rtti::context& ctx, os::event& e);

    void on_start_page_render(rtti::context& ctx);
    void on_opened_project_render(rtti::context& ctx);

    void render_projects_list_view(rtti::context& ctx);
    void render_new_project_creator_view(rtti::context& ctx);
    void render_project_remover_view(rtti::context& ctx);
    
    // Draw a project card with consistent styling
    auto draw_project_card(const std::string& id, 
                          const std::string& name, 
                          const std::string& directory, 
                          const std::chrono::system_clock::time_point& last_modified,
                          bool is_selected = false,
                          bool enable_interaction = true,
                          float form_width = 0.0f) -> bool;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    imgui_panels panels_{};

    enum class view_state
    {
        projects_list,
        new_project_creator,
        project_remover
    };

    view_state current_view_{view_state::projects_list};
    std::string project_name_{};
    std::string project_directory_{};
    std::string project_to_remove_{};
    std::string selected_project_{};
};
} // namespace unravel
