#include "viewport_resolution.h"
#include "../hub.h"

#include <engine/rendering/ecs/components/camera_component.h>

#include <imgui/imgui.h>

#include <algorithm>

namespace unravel::viewport_resolution
{

namespace
{
auto clamp_index(int index, int count) -> int
{
    if(count <= 0)
    {
        return 0;
    }
    return std::clamp(index, 0, count - 1);
}
} // namespace

auto compute_viewport_size(const settings::resolution_settings::resolution& res, ImVec2 avail_size) -> ImVec2
{
    if(res.aspect == 0.0f)
    {
        return avail_size;
    }

    if(res.width > 0 && res.height > 0)
    {
        return ImVec2(static_cast<float>(res.width), static_cast<float>(res.height));
    }

    return compute_fitted_size(res, avail_size);
}

auto compute_fitted_size(const settings::resolution_settings::resolution& res, ImVec2 avail_size) -> ImVec2
{
    if(res.aspect <= 0.0f)
    {
        return avail_size;
    }

    const float avail_aspect = avail_size.x / std::max(avail_size.y, 1.0f);
    if(avail_aspect > res.aspect)
    {
        return ImVec2(avail_size.y * res.aspect, avail_size.y);
    }
    return ImVec2(avail_size.x, avail_size.x / res.aspect);
}

void apply_to_camera(camera_component& camera_comp,
                     const settings::resolution_settings::resolution& res,
                     ImVec2 avail_size)
{
    const ImVec2 viewport = compute_viewport_size(res, avail_size);
    camera_comp.set_viewport_size({static_cast<std::uint32_t>(viewport.x), static_cast<std::uint32_t>(viewport.y)});
}

auto get_resolution(rtti::context& ctx, int index) -> const settings::resolution_settings::resolution*
{
    if(!ctx.has<unravel::settings>())
    {
        return nullptr;
    }

    const auto& resolutions = ctx.get<unravel::settings>().resolution.resolutions;
    if(resolutions.empty())
    {
        return nullptr;
    }

    return &resolutions[clamp_index(index, static_cast<int>(resolutions.size()))];
}

auto draw_menu(rtti::context& ctx, int& current_index) -> bool
{
    if(!ctx.has<unravel::settings>())
    {
        return false;
    }

    const auto& resolutions = ctx.get<unravel::settings>().resolution.resolutions;
    if(resolutions.empty())
    {
        return false;
    }

    current_index = clamp_index(current_index, static_cast<int>(resolutions.size()));

    bool changed = false;
    const auto label = fmt::format("{} {}", resolutions[current_index].name, ICON_MDI_ARROW_DOWN_BOLD);
    if(ImGui::BeginMenu(label.c_str()))
    {
        for(int i = 0; i < static_cast<int>(resolutions.size()); ++i)
        {
            if(ImGui::RadioButton(resolutions[i].name.c_str(), &current_index, i))
            {
                changed = true;
            }
        }

        if(ImGui::MenuItem("Edit ...", "", false))
        {
            ctx.get_cached<hub>().open_project_settings(ctx, "Resolution");
        }
        ImGui::EndMenu();
    }
    ImGui::SetItemTooltipEx("%s", "Resolution Presets");

    return changed;
}

} // namespace unravel::viewport_resolution
