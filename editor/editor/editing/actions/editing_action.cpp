#include "editing_action.h"
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

namespace unravel
{

// No implementation needed for the base class - all methods are virtual
void editing_action_t::draw_in_inspector_impl(rtti::context& ctx, const entt::meta_any& old_value, const entt::meta_any& new_value, const entt::meta_custom& custom)
{
    var_info info;
    info.read_only = true;
    info.is_property = true;

    entt::meta_any old_value_copy = old_value;
    entt::meta_any new_value_copy = new_value;
    ImGui::SetNextWindowSizeConstraints({}, {400.0f, ImGui::GetContentRegionAvail().y});
    ImGui::BeginChild("##tooltip_child", {}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    

    inspect_var(ctx, old_value_copy, make_proxy(old_value_copy), info, custom);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::Text(" %s ", ICON_MDI_ARROW_RIGHT);
    ImGui::SameLine();
    ImGui::SetNextWindowSizeConstraints({}, {400.0f, ImGui::GetContentRegionAvail().y});
    ImGui::BeginChild("##tooltip_child2", {}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(ImGui::Font::Bold);
    info.read_only = false;

    inspect_var(ctx, new_value_copy, make_proxy(new_value_copy), info, custom);
    ImGui::PopFont();
    ImGui::EndChild();
}

} // namespace unravel
