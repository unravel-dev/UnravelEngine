#include "inspector_alignment.h"
#include "imgui/imgui.h"
#include "inspectors.h"

namespace unravel
{
namespace
{
struct align_info
{
    const char* icon;
    uint32_t flag;
    const char* tooltip;
};

constexpr align_info haligns[] = {
    {ICON_MDI_FORMAT_ALIGN_LEFT, align::left, "Top"},
    {ICON_MDI_FORMAT_ALIGN_CENTER, align::center, "Center"},
    {ICON_MDI_FORMAT_ALIGN_RIGHT, align::right, "Right"},
};

constexpr align_info valigns[] = {
    {ICON_MDI_FORMAT_ALIGN_TOP, align::top, "Top"},
    {ICON_MDI_FORMAT_ALIGN_MIDDLE, align::middle, "Middle"},
    {ICON_MDI_FORMAT_ALIGN_BOTTOM, align::bottom, "Bottom"},
    {ICON_MDI_ALIGN_VERTICAL_TOP, align::capline, "Capline"},
    {ICON_MDI_ALIGN_VERTICAL_CENTER, align::midline, "Midline"},
    {ICON_MDI_ALIGN_VERTICAL_BOTTOM, align::baseline, "Baseline"},
};
} // namespace
auto inspector_alignment::inspect(rtti::context& ctx,
                                  rttr::variant& var,
                                  const var_info& info,
                                  const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<alignment>();

    // fetch current
    uint32_t alignment = data.flags;

    // Horizontal row
    for(auto [icon, flag, tooltip] : haligns)
    {
        bool active = (alignment & flag) != 0;

        if(ImGui::Button(icon))
        {
            // Clear only the H bits, leave the V bits untouched
            alignment = (alignment & ~align::horizontal_mask) | flag;
        }
        if(active)
            ImGui::RenderFocusFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        ImGui::SetItemTooltipEx("%s", tooltip);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    // Vertical row
    for(auto [icon, flag, tooltip] : valigns)
    {
        bool active = (alignment & flag) != 0;

        if(ImGui::Button(icon))
        {
            // Clear only the V bits, leave the H bits untouched
            alignment = (alignment & ~align::vertical_text_mask) | flag;
        }
        if(active)
            ImGui::RenderFocusFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        ImGui::SetItemTooltipEx("%s", tooltip);

        ImGui::SameLine();
    }

    inspect_result result{};
    // write back if changed
    result.changed = alignment != data.flags;

    if(result.changed)
    {
        result.edit_finished |= true;

        data.flags = alignment;
        var = data;
    }

    return result;
}

namespace
{
struct style_info
{
    const char* icon;
    uint32_t flag;
    const char* tooltip;
};

constexpr style_info style_flags[] = {
    {ICON_MDI_FORMAT_OVERLINE, gfx::style_overline, "Overline"},
    {ICON_MDI_FORMAT_UNDERLINE, gfx::style_underline, "Underline"},
    {ICON_MDI_FORMAT_STRIKETHROUGH_VARIANT, gfx::style_strike_through, "Strike-through"},
    {ICON_MDI_FORMAT_COLOR_FILL, gfx::style_background, "Background"},
    {ICON_MDI_FORMAT_COLOR_TEXT, gfx::style_foreground, "Foreground"},
};
} // namespace

auto inspector_text_style_flags::inspect(rtti::context& ctx,
                                   rttr::variant& var,
                                   const var_info& info,
                                   const meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<text_style_flags>();

    inspect_result result;
    for(auto [icon, flag, tooltip] : style_flags)
    {
        bool active = (data.flags & flag) != 0;
        if(ImGui::Button(icon))
        {
            data.flags ^= flag;
            result.changed = true;
        }
        if(active)
            ImGui::RenderFocusFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SetItemTooltipEx("%s", tooltip);
        ImGui::SameLine();
    }

    if(result.changed)
    {
        var = data;
    }

    return result;
}
void inspector_text_style::before_inspect(const rttr::property& prop)
{
    layout_ = std::make_unique<property_layout>();
    layout_->set_data(prop, false);
    open_ = layout_->push_tree_layout(ImGuiTreeNodeFlags_SpanFullWidth);
}

auto inspector_text_style::inspect(rtti::context& ctx,
                                   rttr::variant& var,
                                   const var_info& info,
                                   const meta_getter& get_metadata) -> inspect_result
{
    if(!open_)
    {
        return {};
    }
    // Pull out a mutable copy
    auto result = inspect_var_properties(ctx, var, info, get_metadata);

    return result;
}

} // namespace unravel
