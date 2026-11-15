#pragma once

#include <editor/hub/panels/inspector_panel/inspectors/inspector_math.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/rendering/material_graph/material_graph_library.h>

#include <hpp/string_view.hpp>
#include <imgui/imgui.h>

#include <string>

namespace unravel
{

/// Drag and drop payload of a node type dragged from the palette: the type's key.
inline constexpr const char* MATERIAL_GRAPH_NODE_PAYLOAD = "MATERIAL_GRAPH_NODE";

/// How fast a dragged value field changes, per pixel, unless its pin sets its own speed.
inline constexpr float MATERIAL_VALUE_DRAG_SPEED = 0.01f;

/// How the inspector's widgets draw the value of @p pin, @p width components of it: as a color when the pin is one,
/// with the pin's drag speed and range (material_pin_def). Every editor of a node's value draws it so.
auto make_material_value_style(const material_pin_def& pin, uint32_t width) -> vector_value_style;

/// make_material_value_style for an input of @p node: a scalar parameter's default keeps to the node's Slider Min and
/// Slider Max.
auto make_node_value_style(const material_graph_node& node, const material_pin_def& pin, uint32_t width) -> vector_value_style;

/// How the inspector draws graph parameter @p parameter of @p graph: as its node draws its default.
auto make_parameter_value_style(const material_graph& graph, const material_graph_parameter& parameter) -> vector_value_style;

/// A tooltip at the mouse, on the window's own viewport and wrapped at a readable width: for what the canvas shows of
/// a pin, a node or a refused link, which are no ImGui items. printf-style.
void show_wrapped_tooltip(const char* format, ...) IM_FMTARGS(1);

/// show_wrapped_tooltip for the item last drawn, once it is hovered as ImGui shows tooltips. printf-style.
void set_wrapped_item_tooltip(const char* format, ...) IM_FMTARGS(1);

/// @p text in lower case (ASCII), for the node searches.
auto make_lowercase(hpp::string_view text) -> std::string;

/// Header colour of a node category: the canvas and the palette share it.
auto get_material_category_color(material_node_category category) -> ImU32;

/// Icon of a node category.
auto get_material_category_icon(material_node_category category) -> const char*;

/// Colour of a value type, for pins and links: float cyan, vector 2 green, vector 3 yellow, vector 4 pink.
auto get_material_type_color(material_value_type type) -> ImU32;

} // namespace unravel
