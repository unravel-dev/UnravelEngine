#include "game_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "../viewport_device_frame.h"
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
/// The outline that marks a playing game around a device, as thick as the focus frame around a plain image.
constexpr float PLAY_FRAME_THICKNESS = 2.0f;

/// The resolution preset the game view shows, turned to its orientation; nothing without settings or presets.
auto get_current_resolution(rtti::context& ctx) -> hpp::optional<settings::resolution_settings::resolution>
{
    if(!ctx.has<unravel::settings>())
    {
        return hpp::nullopt;
    }
    const auto& s = ctx.get<unravel::settings>().resolution;
    return viewport_resolution::get_oriented_resolution(ctx, s.get_current_resolution_index(), s.orientation);
}

/// The phone or the tablet the view shows an image of this size in, sized to the area at the cursor; nothing for
/// another preset. The toolbar's row and the margin under it stay free on every side, so that the device stays in
/// the middle and clear of the bar.
auto make_device_frame(rtti::context& ctx, const ImVec2& image_size, const ImVec2& area_size)
    -> hpp::optional<viewport_device_frame::layout>
{
    const auto resolution = get_current_resolution(ctx);
    if(!resolution || !viewport_resolution::has_device_frame(*resolution) || image_size.x <= 0.0f ||
       image_size.y <= 0.0f)
    {
        return hpp::nullopt;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    return viewport_device_frame::compute_layout(resolution->device,
                                                 image_size.x / image_size.y,
                                                 ImRect(origin, origin + area_size),
                                                 panel_toolbar::get_rows_extent(1) + panel_toolbar::get_bar_margin());
}

/// Shares of a phone's long side its camera cutout covers (on the edge the top of the phone turns to) and its rounded
/// corners cover (on the opposite edge).
constexpr float PHONE_CUTOUT_SHARE = 0.045f;
constexpr float PHONE_CORNER_SHARE = 0.03f;

/// The phone or the tablet a preset stands for, in the pixels of a view of @p view_size; nothing for a desktop or
/// free preset. Scripts then see its touch screen, its density and its cutout instead of the editor's desktop.
auto make_game_view_device(rtti::context& ctx, const usize32_t& view_size) -> hpp::optional<rendering_system::game_view_device>
{
    const auto resolution = get_current_resolution(ctx);
    const float device_dpi = resolution ? viewport_resolution::get_device_dpi(*resolution) : 0.0f;
    if(device_dpi <= 0.0f || view_size.width == 0 || view_size.height == 0)
    {
        return hpp::nullopt;
    }
    rendering_system::game_view_device device;
    const float view_scale = float(view_size.width) / float(resolution->width);
    device.dpi = device_dpi * view_scale;
    const bool has_safe_area =
        resolution->safe_left > 0 || resolution->safe_top > 0 || resolution->safe_right > 0 || resolution->safe_bottom > 0;
    if(has_safe_area)
    {
        device.inset_left = float(resolution->safe_left) * view_scale;
        device.inset_top = float(resolution->safe_top) * view_scale;
        device.inset_right = float(resolution->safe_right) * view_scale;
        device.inset_bottom = float(resolution->safe_bottom) * view_scale;
    }
    else if(resolution->device == settings::resolution_settings::device_type::phone)
    {
        if(resolution->width > resolution->height)
        {
            device.inset_left = float(view_size.width) * PHONE_CUTOUT_SHARE;
            device.inset_right = float(view_size.width) * PHONE_CORNER_SHARE;
        }
        else
        {
            device.inset_top = float(view_size.height) * PHONE_CUTOUT_SHARE;
            device.inset_bottom = float(view_size.height) * PHONE_CORNER_SHARE;
        }
    }
    return device;
}

/// Whether the camera rendered an output image the panel can show.
auto has_output_image(const camera_component& camera_comp) -> bool
{
    const auto& obuffer = camera_comp.get_render_view().fbo_find("OBUFFER");
    return obuffer && obuffer->is_valid();
}
} // namespace

auto game_panel::get_rendered_pipeline_stats(rtti::context& ctx) -> rendering::pipeline_stats
{
    rendering::pipeline_stats pstats;
    ctx.get_cached<ecs>().get_scene().registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            if(has_output_image(camera_comp))
            {
                pstats.add_stats(camera_comp.get_pipeline_data().get_pipeline()->get_stats());
            }
        });
    return pstats;
}

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

void game_panel::on_frame_update(rtti::context& ctx, const frame_times& times)
{
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    apply_view_size(ctx, scene);
    path.on_frame_update(scene, times);
}

void game_panel::set_forced_render_size(uint32_t width, uint32_t height)
{
    forced_render_width_ = width;
    forced_render_height_ = height;
}

auto game_panel::get_forced_render_size() const -> std::pair<uint32_t, uint32_t>
{
    return {forced_render_width_, forced_render_height_};
}

void game_panel::apply_view_size(rtti::context& ctx, scene& scn) const
{
    auto& rendering = ctx.get_cached<rendering_system>();
    if(forced_render_width_ > 0 && forced_render_height_ > 0)
    {
        const usize32_t forced{forced_render_width_, forced_render_height_};
        rendering.apply_game_view_size(scn, forced);
        rendering.set_game_view_device(make_game_view_device(ctx, forced));
        return;
    }
    // A device stands for the view only while the panel shows it; otherwise scripts see the editor's own screen.
    const auto resolution = get_current_resolution(ctx);
    if(!resolution || (!is_visible() && !is_visible_force_))
    {
        rendering.set_game_view_device(hpp::nullopt);
        return;
    }
    const usize32_t view_size = viewport_resolution::compute_viewport_pixels(*resolution, view_area_size_);
    if(view_size.width == 0 || view_size.height == 0)
    {
        rendering.set_game_view_device(hpp::nullopt);
        return;
    }
    rendering.apply_game_view_size(scn, view_size);
    rendering.set_game_view_device(make_game_view_device(ctx, view_size));
}

auto game_panel::get_output(rtti::context& ctx) const -> gfx::frame_buffer::ptr
{
    const entt::handle camera = get_displayed_camera(ctx);
    if(!camera)
    {
        return {};
    }
    return camera.get<camera_component>().get_render_view().fbo_find("OBUFFER");
}

auto game_panel::get_displayed_camera(rtti::context& ctx) const -> entt::handle
{
    auto& scn = ctx.get_cached<ecs>().get_scene();
    entt::handle displayed;
    scn.registry->view<camera_component>().each(
        [&](auto e, auto&& camera_comp)
        {
            if(!displayed && has_output_image(camera_comp))
            {
                displayed = scn.create_handle(e);
            }
        });
    return displayed;
}

void game_panel::on_frame_before_render(rtti::context& ctx, const frame_times& times)
{
    auto& path = ctx.get_cached<rendering_system>();
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    path.on_frame_before_render(scene, times);
}

void game_panel::on_frame_render(rtti::context& ctx, const frame_times& times)
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

    path.render_scene(scene, times, {.render_scale = view_render_scale::get_render_scale(render_scale_)});

    end_forced_frame();
}

void game_panel::on_after_render(rtti::context& ctx)
{
    auto& input = ctx.get_cached<input_system>();
    input.manager.set_is_input_allowed(is_focused());
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

    auto& ec = ctx.get_cached<ecs>();
    auto size = ImGui::GetContentRegionAvail();
    if(size.x > 0 && size.y > 0)
    {
        view_area_size_ = size;
        const ImVec2 area_origin = ImGui::GetCursorScreenPos();
        const ImRect toolbar_area(area_origin, area_origin + size);
        update_toolbar_visibility(ctx, toolbar_area);

        const auto pstats = get_rendered_pipeline_stats(ctx);

        if(const auto displayed_camera = get_displayed_camera(ctx))
        {
            draw_camera_image(ctx, displayed_camera, size);
        }
        else
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

void game_panel::draw_camera_image(rtti::context& ctx, entt::handle camera, const ImVec2& area_size)
{
    auto& camera_comp = camera.get<camera_component>();
    const auto& obuffer = camera_comp.get_render_view().fbo_find("OBUFFER");
    auto tex = obuffer->get_texture(0);
    auto tex_size = obuffer->get_size();
    ImVec2 tex_size_v(tex_size.width, tex_size.height);
    const auto device_frame = make_device_frame(ctx, tex_size_v, area_size);
    if(device_frame)
    {
        viewport_device_frame::draw(*device_frame, ImGui::ToId(tex));
        // A preset with an aspect and no pixels renders at the size the screen of the device shows it at.
        view_area_size_ = device_frame->screen.GetSize();
    }
    else
    {
        ImGui::ImageWithAspect(ImGui::ToId(tex), tex_size_v, area_size, ImVec2(0.5f, 0.5f));
    }

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    // The scene renders at the render size, which the GI visualization's pixels are in.
    const auto& scene_depth = camera_comp.get_render_view().tex_find("DEPTH");
    const auto render_size = scene_depth ? scene_depth->get_size() : camera_comp.get_camera().get_viewport_size();
    const auto gi_visualize = visualization_menu::make_gi_visualize(visualization_menu_state_, min, max, render_size);

    input::zone work_zone{};
    work_zone.x = min.x;
    work_zone.y = min.y;
    work_zone.w = max.x - min.x;
    work_zone.h = max.y - min.y;
    auto& input = ctx.get_cached<input_system>();
    input.manager.set_work_zone(work_zone);
    input.manager.set_reference_size({tex_size_v.x, tex_size_v.y});
    update_cursor_hold(ctx, ImGui::IsItemHovered());

    if(ctx.get_cached<play_mode>().is_active())
    {
        if(device_frame)
        {
            viewport_device_frame::draw_outline(*device_frame, ImGui::GetColorU32(ImGuiCol_NavCursor), PLAY_FRAME_THICKNESS);
        }
        else
        {
            ImVec2 padding(2.0f, 2.0f);
            ImGui::RenderFocusFrame(min - padding, max + padding);
        }
    }

    const auto& pipeline = camera_comp.get_pipeline_data().get_pipeline();
    pipeline->set_debug_pass(visualize_passes_);
    pipeline->set_gi_visualize_settings(gi_visualize);
    visualization_menu::draw_view_labels(visualize_passes_,
                                         visualization_menu_state_,
                                         pipeline->get_debug_view_labels(),
                                         min,
                                         max);
}


void game_panel::update_cursor_hold(rtti::context& ctx, bool is_view_hovered)
{
    auto& input = ctx.get_cached<input_system>();
    const auto& play = ctx.get_cached<play_mode>();
    const bool is_running = play.is_active() && !play.is_paused();
    if(!is_running)
    {
        is_cursor_released_ = false;
    }
    else if(is_focused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        is_cursor_released_ = true;
    }
    else if(is_view_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        is_cursor_released_ = false;
    }
    auto& cursor = input.manager.get_cursor();
    cursor.set_suspended(!is_running || !is_focused() || is_cursor_released_);
    // The editor's UI sets the OS cursor every frame; the game's request goes through it.
    if(is_view_hovered && cursor.is_hidden())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
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
        view_render_scale::draw_toolbar_dropdown(ctx, render_scale_, toolbar_layout_.is_compact);
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
    auto& resolution = pm.get_settings().resolution;
    int index = resolution.get_current_resolution_index();
    auto orientation = resolution.orientation;
    if(viewport_resolution::draw_toolbar_dropdown(ctx, index, orientation, toolbar_layout_.is_compact))
    {
        resolution.set_current_resolution_index(index);
        resolution.orientation = orientation;
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
