#include "game_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "../viewport_resolution.h"
#include "../panel_toolbar.h"
#include "../visualization_menu.h"
#include "imgui/imgui.h"
#include "imgui_widgets/utils.h"
#include <engine/engine.h>

#include <algorithm>
#include <string>
#include <editor/system/project_manager.h>
#include <engine/ecs/ecs.h>
#include <engine/play_mode.h>
#include <engine/input/input.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/settings/settings.h>
#include <engine/ui/ecs/systems/ui_system.h>

namespace unravel
{
namespace
{
constexpr const char* GAME_TOOLBAR_ID = "##game_toolbar";
constexpr int GAME_TOOLBAR_FIRST_ROW_BARS = 1;
// While the game plays, the pointer reveals the toolbar within this many bar heights of the top
// edge, and the fade takes this long.
constexpr float GAME_TOOLBAR_REVEAL_ROWS = 2.0f;
constexpr float GAME_TOOLBAR_FADE_SECONDS = 0.12f;
} // namespace

game_panel::game_panel(imgui_panels* parent, const char* name)
    : panel_base(name)
    , parent_(parent)
{
}

void game_panel::init(rtti::context& ctx)
{
}

void game_panel::deinit(rtti::context& ctx)
{
}

void game_panel::on_frame_update(rtti::context& ctx, delta_t dt)
{
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    path.on_frame_update(scene, dt);
}

void game_panel::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    path.on_frame_before_render(scene, dt);
}

void game_panel::on_frame_render(rtti::context& ctx, delta_t dt)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    auto& path = ctx.get_cached<rendering_system>();

    if(!is_visible() && !is_visible_force_)
    {
        path.release_pipeline_resources(scene);
        return;
    }

    if(m_skip_frames_ > 0)
    {
        m_skip_frames_--;
        return;
    }

    path.render_scene(scene, dt);

    is_visible_force_ = false;
}

void game_panel::on_after_render(rtti::context& ctx)
{
    auto& input = ctx.get_cached<input_system>();
    input.manager.set_is_input_allowed(is_focused());
}

void game_panel::set_visible_force(bool visible)
{
    is_visible_force_ = visible;
}

void game_panel::on_project_opened()
{
    // m_skip_frames_ = 100;
}

auto game_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_None;
}

void game_panel::draw_ui(rtti::context& ctx)
{
    if(m_skip_frames_ > 0)
    {
        auto spinner_size = ImGui::GetContentRegionAvail().y * 0.2f;

        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - spinner_size * 0.5f);
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           spinner_size,
                           [spinner_size]()
                           {
                                ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
                                    ImSpinner::Radius{spinner_size * 0.5f},
                                    ImSpinner::Thickness{6.0f},
                                    ImSpinner::Color{ImSpinner::white},
                                    ImSpinner::Speed{6.0f});

                           });
        return;
    }

    if(!ctx.has<unravel::settings>())
    {
        return;
    }

    const auto& s = ctx.get<unravel::settings>();
    const auto* current_res = viewport_resolution::get_resolution(ctx, s.resolution.get_current_resolution_index());
    if(!current_res)
    {
        return;
    }

    auto& ec = ctx.get_cached<ecs>();
    auto& play = ctx.get_cached<play_mode>();
    auto size = ImGui::GetContentRegionAvail();
    if(size.x > 0 && size.y > 0)
    {
        const ImVec2 area_origin = ImGui::GetCursorScreenPos();
        const ImRect toolbar_area(area_origin, area_origin + size);
        update_toolbar_visibility(ctx, toolbar_area);

        bool rendered = false;
        rendering::pipeline_stats pstats;
        ec.get_scene().registry->view<camera_component>().each(
            [&](auto e, auto&& camera_comp)
            {
                viewport_resolution::apply_to_camera(camera_comp, *current_res, size);

                const auto& camera = camera_comp.get_camera();
                const auto& rview = camera_comp.get_render_view();
                const auto& obuffer = rview.fbo_safe_get("OBUFFER");

                if(obuffer)
                {
                    auto tex = obuffer->get_texture(0);
                    auto tex_size = obuffer->get_size();
                    ImVec2 tex_size_v(tex_size.width, tex_size.height);
                    ImGui::ImageWithAspect(ImGui::ToId(tex), tex_size_v, size, ImVec2(0.5f, 0.5f));

                    ImVec2 min = ImGui::GetItemRectMin();
                    ImVec2 max = ImGui::GetItemRectMax();

                    input::zone work_zone{};
                    work_zone.x = min.x;
                    work_zone.y = min.y;
                    work_zone.w = max.x - min.x;
                    work_zone.h = max.y - min.y;
                    
               
                    ctx.get_cached<input_system>().manager.set_work_zone(work_zone);
                    ctx.get_cached<input_system>().manager.set_reference_size({tex_size_v.x, tex_size_v.y});

                    if(play.is_active())
                    {
                        ImVec2 padding(2.0f, 2.0f);
                        ImGui::RenderFocusFrame(ImGui::GetItemRectMin() - padding, ImGui::GetItemRectMax() + padding);
                    }
                    rendered = true;

                    const auto& pipeline = camera_comp.get_pipeline_data().get_pipeline();
                    pipeline->set_debug_pass(visualize_passes_);
                    pstats.add_stats(pipeline->get_stats());
                }
            });

        if(!rendered)
        {
            static const auto text = "No cameras rendering";
            ImGui::SetCursorPosY(size.y * 0.5f);
            ImGui::AlignedItem(0.5f,
                               size.x,
                               ImGui::CalcTextSize(text).x,
                               []()
                               {
                                   ImGui::TextUnformatted(text);
                               });
        }
      
                            
        // After the image: the shadow of the bar goes into this window's draw list, on top of it.
        draw_toolbar(ctx, toolbar_area);

        viewport_stats_overlay::draw(ctx, pstats, stats_overlay_state_, "game", panel_toolbar::get_rows_extent(1));
        visualization_menu::draw_legend_overlay(visualize_passes_, visualization_menu_state_, "game");

        if(stats_overlay_state_.open_profiler_requested)
        {
            stats_overlay_state_.open_profiler_requested = false;
            parent_->get_profiler_timeline_panel().show(true);
        }
    }
}


auto game_panel::begin_panel(const char* name, ImGuiWindowFlags flags) -> bool
{
    auto& ctx = engine::context();
    auto& play = ctx.get_cached<play_mode>();
    bool is_playing = play.is_active();
    ImVec2 padding(is_playing ? 1.0f : 0.0f, is_playing ? 1.0f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    bool open = panel_base::begin_panel(name, flags);
    ImGui::PopStyleVar();
    
    return open;
}

void game_panel::update_toolbar_visibility(rtti::context& ctx, const ImRect& area)
{
    auto& play = ctx.get_cached<play_mode>();
    const float reveal_height = panel_toolbar::get_rows_extent(1) * GAME_TOOLBAR_REVEAL_ROWS;
    const ImVec2 reveal_max(area.Max.x, area.Min.y + reveal_height);
    const bool is_pointer_at_top = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                                   ImGui::IsMouseHoveringRect(area.Min, reveal_max, false);
    const bool is_revealed = !play.is_active() || is_pointer_at_top || panel_toolbar::is_dropdown_open();
    const float fade_step = ImGui::GetIO().DeltaTime / GAME_TOOLBAR_FADE_SECONDS;
    toolbar_alpha_ = ImClamp(toolbar_alpha_ + (is_revealed ? fade_step : -fade_step), 0.0f, 1.0f);
}

void game_panel::draw_toolbar(rtti::context& ctx, const ImRect& area)
{
    panel_toolbar::bar_placement placement{};
    placement.area = area;
    placement.anchor = panel_toolbar::bar_anchor::right;
    // The frame rate stays on screen while the bar is out of the way of the game: a passive
    // readout in the place the statistics toggle shows it, so the two fade into each other.
    if(toolbar_alpha_ < 1.0f)
    {
        placement.alpha = 1.0f - toolbar_alpha_;
        viewport_stats_overlay::draw_toolbar_readout(placement);
    }
    // A faded-out bar is not submitted at all: an invisible one would still take the clicks
    // meant for the game.
    if(toolbar_alpha_ <= 0.0f)
    {
        return;
    }
    const float bar_width = panel_toolbar::get_bar_width(GAME_TOOLBAR_ID);
    panel_toolbar::update_layout(toolbar_layout_, bar_width, GAME_TOOLBAR_FIRST_ROW_BARS, area.GetWidth());
    placement.alpha = toolbar_alpha_;
    if(panel_toolbar::begin_bar(GAME_TOOLBAR_ID, placement))
    {
        draw_resolution_dropdown(ctx);
        panel_toolbar::separator();
        visualization_menu::draw_toolbar_dropdown(visualize_passes_, visualization_menu_state_);
        draw_ui_debugger_toggle(ctx);
        panel_toolbar::separator();
        viewport_stats_overlay::draw_toolbar_toggle(stats_overlay_state_);
    }
    panel_toolbar::end_bar();
}

void game_panel::draw_resolution_dropdown(rtti::context& ctx)
{
    if(!ctx.has<unravel::settings>())
    {
        return;
    }
    // The game view shows the resolution the project ships with, so the pick goes to its settings.
    auto& pm = ctx.get_cached<project_manager>();
    auto& project_settings = pm.get_settings();
    int index = project_settings.resolution.get_current_resolution_index();
    if(viewport_resolution::draw_toolbar_dropdown(ctx, index, toolbar_layout_.is_compact))
    {
        project_settings.resolution.set_current_resolution_index(index);
        pm.save_project_settings(ctx);
    }
}

void game_panel::draw_ui_debugger_toggle(rtti::context& ctx)
{
    auto& ui = ctx.get_cached<ui_system>();
    const bool is_enabled = ui.is_debugger_enabled();
    const char* icon = is_enabled ? ICON_MDI_BUG_CHECK : ICON_MDI_BUG;
    const std::string text = panel_toolbar::make_text(icon, "UI Debugger", toolbar_layout_.is_compact);
    const char* tooltip = is_enabled ? "Hide UI Debugger" : "Show UI Debugger";
    if(panel_toolbar::toggle("##ui_debugger", text.c_str(), is_enabled, tooltip))
    {
        ui.set_debugger_enabled(!is_enabled);
    }
}

} // namespace unravel
