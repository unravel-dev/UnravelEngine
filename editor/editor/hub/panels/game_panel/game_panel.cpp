#include "game_panel.h"
#include "../panels_defs.h"
#include "../../hub.h"
#include "imgui/imgui.h"
#include "imgui_widgets/utils.h"

#include <algorithm>
#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/input/input.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/settings/settings.h>


namespace unravel
{

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
    if(!is_visible_ && !is_visible_force_)
    {
        return;
    }
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    auto& path = ctx.get_cached<rendering_system>();

    path.render_scene(scene, dt);

    is_visible_force_ = false;
}

void game_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    auto& input = ctx.get_cached<input_system>();

    bool allowed = true;
    if(ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        set_visible(true);
        draw_ui(ctx);

        allowed &= ImGui::IsWindowFocused();
    }
    else
    {
        allowed = false;
        set_visible(false);
    }
    ImGui::End();

    input.manager.set_is_input_allowed(allowed);
}

void game_panel::set_visible(bool visible)
{
    is_visible_ = visible;
}

void game_panel::set_visible_force(bool visible)
{
    is_visible_force_ = visible;
}

void game_panel::apply_resolution_to_camera(camera_component& camera_comp, const settings::resolution_settings::resolution& res, ImVec2 avail_size)
{
    ImVec2 viewport = avail_size;
    if(res.aspect == 0.0f)
    {
        // Free aspect
        viewport = avail_size;
    }
    else if(res.width > 0 && res.height > 0)
    {
        // Fixed resolution
        viewport.x = static_cast<float>(res.width);
        viewport.y = static_cast<float>(res.height);
    }
    else if(res.aspect > 0.0f)
    {
        float avail_aspect = avail_size.x / std::max(avail_size.y, 1.0f);
        if(avail_aspect > res.aspect)
        {
            viewport.y = avail_size.y;
            viewport.x = avail_size.y * res.aspect;
        }
        else
        {
            viewport.x = avail_size.x;
            viewport.y = avail_size.x / res.aspect;
        }
    }
    camera_comp.set_viewport_size({static_cast<std::uint32_t>(viewport.x), static_cast<std::uint32_t>(viewport.y)});
}

void game_panel::draw_ui(rtti::context& ctx)
{
    if(!ctx.has<unravel::settings>())
    {
        return;
    }

    draw_menubar(ctx);
    auto& ec = ctx.get_cached<ecs>();
    auto& ev = ctx.get_cached<events>();
    auto size = ImGui::GetContentRegionAvail();
    auto& settings = ctx.get<unravel::settings>();
    if(settings.resolution.resolutions.empty())
    {
        return;
    }
    const auto& resolutions = settings.resolution.resolutions;
    if(size.x > 0 && size.y > 0)
    {
        bool rendered = false;
        ec.get_scene().registry->view<camera_component>().each(
            [&](auto e, auto&& camera_comp)
            {
                apply_resolution_to_camera(camera_comp, resolutions[std::clamp(current_resolution_index_, 0, (int)resolutions.size()-1)], size);

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

                    if(ev.is_playing)
                    {
                        ImVec2 padding(2.0f, 2.0f);
                        ImGui::RenderFocusFrame(ImGui::GetItemRectMin() - padding, ImGui::GetItemRectMax() + padding);
                    }
                    rendered = true;

                    camera_comp.get_pipeline_data().get_pipeline()->set_debug_pass(visualize_passes_);
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
    }
}

void game_panel::draw_menubar(rtti::context& ctx)
{
    auto& settings = ctx.get<unravel::settings>();
    if(settings.resolution.resolutions.empty()) 
    {
        return;
    }
    const auto& resolutions = settings.resolution.resolutions;
    if(ImGui::BeginMenuBar())
    {
        if(ImGui::BeginMenu(ICON_MDI_DRAWING_BOX ICON_MDI_ARROW_DOWN_BOLD))
        {
            ImGui::RadioButton("Full", &visualize_passes_, -1);
            ImGui::RadioButton("Base Color", &visualize_passes_, 0);
            ImGui::RadioButton("Diffuse Color", &visualize_passes_, 1);
            ImGui::RadioButton("Specular Color", &visualize_passes_, 2);
            ImGui::RadioButton("Indirect Specular Color", &visualize_passes_, 3);
            ImGui::RadioButton("Ambient Occlusion", &visualize_passes_, 4);
            ImGui::RadioButton("Normals (World Space)", &visualize_passes_, 5);
            ImGui::RadioButton("Roughness", &visualize_passes_, 6);
            ImGui::RadioButton("Metalness", &visualize_passes_, 7);
            ImGui::RadioButton("Emissive Color", &visualize_passes_, 8);
            ImGui::RadioButton("Subsurface Color", &visualize_passes_, 9);
            ImGui::RadioButton("Depth", &visualize_passes_, 10);
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltipEx("%s", "Visualize Render Passes");
        if(ImGui::BeginMenu(fmt::format("{} {}", resolutions[std::clamp(current_resolution_index_, 0, (int)resolutions.size()-1)].name, ICON_MDI_ARROW_DOWN_BOLD).c_str()))
        {
            for(int i = 0; i < (int)resolutions.size(); ++i)
            {
                if(ImGui::RadioButton(resolutions[i].name.c_str(), &current_resolution_index_, i)) {}
            }

            if(ImGui::MenuItem("Edit ...", "", false))
            {
                ctx.get_cached<hub>().open_project_settings(ctx, "Resolution");
            }
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltipEx("%s", "Resolution Presets");

        ImGui::PushFont(ImGui::Font::Mono);
        auto& io = ImGui::GetIO();

        auto fps_size = ImGui::CalcTextSize(fmt::format("{:.1f}", io.Framerate).c_str()).x;
        ImGui::PopFont();

        ImGui::SameLine();

        ImGui::AlignedItem(1.0f,
                           ImGui::GetContentRegionAvail().x,
                           fps_size,
                           [&]()
                           {
                               ImGui::PushFont(ImGui::Font::Mono);
                               ImGui::Text("%.1f", io.Framerate);
                               ImGui::PopFont();
                           });

        ImGui::EndMenuBar();
    }
}

} // namespace unravel
