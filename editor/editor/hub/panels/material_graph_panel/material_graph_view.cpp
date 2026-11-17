#include "material_graph_view.h"
#include "material_graph_style.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/shortcuts.h>
#include <engine/engine.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/meta/rendering/material_graph.hpp>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>

namespace unravel
{

namespace
{
// Node anatomy, in graph units (pixels at zoom 1).
constexpr float NODE_MIN_WIDTH = 110.0f;
/// The Surface Output's Base Color, labeled as what it is in the specular workflow.
constexpr const char* SURFACE_BASE_COLOR_PIN = "base_color";
constexpr const char* SPECULAR_WORKFLOW_BASE_COLOR_LABEL = "Diffuse";
constexpr float NODE_HEADER_HEIGHT = 26.0f;
constexpr float NODE_ROW_HEIGHT = 22.0f;
constexpr float BODY_PADDING = 6.0f;
/// Between a pin and its label, and between a node's edge and its header text.
constexpr float SIDE_PADDING = 12.0f;
/// Between an input row's content and the output label on the same row.
constexpr float NODE_COLUMN_GAP = 18.0f;
/// How far a component row of a split output is set in.
constexpr float COMPONENT_LABEL_INDENT = 10.0f;
constexpr float PIN_RADIUS = 4.5f;
constexpr float HOVERED_PIN_RADIUS = 6.0f;
constexpr float PIN_HIT_RADIUS = 9.0f;
constexpr float CHIP_HEIGHT = 16.0f;
constexpr float CHIP_GAP = 6.0f;
constexpr float CHIP_PADDING = 4.0f;
constexpr float CHIP_ROUNDING = 3.0f;
constexpr float CHIP_COMPONENT_WIDTH = 38.0f;
constexpr float COLOR_CHIP_WIDTH = 34.0f;
/// A texture node's preview: a square under its inputs.
constexpr float NODE_PREVIEW_SIZE = 96.0f;
constexpr float NODE_PREVIEW_GAP = 6.0f;
constexpr float BADGE_RADIUS = 7.0f;
constexpr float NODE_CORNER_RADIUS = 6.0f;
constexpr float NODE_SHADOW_OFFSET = 4.0f;
/// A reroute: a small capsule, its input at the left end, its output at the right one, moved by its middle.
constexpr float REROUTE_WIDTH = 30.0f;
constexpr float REROUTE_HEIGHT = 16.0f;
constexpr float REROUTE_PIN_HIT_RADIUS = 6.0f;
constexpr float NODE_OUTLINE_THICKNESS = 1.0f;
constexpr float SELECTED_BORDER_THICKNESS = 2.5f;
/// Pixels: below this text size (zoomed far out) no text is drawn.
constexpr float MIN_FONT_SIZE = 5.0f;
constexpr float CHIP_TEXT_SCALE = 0.86f;

constexpr float CANVAS_GRID_STEP = 32.0f;
constexpr int CANVAS_GRID_MAJOR_EVERY = 4;
constexpr float CANVAS_MIN_ZOOM = 0.2f;
constexpr float CANVAS_MAX_ZOOM = 2.0f;
/// Framing never zooms in further than this, so a small graph stays its natural size.
constexpr float CANVAS_MAX_FRAME_ZOOM = 1.0f;
constexpr float CANVAS_ZOOM_STEP = 1.1f;
constexpr float CANVAS_FRAME_MARGIN = 60.0f;

/// Pixels.
constexpr float LINK_THICKNESS = 2.2f;
constexpr float HIGHLIGHTED_LINK_THICKNESS = 3.4f;
constexpr float LINK_HIT_DISTANCE = 6.0f;
constexpr int LINK_SEGMENTS = 24;
/// Graph units: how far a link leaves its pin straight before it bends.
constexpr float LINK_MIN_TANGENT = 40.0f;
constexpr float DRAG_THRESHOLD = 3.0f;

/// A dragged value changes this much per pixel; Shift is faster, Ctrl finer.
constexpr float SCRUB_SPEED = 0.01f;
constexpr float SCRUB_FAST = 10.0f;
constexpr float SCRUB_FINE = 0.1f;
constexpr float CANVAS_SNAP_STEP = 16.0f;
constexpr float NODE_DUPLICATE_OFFSET = 30.0f;
/// Where a node added from an output's menu lands, right of the pin.
constexpr float ADDED_NODE_OFFSET = 60.0f;

constexpr float COMMENT_TITLE_HEIGHT = 24.0f;
constexpr float COMMENT_CORNER = 14.0f;
constexpr float COMMENT_MARGIN = 30.0f;
constexpr float COMMENT_ROUNDING = 4.0f;
constexpr float COMMENT_MIN_WIDTH = 160.0f;
constexpr float COMMENT_MIN_HEIGHT = 60.0f;
/// Width a comment without a size wraps its text at.
constexpr float COMMENT_WRAP_WIDTH = 320.0f;

/// Pixels.
constexpr float SEARCH_WIDTH = 300.0f;
constexpr float SEARCH_LIST_HEIGHT = 340.0f;
constexpr float VALUE_POPUP_WIDTH = 260.0f;

/// Unused nodes and their links draw at this opacity.
constexpr float UNUSED_ALPHA = 0.5f;

constexpr const char* SEARCH_POPUP = "##material_graph_search";
constexpr const char* VALUE_POPUP = "##material_graph_value";
constexpr const char* NODE_MENU = "##material_graph_node_menu";
constexpr const char* LINK_MENU = "##material_graph_link_menu";
constexpr const char* PIN_MENU = "##material_graph_pin_menu";
constexpr const char* REROUTE_TYPE = "reroute";
constexpr const char* SPLIT_TYPE = "split";
constexpr const char* PARAMETER_NAME_SETTING = "name";
/// A Static Switch's setting picking its input, and the value picking False.
constexpr const char* SWITCH_VALUE_SETTING = "value";
constexpr const char* SWITCH_FALSE_VALUE = "false";
/// A Compare node's type, and its setting naming how A compares to B.
constexpr const char* COMPARE_TYPE = "compare";
constexpr const char* COMPARE_OPERATION_SETTING = "operation";

const ImU32 COL_MATERIAL_CANVAS = IM_COL32(28, 29, 32, 255);
const ImU32 COL_MATERIAL_GRID = IM_COL32(40, 42, 46, 255);
const ImU32 COL_MATERIAL_GRID_MAJOR = IM_COL32(50, 52, 58, 255);
const ImU32 COL_MATERIAL_NODE_BODY = IM_COL32(44, 46, 52, 245);
const ImU32 COL_MATERIAL_NODE_BORDER = IM_COL32(72, 76, 86, 255);
const ImU32 COL_MATERIAL_NODE_SHADOW = IM_COL32(0, 0, 0, 90);
const ImU32 COL_MATERIAL_SELECTED = IM_COL32(80, 170, 255, 255);
const ImU32 COL_MATERIAL_ERROR = IM_COL32(235, 82, 72, 255);
const ImU32 COL_MATERIAL_WARNING = IM_COL32(240, 180, 70, 255);
const ImU32 COL_MATERIAL_TEXT = IM_COL32(230, 232, 236, 255);
const ImU32 COL_MATERIAL_SUBTEXT = IM_COL32(150, 156, 166, 255);
const ImU32 COL_MATERIAL_CHIP = IM_COL32(24, 25, 29, 255);
const ImU32 COL_MATERIAL_CHIP_HOVER = IM_COL32(38, 40, 46, 255);
const ImU32 COL_MATERIAL_CHIP_TEXT = IM_COL32(214, 218, 224, 255);
const ImU32 COL_MATERIAL_LINK_PREVIEW = IM_COL32(255, 220, 120, 255);
const ImU32 COL_MATERIAL_LINK_REFUSED = IM_COL32(235, 82, 72, 255);
const ImU32 COL_MATERIAL_BOX_FILL = IM_COL32(80, 170, 255, 36);
const ImU32 COL_MATERIAL_BOX_BORDER = IM_COL32(80, 170, 255, 160);
const ImU32 COL_MATERIAL_COMMENT = IM_COL32(64, 70, 82, 64);
const ImU32 COL_MATERIAL_COMMENT_TITLE = IM_COL32(70, 78, 92, 170);
const ImU32 COL_MATERIAL_COMMENT_BORDER = IM_COL32(92, 100, 116, 200);

auto measure_screen_length(const ImVec2& v) -> float
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

auto measure_distance_to_segment(const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float
{
    const ImVec2 ab = b - a;
    const float length2 = ab.x * ab.x + ab.y * ab.y;
    if(length2 <= 1e-6f)
    {
        return measure_screen_length(p - a);
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / length2, 0.0f, 1.0f);
    return measure_screen_length(p - (a + ab * t));
}

auto scale_color_alpha(ImU32 color, float alpha) -> ImU32
{
    const auto a = uint32_t(float((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

auto format_component(float value) -> std::string
{
    return fmt::format("{:.3g}", value);
}

/// What an unlinked input with a built-in source reads, as the node shows it.
auto describe_default_code(const std::string& code) -> const char*
{
    if(code == "i.uv0")
    {
        return "Mesh UV";
    }
    if(code == "i.time")
    {
        return "Time";
    }
    if(code == "i.vertex_normal")
    {
        return "Vertex Normal";
    }
    return "Built-in";
}

/// The key that places a node of the library while held during a click.
auto get_shortcut_key(char shortcut) -> ImGuiKey
{
    if(shortcut >= 'A' && shortcut <= 'Z')
    {
        return ImGuiKey(ImGuiKey_A + (shortcut - 'A'));
    }
    if(shortcut >= '0' && shortcut <= '9')
    {
        return ImGuiKey(ImGuiKey_0 + (shortcut - '0'));
    }
    return ImGuiKey_None;
}

/// The title a node shows: a parameter its name, a Compare its operation, any other node its type.
auto get_node_title(const material_graph_node& node, const material_node_def& def) -> std::string
{
    const auto* name = find_material_graph_setting(node, PARAMETER_NAME_SETTING);
    const bool is_named = name != nullptr && !name->empty();
    if(const auto* operation = def.type == COMPARE_TYPE ? find_material_setting_def(def, COMPARE_OPERATION_SETTING) : nullptr)
    {
        const auto* value = find_material_graph_setting(node, operation->name);
        return def.label + " (A " + (value != nullptr ? *value : operation->default_value) + " B)";
    }
    switch(def.code)
    {
        case material_node_code::parameter:
            return is_named ? *name : node.id;
        case material_node_code::named_reroute_declaration:
            return std::string(ICON_MDI_ARROW_RIGHT_BOLD_BOX_OUTLINE " ") + (is_named ? *name : node.id);
        case material_node_code::named_reroute:
            return std::string(ICON_MDI_ARROW_RIGHT_BOLD_BOX " ") + (is_named ? *name : std::string("(no name)"));
        case material_node_code::function_input:
            return "Input: " + (is_named ? *name : node.id);
        case material_node_code::function_output:
            return "Output: " + (is_named ? *name : node.id);
        case material_node_code::function_call:
        {
            const auto* key = find_material_graph_setting(node, MATERIAL_FUNCTION_KEY_SETTING);
            return key != nullptr && !key->empty() ? fs::path(*key).stem().string() : def.label;
        }
        case material_node_code::static_switch:
        {
            const auto* value = find_material_graph_setting(node, SWITCH_VALUE_SETTING);
            return def.label + (value != nullptr && *value == SWITCH_FALSE_VALUE ? " (False)" : " (True)");
        }
        default:
            return def.label;
    }
}

/// Width of an input's value field, in graph units: none for a texture or attributes input, which only takes a link.
auto get_chip_width(const material_pin_def& pin) -> float
{
    if(!is_material_value_numeric(pin.type))
    {
        return 0.0f;
    }
    if(pin.is_color)
    {
        return COLOR_CHIP_WIDTH;
    }
    const uint32_t width = pin.type == material_value_type::dynamic ? 1 : get_material_value_width(pin.type);
    return CHIP_COMPONENT_WIDTH * float(width);
}

/// The default a material function gives the input a call's pin @p pin feeds; empty for other pins.
auto find_function_input_default(const material_graph_node& call, const std::string& pin) -> hpp::optional<math::vec4>
{
    const auto* key = find_material_graph_setting(call, MATERIAL_FUNCTION_KEY_SETTING);
    const auto function = key != nullptr && !key->empty() ? load_material_function(*key) : nullptr;
    if(function == nullptr)
    {
        return {};
    }
    const auto function_pins = get_material_function_interface(*function);
    for(uint32_t k = 0; k < uint32_t(function_pins.inputs.size()); ++k)
    {
        if(get_material_function_input_pin(k) != pin)
        {
            continue;
        }
        const int input = find_material_graph_node(*function, function_pins.inputs[k].node_id);
        const auto* value = input >= 0 ? find_material_graph_input(function->nodes[size_t(input)], "default") : nullptr;
        return value != nullptr ? value->value : math::vec4(0.0f);
    }
    return {};
}

/// The value an input reads while unlinked.
auto get_input_value(const material_graph_node& node, const material_pin_def& pin) -> math::vec4
{
    const auto* input = find_material_graph_input(node, pin.name);
    if(input != nullptr)
    {
        return input->value;
    }
    // An unlinked pin of a function call reads the function's default.
    if(node.type == MATERIAL_FUNCTION_CALL_TYPE)
    {
        if(const auto value = find_function_input_default(node, pin.name))
        {
            return *value;
        }
    }
    return pin.default_value;
}

/// The component letter of @p pin, upper case, as a row shows it.
auto get_component_label(const material_pin_def& pin, int component) -> std::string
{
    return std::string(1, char(std::toupper(static_cast<unsigned char>(get_material_component_letters(pin)[component]))));
}

/// The nodes a constant converts to as a parameter, and back.
auto get_conversion(const std::string& type) -> const char*
{
    if(type == "constant")
    {
        return "scalar_parameter";
    }
    if(type == "color")
    {
        return "color_parameter";
    }
    if(type == "vector3")
    {
        return "vector_parameter";
    }
    if(type == "scalar_parameter")
    {
        return "constant";
    }
    if(type == "color_parameter")
    {
        return "color";
    }
    return nullptr;
}
} // namespace

// ---------------------------------------------------------------------------------
// Results and selection
// ---------------------------------------------------------------------------------

void material_graph_edit_result::set_committed()
{
    begin_edit = true;
    changed = true;
    finished = true;
}

void material_graph_edit_result::merge(const material_graph_edit_result& other)
{
    begin_edit = begin_edit || other.begin_edit;
    changed = changed || other.changed;
    layout_changed = layout_changed || other.layout_changed;
    finished = finished || other.finished;
}

auto find_material_call_interface(const material_graph_node& node) -> hpp::optional<material_function_interface>
{
    const auto* key = find_material_graph_setting(node, MATERIAL_FUNCTION_KEY_SETTING);
    const auto function = key != nullptr && !key->empty() ? load_material_function(*key) : nullptr;
    if(function == nullptr || !is_material_function_graph(*function))
    {
        return {};
    }
    return get_material_function_interface(*function);
}

auto get_material_pin_presentation(const material_graph_node& node, const material_node_def& def, bool is_output, size_t pin)
    -> material_pin_presentation
{
    const auto& pins = is_output ? def.outputs : def.inputs;
    if(pin >= pins.size() || pins[pin].is_hidden)
    {
        return {};
    }
    if(def.code == material_node_code::surface_output && !is_output)
    {
        // The other workflow's inputs are not drawn; in the specular workflow Base Color is the diffuse color.
        if(!is_material_workflow_pin_used(node, pins[pin].name))
        {
            return {};
        }
        const bool is_diffuse = pins[pin].name == SURFACE_BASE_COLOR_PIN && is_material_specular_workflow(node);
        return {true, is_diffuse ? std::string(SPECULAR_WORKFLOW_BASE_COLOR_LABEL) : pins[pin].label};
    }
    if(def.code != material_node_code::function_call)
    {
        return {true, pins[pin].label};
    }
    const auto function_pins = find_material_call_interface(node);
    if(!function_pins)
    {
        return {};
    }
    const auto& named = is_output ? function_pins->outputs : function_pins->inputs;
    return pin < named.size() ? material_pin_presentation{true, named[pin].name} : material_pin_presentation{};
}

auto material_graph_selection::has_node(hpp::string_view id) const -> bool
{
    return std::find(nodes.begin(), nodes.end(), id) != nodes.end();
}

auto material_graph_selection::is_empty() const -> bool
{
    return nodes.empty() && comment < 0 && link.empty();
}

void material_graph_selection::clear()
{
    nodes.clear();
    comment = -1;
    link.clear();
}

// ---------------------------------------------------------------------------------
// Coordinates and measuring
// ---------------------------------------------------------------------------------

void material_graph_view::request_frame_all()
{
    frame_request_ = true;
}

void material_graph_view::set_previews(const material_graph_previews* previews, bool are_shown)
{
    previews_ = previews;
    are_swatches_shown_ = are_shown && previews != nullptr;
}

void material_graph_view::cancel_interaction()
{
    interaction_ = interaction::none;
    moving_nodes_.clear();
    moving_comment_ = -1;
    has_dragged_ = false;
    link_picked_up_ = false;
    has_scrub_edit_ = false;
    link_checks_.clear();
    link_error_.clear();
}

auto material_graph_view::to_screen(const math::vec2& graph) const -> ImVec2
{
    return ImVec2(origin_.x + (graph.x + pan_.x) * zoom_, origin_.y + (graph.y + pan_.y) * zoom_);
}

auto material_graph_view::to_graph(const ImVec2& screen) const -> math::vec2
{
    return {(screen.x - origin_.x) / zoom_ - pan_.x, (screen.y - origin_.y) / zoom_ - pan_.y};
}

auto material_graph_view::get_base_font_size() const -> float
{
    return ImGui::GetFontSize();
}

auto material_graph_view::get_text_width(const std::string& text) const -> float
{
    return ImGui::GetFont()->CalcTextSizeA(get_base_font_size(), FLT_MAX, 0.0f, text.c_str()).x;
}

auto material_graph_view::make_rows(const material_graph_node& node, const material_node_def& def) const -> std::vector<pin_row>
{
    std::vector<pin_row> rows;
    for(size_t pin = 0; pin < def.inputs.size(); ++pin)
    {
        const auto& input = def.inputs[pin];
        const auto shown = get_material_pin_presentation(node, def, false, pin);
        if(!shown.is_shown)
        {
            continue;
        }
        if(!can_split_material_pin(input) || !is_material_graph_input_split(node, input.name))
        {
            rows.push_back({pin_side::input, int(pin), -1, shown.label});
            continue;
        }
        // A split input is its components, each a pin of its own.
        for(int component = 0; component < int(get_material_value_width(input.type)); ++component)
        {
            const std::string letter = get_component_label(input, component);
            rows.push_back({pin_side::input, int(pin), component, input.label.empty() ? letter : input.label + " " + letter});
        }
    }
    for(size_t pin = 0; pin < def.outputs.size(); ++pin)
    {
        const auto& output = def.outputs[pin];
        const auto shown = get_material_pin_presentation(node, def, true, pin);
        if(!shown.is_shown)
        {
            continue;
        }
        rows.push_back({pin_side::output, int(pin), -1, shown.label});
        if(!can_split_material_pin(output) || !is_material_graph_output_split(node, output.name))
        {
            continue;
        }
        // A split output keeps its own pin and offers each component under it.
        for(int component = 0; component < int(get_material_value_width(output.type)); ++component)
        {
            rows.push_back({pin_side::output, int(pin), component, get_component_label(output, component)});
        }
    }
    return rows;
}

auto material_graph_view::get_input_label_column(const std::vector<pin_row>& rows) const -> float
{
    // The value fields of a node line up in one column after its longest input label.
    float column = 0.0f;
    for(const auto& row : rows)
    {
        if(row.side == pin_side::input)
        {
            column = std::max(column, get_text_width(row.label));
        }
    }
    return column;
}

auto material_graph_view::measure_node(const material_graph_node& node) const -> math::vec2
{
    const auto* def = find_material_node_def(node.type);
    if(!def)
    {
        return {NODE_MIN_WIDTH, NODE_HEADER_HEIGHT + NODE_ROW_HEIGHT + BODY_PADDING};
    }
    if(def->code == material_node_code::reroute)
    {
        return {REROUTE_WIDTH, REROUTE_HEIGHT};
    }
    const auto rows = make_rows(node, *def);
    const float label_column = get_input_label_column(rows);
    float left_width = 0.0f;
    float right_width = 0.0f;
    size_t left_rows = 0;
    size_t right_rows = 0;
    for(const auto& row : rows)
    {
        if(row.side == pin_side::output)
        {
            ++right_rows;
            right_width = std::max(right_width, get_text_width(row.label) + SIDE_PADDING + (row.component >= 0 ? COMPONENT_LABEL_INDENT : 0.0f));
            continue;
        }
        ++left_rows;
        const auto& pin = def->inputs[size_t(row.pin)];
        float content = row.component >= 0 ? CHIP_COMPONENT_WIDTH : get_chip_width(pin);
        if(row.component < 0 && !pin.default_code.empty())
        {
            content = get_text_width(describe_default_code(pin.default_code));
        }
        left_width = std::max(left_width, SIDE_PADDING + label_column + CHIP_GAP + content);
    }
    const bool has_preview = shows_texture_preview(node) || has_swatch(node, *def);
    float left_height = float(left_rows) * NODE_ROW_HEIGHT;
    if(has_preview)
    {
        left_width = std::max(left_width, SIDE_PADDING + NODE_PREVIEW_SIZE);
        left_height += NODE_PREVIEW_SIZE + NODE_PREVIEW_GAP;
    }
    const std::string title = std::string(get_material_category_icon(def->category)) + " " + get_node_title(node, *def);
    const float width = std::max({NODE_MIN_WIDTH,
                                  get_text_width(title) + SIDE_PADDING * 2.0f + BADGE_RADIUS * 2.0f,
                                  left_width + NODE_COLUMN_GAP + right_width});
    const float height = NODE_HEADER_HEIGHT + std::max(left_height, float(right_rows) * NODE_ROW_HEIGHT) + BODY_PADDING;
    return {std::ceil(width), height};
}

auto material_graph_view::get_pin_offset(const material_graph_node& node, const std::string& pin, bool is_output) const -> float
{
    const auto* def = find_material_node_def(node.type);
    if(!def || def->code == material_node_code::reroute)
    {
        return REROUTE_HEIGHT * 0.5f;
    }
    // The pin and the component it names: "base_color.r" on an input, "rgba.r" on an output.
    std::string name = pin;
    int component = -1;
    const auto separator = pin.find('.');
    if(separator != std::string::npos)
    {
        name = pin.substr(0, separator);
        const std::string components = pin.substr(separator + 1);
        component = components.size() == 1 ? get_material_component_index(components.front()) : -1;
    }
    const auto& pins = is_output ? def->outputs : def->inputs;
    const auto* found = name.empty() ? (pins.empty() ? nullptr : &pins.front()) : find_material_pin_def(pins, name);
    const int index = found ? int(found - pins.data()) : 0;
    const pin_side side = is_output ? pin_side::output : pin_side::input;
    int row = 0;
    int found_row = 0;
    for(const auto& candidate : make_rows(node, *def))
    {
        if(candidate.side != side)
        {
            continue;
        }
        if(candidate.pin == index && candidate.component == component)
        {
            found_row = row;
            break;
        }
        // The pin's own row stands in for a component it does not show.
        found_row = candidate.pin == index && candidate.component < 0 ? row : found_row;
        ++row;
    }
    return NODE_HEADER_HEIGHT + (float(found_row) + 0.5f) * NODE_ROW_HEIGHT;
}

auto material_graph_view::get_layout_metrics() const -> material_graph_layout_metrics
{
    material_graph_layout_metrics metrics;
    metrics.measure = [this](const material_graph_node& node)
    {
        return measure_node(node);
    };
    metrics.get_pin_offset = [this](const material_graph_node& node, const std::string& pin, bool is_output)
    {
        return get_pin_offset(node, pin, is_output);
    };
    return metrics;
}

auto material_graph_view::get_comment_size(const material_graph_comment& comment) const -> math::vec2
{
    if(comment.size.x > 0.0f && comment.size.y > 0.0f)
    {
        return comment.size;
    }
    // A comment written without a size is a note as big as its text.
    const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(get_base_font_size(), FLT_MAX, COMMENT_WRAP_WIDTH, comment.text.c_str());
    return {std::max(COMMENT_MIN_WIDTH, text.x + SIDE_PADDING * 2.0f), std::max(COMMENT_MIN_HEIGHT, text.y + COMMENT_TITLE_HEIGHT)};
}

auto material_graph_view::has_swatch(const material_graph_node& node, const material_node_def& def) const -> bool
{
    return are_swatches_shown_ && !node.is_preview_hidden && has_material_node_preview(def) && !shows_texture_preview(node);
}

auto material_graph_view::shows_texture_preview(const material_graph_node& node) -> bool
{
    const auto* def = find_material_node_def(node.type);
    // A cube texture has no flat picture: its swatch shows it unwrapped.
    if(def == nullptr || !holds_material_graph_texture(node) || def->code == material_node_code::function_input ||
       get_material_texture_node_kind(*def) == material_value_type::texture_cube)
    {
        return false;
    }
    const auto* input = find_material_graph_input(node, MATERIAL_TEXTURE_PIN);
    return input == nullptr || input->link.empty();
}

auto material_graph_view::get_entry_name(const material_graph& graph, const pin_ref& input) const -> std::string
{
    (void)graph;
    const auto& pin = geometry_[size_t(input.node)].def->inputs[size_t(input.pin)];
    return input.component < 0 ? pin.name : make_material_graph_component_pin(pin.name, input.component, get_material_component_letters(pin));
}

auto material_graph_view::get_output_name(const material_graph& graph, const pin_ref& output) const -> std::string
{
    (void)graph;
    const auto& pin = geometry_[size_t(output.node)].def->outputs[size_t(output.pin)];
    return output.component < 0 ? pin.name : make_material_graph_component_pin(pin.name, output.component, get_material_component_letters(pin));
}

auto material_graph_view::find_slot(const pin_ref& pin) const -> const pin_slot*
{
    if(pin.node < 0 || size_t(pin.node) >= geometry_.size())
    {
        return nullptr;
    }
    for(const auto& slot : geometry_[size_t(pin.node)].slots)
    {
        if(slot.ref == pin)
        {
            return &slot;
        }
    }
    return nullptr;
}

void material_graph_view::update_geometry(const material_graph& graph, const material_graph_selection& selection)
{
    is_geometry_stale_ = false;
    geometry_.assign(graph.nodes.size(), {});
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        const auto& node = graph.nodes[i];
        auto& geometry = geometry_[i];
        geometry.def = find_material_node_def(node.type);
        const math::vec2 size = measure_node(node);
        geometry.rect = ImRect(to_screen(node.position), to_screen(node.position + size));
        geometry.header = ImRect(geometry.rect.Min, ImVec2(geometry.rect.Max.x, geometry.rect.Min.y + NODE_HEADER_HEIGHT * zoom_));
        if(!geometry.def)
        {
            continue;
        }
        geometry.is_reroute = geometry.def->code == material_node_code::reroute;
        if(geometry.is_reroute)
        {
            const float y = geometry.rect.GetCenter().y;
            geometry.slots.push_back({{int(i), pin_side::input, 0, -1}, ImVec2(geometry.rect.Min.x, y)});
            geometry.slots.push_back({{int(i), pin_side::output, 0, -1}, ImVec2(geometry.rect.Max.x, y)});
            continue;
        }
        const auto rows = make_rows(node, *geometry.def);
        const float chip_x = geometry.rect.Min.x + (SIDE_PADDING + get_input_label_column(rows) + CHIP_GAP) * zoom_;
        const float half_chip = CHIP_HEIGHT * 0.5f * zoom_;
        int left_row = 0;
        int right_row = 0;
        for(const auto& row : rows)
        {
            const bool is_input = row.side == pin_side::input;
            const int line = is_input ? left_row++ : right_row++;
            const float y = geometry.rect.Min.y + (NODE_HEADER_HEIGHT + (float(line) + 0.5f) * NODE_ROW_HEIGHT) * zoom_;
            pin_slot slot;
            slot.ref = {int(i), row.side, row.pin, row.component};
            slot.label = row.label;
            slot.center = ImVec2(is_input ? geometry.rect.Min.x : geometry.rect.Max.x, y);
            slot.is_component = !is_input && row.component >= 0;
            if(is_input)
            {
                const auto& pin = geometry.def->inputs[size_t(row.pin)];
                slot.has_pin = !pin.is_value_only;
                const float width = row.component >= 0 ? CHIP_COMPONENT_WIDTH : get_chip_width(pin);
                slot.chip = ImRect(ImVec2(chip_x, y - half_chip), ImVec2(chip_x + width * zoom_, y + half_chip));
            }
            geometry.slots.push_back(slot);
        }
        geometry.has_swatch = has_swatch(node, *geometry.def);
        geometry.has_preview = shows_texture_preview(node) || geometry.has_swatch;
        if(geometry.has_preview)
        {
            const ImVec2 min(geometry.rect.Min.x + SIDE_PADDING * zoom_,
                             geometry.rect.Min.y + (NODE_HEADER_HEIGHT + float(left_row) * NODE_ROW_HEIGHT + NODE_PREVIEW_GAP * 0.5f) * zoom_);
            geometry.preview = ImRect(min, min + ImVec2(NODE_PREVIEW_SIZE, NODE_PREVIEW_SIZE) * zoom_);
        }
    }
    update_links(graph);
    // The selected nodes draw last, over the others, and are hit first.
    draw_order_.clear();
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        if(!selection.has_node(graph.nodes[i].id))
        {
            draw_order_.push_back(int(i));
        }
    }
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        if(selection.has_node(graph.nodes[i].id))
        {
            draw_order_.push_back(int(i));
        }
    }
}

void material_graph_view::update_links(const material_graph& graph)
{
    links_.clear();
    for(size_t node = 0; node < geometry_.size(); ++node)
    {
        for(auto& slot : geometry_[node].slots)
        {
            if(slot.ref.side != pin_side::input)
            {
                continue;
            }
            const std::string entry = get_entry_name(graph, slot.ref);
            const auto* input = find_material_graph_input(graph.nodes[node], entry);
            if(!input || input->link.empty())
            {
                continue;
            }
            const auto end = parse_material_graph_link(input->link);
            const int source = find_material_graph_node(graph, end.node);
            const auto* source_def = source >= 0 ? geometry_[size_t(source)].def : nullptr;
            if(!source_def || source_def->outputs.empty())
            {
                continue;
            }
            const auto* output = end.pin.empty() ? &source_def->outputs.front() : find_material_pin_def(source_def->outputs, end.pin);
            if(!output)
            {
                continue;
            }
            pin_ref from{source, pin_side::output, int(output - source_def->outputs.data()), -1};
            // A link reading one component starts at that component's pin when the output shows it.
            pin_ref component_from = from;
            component_from.component = end.components.size() == 1 ? get_material_component_index(end.components.front()) : -1;
            if(component_from.component >= 0 && find_slot(component_from))
            {
                from = component_from;
            }
            const auto* from_slot = find_slot(from);
            if(!from_slot)
            {
                continue;
            }
            link_geometry link;
            link.from = from;
            link.to = slot.ref;
            link.start = from_slot->center;
            link.end = slot.center;
            link.type = end.components.empty() ? get_output_type(from.node, from.pin) : get_material_value_type(uint32_t(end.components.size()));
            link.components = from.component < 0 ? end.components : std::string{};
            link.entry = entry;
            links_.push_back(link);
            slot.is_linked = true;
        }
    }
    for(const auto& link : links_)
    {
        for(auto& slot : geometry_[size_t(link.from.node)].slots)
        {
            slot.is_linked = slot.is_linked || slot.ref == link.from;
        }
    }
}

auto material_graph_view::is_compiled_current(int node) const -> bool
{
    return compiled_ && compiled_->nodes.size() == geometry_.size() && node >= 0 && size_t(node) < compiled_->nodes.size();
}

auto material_graph_view::get_input_type(int node, int pin) const -> material_value_type
{
    if(is_compiled_current(node) && size_t(pin) < compiled_->nodes[size_t(node)].input_types.size())
    {
        return compiled_->nodes[size_t(node)].input_types[size_t(pin)];
    }
    const auto* def = geometry_[size_t(node)].def;
    return def ? def->inputs[size_t(pin)].type : material_value_type::dynamic;
}

auto material_graph_view::get_output_type(int node, int pin) const -> material_value_type
{
    if(is_compiled_current(node) && size_t(pin) < compiled_->nodes[size_t(node)].output_types.size())
    {
        return compiled_->nodes[size_t(node)].output_types[size_t(pin)];
    }
    const auto* def = geometry_[size_t(node)].def;
    return def ? def->outputs[size_t(pin)].type : material_value_type::dynamic;
}

auto material_graph_view::get_pin_type(const pin_ref& pin) const -> material_value_type
{
    if(pin.component >= 0)
    {
        return material_value_type::float1;
    }
    return pin.side == pin_side::input ? get_input_type(pin.node, pin.pin) : get_output_type(pin.node, pin.pin);
}

auto material_graph_view::is_node_used(int node) const -> bool
{
    return !is_compiled_current(node) || compiled_->nodes[size_t(node)].is_used;
}

void material_graph_view::frame_rect(const math::vec2& min, const math::vec2& max)
{
    if(size_.x <= 1.0f || size_.y <= 1.0f)
    {
        return;
    }
    const math::vec2 extent = max - min + math::vec2(CANVAS_FRAME_MARGIN * 2.0f);
    zoom_ = std::clamp(std::min(size_.x / extent.x, size_.y / extent.y), CANVAS_MIN_ZOOM, CANVAS_MAX_FRAME_ZOOM);
    const math::vec2 center = (min + max) * 0.5f;
    pan_ = {size_.x * 0.5f / zoom_ - center.x, size_.y * 0.5f / zoom_ - center.y};
}

void material_graph_view::frame_all(const material_graph& graph)
{
    if(graph.nodes.empty())
    {
        return;
    }
    math::vec2 min(FLT_MAX);
    math::vec2 max(-FLT_MAX);
    for(const auto& node : graph.nodes)
    {
        min = math::min(min, node.position);
        max = math::max(max, node.position + measure_node(node));
    }
    for(const auto& comment : graph.comments)
    {
        min = math::min(min, comment.position);
        max = math::max(max, comment.position + get_comment_size(comment));
    }
    frame_rect(min, max);
}

void material_graph_view::frame_selection(const material_graph& graph, const material_graph_selection& selection)
{
    if(selection.nodes.empty())
    {
        frame_all(graph);
        return;
    }
    math::vec2 min(FLT_MAX);
    math::vec2 max(-FLT_MAX);
    for(const auto& node : graph.nodes)
    {
        if(selection.has_node(node.id))
        {
            min = math::min(min, node.position);
            max = math::max(max, node.position + measure_node(node));
        }
    }
    frame_rect(min, max);
}

// ---------------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------------

auto material_graph_view::hit_node(const ImVec2& mouse) const -> int
{
    for(auto it = draw_order_.rbegin(); it != draw_order_.rend(); ++it)
    {
        if(geometry_[size_t(*it)].rect.Contains(mouse))
        {
            return *it;
        }
    }
    return -1;
}

auto material_graph_view::hit_pin(const ImVec2& mouse) const -> pin_ref
{
    for(auto it = draw_order_.rbegin(); it != draw_order_.rend(); ++it)
    {
        const auto& geometry = geometry_[size_t(*it)];
        // A reroute's pins are its two ends; its middle moves it.
        const float base = geometry.is_reroute ? REROUTE_PIN_HIT_RADIUS : PIN_HIT_RADIUS;
        const float radius = std::max(base * zoom_, base * 0.6f);
        for(const auto& slot : geometry.slots)
        {
            if(slot.has_pin && measure_screen_length(mouse - slot.center) <= radius)
            {
                return slot.ref;
            }
        }
    }
    return {};
}

auto material_graph_view::hit_chip(const ImVec2& mouse) const -> pin_ref
{
    for(auto it = draw_order_.rbegin(); it != draw_order_.rend(); ++it)
    {
        const auto& geometry = geometry_[size_t(*it)];
        if(!geometry.def || geometry.is_reroute || !geometry.rect.Contains(mouse))
        {
            continue;
        }
        for(const auto& slot : geometry.slots)
        {
            if(slot.ref.side != pin_side::input || slot.is_linked || !slot.chip.Contains(mouse))
            {
                continue;
            }
            const auto& pin = geometry.def->inputs[size_t(slot.ref.pin)];
            if(slot.ref.component >= 0 || pin.default_code.empty())
            {
                return slot.ref;
            }
        }
        return {};
    }
    return {};
}

void material_graph_view::get_link_points(const ImVec2& from, const ImVec2& to, ImVec2& control_from, ImVec2& control_to) const
{
    const float tangent = std::max(std::fabs(to.x - from.x) * 0.5f, LINK_MIN_TANGENT * zoom_);
    control_from = ImVec2(from.x + tangent, from.y);
    control_to = ImVec2(to.x - tangent, to.y);
}

auto material_graph_view::hit_link(const ImVec2& mouse) const -> int
{
    for(size_t i = 0; i < links_.size(); ++i)
    {
        const auto& link = links_[i];
        ImVec2 c1;
        ImVec2 c2;
        get_link_points(link.start, link.end, c1, c2);
        ImVec2 previous = link.start;
        for(int step = 1; step <= LINK_SEGMENTS; ++step)
        {
            const ImVec2 point = ImBezierCubicCalc(link.start, c1, c2, link.end, float(step) / float(LINK_SEGMENTS));
            if(measure_distance_to_segment(mouse, previous, point) <= LINK_HIT_DISTANCE)
            {
                return int(i);
            }
            previous = point;
        }
    }
    return -1;
}

auto material_graph_view::hit_comment(const material_graph& graph, const ImVec2& mouse, comment_part& part) const -> int
{
    part = comment_part::none;
    for(size_t i = graph.comments.size(); i-- > 0;)
    {
        const auto& comment = graph.comments[i];
        const ImVec2 min = to_screen(comment.position);
        const ImVec2 max = to_screen(comment.position + get_comment_size(comment));
        const float corner = COMMENT_CORNER * zoom_;
        if(ImRect(ImVec2(max.x - corner, max.y - corner), max).Contains(mouse))
        {
            part = comment_part::corner;
            return int(i);
        }
        if(ImRect(min, ImVec2(max.x, min.y + COMMENT_TITLE_HEIGHT * zoom_)).Contains(mouse))
        {
            part = comment_part::title;
            return int(i);
        }
    }
    return -1;
}

auto material_graph_view::get_link_error(const material_graph& graph, const pin_ref& from, const pin_ref& to) -> std::string
{
    const pin_ref& output = from.side == pin_side::output ? from : to;
    const pin_ref& input = from.side == pin_side::output ? to : from;
    if(output.side != pin_side::output || input.side != pin_side::input)
    {
        return "An output links into an input.";
    }
    const std::string& output_id = graph.nodes[size_t(output.node)].id;
    const std::string& input_id = graph.nodes[size_t(input.node)].id;
    const std::string output_name = get_output_name(graph, output);
    const std::string entry = get_entry_name(graph, input);
    const std::string key = output_id + "." + output_name + ">" + input_id + "." + entry;
    const auto known = link_checks_.find(key);
    if(known != link_checks_.end())
    {
        return known->second;
    }
    std::string error = check_material_graph_link(graph, output_id, output_name, input_id, entry);
    link_checks_[key] = error;
    return error;
}

auto material_graph_view::find_link_target(const material_graph& graph, const ImVec2& mouse) -> pin_ref
{
    link_error_.clear();
    const pin_side wanted = link_source_.side == pin_side::output ? pin_side::input : pin_side::output;
    const pin_ref pin = hit_pin(mouse);
    if(pin.is_valid() && pin.side == wanted && pin.node != link_source_.node)
    {
        link_error_ = get_link_error(graph, link_source_, pin);
        return pin;
    }
    const int node = hit_node(mouse);
    if(node < 0 || node == link_source_.node || !geometry_[size_t(node)].def)
    {
        return {};
    }
    // Into a node: of its pins that take the link, the one nearest the mouse vertically, as if dropped on its row.
    pin_ref nearest;
    float nearest_distance = FLT_MAX;
    std::string first_error;
    for(const auto& slot : geometry_[size_t(node)].slots)
    {
        if(slot.ref.side != wanted || !slot.has_pin)
        {
            continue;
        }
        const std::string error = get_link_error(graph, link_source_, slot.ref);
        if(!error.empty())
        {
            first_error = first_error.empty() ? error : first_error;
            continue;
        }
        const float distance = std::abs(slot.center.y - mouse.y);
        if(distance < nearest_distance)
        {
            nearest = slot.ref;
            nearest_distance = distance;
        }
    }
    link_error_ = nearest.is_valid() ? std::string{} : first_error;
    return nearest;
}

// ---------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------

auto material_graph_view::draw(material_graph& graph, const material_graph_compile_result& compiled, material_graph_selection& selection)
    -> result
{
    result out;
    compiled_ = &compiled;
    origin_ = ImGui::GetCursorScreenPos();
    size_ = ImGui::GetContentRegionAvail();
    size_.x = std::max(size_.x, 50.0f);
    size_.y = std::max(size_.y, 50.0f);
    if(frame_request_)
    {
        frame_request_ = false;
        // A graph shown for the first time with every node on one spot (written by hand or by a tool) is laid out.
        if(is_material_graph_never_laid_out(graph))
        {
            layout_material_graph(graph, get_layout_metrics());
            out.begin_edit = true;
            out.layout_changed = true;
            out.finished = true;
        }
        frame_all(graph);
    }
    update_geometry(graph, selection);
    ImGui::InvisibleButton("##material_graph_canvas",
                           size_,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    handle_drop(graph, selection, out);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin_, origin_ + size_, true);
    draw_grid(draw_list);
    draw_comments(draw_list, graph, selection);
    draw_links(draw_list, graph, selection);
    draw_nodes(draw_list, graph, selection);
    draw_interaction(draw_list, graph);
    draw_empty_hint(draw_list, graph);
    draw_list->PopClipRect();
    draw_tooltip(graph);
    handle_input(graph, selection, out);
    draw_search_popup(graph, selection, out);
    draw_value_popup(graph, out);
    draw_context_menus(graph, selection, out);
    compiled_ = nullptr;
    return out;
}

void material_graph_view::draw_grid(ImDrawList* draw_list) const
{
    draw_list->AddRectFilled(origin_, origin_ + size_, COL_MATERIAL_CANVAS);
    const float step = CANVAS_GRID_STEP * zoom_;
    if(step < 6.0f)
    {
        return;
    }
    const float offset_x = std::fmod(pan_.x * zoom_, step);
    const float offset_y = std::fmod(pan_.y * zoom_, step);
    for(float x = offset_x; x < size_.x; x += step)
    {
        const int line = int(std::floor((x - pan_.x * zoom_) / step + 0.5f));
        draw_list->AddLine(ImVec2(origin_.x + x, origin_.y),
                           ImVec2(origin_.x + x, origin_.y + size_.y),
                           line % CANVAS_GRID_MAJOR_EVERY == 0 ? COL_MATERIAL_GRID_MAJOR : COL_MATERIAL_GRID);
    }
    for(float y = offset_y; y < size_.y; y += step)
    {
        const int line = int(std::floor((y - pan_.y * zoom_) / step + 0.5f));
        draw_list->AddLine(ImVec2(origin_.x, origin_.y + y),
                           ImVec2(origin_.x + size_.x, origin_.y + y),
                           line % CANVAS_GRID_MAJOR_EVERY == 0 ? COL_MATERIAL_GRID_MAJOR : COL_MATERIAL_GRID);
    }
}

void material_graph_view::draw_text(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const char* text, float scale) const
{
    const float size = get_base_font_size() * zoom_ * scale;
    if(size >= MIN_FONT_SIZE)
    {
        draw_list->AddText(ImGui::GetFont(), size, position, color, text);
    }
}

void material_graph_view::draw_comments(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const
{
    for(size_t i = 0; i < graph.comments.size(); ++i)
    {
        const auto& comment = graph.comments[i];
        const ImVec2 min = to_screen(comment.position);
        const ImVec2 max = to_screen(comment.position + get_comment_size(comment));
        const float rounding = COMMENT_ROUNDING * zoom_;
        const bool selected = selection.comment == int(i);
        draw_list->AddRectFilled(min, max, COL_MATERIAL_COMMENT, rounding);
        draw_list->AddRectFilled(min, ImVec2(max.x, min.y + COMMENT_TITLE_HEIGHT * zoom_), COL_MATERIAL_COMMENT_TITLE, rounding, ImDrawFlags_RoundCornersTop);
        draw_list->AddRect(min,
                           max,
                           selected ? COL_MATERIAL_SELECTED : COL_MATERIAL_COMMENT_BORDER,
                           rounding,
                           0,
                           selected ? SELECTED_BORDER_THICKNESS : NODE_OUTLINE_THICKNESS);
        const float font_size = get_base_font_size() * zoom_;
        if(font_size >= MIN_FONT_SIZE)
        {
            const float wrap = std::max(max.x - min.x - SIDE_PADDING * 2.0f * zoom_, 1.0f);
            draw_list->AddText(ImGui::GetFont(),
                               font_size,
                               ImVec2(min.x + SIDE_PADDING * zoom_, min.y + (COMMENT_TITLE_HEIGHT * zoom_ - font_size) * 0.5f),
                               COL_MATERIAL_TEXT,
                               comment.text.c_str(),
                               nullptr,
                               wrap);
        }
        const float corner = COMMENT_CORNER * zoom_;
        draw_list->AddTriangleFilled(max, ImVec2(max.x - corner, max.y), ImVec2(max.x, max.y - corner), COL_MATERIAL_COMMENT_BORDER);
    }
}

void material_graph_view::draw_link_curve(ImDrawList* draw_list, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) const
{
    ImVec2 c1;
    ImVec2 c2;
    get_link_points(from, to, c1, c2);
    draw_list->AddBezierCubic(from, c1, c2, to, color, thickness);
}

void material_graph_view::draw_links(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const
{
    const float scale = std::max(zoom_, 0.6f);
    for(const auto& link : links_)
    {
        const std::string& reader = graph.nodes[size_t(link.to.node)].id;
        const bool is_selected = selection.link == reader + "." + link.entry;
        const bool highlighted = is_selected || selection.has_node(reader) || selection.has_node(graph.nodes[size_t(link.from.node)].id);
        ImU32 color = get_material_type_color(link.type);
        if(!is_node_used(link.to.node))
        {
            color = scale_color_alpha(color, UNUSED_ALPHA);
        }
        color = is_selected ? COL_MATERIAL_SELECTED : color;
        draw_link_curve(draw_list, link.start, link.end, color, (highlighted ? HIGHLIGHTED_LINK_THICKNESS : LINK_THICKNESS) * scale);
        if(link.components.empty())
        {
            continue;
        }
        // A link picking components from a whole output says which, beside the input it feeds.
        const std::string label = "." + link.components;
        const float font_size = get_base_font_size() * zoom_ * CHIP_TEXT_SCALE;
        const float width = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str()).x;
        draw_text(draw_list,
                  ImVec2(link.end.x - width - SIDE_PADDING * zoom_, link.end.y - font_size * 1.2f),
                  COL_MATERIAL_SUBTEXT,
                  label.c_str(),
                  CHIP_TEXT_SCALE);
    }
}

void material_graph_view::draw_pin(ImDrawList* draw_list, const ImVec2& center, ImU32 color, bool is_linked, bool is_hovered) const
{
    const float radius = (is_hovered ? HOVERED_PIN_RADIUS : PIN_RADIUS) * std::max(zoom_, 0.6f);
    if(is_linked)
    {
        draw_list->AddCircleFilled(center, radius, color);
        return;
    }
    draw_list->AddCircleFilled(center, radius, COL_MATERIAL_NODE_BODY);
    draw_list->AddCircle(center, radius, color, 0, std::max(1.5f * zoom_, 1.0f));
}

void material_graph_view::draw_nodes(ImDrawList* draw_list, const material_graph& graph, const material_graph_selection& selection) const
{
    for(const int index : draw_order_)
    {
        draw_node(draw_list, graph, index, selection.has_node(graph.nodes[size_t(index)].id));
    }
}

void material_graph_view::draw_reroute(ImDrawList* draw_list, int index, bool selected) const
{
    const auto& geometry = geometry_[size_t(index)];
    const ImRect& rect = geometry.rect;
    const ImU32 color = get_material_type_color(get_output_type(index, 0));
    const float radius = rect.GetHeight() * 0.5f;
    draw_list->AddRectFilled(rect.Min, rect.Max, COL_MATERIAL_NODE_BODY, radius);
    draw_list->AddLine(geometry.slots[0].center, geometry.slots[1].center, color, std::max(LINK_THICKNESS * zoom_, 1.0f));
    draw_list->AddRect(rect.Min,
                       rect.Max,
                       selected ? COL_MATERIAL_SELECTED : scale_color_alpha(color, 0.6f),
                       radius,
                       0,
                       selected ? SELECTED_BORDER_THICKNESS : NODE_OUTLINE_THICKNESS);
    const pin_ref hovered = ImGui::IsItemHovered() ? hit_pin(ImGui::GetIO().MousePos) : pin_ref{};
    for(const auto& slot : geometry.slots)
    {
        draw_pin(draw_list, slot.center, color, slot.is_linked, hovered == slot.ref);
    }
}

void material_graph_view::draw_node(ImDrawList* draw_list, const material_graph& graph, int index, bool selected) const
{
    const auto& geometry = geometry_[size_t(index)];
    if(geometry.is_reroute)
    {
        draw_reroute(draw_list, index, selected);
        return;
    }
    const bool is_used = is_node_used(index);
    const float alpha = is_used ? 1.0f : UNUSED_ALPHA;
    const float rounding = NODE_CORNER_RADIUS * zoom_;
    const ImRect& rect = geometry.rect;
    draw_list->AddRectFilled(rect.Min + ImVec2(NODE_SHADOW_OFFSET, NODE_SHADOW_OFFSET),
                             rect.Max + ImVec2(NODE_SHADOW_OFFSET, NODE_SHADOW_OFFSET),
                             scale_color_alpha(COL_MATERIAL_NODE_SHADOW, alpha),
                             rounding);
    draw_list->AddRectFilled(rect.Min, rect.Max, scale_color_alpha(COL_MATERIAL_NODE_BODY, alpha), rounding);
    draw_node_header(draw_list, graph, index, is_used);
    draw_node_slots(draw_list, graph, index, scale_color_alpha(COL_MATERIAL_TEXT, alpha));
    if(geometry.has_preview && shows_texture_preview(graph.nodes[size_t(index)]))
    {
        draw_texture_preview(draw_list, graph, index);
    }
    else if(geometry.has_preview)
    {
        draw_swatch(draw_list, graph, index);
    }
    const auto diagnostics = is_compiled_current(index) ? compiled_->get_node_diagnostics(graph.nodes[size_t(index)].id)
                                                        : std::vector<const material_graph_diagnostic*>{};
    const bool has_error = !diagnostics.empty() && diagnostics.front()->severity == material_graph_severity::error;
    ImU32 border = has_error ? COL_MATERIAL_ERROR : scale_color_alpha(COL_MATERIAL_NODE_BORDER, alpha);
    border = selected ? COL_MATERIAL_SELECTED : border;
    draw_list->AddRect(rect.Min, rect.Max, border, rounding, 0, selected || has_error ? SELECTED_BORDER_THICKNESS : NODE_OUTLINE_THICKNESS);
    draw_node_badge(draw_list, graph, index);
}

void material_graph_view::draw_node_header(ImDrawList* draw_list, const material_graph& graph, int index, bool is_used) const
{
    const auto& geometry = geometry_[size_t(index)];
    const auto& node = graph.nodes[size_t(index)];
    const float alpha = is_used ? 1.0f : UNUSED_ALPHA;
    const ImU32 color = geometry.def ? get_material_category_color(geometry.def->category) : COL_MATERIAL_ERROR;
    draw_list->AddRectFilled(geometry.header.Min,
                             geometry.header.Max,
                             scale_color_alpha(color, alpha),
                             NODE_CORNER_RADIUS * zoom_,
                             ImDrawFlags_RoundCornersTop);
    const std::string title = geometry.def ? std::string(get_material_category_icon(geometry.def->category)) + " " + get_node_title(node, *geometry.def)
                                           : std::string(ICON_MDI_ALERT " ") + node.type;
    const float font_size = get_base_font_size() * zoom_;
    const ImVec2 position(geometry.header.Min.x + SIDE_PADDING * zoom_ * 0.75f, geometry.header.Min.y + (geometry.header.GetHeight() - font_size) * 0.5f);
    draw_text(draw_list, position, scale_color_alpha(COL_MATERIAL_TEXT, alpha), title.c_str());
}

void material_graph_view::draw_node_badge(ImDrawList* draw_list, const material_graph& graph, int index) const
{
    if(!is_compiled_current(index))
    {
        return;
    }
    const auto diagnostics = compiled_->get_node_diagnostics(graph.nodes[size_t(index)].id);
    if(diagnostics.empty() || diagnostics.front()->severity == material_graph_severity::info)
    {
        return;
    }
    const auto& header = geometry_[size_t(index)].header;
    const float radius = BADGE_RADIUS * zoom_;
    const ImVec2 center(header.Max.x - radius - SIDE_PADDING * 0.5f * zoom_, header.GetCenter().y);
    const bool is_error = diagnostics.front()->severity == material_graph_severity::error;
    draw_list->AddCircleFilled(center, radius, is_error ? COL_MATERIAL_ERROR : COL_MATERIAL_WARNING);
    const float font_size = get_base_font_size() * zoom_ * CHIP_TEXT_SCALE;
    if(font_size >= MIN_FONT_SIZE)
    {
        const ImVec2 text = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, "!");
        draw_list->AddText(ImGui::GetFont(), font_size, center - text * 0.5f, IM_COL32(20, 20, 20, 255), "!");
    }
}

void material_graph_view::draw_node_slots(ImDrawList* draw_list, const material_graph& graph, int index, ImU32 text_color) const
{
    const auto& geometry = geometry_[size_t(index)];
    const pin_ref hovered = ImGui::IsItemHovered() ? hit_pin(ImGui::GetIO().MousePos) : pin_ref{};
    const float font_size = get_base_font_size() * zoom_;
    const float text_offset = font_size * 0.5f;
    for(const auto& slot : geometry.slots)
    {
        const ImU32 color = get_material_type_color(get_pin_type(slot.ref));
        if(slot.ref.side == pin_side::input)
        {
            if(slot.has_pin)
            {
                draw_pin(draw_list, slot.center, color, slot.is_linked, hovered == slot.ref);
            }
            draw_text(draw_list, ImVec2(slot.center.x + SIDE_PADDING * zoom_, slot.center.y - text_offset), text_color, slot.label.c_str());
            if(!slot.is_linked)
            {
                draw_chip(draw_list, graph, index, slot);
            }
            continue;
        }
        draw_pin(draw_list, slot.center, color, slot.is_linked, hovered == slot.ref);
        const float width = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, slot.label.c_str()).x;
        const float indent = slot.is_component ? COMPONENT_LABEL_INDENT * zoom_ : 0.0f;
        draw_text(draw_list,
                  ImVec2(slot.center.x - SIDE_PADDING * zoom_ - width - indent, slot.center.y - text_offset),
                  slot.is_component ? COL_MATERIAL_SUBTEXT : text_color,
                  slot.label.c_str());
    }
}

void material_graph_view::draw_texture_preview(ImDrawList* draw_list, const material_graph& graph, int index) const
{
    const ImRect& frame = geometry_[size_t(index)].preview;
    const auto& node = graph.nodes[size_t(index)];
    const float rounding = CHIP_ROUNDING * zoom_;
    draw_list->AddRectFilled(frame.Min, frame.Max, COL_MATERIAL_CHIP, rounding);
    // The swatch shows the texture as the node samples it, with its UV.
    if(node.texture && geometry_[size_t(index)].has_swatch && draw_swatch_image(draw_list, graph, index, frame))
    {
        draw_list->AddRect(frame.Min, frame.Max, COL_MATERIAL_NODE_BORDER, rounding);
        return;
    }
    // try_get, not get: it starts the load without waiting for it, so the canvas never stalls on a texture.
    const auto texture = node.texture ? node.texture.try_get() : nullptr;
    if(texture && texture->info.width > 0 && texture->info.height > 0)
    {
        const float aspect = float(texture->info.width) / float(texture->info.height);
        ImVec2 size = frame.GetSize();
        size = aspect >= 1.0f ? ImVec2(size.x, size.x / aspect) : ImVec2(size.y * aspect, size.y);
        const ImVec2 min = frame.GetCenter() - size * 0.5f;
        draw_list->AddImage(ImGui::ToId(texture), min, min + size);
    }
    else
    {
        const char* text = node.texture ? ICON_MDI_IMAGE " Loading" : ICON_MDI_IMAGE_OFF " No texture";
        const float font_size = get_base_font_size() * zoom_ * CHIP_TEXT_SCALE;
        const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
        draw_text(draw_list, frame.GetCenter() - text_size * 0.5f, COL_MATERIAL_SUBTEXT, text, CHIP_TEXT_SCALE);
    }
    draw_list->AddRect(frame.Min, frame.Max, COL_MATERIAL_NODE_BORDER, rounding);
}

void material_graph_view::draw_swatch(ImDrawList* draw_list, const material_graph& graph, int index) const
{
    const ImRect& frame = geometry_[size_t(index)].preview;
    const float rounding = CHIP_ROUNDING * zoom_;
    draw_list->AddRectFilled(frame.Min, frame.Max, COL_MATERIAL_CHIP, rounding);
    if(!draw_swatch_image(draw_list, graph, index, frame))
    {
        const bool has_errors = previews_ && previews_->has_graph_errors();
        const bool has_failed = previews_ && previews_->has_failed();
        const char* text = has_errors ? ICON_MDI_ALERT " No preview" : has_failed ? ICON_MDI_ALERT " Failed" : ICON_MDI_TIMER_SAND " Compiling";
        const float font_size = get_base_font_size() * zoom_ * CHIP_TEXT_SCALE;
        const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
        draw_text(draw_list, frame.GetCenter() - text_size * 0.5f, COL_MATERIAL_SUBTEXT, text, CHIP_TEXT_SCALE);
    }
    draw_list->AddRect(frame.Min, frame.Max, COL_MATERIAL_NODE_BORDER, rounding);
}

auto material_graph_view::draw_swatch_image(ImDrawList* draw_list, const material_graph& graph, int index, const ImRect& frame) const
    -> bool
{
    ImVec2 uv0;
    ImVec2 uv1;
    if(!previews_ || !previews_->get_swatch_texture() || !previews_->get_swatch_uv(graph.nodes[size_t(index)].id, uv0, uv1))
    {
        return false;
    }
    draw_list->AddImage(ImGui::ToId(previews_->get_swatch_texture()), frame.Min, frame.Max, uv0, uv1);
    return true;
}

void material_graph_view::draw_chip(ImDrawList* draw_list, const material_graph& graph, int node, const pin_slot& slot) const
{
    const auto& def = geometry_[size_t(node)].def->inputs[size_t(slot.ref.pin)];
    const ImRect& chip = slot.chip;
    if(chip.GetWidth() <= 0.0f)
    {
        return;
    }
    const float font_size = get_base_font_size() * zoom_ * CHIP_TEXT_SCALE;
    const float text_y = chip.GetCenter().y - font_size * 0.5f;
    if(slot.ref.component < 0 && !def.default_code.empty())
    {
        draw_text(draw_list, ImVec2(chip.Min.x, text_y), COL_MATERIAL_SUBTEXT, describe_default_code(def.default_code), CHIP_TEXT_SCALE);
        return;
    }
    const bool is_hovered = ImGui::IsItemHovered() && chip.Contains(ImGui::GetIO().MousePos) && interaction_ == interaction::none;
    const float rounding = CHIP_ROUNDING * zoom_;
    draw_list->AddRectFilled(chip.Min, chip.Max, is_hovered ? COL_MATERIAL_CHIP_HOVER : COL_MATERIAL_CHIP, rounding);
    const math::vec4 value = get_input_value(graph.nodes[size_t(node)], def);
    if(def.is_color && slot.ref.component < 0)
    {
        const ImRect swatch(chip.Min + ImVec2(2.0f, 2.0f) * zoom_, chip.Max - ImVec2(2.0f, 2.0f) * zoom_);
        draw_list->AddRectFilled(swatch.Min, swatch.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(value.x, value.y, value.z, 1.0f)), rounding);
        return;
    }
    std::string text;
    if(slot.ref.component >= 0)
    {
        text = format_component(value[slot.ref.component]);
    }
    else
    {
        const uint32_t width = def.type == material_value_type::dynamic ? 1 : get_material_value_width(def.type);
        for(uint32_t i = 0; i < width; ++i)
        {
            text += (i == 0 ? "" : ", ") + format_component(value[int(i)]);
        }
    }
    draw_list->PushClipRect(chip.Min, chip.Max, true);
    draw_text(draw_list, ImVec2(chip.Min.x + CHIP_PADDING * zoom_, text_y), COL_MATERIAL_CHIP_TEXT, text.c_str(), CHIP_TEXT_SCALE);
    draw_list->PopClipRect();
}

void material_graph_view::draw_interaction(ImDrawList* draw_list, const material_graph& graph)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if(interaction_ == interaction::box_selecting && has_dragged_)
    {
        const ImVec2 min(std::min(press_position_.x, mouse.x), std::min(press_position_.y, mouse.y));
        const ImVec2 max(std::max(press_position_.x, mouse.x), std::max(press_position_.y, mouse.y));
        draw_list->AddRectFilled(min, max, COL_MATERIAL_BOX_FILL);
        draw_list->AddRect(min, max, COL_MATERIAL_BOX_BORDER);
        return;
    }
    const auto* source = interaction_ == interaction::linking ? find_slot(link_source_) : nullptr;
    if(!source)
    {
        return;
    }
    ImVec2 end = mouse;
    const pin_ref target = find_link_target(graph, mouse);
    if(const auto* target_slot = find_slot(target))
    {
        end = target_slot->center;
    }
    // A target that refuses the link turns it red and says why.
    const bool is_refused = !link_error_.empty();
    const ImU32 color = is_refused ? COL_MATERIAL_LINK_REFUSED : COL_MATERIAL_LINK_PREVIEW;
    const float thickness = HIGHLIGHTED_LINK_THICKNESS * std::max(zoom_, 0.6f);
    if(link_source_.side == pin_side::output)
    {
        draw_link_curve(draw_list, source->center, end, color, thickness);
    }
    else
    {
        draw_link_curve(draw_list, end, source->center, color, thickness);
    }
    if(is_refused)
    {
        show_wrapped_tooltip(ICON_MDI_CLOSE_CIRCLE " %s", link_error_.c_str());
    }
}

void material_graph_view::draw_empty_hint(ImDrawList* draw_list, const material_graph& graph) const
{
    if(graph.nodes.size() > 1)
    {
        return;
    }
    const char* hint = "Right-click or press Space to add a node, drag one in from the palette, or drop a texture.";
    const ImVec2 hint_size = ImGui::CalcTextSize(hint);
    draw_list->AddText(ImVec2(origin_.x + (size_.x - hint_size.x) * 0.5f, origin_.y + size_.y * 0.85f), COL_MATERIAL_SUBTEXT, hint);
}

void material_graph_view::draw_tooltip(const material_graph& graph) const
{
    if(!ImGui::IsItemHovered() || interaction_ != interaction::none || is_geometry_stale_)
    {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const pin_ref pin = hit_pin(mouse);
    if(pin.is_valid())
    {
        const auto& def = *geometry_[size_t(pin.node)].def;
        const auto& pin_def = pin.side == pin_side::input ? def.inputs[size_t(pin.pin)] : def.outputs[size_t(pin.pin)];
        const auto* slot = find_slot(pin);
        const std::string label = slot && !slot->label.empty() ? slot->label : std::string(pin.side == pin_side::input ? "Input" : "Output");
        const char* hint = can_split_material_pin(pin_def) ? "\nDouble-click to split it into components, or join them again." : "";
        show_wrapped_tooltip("%s: %s%s%s\n%s%s",
                          label.c_str(),
                          get_material_value_type_name(get_pin_type(pin)),
                          pin_def.tooltip.empty() ? "" : "\n",
                          pin_def.tooltip.c_str(),
                          pin.side == pin_side::input ? "Drag to a node's output to link. Alt+click breaks the link."
                                                      : "Drag to a node's input to link, or to empty space to add a node.",
                          hint);
        return;
    }
    if(hit_chip(mouse).is_valid())
    {
        show_wrapped_tooltip("Drag sideways to change (Shift faster, Ctrl finer). Click to type a value.");
        return;
    }
    const int node = hit_node(mouse);
    if(node < 0)
    {
        return;
    }
    const auto& geometry = geometry_[size_t(node)];
    if(geometry.has_preview && geometry.preview.Contains(mouse) && !shows_texture_preview(graph.nodes[size_t(node)]))
    {
        show_wrapped_tooltip("The node's value over a flat patch facing you, its UV running across it: a float shows as "
                          "grey, a vector's first three components as a colour, clamped to 0..1.\nRight-click "
                          "the node to hide its preview.");
        return;
    }
    if(geometry.has_preview && geometry.preview.Contains(mouse))
    {
        const auto& texture = graph.nodes[size_t(node)].texture;
        const std::string name = texture ? texture.id() : std::string("No texture.");
        show_wrapped_tooltip("%s\nDrop a texture here to use it.", name.c_str());
        return;
    }
    if(!geometry.header.Contains(mouse))
    {
        return;
    }
    std::string text = geometry.def ? geometry.def->label + "\n" + geometry.def->description : "Unknown node type.";
    text += "\nId: " + graph.nodes[size_t(node)].id;
    if(!is_node_used(node))
    {
        text += "\nNot used: nothing it feeds reaches the output node.";
    }
    if(is_compiled_current(node))
    {
        for(const auto* diagnostic : compiled_->get_node_diagnostics(graph.nodes[size_t(node)].id))
        {
            text += std::string("\n") + (diagnostic->severity == material_graph_severity::error ? "Error: " : "Warning: ") + diagnostic->message;
        }
    }
    show_wrapped_tooltip("%s", text.c_str());
}

// ---------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------

void material_graph_view::handle_input(material_graph& graph, material_graph_selection& selection, result& out)
{
    handle_zoom_and_pan();
    if(is_geometry_stale_)
    {
        return;
    }
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        handle_double_click(graph, selection, out);
        return;
    }
    handle_press(graph, selection, out);
    if(!is_geometry_stale_)
    {
        handle_drag(graph, out);
        handle_release(graph, selection, out);
    }
    if(!is_geometry_stale_)
    {
        handle_context_click(graph, selection);
    }
    if(!is_geometry_stale_)
    {
        handle_keys(graph, selection, out);
    }
}

void material_graph_view::handle_zoom_and_pan()
{
    auto& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if(ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        const math::vec2 anchor = to_graph(mouse);
        zoom_ = std::clamp(zoom_ * std::pow(CANVAS_ZOOM_STEP, io.MouseWheel), CANVAS_MIN_ZOOM, CANVAS_MAX_ZOOM);
        pan_ = {(mouse.x - origin_.x) / zoom_ - anchor.x, (mouse.y - origin_.y) / zoom_ - anchor.y};
    }
    const bool is_alt_pan = io.KeyAlt && interaction_ == interaction::none && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    const bool is_pan = ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right) || is_alt_pan);
    if(is_pan)
    {
        pan_.x += io.MouseDelta.x / zoom_;
        pan_.y += io.MouseDelta.y / zoom_;
    }
}

void material_graph_view::handle_press(material_graph& graph, material_graph_selection& selection, result& out)
{
    if(!ImGui::IsItemHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    press_position_ = mouse;
    has_dragged_ = false;
    pressed_node_ = -1;
    const pin_ref pin = hit_pin(mouse);
    if(pin.is_valid())
    {
        press_pin(graph, pin, out);
        return;
    }
    const pin_ref chip = hit_chip(mouse);
    if(chip.is_valid())
    {
        press_chip(graph, chip);
        return;
    }
    const int node = hit_node(mouse);
    if(node >= 0)
    {
        press_node(graph, node, selection);
        return;
    }
    comment_part part = comment_part::none;
    const int comment = hit_comment(graph, mouse, part);
    if(comment >= 0)
    {
        press_comment(graph, comment, part, selection);
        return;
    }
    const int link = hit_link(mouse);
    if(link >= 0)
    {
        selection.clear();
        selection.link = graph.nodes[size_t(links_[size_t(link)].to.node)].id + "." + links_[size_t(link)].entry;
        return;
    }
    press_empty(graph, selection, out);
}

void material_graph_view::press_pin(material_graph& graph, const pin_ref& pin, result& out)
{
    if(ImGui::GetIO().KeyAlt)
    {
        break_links(graph, pin);
        out.set_committed();
        return;
    }
    interaction_ = interaction::linking;
    link_source_ = pin;
    link_picked_up_ = false;
    link_checks_.clear();
    if(pin.side != pin_side::input)
    {
        return;
    }
    // A press on a linked input picks its link up: it hangs from its output until dropped somewhere.
    const auto link = std::find_if(links_.begin(),
                                   links_.end(),
                                   [&](const link_geometry& candidate)
                                   {
                                       return candidate.to == pin;
                                   });
    if(link == links_.end())
    {
        return;
    }
    link_source_ = link->from;
    find_material_graph_input(graph.nodes[size_t(pin.node)], link->entry)->link.clear();
    link_picked_up_ = true;
    out.begin_edit = true;
    out.changed = true;
}

void material_graph_view::press_chip(material_graph& graph, const pin_ref& chip)
{
    const auto& def = geometry_[size_t(chip.node)].def->inputs[size_t(chip.pin)];
    value_node_ = graph.nodes[size_t(chip.node)].id;
    value_pin_ = def.name;
    value_component_ = chip.component;
    const bool is_scalar = chip.component >= 0 || (!def.is_color && (def.type == material_value_type::dynamic || def.type == material_value_type::float1));
    if(!is_scalar)
    {
        ImGui::OpenPopup(VALUE_POPUP);
        return;
    }
    interaction_ = interaction::scrubbing;
    scrubbed_ = chip;
    scrub_start_value_ = get_input_value(graph.nodes[size_t(chip.node)], def)[std::max(chip.component, 0)];
    has_scrub_edit_ = false;
}

void material_graph_view::press_node(material_graph& graph, int node, material_graph_selection& selection)
{
    const std::string& id = graph.nodes[size_t(node)].id;
    const auto& io = ImGui::GetIO();
    selection.comment = -1;
    selection.link.clear();
    if(io.KeyCtrl && selection.has_node(id))
    {
        selection.nodes.erase(std::remove(selection.nodes.begin(), selection.nodes.end(), id), selection.nodes.end());
        return;
    }
    if(io.KeyCtrl || io.KeyShift)
    {
        if(!selection.has_node(id))
        {
            selection.nodes.push_back(id);
        }
    }
    else if(!selection.has_node(id))
    {
        selection.nodes = {id};
    }
    pressed_node_ = node;
    interaction_ = interaction::moving_nodes;
    moving_nodes_.clear();
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        if(selection.has_node(graph.nodes[i].id))
        {
            moving_nodes_.emplace_back(int(i), graph.nodes[i].position);
        }
    }
}

void material_graph_view::press_comment(material_graph& graph, int comment, comment_part part, material_graph_selection& selection)
{
    selection.clear();
    selection.comment = comment;
    moving_comment_ = comment;
    const auto& target = graph.comments[size_t(comment)];
    comment_start_position_ = target.position;
    comment_start_size_ = get_comment_size(target);
    if(part == comment_part::corner)
    {
        interaction_ = interaction::resizing_comment;
        return;
    }
    // Moving a comment moves the nodes it frames.
    interaction_ = interaction::moving_comment;
    moving_nodes_.clear();
    const ImRect frame(ImVec2(target.position.x, target.position.y),
                       ImVec2(target.position.x + comment_start_size_.x, target.position.y + comment_start_size_.y));
    for(size_t i = 0; i < graph.nodes.size(); ++i)
    {
        const math::vec2 size = measure_node(graph.nodes[i]);
        const auto& position = graph.nodes[i].position;
        if(frame.Contains(ImRect(ImVec2(position.x, position.y), ImVec2(position.x + size.x, position.y + size.y))))
        {
            moving_nodes_.emplace_back(int(i), position);
        }
    }
}

void material_graph_view::press_empty(material_graph& graph, material_graph_selection& selection, result& out)
{
    const auto& io = ImGui::GetIO();
    if(io.KeyAlt)
    {
        return;
    }
    if(place_shortcut_node(graph, selection, out))
    {
        return;
    }
    if(!io.KeyCtrl && !io.KeyShift)
    {
        selection.clear();
    }
    interaction_ = interaction::box_selecting;
}

auto material_graph_view::place_shortcut_node(material_graph& graph, material_graph_selection& selection, result& out) -> bool
{
    if(ImGui::GetIO().KeyCtrl)
    {
        return false;
    }
    const auto domain = get_material_graph_domain(graph);
    for(const auto& def : get_material_node_library())
    {
        if(def.shortcut != 0 && is_material_node_in_domain(def, domain) && ImGui::IsKeyDown(get_shortcut_key(def.shortcut)))
        {
            const int node = create_node(graph, def.type, to_graph(ImGui::GetIO().MousePos));
            selection.clear();
            selection.nodes = {graph.nodes[size_t(node)].id};
            out.set_committed();
            return true;
        }
    }
    return false;
}

void material_graph_view::handle_drag(material_graph& graph, result& out)
{
    if(interaction_ == interaction::none || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        return;
    }
    auto& io = ImGui::GetIO();
    const ImVec2 delta = io.MousePos - press_position_;
    has_dragged_ = has_dragged_ || measure_screen_length(delta) > DRAG_THRESHOLD;
    if(!has_dragged_)
    {
        return;
    }
    const math::vec2 graph_delta(delta.x / zoom_, delta.y / zoom_);
    switch(interaction_)
    {
        case interaction::moving_nodes:
        case interaction::moving_comment:
            out.begin_edit = true;
            out.layout_changed = true;
            for(const auto& [node, start] : moving_nodes_)
            {
                if(size_t(node) < graph.nodes.size())
                {
                    math::vec2 position = start + graph_delta;
                    position = io.KeyCtrl ? math::round(position / CANVAS_SNAP_STEP) * CANVAS_SNAP_STEP : position;
                    graph.nodes[size_t(node)].position = position;
                }
            }
            if(interaction_ == interaction::moving_comment && size_t(moving_comment_) < graph.comments.size())
            {
                graph.comments[size_t(moving_comment_)].position = comment_start_position_ + graph_delta;
            }
            break;
        case interaction::resizing_comment:
            if(size_t(moving_comment_) < graph.comments.size())
            {
                out.begin_edit = true;
                out.layout_changed = true;
                graph.comments[size_t(moving_comment_)].size =
                    math::max(comment_start_size_ + graph_delta, math::vec2(COMMENT_MIN_WIDTH, COMMENT_MIN_HEIGHT));
            }
            break;
        case interaction::scrubbing:
            scrub_value(graph, out);
            break;
        case interaction::none:
        case interaction::box_selecting:
        case interaction::linking:
            break;
    }
}

void material_graph_view::scrub_value(material_graph& graph, result& out)
{
    if(size_t(scrubbed_.node) >= graph.nodes.size() || !geometry_[size_t(scrubbed_.node)].def)
    {
        return;
    }
    auto& io = ImGui::GetIO();
    const float speed = SCRUB_SPEED * (io.KeyShift ? SCRUB_FAST : 1.0f) * (io.KeyCtrl ? SCRUB_FINE : 1.0f);
    const auto& def = geometry_[size_t(scrubbed_.node)].def->inputs[size_t(scrubbed_.pin)];
    auto& input = get_or_add_material_graph_input(graph.nodes[size_t(scrubbed_.node)], def.name, def.default_value);
    const int component = std::max(scrubbed_.component, 0);
    const float value = scrub_start_value_ + (io.MousePos.x - press_position_.x) * speed;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if(value == input.value[component])
    {
        return;
    }
    out.begin_edit = true;
    out.changed = true;
    has_scrub_edit_ = true;
    input.value[component] = value;
}

void material_graph_view::handle_release(material_graph& graph, material_graph_selection& selection, result& out)
{
    if(interaction_ == interaction::none || ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        return;
    }
    const auto& io = ImGui::GetIO();
    switch(interaction_)
    {
        case interaction::linking:
            finish_link(graph, out);
            break;
        case interaction::box_selecting:
            if(has_dragged_)
            {
                select_nodes_in_box(graph, selection);
            }
            break;
        case interaction::moving_nodes:
            if(has_dragged_)
            {
                out.finished = true;
                out.layout_changed = true;
            }
            else if(!io.KeyCtrl && !io.KeyShift && size_t(pressed_node_) < graph.nodes.size())
            {
                selection.nodes = {graph.nodes[size_t(pressed_node_)].id};
            }
            break;
        case interaction::moving_comment:
        case interaction::resizing_comment:
            if(has_dragged_)
            {
                out.finished = true;
                out.layout_changed = true;
            }
            break;
        case interaction::scrubbing:
            if(has_scrub_edit_)
            {
                out.changed = true;
                out.finished = true;
            }
            else
            {
                ImGui::OpenPopup(VALUE_POPUP);
            }
            break;
        case interaction::none:
            break;
    }
    interaction_ = interaction::none;
    moving_nodes_.clear();
    moving_comment_ = -1;
    has_scrub_edit_ = false;
}

void material_graph_view::finish_link(material_graph& graph, result& out)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const pin_ref target = find_link_target(graph, mouse);
    if(target.is_valid() && link_error_.empty() && connect(graph, link_source_, target))
    {
        out.set_committed();
    }
    else if(link_picked_up_)
    {
        // A picked-up link dropped anywhere else is removed.
        out.changed = true;
        out.finished = true;
    }
    else if(!target.is_valid() && hit_node(mouse) < 0 && has_dragged_)
    {
        search_link_ = link_source_;
        search_link_node_ = graph.nodes[size_t(link_source_.node)].id;
        open_search(to_graph(mouse));
    }
    link_picked_up_ = false;
    link_checks_.clear();
    link_error_.clear();
}

void material_graph_view::handle_double_click(material_graph& graph, material_graph_selection& selection, result& out)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const pin_ref pin = hit_pin(mouse);
    if(pin.is_valid())
    {
        // A double-click on a vector pin splits it into components, or joins them again.
        const auto& def = *geometry_[size_t(pin.node)].def;
        const auto& pin_def = pin.side == pin_side::input ? def.inputs[size_t(pin.pin)] : def.outputs[size_t(pin.pin)];
        if(can_split_material_pin(pin_def))
        {
            toggle_split(graph, pin);
            out.set_committed();
        }
        return;
    }
    if(hit_node(mouse) >= 0)
    {
        return;
    }
    const int link = hit_link(mouse);
    if(link >= 0)
    {
        insert_reroute(graph, links_[size_t(link)], to_graph(mouse), selection);
        out.set_committed();
        return;
    }
    search_link_ = {};
    open_search(to_graph(mouse));
}

void material_graph_view::handle_context_click(material_graph& graph, material_graph_selection& selection)
{
    if(!ImGui::IsItemHovered() || !ImGui::IsMouseReleased(ImGuiMouseButton_Right) || ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right))
    {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    interaction_ = interaction::none;
    context_position_ = to_graph(mouse);
    const pin_ref pin = hit_pin(mouse);
    if(pin.is_valid())
    {
        context_pin_ = pin;
        context_node_id_ = graph.nodes[size_t(pin.node)].id;
        ImGui::OpenPopup(PIN_MENU);
        return;
    }
    const int node = hit_node(mouse);
    if(node >= 0)
    {
        context_node_id_ = graph.nodes[size_t(node)].id;
        if(!selection.has_node(context_node_id_))
        {
            selection.clear();
            selection.nodes = {context_node_id_};
        }
        ImGui::OpenPopup(NODE_MENU);
        return;
    }
    const int link = hit_link(mouse);
    if(link >= 0)
    {
        context_link_ = link;
        context_node_id_ = graph.nodes[size_t(links_[size_t(link)].to.node)].id;
        selection.clear();
        selection.link = context_node_id_ + "." + links_[size_t(link)].entry;
        ImGui::OpenPopup(LINK_MENU);
        return;
    }
    search_link_ = {};
    open_search(context_position_);
}

void material_graph_view::handle_keys(material_graph& graph, material_graph_selection& selection, result& out)
{
    // The canvas's keys work while it has focus (the last click was on it), not while a field or menu has it.
    const bool canvas_focused = ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput &&
                                !ImGui::IsPopupOpen(ImGuiID(0), ImGuiPopupFlags_AnyPopup);
    if(!canvas_focused)
    {
        return;
    }
    const auto& io = ImGui::GetIO();
    if(ImGui::IsKeyPressed(shortcuts::item_cancel))
    {
        interaction_ == interaction::linking ? cancel_interaction() : selection.clear();
        return;
    }
    if(interaction_ != interaction::none)
    {
        return;
    }
    if(ImGui::IsKeyPressed(shortcuts::delete_item))
    {
        delete_selection(graph, selection, out);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::duplicate_item))
    {
        duplicate_selection(graph, selection, out);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::copy))
    {
        copy_selection(graph, selection);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::cut))
    {
        copy_selection(graph, selection);
        delete_selection(graph, selection, out);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::paste))
    {
        paste(graph, selection, out);
    }
    else if(ImGui::IsCombinationKeyPressed(shortcuts::select_all))
    {
        selection.clear();
        for(const auto& node : graph.nodes)
        {
            selection.nodes.push_back(node.id);
        }
    }
    else if(!io.KeyCtrl && ImGui::IsKeyPressed(shortcuts::focus_selected))
    {
        frame_selection(graph, selection);
    }
    else if(!io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Tab)))
    {
        search_link_ = {};
        open_search(ImGui::IsItemHovered() ? to_graph(io.MousePos) : to_graph(origin_ + size_ * 0.5f));
    }
    else if(!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        comment_selection(graph, selection, out);
    }
}

void material_graph_view::handle_drop(material_graph& graph, material_graph_selection& selection, result& out)
{
    if(!ImGui::BeginDragDropTarget())
    {
        return;
    }
    const math::vec2 position = to_graph(ImGui::GetIO().MousePos);
    if(const auto* payload = ImGui::AcceptDragDropPayload(MATERIAL_GRAPH_NODE_PAYLOAD))
    {
        const std::string type(reinterpret_cast<const char*>(payload->Data), size_t(payload->DataSize));
        if(find_material_node_def(type))
        {
            const int node = create_node(graph, type, position);
            selection.clear();
            selection.nodes = {graph.nodes[size_t(node)].id};
            out.set_committed();
        }
    }
    // A texture from the Content Browser goes to the texture node it is dropped on, or into a new Texture Sample.
    for(const auto& format : ex::get_suported_formats<gfx::texture>())
    {
        const auto* payload = ImGui::AcceptDragDropPayload(format.c_str());
        if(!payload)
        {
            continue;
        }
        const std::string absolute_path(reinterpret_cast<const char*>(payload->Data), size_t(payload->DataSize));
        out.dropped_texture_key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
        out.drop_position = position;
        const int node = hit_node(ImGui::GetIO().MousePos);
        if(node >= 0 && holds_material_graph_texture(graph.nodes[size_t(node)]))
        {
            out.drop_node = graph.nodes[size_t(node)].id;
        }
        break;
    }
    ImGui::EndDragDropTarget();
}

// ---------------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------------

void material_graph_view::open_search(const math::vec2& position)
{
    search_position_ = position;
    search_text_.fill('\0');
    search_highlight_ = 0;
    search_focus_ = true;
    ImGui::OpenPopup(SEARCH_POPUP);
}

auto material_graph_view::get_search_candidates() const -> std::vector<const material_node_def*>
{
    const std::string filter = make_lowercase(search_text_.data());
    const bool from_output = search_link_.is_valid() && search_link_.side == pin_side::output;
    const bool from_input = search_link_.is_valid() && search_link_.side == pin_side::input;
    std::vector<std::pair<int, const material_node_def*>> scored;
    const auto domain = compiled_ != nullptr ? compiled_->domain : material_domain::surface;
    for(const auto& def : get_material_node_library())
    {
        if(def.code == material_node_code::surface_output || !is_material_node_in_domain(def, domain))
        {
            continue;
        }
        const bool takes_link = std::any_of(def.inputs.begin(),
                                            def.inputs.end(),
                                            [](const material_pin_def& pin)
                                            {
                                                return !pin.is_value_only;
                                            });
        if((from_output && !takes_link) || (from_input && def.outputs.empty()))
        {
            continue;
        }
        const std::string label = make_lowercase(def.label);
        int score = 0;
        if(!filter.empty())
        {
            const std::string words = def.type + " " + make_lowercase(def.keywords) + " " + make_lowercase(get_material_node_category_name(def.category));
            score = label.rfind(filter, 0) == 0 ? 0 : (label.find(filter) != std::string::npos ? 1 : (words.find(filter) != std::string::npos ? 2 : -1));
        }
        if(score >= 0)
        {
            scored.emplace_back(score, &def);
        }
    }
    std::stable_sort(scored.begin(),
                     scored.end(),
                     [](const auto& a, const auto& b)
                     {
                         return a.first < b.first;
                     });
    std::vector<const material_node_def*> candidates;
    for(const auto& [score, def] : scored)
    {
        candidates.push_back(def);
    }
    return candidates;
}

void material_graph_view::draw_search_popup(material_graph& graph, material_graph_selection& selection, result& out)
{
    ImGui::SetNextWindowSize(ImVec2(SEARCH_WIDTH, 0.0f));
    if(!ImGui::BeginPopup(SEARCH_POPUP))
    {
        return;
    }
    if(search_focus_)
    {
        ImGui::SetKeyboardFocusHere();
        search_focus_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::InputTextWithHint("##search", ICON_MDI_MAGNIFY " Search nodes...", search_text_.data(), search_text_.size()))
    {
        search_highlight_ = 0;
    }
    const auto candidates = get_search_candidates();
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        search_highlight_ = std::min(search_highlight_ + 1, int(candidates.size()) - 1);
    }
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        search_highlight_ = std::max(search_highlight_ - 1, 0);
    }
    const material_node_def* chosen = nullptr;
    if((ImGui::IsKeyPressed(shortcuts::item_action) || ImGui::IsKeyPressed(shortcuts::item_action_alt)) && !candidates.empty())
    {
        chosen = candidates[size_t(std::clamp(search_highlight_, 0, int(candidates.size()) - 1))];
    }
    ImGui::BeginChild("##search_list", ImVec2(0.0f, SEARCH_LIST_HEIGHT));
    const bool is_grouped = search_text_[0] == '\0';
    int previous_category = -1;
    for(size_t i = 0; i < candidates.size(); ++i)
    {
        const auto& def = *candidates[i];
        if(is_grouped && int(def.category) != previous_category)
        {
            previous_category = int(def.category);
            ImGui::SeparatorText(get_material_node_category_name(def.category));
        }
        const bool highlighted = int(i) == search_highlight_;
        const float line_right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if(highlighted && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
        {
            ImGui::SetScrollHereY();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(get_material_category_color(def.category) | IM_COL32_A_MASK));
        ImGui::TextUnformatted(get_material_category_icon(def.category));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if(ImGui::Selectable((def.label + "##" + def.type).c_str(), highlighted))
        {
            chosen = &def;
        }
        set_wrapped_item_tooltip("%s", def.description.c_str());
        if(def.shortcut != 0)
        {
            const char key[] = {def.shortcut, '\0'};
            ImGui::SameLine(line_right - ImGui::CalcTextSize(key).x);
            ImGui::TextDisabled("%s", key);
        }
    }
    if(candidates.empty())
    {
        ImGui::TextDisabled("No node matches.");
    }
    ImGui::EndChild();
    if(chosen)
    {
        const int node = create_node(graph, chosen->type, search_position_);
        connect_pending(graph, node);
        selection.clear();
        selection.nodes = {graph.nodes[size_t(node)].id};
        out.set_committed();
        search_link_ = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void material_graph_view::draw_value_popup(material_graph& graph, result& out)
{
    ImGui::SetNextWindowSize(ImVec2(VALUE_POPUP_WIDTH, 0.0f));
    if(!ImGui::BeginPopup(VALUE_POPUP))
    {
        return;
    }
    const int index = find_material_graph_node(graph, value_node_);
    const auto* def = index >= 0 ? find_material_node_def(graph.nodes[size_t(index)].type) : nullptr;
    const auto* pin = def ? find_material_pin_def(def->inputs, value_pin_) : nullptr;
    if(!pin)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& node = graph.nodes[size_t(index)];
    const std::string component = value_component_ >= 0 ? " " + get_component_label(*pin, value_component_) : std::string{};
    ImGui::TextDisabled("%s - %s%s", get_node_title(node, *def).c_str(), pin->label.c_str(), component.c_str());
    math::vec4 value = get_input_value(node, *pin);
    const uint32_t width = pin->type == material_value_type::dynamic ? 1 : get_material_value_width(pin->type);
    const bool is_component = value_component_ >= 0;
    const bool is_single = is_component || (width == 1 && !pin->is_color);
    // The inspector's widget for the value, or for the one component the popup was opened on.
    math::vec4 shown = is_component ? math::vec4(value[value_component_]) : value;
    if(ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushItemWidth(-FLT_MIN);
    const auto edit = inspect_vector_value(engine::context(), shown, make_node_value_style(node, *pin, is_component ? 1 : width));
    ImGui::PopItemWidth();
    if(ImGui::IsItemActivated())
    {
        out.begin_edit = true;
    }
    if(edit.changed)
    {
        if(is_component)
        {
            value[value_component_] = shown.x;
        }
        else
        {
            value = shown;
        }
        out.begin_edit = true;
        out.changed = true;
        get_or_add_material_graph_input(node, pin->name, pin->default_value).value = value;
    }
    if(edit.edit_finished)
    {
        out.finished = true;
    }
    if(is_single && ImGui::IsKeyPressed(shortcuts::item_action))
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void material_graph_view::draw_pin_menu(material_graph& graph, result& out)
{
    const int index = find_material_graph_node(graph, context_node_id_);
    if(index != context_pin_.node || size_t(index) >= geometry_.size() || !geometry_[size_t(index)].def)
    {
        return;
    }
    const auto& def = *geometry_[size_t(index)].def;
    const bool is_input = context_pin_.side == pin_side::input;
    const auto& pin_def = is_input ? def.inputs[size_t(context_pin_.pin)] : def.outputs[size_t(context_pin_.pin)];
    if(ImGui::MenuItem(ICON_MDI_LINK_VARIANT_OFF " Break Links", "Alt+Click"))
    {
        break_links(graph, context_pin_);
        out.set_committed();
    }
    if(can_split_material_pin(pin_def))
    {
        const bool split = is_split(graph, context_pin_);
        if(ImGui::MenuItem(split ? ICON_MDI_CALL_MERGE " Join Components" : ICON_MDI_CALL_SPLIT " Split into Components", "Double-click"))
        {
            toggle_split(graph, context_pin_);
            out.set_committed();
        }
    }
    // A dynamic output has no fixed components to show: a Split node reads them.
    if(!is_input && pin_def.type == material_value_type::dynamic && context_pin_.component < 0 &&
       ImGui::MenuItem(ICON_MDI_CALL_SPLIT " Add Split Node"))
    {
        const auto* slot = find_slot(context_pin_);
        const math::vec2 position = to_graph(slot ? slot->center : origin_) + math::vec2(ADDED_NODE_OFFSET, 0.0f);
        const int split = create_node(graph, SPLIT_TYPE, position);
        update_geometry(graph, {});
        connect(graph, context_pin_, {split, pin_side::input, 0, -1});
        out.set_committed();
    }
}

void material_graph_view::draw_context_menus(material_graph& graph, material_graph_selection& selection, result& out)
{
    if(ImGui::BeginPopup(NODE_MENU))
    {
        const int index = find_material_graph_node(graph, context_node_id_);
        if(index >= 0 && size_t(index) < geometry_.size())
        {
            const auto& node = graph.nodes[size_t(index)];
            const bool is_output = is_material_graph_output_type(node.type);
            if(ImGui::MenuItem(ICON_MDI_CONTENT_DUPLICATE " Duplicate", "Ctrl+D", false, !is_output))
            {
                duplicate_selection(graph, selection, out);
            }
            if(ImGui::MenuItem(ICON_MDI_CONTENT_COPY " Copy", "Ctrl+C"))
            {
                copy_selection(graph, selection);
            }
            if(ImGui::MenuItem(ICON_MDI_LINK_VARIANT_OFF " Break Links"))
            {
                const auto slots = geometry_[size_t(index)].slots;
                for(const auto& slot : slots)
                {
                    break_links(graph, slot.ref);
                }
                out.set_committed();
            }
            if(ImGui::MenuItem(ICON_MDI_COMMENT_TEXT_OUTLINE " Comment Selection", "C"))
            {
                comment_selection(graph, selection, out);
            }
            const auto* def = geometry_[size_t(index)].def;
            if(def && has_material_node_preview(*def))
            {
                const bool is_hidden = node.is_preview_hidden;
                if(ImGui::MenuItem(is_hidden ? ICON_MDI_EYE " Show Preview" : ICON_MDI_EYE_OFF " Hide Preview", nullptr, false, are_swatches_shown_))
                {
                    graph.nodes[size_t(index)].is_preview_hidden = !is_hidden;
                    is_geometry_stale_ = true;
                    out.set_committed();
                }
            }
            if(const char* conversion = get_conversion(node.type))
            {
                const auto* target = find_material_node_def(conversion);
                const bool to_parameter = target && target->code == material_node_code::parameter;
                if(ImGui::MenuItem(to_parameter ? ICON_MDI_TUNE " Convert to Parameter" : ICON_MDI_NUMERIC " Convert to Constant"))
                {
                    convert_node(graph, index, conversion, out);
                }
            }
            ImGui::Separator();
            if(ImGui::MenuItem(ICON_MDI_DELETE " Delete", "Del", false, !is_output || selection.nodes.size() > 1))
            {
                delete_selection(graph, selection, out);
            }
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup(LINK_MENU))
    {
        const bool is_valid = size_t(context_link_) < links_.size() &&
                              find_material_graph_node(graph, context_node_id_) == links_[size_t(context_link_)].to.node;
        if(ImGui::MenuItem(ICON_MDI_CIRCLE_SMALL " Insert Reroute", nullptr, false, is_valid))
        {
            insert_reroute(graph, links_[size_t(context_link_)], context_position_, selection);
            out.set_committed();
        }
        if(ImGui::MenuItem(ICON_MDI_DELETE " Delete Link", "Del", false, is_valid))
        {
            delete_selection(graph, selection, out);
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup(PIN_MENU))
    {
        draw_pin_menu(graph, out);
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------------

auto material_graph_view::create_node(material_graph& graph, const std::string& type, const math::vec2& position) -> int
{
    material_graph_node node;
    node.id = make_unique_material_graph_node_id(graph, type);
    node.type = type;
    // The header lands under the pointer.
    node.position = position - math::vec2(SIDE_PADDING, NODE_HEADER_HEIGHT * 0.5f);
    graph.nodes.push_back(std::move(node));
    is_geometry_stale_ = true;
    return int(graph.nodes.size() - 1);
}

auto material_graph_view::add_node_at_center(material_graph& graph, const std::string& type, material_graph_selection& selection) -> int
{
    if(!find_material_node_def(type))
    {
        return -1;
    }
    const int node = create_node(graph, type, to_graph(origin_ + size_ * 0.5f));
    selection.clear();
    selection.nodes = {graph.nodes[size_t(node)].id};
    return node;
}

auto material_graph_view::connect(material_graph& graph, const pin_ref& from, const pin_ref& to) -> bool
{
    const pin_ref& output = from.side == pin_side::output ? from : to;
    const pin_ref& input = from.side == pin_side::output ? to : from;
    const bool has_nodes = size_t(output.node) < geometry_.size() && size_t(input.node) < geometry_.size() &&
                           geometry_[size_t(output.node)].def && geometry_[size_t(input.node)].def;
    if(output.side != pin_side::output || input.side != pin_side::input || !has_nodes)
    {
        return false;
    }
    const std::string& output_id = graph.nodes[size_t(output.node)].id;
    const std::string output_name = get_output_name(graph, output);
    const std::string entry = get_entry_name(graph, input);
    if(!check_material_graph_link(graph, output_id, output_name, graph.nodes[size_t(input.node)].id, entry).empty())
    {
        return false;
    }
    const auto& input_pin = geometry_[size_t(input.node)].def->inputs[size_t(input.pin)];
    get_or_add_material_graph_input(graph.nodes[size_t(input.node)], entry, input_pin.default_value).link = output_id + "." + output_name;
    is_geometry_stale_ = true;
    return true;
}

void material_graph_view::connect_pending(material_graph& graph, int node)
{
    const int source = find_material_graph_node(graph, search_link_node_);
    if(!search_link_.is_valid() || source < 0)
    {
        return;
    }
    // The new node has no geometry yet: one is made for it, so its pins can be named.
    update_geometry(graph, {});
    const pin_ref from{source, search_link_.side, search_link_.pin, search_link_.component};
    const pin_side wanted = from.side == pin_side::output ? pin_side::input : pin_side::output;
    const auto slots = geometry_[size_t(node)].slots;
    for(const auto& slot : slots)
    {
        if(slot.ref.side == wanted && slot.has_pin && connect(graph, from, slot.ref))
        {
            return;
        }
    }
}

void material_graph_view::break_links(material_graph& graph, const pin_ref& pin)
{
    if(pin.side == pin_side::input)
    {
        if(auto* input = find_material_graph_input(graph.nodes[size_t(pin.node)], get_entry_name(graph, pin)))
        {
            input->link.clear();
        }
        is_geometry_stale_ = true;
        return;
    }
    // An output's links: all of them from its own pin, those reading that one component from a component's.
    for(const auto& link : links_)
    {
        const bool is_from_pin = link.from.node == pin.node && link.from.pin == pin.pin;
        if(!is_from_pin || (pin.component >= 0 && link.from.component != pin.component))
        {
            continue;
        }
        if(auto* input = find_material_graph_input(graph.nodes[size_t(link.to.node)], link.entry))
        {
            input->link.clear();
        }
    }
    is_geometry_stale_ = true;
}

auto material_graph_view::is_split(const material_graph& graph, const pin_ref& pin) const -> bool
{
    const auto& node = graph.nodes[size_t(pin.node)];
    const auto& def = *geometry_[size_t(pin.node)].def;
    return pin.side == pin_side::input ? is_material_graph_input_split(node, def.inputs[size_t(pin.pin)].name)
                                       : is_material_graph_output_split(node, def.outputs[size_t(pin.pin)].name);
}

void material_graph_view::toggle_split(material_graph& graph, const pin_ref& pin)
{
    const auto& def = *geometry_[size_t(pin.node)].def;
    const bool is_output = pin.side == pin_side::output;
    const std::string& name = is_output ? def.outputs[size_t(pin.pin)].name : def.inputs[size_t(pin.pin)].name;
    set_material_graph_pin_split(graph.nodes[size_t(pin.node)], name, is_output, !is_split(graph, pin));
    is_geometry_stale_ = true;
}

void material_graph_view::insert_reroute(material_graph& graph, const link_geometry& link, const math::vec2& position, material_graph_selection& selection)
{
    const std::string reader_id = graph.nodes[size_t(link.to.node)].id;
    const auto* entry = find_material_graph_input(graph.nodes[size_t(link.to.node)], link.entry);
    if(!entry)
    {
        return;
    }
    material_graph_node reroute;
    reroute.id = make_unique_material_graph_node_id(graph, REROUTE_TYPE);
    reroute.type = REROUTE_TYPE;
    reroute.position = position - math::vec2(REROUTE_WIDTH, REROUTE_HEIGHT) * 0.5f;
    reroute.inputs.push_back({"in", entry->link, {}});
    const std::string reroute_id = reroute.id;
    graph.nodes.push_back(std::move(reroute));
    find_material_graph_input(graph.nodes[size_t(find_material_graph_node(graph, reader_id))], link.entry)->link =
        make_material_graph_link(reroute_id, "out");
    selection.clear();
    selection.nodes = {reroute_id};
    is_geometry_stale_ = true;
}

void material_graph_view::delete_selection(material_graph& graph, material_graph_selection& selection, result& out)
{
    bool deleted = false;
    for(const auto& id : selection.nodes)
    {
        deleted = remove_material_graph_node(graph, id) || deleted;
    }
    if(selection.comment >= 0 && size_t(selection.comment) < graph.comments.size())
    {
        graph.comments.erase(graph.comments.begin() + selection.comment);
        deleted = true;
    }
    if(!selection.link.empty())
    {
        // "node.entry": the node's id, then the entry the link is written on.
        const auto separator = selection.link.find('.');
        const int node = find_material_graph_node(graph, selection.link.substr(0, separator));
        auto* input = node >= 0 && separator != std::string::npos
                          ? find_material_graph_input(graph.nodes[size_t(node)], selection.link.substr(separator + 1))
                          : nullptr;
        if(input && !input->link.empty())
        {
            input->link.clear();
            deleted = true;
        }
    }
    if(deleted)
    {
        selection.clear();
        out.set_committed();
        is_geometry_stale_ = true;
    }
}

void material_graph_view::duplicate_selection(material_graph& graph, material_graph_selection& selection, result& out)
{
    const auto fragment = copy_material_graph_nodes(graph, selection.nodes);
    if(fragment.nodes.empty())
    {
        return;
    }
    selection.clear();
    selection.nodes = paste_material_graph_nodes(graph, fragment, math::vec2(NODE_DUPLICATE_OFFSET));
    out.set_committed();
    is_geometry_stale_ = true;
}

void material_graph_view::copy_selection(const material_graph& graph, const material_graph_selection& selection) const
{
    const auto fragment = copy_material_graph_nodes(graph, selection.nodes);
    if(!fragment.nodes.empty())
    {
        ImGui::SetClipboardText(save_to_string(fragment).c_str());
    }
}

void material_graph_view::paste(material_graph& graph, material_graph_selection& selection, result& out)
{
    const char* text = ImGui::GetClipboardText();
    material_graph fragment;
    if(!text || !load_from_string(text, fragment) || fragment.nodes.empty())
    {
        return;
    }
    split_material_graph_component_pins(fragment);
    // Text written by hand or by a tool has no positions: it is laid out on its own first.
    if(is_material_graph_never_laid_out(fragment))
    {
        layout_material_graph(fragment, get_layout_metrics());
    }
    selection.clear();
    selection.nodes = paste_material_graph_nodes(graph, fragment, get_paste_offset(graph, fragment));
    split_material_graph_component_pins(graph);
    out.set_committed();
    is_geometry_stale_ = true;
    // A whole graph lands around the output it wired up: show all of it.
    if(find_material_graph_output(fragment) >= 0)
    {
        frame_request_ = true;
    }
}

auto material_graph_view::get_paste_offset(const material_graph& graph, const material_graph& fragment) const -> math::vec2
{
    // A fragment that brings a surface output joins the graph's own: its nodes keep their place relative to it.
    const int fragment_output = find_material_graph_output(fragment);
    const int graph_output = find_material_graph_output(graph);
    if(fragment_output >= 0 && graph_output >= 0)
    {
        return graph.nodes[size_t(graph_output)].position - fragment.nodes[size_t(fragment_output)].position;
    }
    // Any other fragment lands with its top-left corner at the pointer.
    math::vec2 min(FLT_MAX);
    for(const auto& node : fragment.nodes)
    {
        min = math::min(min, node.position);
    }
    const ImVec2 target = ImGui::IsItemHovered() ? ImGui::GetIO().MousePos : origin_ + size_ * 0.5f;
    return min.x == FLT_MAX ? math::vec2(0.0f) : to_graph(target) - min;
}

void material_graph_view::comment_selection(material_graph& graph, material_graph_selection& selection, result& out)
{
    math::vec2 min(FLT_MAX);
    math::vec2 max(-FLT_MAX);
    for(const auto& node : graph.nodes)
    {
        if(selection.has_node(node.id))
        {
            min = math::min(min, node.position);
            max = math::max(max, node.position + measure_node(node));
        }
    }
    if(min.x == FLT_MAX)
    {
        return;
    }
    material_graph_comment comment;
    comment.text = "Comment";
    comment.position = min - math::vec2(COMMENT_MARGIN, COMMENT_MARGIN + COMMENT_TITLE_HEIGHT);
    comment.size = max - min + math::vec2(COMMENT_MARGIN * 2.0f, COMMENT_MARGIN * 2.0f + COMMENT_TITLE_HEIGHT);
    graph.comments.push_back(comment);
    selection.clear();
    selection.comment = int(graph.comments.size() - 1);
    out.set_committed();
}

void material_graph_view::convert_node(material_graph& graph, int node, const char* type, result& out)
{
    auto& converted = graph.nodes[size_t(node)];
    converted.type = type;
    // A parameter's name means nothing to a constant: only settings the new type has stay.
    const auto* def = find_material_node_def(type);
    converted.settings.erase(std::remove_if(converted.settings.begin(),
                                            converted.settings.end(),
                                            [&](const material_graph_setting& setting)
                                            {
                                                return !def || !find_material_setting_def(*def, setting.name);
                                            }),
                             converted.settings.end());
    out.set_committed();
    is_geometry_stale_ = true;
}

void material_graph_view::select_nodes_in_box(const material_graph& graph, material_graph_selection& selection) const
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImRect box(ImVec2(std::min(press_position_.x, mouse.x), std::min(press_position_.y, mouse.y)),
                     ImVec2(std::max(press_position_.x, mouse.x), std::max(press_position_.y, mouse.y)));
    for(size_t i = 0; i < geometry_.size(); ++i)
    {
        if(box.Overlaps(geometry_[i].rect) && !selection.has_node(graph.nodes[i].id))
        {
            selection.nodes.push_back(graph.nodes[i].id);
        }
    }
}

} // namespace unravel
