#include "view_render_scale.h"
#include "../hub.h"
#include "panel_toolbar.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <engine/rendering/render_scale.h>
#include <engine/settings/settings.h>

#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace unravel::view_render_scale
{
namespace
{
constexpr float PERCENT = 100.0f;
/// The Custom slider's width, in font sizes.
constexpr float CUSTOM_SLIDER_WIDTH = 8.0f;
/// The widest scale the toolbar shows, which its button is measured from.
constexpr const char* WIDEST_SCALE_TEXT = "100%";

auto get_project_scale(rtti::context& ctx) -> float
{
    return ctx.has<settings>() ? ctx.get<settings>().graphics.render_scale : 1.0f;
}

auto to_percent_text(float scale) -> std::string
{
    return fmt::format("{}%", std::lround(std::clamp(scale, MIN_RENDER_SCALE, 1.0f) * PERCENT));
}

/// A scale the settings offer: from MIN_RENDER_SCALE to 1, in RENDER_SCALE_STEP steps.
auto snap_scale(float scale) -> float
{
    return std::clamp(std::round(scale / RENDER_SCALE_STEP) * RENDER_SCALE_STEP, MIN_RENDER_SCALE, 1.0f);
}

/// A choice of the dropdown, with what it does as its tooltip.
auto draw_choice(const char* label, const char* tooltip, int& picked, source choice) -> bool
{
    const bool is_pressed = ImGui::RadioButton(label, &picked, static_cast<int>(choice));
    ImGui::SetItemTooltipEx("%s", tooltip);
    return is_pressed;
}
} // namespace

auto get_render_scale(const state& view) -> hpp::optional<float>
{
    switch(view.scale_source)
    {
        case source::full:
            return 1.0f;
        case source::custom:
            return view.custom_scale;
        case source::project:
        default:
            return hpp::nullopt;
    }
}

auto draw_toolbar_dropdown(rtti::context& ctx, state& view, bool is_compact) -> bool
{
    const float project_scale = get_project_scale(ctx);
    const float shown_scale = get_render_scale(view).value_or(project_scale);
    const std::string text = panel_toolbar::make_text(ICON_MDI_IMAGE_SIZE_SELECT_LARGE, to_percent_text(shown_scale).c_str(), is_compact);
    const std::string width_text = panel_toolbar::make_text(ICON_MDI_IMAGE_SIZE_SELECT_LARGE, WIDEST_SCALE_TEXT, is_compact);
    if(!panel_toolbar::begin_dropdown("##render_scale", text.c_str(), "Render Scale", 0, width_text.c_str()))
    {
        return false;
    }
    int picked = static_cast<int>(view.scale_source);
    bool is_changed = false;
    ImGui::SeparatorText("Render Scale");
    const std::string project_label = fmt::format("Project Setting ({})", to_percent_text(project_scale));
    is_changed |= draw_choice(project_label.c_str(),
                              "Render at the project's render scale, as the previewed profile sets it.",
                              picked,
                              source::project);
    is_changed |= draw_choice("Full Resolution", "Render at the view's full resolution.", picked, source::full);
    is_changed |= draw_choice("Custom",
                              "Render at a scale of this view's own. The project and the other views keep theirs.",
                              picked,
                              source::custom);
    ImGui::SameLine();
    float percent = view.custom_scale * PERCENT;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * CUSTOM_SLIDER_WIDTH);
    if(ImGui::SliderFloat("##custom_render_scale", &percent, MIN_RENDER_SCALE * PERCENT, PERCENT, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
    {
        view.custom_scale = snap_scale(percent / PERCENT);
        picked = static_cast<int>(source::custom);
        is_changed = true;
    }
    view.scale_source = static_cast<source>(picked);
    if(ImGui::MenuItem("Edit ...", "", false))
    {
        ctx.get_cached<hub>().open_project_settings(ctx, "Graphics");
    }
    panel_toolbar::end_dropdown();
    return is_changed;
}

} // namespace unravel::view_render_scale
