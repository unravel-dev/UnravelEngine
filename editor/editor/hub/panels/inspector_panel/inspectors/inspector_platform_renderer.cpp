#include "inspector_platform_renderer.h"

#include "imgui/imgui.h"

namespace unravel
{
namespace
{

auto inspect_platform_renderer_combo(const char* label,
                                     preferred_renderer& value,
                                     renderer_platform platform,
                                     bool read_only) -> inspect_result
{
    inspect_result result{};
    const hpp::span<const preferred_renderer> options = preferred_renderers_for_platform(platform);
    if(!is_preferred_renderer_available_on(value, platform))
    {
        value = preferred_renderer::auto_detect;
        result.changed = true;
        result.edit_finished = true;
    }
    ImGui::PushID(static_cast<int>(platform));
    {
        // property_layout PushID/PopID must nest inside this scope before our PopID.
        property_layout layout(label,
                               "Preferred renderer for this platform. Requires an editor restart to apply.");
        if(read_only)
        {
            ImGui::TextUnformatted(preferred_renderer_pretty_name(value).data());
        }
        else if(ImGui::BeginCombo("##renderer", preferred_renderer_pretty_name(value).data()))
        {
            for(const preferred_renderer candidate : options)
            {
                const bool is_selected = candidate == value;
                if(ImGui::Selectable(preferred_renderer_pretty_name(candidate).data(), is_selected))
                {
                    if(candidate != value)
                    {
                        value = candidate;
                        result.changed = true;
                        result.edit_finished = true;
                    }
                }
                if(is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if(!read_only)
        {
            ImGui::DrawItemActivityOutline();
        }
    }
    ImGui::PopID();
    return result;
}

} // namespace

auto inspector_platform_renderer_settings::inspect(rtti::context& ctx,
                                                   entt::meta_any& var,
                                                   const meta_any_proxy& var_proxy,
                                                   const var_info& info,
                                                   const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<platform_renderer_settings&>();
    inspect_result result{};
    result |= inspect_platform_renderer_combo("Windows", data.windows, renderer_platform::windows, info.read_only);
    result |= inspect_platform_renderer_combo("Linux", data.linux, renderer_platform::linux, info.read_only);
    result |= inspect_platform_renderer_combo("macOS", data.macos, renderer_platform::macos, info.read_only);
    return result;
}

} // namespace unravel
