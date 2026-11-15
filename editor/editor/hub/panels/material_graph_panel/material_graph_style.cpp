#include "material_graph_style.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>

namespace unravel
{

namespace
{
const ImU32 COL_CATEGORY_OUTPUT = IM_COL32(72, 84, 110, 255);
const ImU32 COL_CATEGORY_INPUT = IM_COL32(150, 64, 60, 255);
const ImU32 COL_CATEGORY_CONSTANT = IM_COL32(112, 112, 46, 255);
const ImU32 COL_CATEGORY_PARAMETER = IM_COL32(54, 124, 72, 255);
const ImU32 COL_CATEGORY_TEXTURE = IM_COL32(52, 92, 156, 255);
const ImU32 COL_CATEGORY_MATH = IM_COL32(52, 108, 116, 255);
const ImU32 COL_CATEGORY_VECTOR = IM_COL32(112, 72, 142, 255);
const ImU32 COL_CATEGORY_PROCEDURAL = IM_COL32(162, 102, 42, 255);
const ImU32 COL_CATEGORY_UTILITY = IM_COL32(92, 94, 100, 255);
const ImU32 COL_CATEGORY_FUNCTION = IM_COL32(138, 70, 100, 255);

/// Tooltips wrap at this many font heights, as the editor's other wrapped tooltips (ImGui::ItemTooltipEx).
constexpr float TOOLTIP_WRAP_EM = 35.0f;

// The value type colours most node editors share (Unity Shader Graph's), so a pin reads at a glance.
const ImU32 COL_TYPE_FLOAT1 = IM_COL32(132, 228, 231, 255);
const ImU32 COL_TYPE_FLOAT2 = IM_COL32(154, 239, 146, 255);
const ImU32 COL_TYPE_FLOAT3 = IM_COL32(246, 255, 154, 255);
const ImU32 COL_TYPE_FLOAT4 = IM_COL32(251, 203, 244, 255);
const ImU32 COL_TYPE_DYNAMIC = IM_COL32(200, 202, 208, 255);
} // namespace

auto make_lowercase(hpp::string_view text) -> std::string
{
    std::string lower(text);
    std::transform(lower.begin(),
                   lower.end(),
                   lower.begin(),
                   [](unsigned char c)
                   {
                       return char(std::tolower(c));
                   });
    return lower;
}

namespace
{
void show_wrapped_tooltip_v(const char* format, va_list args)
{
    ImGui::SetNextWindowViewportToCurrent();
    if(ImGui::BeginTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * TOOLTIP_WRAP_EM);
        ImGui::TextV(format, args);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
} // namespace

void show_wrapped_tooltip(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    show_wrapped_tooltip_v(format, args);
    va_end(args);
}

void set_wrapped_item_tooltip(const char* format, ...)
{
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        return;
    }
    va_list args;
    va_start(args, format);
    show_wrapped_tooltip_v(format, args);
    va_end(args);
}

auto make_material_value_style(const material_pin_def& pin, uint32_t width) -> vector_value_style
{
    vector_value_style style;
    style.width = width;
    style.is_color = pin.is_color && width > 1;
    style.is_hdr = style.is_color;
    style.drag_speed = pin.drag_speed > 0.0f ? pin.drag_speed : MATERIAL_VALUE_DRAG_SPEED;
    style.min_value = pin.min_value;
    style.max_value = pin.max_value;
    return style;
}

auto make_node_value_style(const material_graph_node& node, const material_pin_def& pin, uint32_t width) -> vector_value_style
{
    auto style = make_material_value_style(pin, width);
    if(const auto slider = find_material_parameter_slider(node))
    {
        style.min_value = slider->x;
        style.max_value = slider->y;
    }
    return style;
}

auto make_parameter_value_style(const material_graph& graph, const material_graph_parameter& parameter) -> vector_value_style
{
    const uint32_t width = get_material_value_width(parameter.type);
    const int index = find_material_graph_node(graph, parameter.node_id);
    const auto* node = index >= 0 ? &graph.nodes[size_t(index)] : nullptr;
    const auto* def = node != nullptr ? find_material_node_def(node->type) : nullptr;
    if(def == nullptr || def->inputs.empty())
    {
        vector_value_style style;
        style.width = width;
        style.drag_speed = MATERIAL_VALUE_DRAG_SPEED;
        return style;
    }
    return make_node_value_style(*node, def->inputs.front(), width);
}

auto get_material_category_color(material_node_category category) -> ImU32
{
    switch(category)
    {
        case material_node_category::output:
            return COL_CATEGORY_OUTPUT;
        case material_node_category::input:
            return COL_CATEGORY_INPUT;
        case material_node_category::constant:
            return COL_CATEGORY_CONSTANT;
        case material_node_category::parameter:
            return COL_CATEGORY_PARAMETER;
        case material_node_category::texture:
            return COL_CATEGORY_TEXTURE;
        case material_node_category::math:
            return COL_CATEGORY_MATH;
        case material_node_category::vector:
            return COL_CATEGORY_VECTOR;
        case material_node_category::procedural:
            return COL_CATEGORY_PROCEDURAL;
        case material_node_category::function:
            return COL_CATEGORY_FUNCTION;
        case material_node_category::utility:
            break;
    }
    return COL_CATEGORY_UTILITY;
}

auto get_material_category_icon(material_node_category category) -> const char*
{
    switch(category)
    {
        case material_node_category::output:
            return ICON_MDI_EXPORT;
        case material_node_category::input:
            return ICON_MDI_LOGIN;
        case material_node_category::constant:
            return ICON_MDI_NUMERIC;
        case material_node_category::parameter:
            return ICON_MDI_TUNE;
        case material_node_category::texture:
            return ICON_MDI_IMAGE;
        case material_node_category::math:
            return ICON_MDI_CALCULATOR;
        case material_node_category::vector:
            return ICON_MDI_VECTOR_LINE;
        case material_node_category::procedural:
            return ICON_MDI_CHECKERBOARD;
        case material_node_category::function:
            return ICON_MDI_FUNCTION_VARIANT;
        case material_node_category::utility:
            break;
    }
    return ICON_MDI_TOOLS;
}

auto get_material_type_color(material_value_type type) -> ImU32
{
    switch(type)
    {
        case material_value_type::float1:
            return COL_TYPE_FLOAT1;
        case material_value_type::float2:
            return COL_TYPE_FLOAT2;
        case material_value_type::float3:
            return COL_TYPE_FLOAT3;
        case material_value_type::float4:
            return COL_TYPE_FLOAT4;
        case material_value_type::dynamic:
            break;
    }
    return COL_TYPE_DYNAMIC;
}

} // namespace unravel
