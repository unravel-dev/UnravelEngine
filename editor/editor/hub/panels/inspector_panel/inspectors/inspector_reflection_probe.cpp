#include "inspector_reflection_probe.h"
#include "inspectors.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>

namespace unravel
{

auto inspector_reflection_probe_component::inspect(rtti::context& ctx,
                                                   entt::meta_any& var,
                                                   const meta_any_proxy& var_proxy,
                                                   const var_info& info,
                                                   const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result;
    auto& data = var.cast<reflection_probe_component&>();

    // Status line: shows whether a bake is currently in flight.
    bool is_realitme = data.get_update_mode() == probe_update_mode::realtime;
    bool is_realitme_live = data.get_update_interval() < 0.01f;
    const bool dirty = data.is_dirty();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    if(is_realitme && is_realitme_live)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), ICON_MDI_REFRESH " Realtime");
    }
    else if(dirty)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), ICON_MDI_REFRESH " Baking...");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Ready");
    }

    // "Bake Now" triggers a full 1-frame rebuild so the user sees an instant result.
    if(ImGui::Button(ICON_MDI_HAMMER " Bake Now"))
    {
        data.mark_dirty(true);
        result.changed = true;
    }
    ImGui::SetItemTooltip(
        "Force an immediate full rebuild of this probe's cubemap in the next frame.\n"
        "Ignores time-slicing so the result is visible right away.");

    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_RELOAD " Refresh"))
    {
        data.mark_dirty(false);
        result.changed = true;
    }
    ImGui::SetItemTooltip(
        "Schedule a time-sliced refresh. Faces will be rebuilt over multiple frames\n"
        "according to Faces Per Frame, avoiding editor hitches.");

    ImGui::Separator();

    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);

    return result;
}

} // namespace unravel
