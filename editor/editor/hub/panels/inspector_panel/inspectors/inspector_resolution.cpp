#include "inspector_resolution.h"
#include "inspector_container_widgets.h"
#include "inspectors.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace unravel
{
namespace
{
using resolution = settings::resolution_settings::resolution;

// Sizes are in units of the font size.
constexpr float RESOLUTION_ROW_GAP = 0.35f;
constexpr float RESOLUTION_NAME_WEIGHT = 1.4f;
constexpr float RESOLUTION_TYPE_WEIGHT = 1.0f;
constexpr float RESOLUTION_SIZE_WEIGHT = 1.4f;
constexpr float RESOLUTION_MUTED_ALPHA = 0.55f;
constexpr int RESOLUTION_MIN_SIZE = 1;
constexpr int RESOLUTION_MAX_SIZE = 16384;
constexpr float RESOLUTION_MIN_ASPECT = 0.1f;
constexpr float RESOLUTION_MAX_ASPECT = 10.0f;
constexpr float RESOLUTION_ASPECT_SPEED = 0.005f;
constexpr int RESOLUTION_DEFAULT_WIDTH = 1920;
constexpr int RESOLUTION_DEFAULT_HEIGHT = 1080;
constexpr const char* RESOLUTION_NEW_NAME = "New Resolution";
// The first preset is the free aspect the Game view falls back to: it stays as it is.
constexpr std::size_t RESOLUTION_BUILT_IN_COUNT = 1;

/// What a preset fixes: nothing, the aspect ratio, or the size in pixels. Read from the fields
/// the way the Game view reads them (viewport_resolution.cpp): no aspect is a free aspect, a size
/// wins over an aspect ratio.
enum class resolution_type
{
    free,
    aspect_ratio,
    fixed_size,
    count
};

constexpr std::array<const char*, static_cast<std::size_t>(resolution_type::count)> RESOLUTION_TYPE_NAMES{
    "Free",
    "Aspect Ratio",
    "Fixed Size"};

auto get_type(const resolution& preset) -> resolution_type
{
    if(preset.aspect <= 0.0f)
    {
        return resolution_type::free;
    }
    return preset.width > 0 && preset.height > 0 ? resolution_type::fixed_size : resolution_type::aspect_ratio;
}

auto get_fixed_aspect(const resolution& preset) -> float
{
    return static_cast<float>(preset.width) / static_cast<float>(preset.height);
}

/// Turns the preset into another type, keeping the shape it had where that makes sense.
void set_type(resolution& preset, resolution_type type)
{
    const float aspect = preset.aspect > 0.0f ? preset.aspect : static_cast<float>(RESOLUTION_DEFAULT_WIDTH) /
                                                                    static_cast<float>(RESOLUTION_DEFAULT_HEIGHT);
    switch(type)
    {
        case resolution_type::free:
            preset.width = 0;
            preset.height = 0;
            preset.aspect = 0.0f;
            break;
        case resolution_type::aspect_ratio:
            preset.width = 0;
            preset.height = 0;
            preset.aspect = aspect;
            break;
        case resolution_type::fixed_size:
        case resolution_type::count:
        default:
            preset.width = RESOLUTION_DEFAULT_WIDTH;
            preset.height = std::max(RESOLUTION_MIN_SIZE,
                                     static_cast<int>(std::lround(static_cast<float>(RESOLUTION_DEFAULT_WIDTH) / aspect)));
            preset.aspect = get_fixed_aspect(preset);
            break;
    }
}

auto make_unique_name(const std::vector<resolution>& presets) -> std::string
{
    const auto is_taken = [&](const std::string& name)
    {
        return std::any_of(presets.begin(),
                           presets.end(),
                           [&](const resolution& preset)
                           {
                               return preset.name == name;
                           });
    };
    std::string name = RESOLUTION_NEW_NAME;
    for(int number = 2; is_taken(name); ++number)
    {
        name = fmt::format("{} {}", RESOLUTION_NEW_NAME, number);
    }
    return name;
}

void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text, RESOLUTION_MUTED_ALPHA));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void add_edit(inspect_result& result, bool is_changed)
{
    result.changed |= is_changed;
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DrawItemActivityOutline();
}

auto draw_type_combo(resolution& preset) -> inspect_result
{
    inspect_result result{};
    const resolution_type type = get_type(preset);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::BeginCombo("##type", RESOLUTION_TYPE_NAMES[static_cast<std::size_t>(type)]))
    {
        for(std::size_t i = 0; i < RESOLUTION_TYPE_NAMES.size(); ++i)
        {
            const auto candidate = static_cast<resolution_type>(i);
            if(ImGui::Selectable(RESOLUTION_TYPE_NAMES[i], candidate == type) && candidate != type)
            {
                set_type(preset, candidate);
                result.changed = true;
                result.edit_finished = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::DrawItemActivityOutline();
    return result;
}

/// The size of a fixed preset as width x height, the ratio of an aspect preset, nothing for a
/// free one.
auto draw_size_fields(resolution& preset) -> inspect_result
{
    inspect_result result{};
    switch(get_type(preset))
    {
        case resolution_type::fixed_size:
        {
            const char* separator = "x";
            const float separator_width = ImGui::CalcTextSize(separator).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
            const float field_width = ImMax(1.0f, (ImGui::GetContentRegionAvail().x - separator_width) * 0.5f);
            const ImGuiSliderFlags clamp = ImGuiSliderFlags_AlwaysClamp;
            ImGui::SetNextItemWidth(field_width);
            add_edit(result,
                     ImGui::DragInt("##width", &preset.width, 1.0f, RESOLUTION_MIN_SIZE, RESOLUTION_MAX_SIZE, "%d", clamp));
            ImGui::SameLine();
            ImGui::TextUnformatted(separator);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            add_edit(result,
                     ImGui::DragInt("##height", &preset.height, 1.0f, RESOLUTION_MIN_SIZE, RESOLUTION_MAX_SIZE, "%d", clamp));
            // The Game view reads the aspect of every preset, so a size keeps its own in step.
            if(result.changed)
            {
                preset.aspect = get_fixed_aspect(preset);
            }
            break;
        }
        case resolution_type::aspect_ratio:
            ImGui::SetNextItemWidth(-FLT_MIN);
            add_edit(result,
                     ImGui::DragFloat("##aspect",
                                      &preset.aspect,
                                      RESOLUTION_ASPECT_SPEED,
                                      RESOLUTION_MIN_ASPECT,
                                      RESOLUTION_MAX_ASPECT,
                                      "%.3f : 1",
                                      ImGuiSliderFlags_AlwaysClamp));
            break;
        case resolution_type::free:
        case resolution_type::count:
        default:
            ImGui::AlignTextToFramePadding();
            draw_muted_text("Fills the view");
            break;
    }
    return result;
}

/// One preset on one row: name, type, size, and a remove button.
auto draw_preset_row(resolution& preset, bool is_built_in, bool is_editable, bool& is_remove_pressed) -> inspect_result
{
    inspect_result result{};
    ImGui::PushReadonly(is_built_in || !is_editable);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    add_edit(result, ImGui::InputTextWidget<128>("##name", preset.name));

    ImGui::TableNextColumn();
    result |= draw_type_combo(preset);

    ImGui::TableNextColumn();
    result |= draw_size_fields(preset);

    ImGui::PopReadonly();

    ImGui::TableNextColumn();
    if(!is_built_in && is_editable)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float side = ImGui::GetFrameHeight();
        const ImRect rect(min, min + ImVec2(side, side));
        ImGui::ItemSize(rect);
        is_remove_pressed = container_widgets::draw_icon_button({"##remove", ICON_MDI_DELETE_OUTLINE, "Remove the preset", true}, rect);
    }
    else if(is_built_in)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ICON_MDI_LOCK_OUTLINE);
        ImGui::SetItemTooltipEx("%s", "Built in: the Game view falls back to it.");
    }
    return result;
}

/// How many presets there are, and a button to add one.
auto draw_header(std::size_t count, bool is_editable) -> bool
{
    const char* add_label = ICON_MDI_PLUS " Add Preset";
    const float row_x = ImGui::GetCursorPosX();
    const float row_width = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    draw_muted_text(fmt::format("{} {}", count, count == 1 ? "preset" : "presets").c_str());
    if(!is_editable)
    {
        return false;
    }
    const float add_width = ImGui::CalcTextSize(add_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(row_x + row_width - add_width);
    return ImGui::Button(add_label);
}
} // namespace

auto inspector_resolution_settings::inspect(rtti::context& ctx,
                                            entt::meta_any& var,
                                            const meta_any_proxy& var_proxy,
                                            const var_info& info,
                                            const entt::meta_custom& custom) -> inspect_result
{
    auto& presets = var.cast<settings::resolution_settings&>().resolutions;
    const bool is_editable = !info.read_only;
    inspect_result result{};

    const bool is_add_pressed = draw_header(presets.size(), is_editable);
    ImGui::Dummy(ImVec2(0.0f, container_widgets::to_pixels(RESOLUTION_ROW_GAP)));

    int index_to_remove = -1;
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp;
    if(ImGui::BeginTable("##resolution_presets", 4, table_flags))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, RESOLUTION_NAME_WEIGHT);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, RESOLUTION_TYPE_WEIGHT);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, RESOLUTION_SIZE_WEIGHT);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();
        for(std::size_t i = 0; i < presets.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            bool is_remove_pressed = false;
            result |= draw_preset_row(presets[i], i < RESOLUTION_BUILT_IN_COUNT, is_editable, is_remove_pressed);
            if(is_remove_pressed)
            {
                index_to_remove = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if(index_to_remove >= 0)
    {
        presets.erase(presets.begin() + index_to_remove);
        result.changed = true;
        result.edit_finished = true;
    }
    if(is_add_pressed)
    {
        resolution preset{};
        preset.name = make_unique_name(presets);
        set_type(preset, resolution_type::fixed_size);
        presets.push_back(preset);
        result.changed = true;
        result.edit_finished = true;
    }
    return result;
}

} // namespace unravel
