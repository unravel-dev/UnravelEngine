#include "viewport_resolution.h"
#include "panel_toolbar.h"
#include "visualization_widgets.h"
#include "../hub.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/screen_card.h>
#include <engine/rendering/ecs/components/camera_component.h>

#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace unravel::viewport_resolution
{

namespace
{
using screen_card::to_pixels;
using resolution = settings::resolution_settings::resolution;
using device_type = settings::resolution_settings::device_type;
using screen_orientation = settings::resolution_settings::screen_orientation;

/// The popover's width, in units of the font size.
constexpr float POPOVER_WIDTH = 18.0f;
/// Short sides the handheld presets stand for, in the inches the platform's density reports (Android gives a
/// 1080-pixel-wide phone about 450 dpi): a common phone and a common tablet.
constexpr float PHONE_SHORT_SIDE_INCHES = 2.4f;
constexpr float TABLET_SHORT_SIDE_INCHES = 5.6f;
constexpr std::array<const char*, 2> ORIENTATION_LABELS{ICON_MDI_CROP_PORTRAIT " Portrait",
                                                        ICON_MDI_CROP_LANDSCAPE " Landscape"};

/// A section of the popover: the presets of one device that have a shape.
struct device_section
{
    device_type device;
    const char* header;
};

constexpr std::array<device_section, 3> DEVICE_SECTIONS{{{device_type::desktop, ICON_MDI_MONITOR " Desktop"},
                                                         {device_type::phone, ICON_MDI_CELLPHONE " Phone"},
                                                         {device_type::tablet, ICON_MDI_TABLET " Tablet"}}};

auto clamp_index(int index, int count) -> int
{
    if(count <= 0)
    {
        return 0;
    }
    return std::clamp(index, 0, count - 1);
}

auto is_free(const resolution& res) -> bool
{
    return res.aspect <= 0.0f;
}

auto has_pixels(const resolution& res) -> bool
{
    return res.width > 0 && res.height > 0;
}

/// The icon of the toolbar button: the device of a preset with pixels, the aspect ratio otherwise.
auto get_icon(const resolution& res) -> const char*
{
    switch(res.device)
    {
        case device_type::phone:
            return ICON_MDI_CELLPHONE;
        case device_type::tablet:
            return ICON_MDI_TABLET;
        case device_type::desktop:
        default:
            return has_pixels(res) ? ICON_MDI_MONITOR : ICON_MDI_ASPECT_RATIO;
    }
}

auto make_size_text(const resolution& res) -> std::string
{
    return has_pixels(res) ? fmt::format("{} x {}", res.width, res.height) : std::string{};
}

auto make_tooltip(const resolution& shown) -> std::string
{
    if(is_free(shown))
    {
        return "Renders at the size of the view.";
    }
    const std::string frame =
        has_device_frame(shown) ? fmt::format(", inside a {} frame", shown.device == device_type::tablet ? "tablet" : "phone")
                                : std::string{};
    if(has_pixels(shown))
    {
        return fmt::format("Renders at {} x {} pixels{}.", shown.width, shown.height, frame);
    }
    return fmt::format("Renders at the size of the view in the shape of the preset{}.", frame);
}

/// One preset of the popover, its size as the orientation turns it. Returns true when it was picked.
auto draw_preset(const std::vector<resolution>& presets, int index, screen_orientation orientation, int& current_index)
    -> bool
{
    const resolution shown = get_oriented(presets[index], orientation);
    const std::string size_text = make_size_text(shown);
    ImGui::PushID(index);
    const bool is_pressed = visualization_widgets::draw_list_row("##preset",
                                                                 nullptr,
                                                                 shown.name.c_str(),
                                                                 index == current_index,
                                                                 false,
                                                                 size_text.empty() ? nullptr : size_text.c_str());
    ImGui::SetItemTooltipEx("%s", make_tooltip(shown).c_str());
    ImGui::PopID();
    if(!is_pressed || index == current_index)
    {
        return false;
    }
    current_index = index;
    return true;
}

/// The presets of one device under its heading; nothing for a device without presets.
auto draw_section(const std::vector<resolution>& presets,
                  const device_section& section,
                  screen_orientation orientation,
                  int& current_index) -> bool
{
    const auto is_listed = [&](const resolution& preset)
    {
        return !is_free(preset) && preset.device == section.device;
    };
    if(std::none_of(presets.begin(), presets.end(), is_listed))
    {
        return false;
    }
    visualization_widgets::draw_section_header(section.header);
    bool is_changed = false;
    for(int i = 0; i < static_cast<int>(presets.size()); ++i)
    {
        if(is_listed(presets[i]))
        {
            is_changed |= draw_preset(presets, i, orientation, current_index);
        }
    }
    return is_changed;
}

/// The free presets first, then a section per device. Returns true when the pick changed.
auto draw_presets(const std::vector<resolution>& presets, screen_orientation orientation, int& current_index) -> bool
{
    bool is_changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for(int i = 0; i < static_cast<int>(presets.size()); ++i)
    {
        if(is_free(presets[i]))
        {
            is_changed |= draw_preset(presets, i, orientation, current_index);
        }
    }
    for(const device_section& section : DEVICE_SECTIONS)
    {
        is_changed |= draw_section(presets, section, orientation, current_index);
    }
    ImGui::PopStyleVar();
    return is_changed;
}

/// Portrait or landscape, for a phone or a tablet; desktop presets keep their shape, so it is off for them.
auto draw_orientation(screen_orientation& orientation, bool is_enabled) -> bool
{
    int index = static_cast<int>(orientation);
    ImGui::BeginDisabled(!is_enabled);
    const bool is_changed = visualization_widgets::draw_segmented("##orientation",
                                                                  ORIENTATION_LABELS.data(),
                                                                  nullptr,
                                                                  static_cast<int>(ORIENTATION_LABELS.size()),
                                                                  ImGui::GetContentRegionAvail().x,
                                                                  index);
    ImGui::EndDisabled();
    orientation = static_cast<screen_orientation>(index);
    return is_changed;
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

auto compute_viewport_pixels(const settings::resolution_settings::resolution& res, ImVec2 avail_size) -> usize32_t
{
    const ImVec2 viewport = compute_viewport_size(res, avail_size);
    return {static_cast<std::uint32_t>(viewport.x), static_cast<std::uint32_t>(viewport.y)};
}

auto get_render_resolution(const settings::resolution_settings::resolution& res) -> usize32_t
{
    if(res.width <= 0 || res.height <= 0)
    {
        return {};
    }
    return {static_cast<std::uint32_t>(res.width), static_cast<std::uint32_t>(res.height)};
}

void apply_to_camera(camera_component& camera_comp,
                     const settings::resolution_settings::resolution& res,
                     ImVec2 avail_size)
{
    camera_comp.set_viewport_size(compute_viewport_pixels(res, avail_size));
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

auto is_handheld(const settings::resolution_settings::resolution& res) -> bool
{
    return res.device != device_type::desktop;
}

auto has_device_frame(const settings::resolution_settings::resolution& res) -> bool
{
    return is_handheld(res) && !is_free(res);
}

auto get_device_dpi(const settings::resolution_settings::resolution& res) -> float
{
    if(!has_device_frame(res) || !has_pixels(res))
    {
        return 0.0f;
    }
    if(res.dpi > 0.0f)
    {
        return res.dpi;
    }
    const float short_side_inches = res.device == device_type::tablet ? TABLET_SHORT_SIDE_INCHES : PHONE_SHORT_SIDE_INCHES;
    return float(std::min(res.width, res.height)) / short_side_inches;
}

auto get_oriented(const settings::resolution_settings::resolution& res,
                  settings::resolution_settings::screen_orientation orientation)
    -> settings::resolution_settings::resolution
{
    const bool is_landscape = orientation == screen_orientation::landscape;
    if(!is_handheld(res) || is_free(res) || (res.aspect > 1.0f) == is_landscape)
    {
        return res;
    }
    resolution turned = res;
    std::swap(turned.width, turned.height);
    turned.aspect = 1.0f / res.aspect;
    if(is_landscape)
    {
        // Turned a quarter counter-clockwise: the top of the device (its cutout) goes to the left.
        turned.safe_left = res.safe_top;
        turned.safe_top = res.safe_right;
        turned.safe_right = res.safe_bottom;
        turned.safe_bottom = res.safe_left;
    }
    else
    {
        turned.safe_top = res.safe_left;
        turned.safe_right = res.safe_top;
        turned.safe_bottom = res.safe_right;
        turned.safe_left = res.safe_bottom;
    }
    return turned;
}

auto get_oriented_resolution(rtti::context& ctx, int index, settings::resolution_settings::screen_orientation orientation)
    -> hpp::optional<settings::resolution_settings::resolution>
{
    const auto* res = get_resolution(ctx, index);
    if(res == nullptr)
    {
        return hpp::nullopt;
    }
    return get_oriented(*res, orientation);
}

auto draw_toolbar_dropdown(rtti::context& ctx,
                           int& current_index,
                           settings::resolution_settings::screen_orientation& orientation,
                           bool is_compact) -> bool
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

    const auto& current = resolutions[current_index];
    const std::string text = panel_toolbar::make_text(get_icon(current), current.name.c_str(), is_compact);
    ImGui::SetNextWindowSize(ImVec2(to_pixels(POPOVER_WIDTH), 0.0f));
    if(!panel_toolbar::begin_dropdown("##resolution", text.c_str(), "Resolution Presets"))
    {
        return false;
    }

    bool changed = draw_presets(resolutions, orientation, current_index);
    ImGui::Separator();
    changed |= draw_orientation(orientation, is_handheld(resolutions[current_index]));

    if(ImGui::MenuItem("Edit ...", "", false))
    {
        ctx.get_cached<hub>().open_project_settings(ctx, "Resolution");
    }
    panel_toolbar::end_dropdown();

    return changed;
}

} // namespace unravel::viewport_resolution
