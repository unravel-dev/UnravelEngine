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
    if(data.is_set_aside())
    {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Not used");
        ImGui::SetItemTooltip("Global illumination reflects the scene in the views that show this probe.\n"
                              "They bake and draw only Environment probes, for the reflections that reach the sky.");
    }
    else if(is_realitme && is_realitme_live)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), ICON_MDI_REFRESH " Realtime");
    }
    else if(dirty)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), ICON_MDI_REFRESH " Baking...");
    }
    else if(!data.has_capture())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Not baked");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Ready");
    }

    // "Bake Now" bakes every face as fast as the project's face budget allows, so the result shows at once.
    if(ImGui::Button(ICON_MDI_HAMMER " Bake Now"))
    {
        data.mark_dirty(true);
        result.changed = true;
    }
    ImGui::SetItemTooltip(
        "Rebuild this probe's cubemap now, ignoring its Faces Per Frame.\n"
        "The project's Reflection Probe Faces Per Frame still applies: with a low value, or other probes baking,\n"
        "the bake takes a few frames.");

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
