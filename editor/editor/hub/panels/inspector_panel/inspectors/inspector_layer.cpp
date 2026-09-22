#include "inspector_layer.h"
#include "imgui/imgui.h"
#include "inspectors.h"
#include <editor/hub/hub.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <engine/settings/settings.h>

#include <bitset>

namespace unravel
{
namespace
{
constexpr size_t LAYER_MASK_BIT_COUNT = 32;
constexpr const char* LAYER_MASK_SETTINGS_HINT = "Layers";

using layer_mask_bits = std::bitset<LAYER_MASK_BIT_COUNT>;

auto to_layer_mask_bits(const layer_mask& mask) -> layer_mask_bits
{
    return layer_mask_bits(static_cast<uint32_t>(mask.mask));
}
} // namespace

auto get_layer_mask_text(rtti::context& ctx, const layer_mask& mask) -> std::string
{
    const layer_mask_bits bits = to_layer_mask_bits(mask);
    if(bits.all())
    {
        return "Everything";
    }
    if(bits.none())
    {
        return "Nothing";
    }
    const auto& layer_names = ctx.get<settings>().layer.layers;
    std::string text;
    for(size_t i = 0; i < bits.size(); ++i)
    {
        if(!bits.test(i) || layer_names[i].empty())
        {
            continue;
        }
        if(!text.empty())
        {
            text += ", ";
        }
        text += layer_names[i];
    }
    return text.empty() ? std::string("Unnamed") : text;
}

auto draw_layer_mask_menu_items(rtti::context& ctx, layer_mask& mask) -> bool
{
    if(ImGui::MenuItemIcon(ICON_MDI_PENCIL, "Edit Layers..."))
    {
        ctx.get_cached<hub>().open_project_settings(ctx, LAYER_MASK_SETTINGS_HINT);
    }
    ImGui::Separator();
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    layer_mask_bits bits = to_layer_mask_bits(mask);
    bool is_changed = false;
    if(ImGui::MenuItem("Nothing", nullptr, bits.none()))
    {
        bits.reset();
        is_changed = true;
    }
    if(ImGui::MenuItem("Everything", nullptr, bits.all()))
    {
        bits.set();
        is_changed = true;
    }
    ImGui::Separator();
    const auto& layer_names = ctx.get<settings>().layer.layers;
    for(size_t i = 0; i < bits.size(); ++i)
    {
        if(layer_names[i].empty())
        {
            continue;
        }
        if(ImGui::MenuItem(layer_names[i].c_str(), nullptr, bits.test(i)))
        {
            bits.flip(i);
            is_changed = true;
        }
        ImGui::DrawItemActivityOutline();
    }
    ImGui::PopItemFlag();
    if(is_changed)
    {
        mask.mask = static_cast<int>(bits.to_ulong());
    }
    return is_changed;
}

auto inspector_layer::inspect(rtti::context& ctx,
                              entt::meta_any& var,
                              const meta_any_proxy& var_proxy,
                              const var_info& info,
                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<layer_mask&>();
    inspect_result result{};
    std::string preview = get_layer_mask_text(ctx, data);
    if(ImGui::CalcTextSize(preview.c_str()).x > ImGui::GetContentRegionAvail().x)
    {
        preview = "Mixed...";
    }
    if(ImGui::BeginCombo("##Type", preview.c_str()))
    {
        result.changed = draw_layer_mask_menu_items(ctx, data);
        ImGui::EndCombo();
    }
    result.edit_finished |= result.changed;
    return result;
}

} // namespace unravel
