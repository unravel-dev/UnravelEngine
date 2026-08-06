#include "game_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "../viewport_resolution.h"
#include "imgui/imgui.h"
#include "imgui_widgets/utils.h"
#include <engine/engine.h>

#include <algorithm>
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

void game_panel::draw_ui(rtti::context& ctx)
{
    draw_menubar(ctx);

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
      
                            
        viewport_stats_overlay::draw(pstats, stats_overlay_state_, "game");

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

void game_panel::draw_menubar(rtti::context& ctx)
{
    if(ImGui::BeginMenuBar())
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_TabSelected));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline));

        if(ctx.has<unravel::settings>())
        {
            auto& pm = ctx.get_cached<project_manager>();
            auto& s = pm.get_settings();
            int index = s.resolution.get_current_resolution_index();
            if(viewport_resolution::draw_menu(ctx, index))
            {
                s.resolution.set_current_resolution_index(index);
                pm.save_project_settings(ctx);
            }
        }

        if(ImGui::BeginMenu(ICON_MDI_DRAWING_BOX ICON_MDI_ARROW_DOWN_BOLD))
        {
            ImGui::RadioButton("Full", &visualize_passes_, -1);
            ImGui::RadioButton("Base Color", &visualize_passes_, 0);
            ImGui::RadioButton("Diffuse Color", &visualize_passes_, 1);
            ImGui::RadioButton("Specular Color", &visualize_passes_, 2);
            ImGui::RadioButton("Radiance", &visualize_passes_, 3);
            ImGui::RadioButton("Irradiance", &visualize_passes_, 4);
            ImGui::RadioButton("Ambient Occlusion", &visualize_passes_, 5);
            ImGui::RadioButton("Normals (World Space)", &visualize_passes_, 6);
            ImGui::RadioButton("Roughness", &visualize_passes_, 7);
            ImGui::RadioButton("Metalness", &visualize_passes_, 8);
            ImGui::RadioButton("Emissive Color", &visualize_passes_, 9);
            ImGui::RadioButton("Subsurface Color", &visualize_passes_, 10);
            ImGui::RadioButton("Depth", &visualize_passes_, 11);
            ImGui::RadioButton("SSIL", &visualize_passes_, 12);
            ImGui::RadioButton("SDF (Normals)", &visualize_passes_, 15);
            ImGui::RadioButton("SDF (Step Count)", &visualize_passes_, 16);
            ImGui::RadioButton("SDF (Headers)", &visualize_passes_, 17);
            ImGui::RadioButton("SDF (Probe)", &visualize_passes_, 18);
            ImGui::RadioButton("SDF (Entry)", &visualize_passes_, 19);
            ImGui::RadioButton("SDF (Clipmap)", &visualize_passes_, 20);
            ImGui::RadioButton("SDF (Direct Light)", &visualize_passes_, 21);
            ImGui::RadioButton("SDF (Cascade Levels)", &visualize_passes_, 22);
            ImGui::RadioButton("SDF (Attr Albedo)", &visualize_passes_, 23);
            ImGui::RadioButton("SDF (Light Voxels)", &visualize_passes_, 24);
            ImGui::RadioButton("SDF (World Probes)", &visualize_passes_, 25);
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltipEx("%s", "Visualize Render Passes");

        auto& ui    = ctx.get_cached<ui_system>();
        bool debugger_enabled = ui.is_debugger_enabled();
        const char* modes[] = {ICON_MDI_BUG_CHECK " UI Debugger", ICON_MDI_BUG " UI Debugger"};
        const char* debugger_preview = modes[int(!debugger_enabled)];

        if(debugger_enabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        }
        if(ImGui::MenuItem(debugger_preview, "", debugger_enabled))
        {
            ui.set_debugger_enabled(!debugger_enabled);
        }
        if(debugger_enabled)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SetItemTooltipEx("%s", debugger_enabled ? "Hide UI Debugger" : "Show UI Debugger");

        viewport_stats_overlay::draw_stats_toggle(stats_overlay_state_);

        ImGui::PopStyleColor(3);

        ImGui::EndMenuBar();
    }
}

} // namespace unravel
