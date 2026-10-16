#include "animation_graph_view.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/shortcuts.h>
#include <engine/assets/impl/asset_extensions.h>
#include <filesystem/filesystem.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace unravel
{

namespace
{
constexpr float STATE_NODE_WIDTH = 180.0f;
constexpr float STATE_NODE_HEIGHT = 46.0f;
constexpr float SPECIAL_NODE_WIDTH = 120.0f;
constexpr float SPECIAL_NODE_HEIGHT = 34.0f;
constexpr float NODE_ROUNDING = 6.0f;
constexpr float GRID_STEP = 32.0f;
constexpr float MIN_ZOOM = 0.3f;
constexpr float MAX_ZOOM = 2.0f;
/// Framing never zooms in further than this, so a layer of one or two states stays readable.
constexpr float MAX_FRAME_ZOOM = 1.25f;
constexpr float ZOOM_STEP = 1.1f;
/// Screen distance within which a click selects a transition arrow.
constexpr float ARROW_HIT_DISTANCE = 6.0f;
/// Screen offset that separates the two arrows of a two-way pair at zoom 1 and above; below it the
/// offset shrinks with the nodes, so the shifted line still runs through them.
constexpr float PAIR_OFFSET = 7.0f;
constexpr float ARROW_HEAD_SIZE = 7.0f;
/// Stacked heads drawn at most on an arrow carrying several transitions.
constexpr int MAX_ARROW_HEADS = 3;
/// Height and spread of a state's loop arrow to itself, in graph units.
constexpr float SELF_LOOP_LIFT = 26.0f;
constexpr float SELF_LOOP_SPREAD = 22.0f;
/// Hit radius of a self loop around its top, and that top's height above the node.
constexpr float SELF_LOOP_HIT_RADIUS = 14.0f;
constexpr float SELF_LOOP_HIT_LIFT = 18.0f;
constexpr float FRAME_MARGIN = 60.0f;
/// Grid the layout of a layer authored without graph positions snaps into.
constexpr float AUTO_LAYOUT_COLUMN = 230.0f;
constexpr float AUTO_LAYOUT_ROW = 110.0f;
constexpr int AUTO_LAYOUT_COLUMNS = 3;
/// Graph-space distance under which two states count as stacked on each other.
constexpr float OVERLAP_DISTANCE = 1.0f;
/// Position grid nodes snap to while dragging with Ctrl held.
constexpr float SNAP_STEP = 10.0f;
/// Mouse travel, in pixels, before a press on a node starts dragging it.
constexpr float NODE_DRAG_THRESHOLD = 2.0f;
/// Offset of a duplicated state from its original, in graph units.
constexpr float DUPLICATE_OFFSET = 30.0f;
/// Arrows are drawn at least as thick as at this zoom, so they stay visible zoomed out.
constexpr float MIN_LINE_ZOOM = 0.7f;
/// Radius of the dot that runs along an active transition, in pixels at zoom 1.
constexpr float ACTIVE_DOT_RADIUS = 5.0f;
/// Arrow thicknesses: the entry arrow, a transition, a selected or active transition.
constexpr float ENTRY_ARROW_THICKNESS = 1.5f;
constexpr float TRANSITION_THICKNESS = 1.6f;
constexpr float HIGHLIGHTED_TRANSITION_THICKNESS = 2.6f;
/// Offset of a node's drop shadow, in pixels.
constexpr float NODE_SHADOW_OFFSET_X = 3.0f;
constexpr float NODE_SHADOW_OFFSET_Y = 4.0f;
/// Width of the accent strip on a node's left edge, in graph units.
constexpr float NODE_ACCENT_WIDTH = 5.0f;
/// Progress bar: inset from the node's edges, height (in graph units, at least the minimum in
/// pixels) and the share of the inset left below it.
constexpr float PROGRESS_INSET = 6.0f;
constexpr float PROGRESS_HEIGHT = 3.0f;
constexpr float MIN_PROGRESS_HEIGHT = 2.0f;
constexpr float PROGRESS_BOTTOM_SHARE = 0.6f;
constexpr float PROGRESS_ROUNDING = 1.0f;
/// Node border thickness, plain and selected.
constexpr float NODE_BORDER_THICKNESS = 1.0f;
constexpr float SELECTED_NODE_BORDER_THICKNESS = 2.5f;
/// Below this font size (zoomed far out) node labels are not drawn.
constexpr float MIN_NODE_FONT_SIZE = 5.0f;
/// Label indent past the accent strip and margin before the right edge, in graph units.
constexpr float NODE_LABEL_INDENT = 12.0f;
constexpr float NODE_LABEL_MARGIN = 6.0f;
/// With a sublabel the label sits this share of the node height below its top.
constexpr float NODE_LABEL_TOP_SHARE = 0.18f;
/// Sublabel size relative to the label, and the gap between them in graph units.
constexpr float NODE_SUBLABEL_SCALE = 0.82f;
constexpr float NODE_SUBLABEL_GAP = 1.0f;

const ImU32 COL_CANVAS = IM_COL32(32, 33, 36, 255);
const ImU32 COL_GRID = IM_COL32(48, 50, 54, 255);
const ImU32 COL_GRID_MAJOR = IM_COL32(58, 60, 66, 255);
const ImU32 COL_NODE = IM_COL32(58, 61, 68, 255);
const ImU32 COL_NODE_HOVER = IM_COL32(68, 72, 80, 255);
const ImU32 COL_NODE_BORDER = IM_COL32(80, 84, 94, 255);
const ImU32 COL_NODE_SHADOW = IM_COL32(0, 0, 0, 90);
const ImU32 COL_NODE_TEXT = IM_COL32(230, 232, 236, 255);
const ImU32 COL_NODE_SUBTEXT = IM_COL32(160, 166, 176, 255);
const ImU32 COL_PROGRESS_TRACK = IM_COL32(20, 22, 26, 255);
const ImU32 COL_STATE_ACCENT = IM_COL32(110, 120, 140, 255);
const ImU32 COL_DEFAULT_ACCENT = IM_COL32(222, 132, 50, 255);
const ImU32 COL_ENTRY_ACCENT = IM_COL32(58, 160, 92, 255);
const ImU32 COL_ANY_ACCENT = IM_COL32(50, 160, 170, 255);
const ImU32 COL_SELECTED = IM_COL32(80, 170, 255, 255);
const ImU32 COL_ARROW = IM_COL32(190, 194, 202, 255);
const ImU32 COL_ARROW_ENTRY = IM_COL32(222, 132, 50, 255);
const ImU32 COL_ACTIVE = IM_COL32(90, 200, 255, 255);
const ImU32 COL_PROGRESS = IM_COL32(70, 160, 255, 255);
const ImU32 COL_PROGRESS_NEXT = IM_COL32(120, 220, 140, 255);
const ImU32 COL_LINK = IM_COL32(255, 220, 120, 255);

auto length_of(const ImVec2& v) -> float
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

auto get_pair_offset(float zoom) -> float
{
    return PAIR_OFFSET * std::min(zoom, 1.0f);
}

auto distance_to_segment(const ImVec2& p, const ImVec2& a, const ImVec2& b) -> float
{
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    if(len2 <= 1e-6f)
    {
        return length_of(ImVec2(p.x - a.x, p.y - a.y));
    }
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f);
    return length_of(ImVec2(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t)));
}

/// Where the line through `from` and `to` enters and leaves the rectangle, as parameters along it
/// (0 at `from`, 1 at `to`), or nothing when it misses the rectangle.
auto clip_line_to_rect(const ImVec2& from, const ImVec2& to, const ImVec2& rect_min, const ImVec2& rect_max)
    -> std::optional<std::pair<float, float>>
{
    const float start[2] = {from.x, from.y};
    const float delta[2] = {to.x - from.x, to.y - from.y};
    const float low[2] = {rect_min.x, rect_min.y};
    const float high[2] = {rect_max.x, rect_max.y};
    float t_near = -FLT_MAX;
    float t_far = FLT_MAX;
    for(int axis = 0; axis < 2; ++axis)
    {
        if(std::fabs(delta[axis]) < 1e-6f)
        {
            if(start[axis] < low[axis] || start[axis] > high[axis])
            {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (low[axis] - start[axis]) / delta[axis];
        float t1 = (high[axis] - start[axis]) / delta[axis];
        if(t0 > t1)
        {
            std::swap(t0, t1);
        }
        t_near = std::max(t_near, t0);
        t_far = std::min(t_far, t1);
    }
    if(t_near > t_far)
    {
        return std::nullopt;
    }
    return std::make_pair(t_near, t_far);
}

/// The visible part of a link between two nodes.
struct link_segment
{
    ImVec2 from;
    ImVec2 to;
    bool valid{};
};

/**
 * @brief The line between two node rectangles' centres, shifted sideways by `offset` (two-way
 * pairs sit side by side), from where it leaves the first rectangle to where it enters the second:
 * the arrow starts and ends exactly on the borders at any angle.
 */
auto make_link_segment(const ImVec2& source_min,
                       const ImVec2& source_max,
                       const ImVec2& destination_min,
                       const ImVec2& destination_max,
                       float offset) -> link_segment
{
    const ImVec2 source_center((source_min.x + source_max.x) * 0.5f, (source_min.y + source_max.y) * 0.5f);
    const ImVec2 destination_center((destination_min.x + destination_max.x) * 0.5f, (destination_min.y + destination_max.y) * 0.5f);
    const ImVec2 delta(destination_center.x - source_center.x, destination_center.y - source_center.y);
    const float length = length_of(delta);
    if(length < 1.0f)
    {
        return {};
    }
    const ImVec2 normal(-delta.y / length, delta.x / length);
    const ImVec2 a(source_center.x + normal.x * offset, source_center.y + normal.y * offset);
    const ImVec2 b(destination_center.x + normal.x * offset, destination_center.y + normal.y * offset);
    const auto leaves = clip_line_to_rect(a, b, source_min, source_max);
    const auto enters = clip_line_to_rect(a, b, destination_min, destination_max);
    const float t_from = leaves ? std::max(leaves->second, 0.0f) : 0.0f;
    const float t_to = enters ? std::min(enters->first, 1.0f) : 1.0f;
    if(t_to <= t_from)
    {
        // The nodes overlap: nothing of the link shows.
        return {};
    }
    return {ImVec2(a.x + (b.x - a.x) * t_from, a.y + (b.y - a.y) * t_from),
            ImVec2(a.x + (b.x - a.x) * t_to, a.y + (b.y - a.y) * t_to),
            true};
}

auto get_motion_label(const animation_state_def& state) -> std::string
{
    switch(state.motion)
    {
        case animation_motion_type::clip:
            return state.clip ? get_clip_label(state.clip) : std::string("No clip");
        case animation_motion_type::blend_1d:
            return "Blend 1D (" + std::to_string(state.points.size()) + ")";
        case animation_motion_type::blend_2d_cartesian:
            return "Blend 2D (" + std::to_string(state.points.size()) + ")";
        case animation_motion_type::blend_2d_directional:
            return "Blend 2D Directional (" + std::to_string(state.points.size()) + ")";
    }
    return {};
}

/// Every state on one position: a layer authored without graph positions (in a file or by a tool).
auto is_never_laid_out(const animation_layer_def& layer) -> bool
{
    if(layer.states.size() < 2)
    {
        return false;
    }
    const math::vec2 first = layer.states.front().editor_position;
    return std::all_of(layer.states.begin(),
                       layer.states.end(),
                       [&](const animation_state_def& state)
                       {
                           const math::vec2 d = state.editor_position - first;
                           return std::fabs(d.x) < OVERLAP_DISTANCE && std::fabs(d.y) < OVERLAP_DISTANCE;
                       });
}

void apply_auto_layout(animation_layer_def& layer)
{
    for(size_t i = 0; i < layer.states.size(); ++i)
    {
        const float column = float(i % AUTO_LAYOUT_COLUMNS);
        const float row = float(i / AUTO_LAYOUT_COLUMNS);
        layer.states[i].editor_position = {column * AUTO_LAYOUT_COLUMN, row * AUTO_LAYOUT_ROW};
    }
    layer.entry_position = {-AUTO_LAYOUT_COLUMN, 0.0f};
    layer.any_state_position = {-AUTO_LAYOUT_COLUMN, AUTO_LAYOUT_ROW};
}

/// The transition's source as a state index: -2 for Any State, -1 for a state that does not exist.
auto get_source_index(const animation_layer_def& layer, const animation_transition_def& transition) -> int
{
    return transition.source.empty() ? -2 : find_animation_state(layer, transition.source);
}
} // namespace

void animation_graph_view::request_frame_all()
{
    frame_request_ = true;
}

void animation_graph_view::cancel_interaction()
{
    linking_ = false;
    dragging_ = {node_kind::state, -1};
    dragged_ = false;
}

void animation_graph_view::remove_state(animation_layer_def& layer, size_t index)
{
    // A link or a drag holds a state index the removal shifts onto another state.
    cancel_interaction();
    remove_animation_state(layer, index);
}

auto animation_graph_view::to_screen(const math::vec2& graph) const -> ImVec2
{
    return ImVec2(origin_.x + (graph.x + pan_.x) * zoom_, origin_.y + (graph.y + pan_.y) * zoom_);
}

auto animation_graph_view::to_graph(const ImVec2& screen) const -> math::vec2
{
    return {(screen.x - origin_.x) / zoom_ - pan_.x, (screen.y - origin_.y) / zoom_ - pan_.y};
}

auto animation_graph_view::get_node_position(const animation_layer_def& layer, const node_ref& node) const -> math::vec2
{
    switch(node.kind)
    {
        case node_kind::entry:
            return layer.entry_position;
        case node_kind::any_state:
            return layer.any_state_position;
        case node_kind::state:
            break;
    }
    if(node.index < 0 || size_t(node.index) >= layer.states.size())
    {
        return {};
    }
    return layer.states[size_t(node.index)].editor_position;
}

void animation_graph_view::set_node_position(animation_layer_def& layer,
                                             const node_ref& node,
                                             const math::vec2& position) const
{
    switch(node.kind)
    {
        case node_kind::entry:
            layer.entry_position = position;
            return;
        case node_kind::any_state:
            layer.any_state_position = position;
            return;
        case node_kind::state:
            break;
    }
    if(node.index >= 0 && size_t(node.index) < layer.states.size())
    {
        layer.states[size_t(node.index)].editor_position = position;
    }
}

auto animation_graph_view::is_valid_node(const animation_layer_def& layer, const node_ref& node) const -> bool
{
    return node.kind != node_kind::state || (node.index >= 0 && size_t(node.index) < layer.states.size());
}

auto animation_graph_view::get_node_rect(const animation_layer_def& layer, const node_ref& node) const -> node_rect
{
    const bool special = node.kind != node_kind::state;
    const float width = (special ? SPECIAL_NODE_WIDTH : STATE_NODE_WIDTH) * zoom_;
    const float height = (special ? SPECIAL_NODE_HEIGHT : STATE_NODE_HEIGHT) * zoom_;
    const ImVec2 center = to_screen(get_node_position(layer, node));
    return {ImVec2(center.x - width * 0.5f, center.y - height * 0.5f), ImVec2(center.x + width * 0.5f, center.y + height * 0.5f)};
}

auto animation_graph_view::get_top_state(const animation_graph_selection& selection) const -> int
{
    return selection.what == animation_graph_selection::kind::state ? selection.index : -1;
}

auto animation_graph_view::hit_node(const animation_layer_def& layer, const ImVec2& mouse) const -> node_ref
{
    const auto inside = [&](const node_rect& rect)
    {
        return mouse.x >= rect.min.x && mouse.x <= rect.max.x && mouse.y >= rect.min.y && mouse.y <= rect.max.y;
    };
    // Topmost first, the reverse of the drawing order: Any State, Entry, the top state, the others.
    if(inside(get_node_rect(layer, {node_kind::any_state, -1})))
    {
        return {node_kind::any_state, -1};
    }
    if(inside(get_node_rect(layer, {node_kind::entry, -1})))
    {
        return {node_kind::entry, -1};
    }
    if(top_state_ >= 0 && size_t(top_state_) < layer.states.size() && inside(get_node_rect(layer, {node_kind::state, top_state_})))
    {
        return {node_kind::state, top_state_};
    }
    for(size_t i = layer.states.size(); i-- > 0;)
    {
        if(inside(get_node_rect(layer, {node_kind::state, int(i)})))
        {
            return {node_kind::state, int(i)};
        }
    }
    return {node_kind::state, -1};
}

auto animation_graph_view::hit_transition(const animation_layer_def& layer, const ImVec2& mouse) const -> int
{
    for(size_t i = 0; i < layer.transitions.size(); ++i)
    {
        const auto& transition = layer.transitions[i];
        const int destination = find_animation_state(layer, transition.destination);
        const int source = get_source_index(layer, transition);
        if(destination < 0 || source == -1)
        {
            continue;
        }
        const node_ref source_node = source == -2 ? node_ref{node_kind::any_state, -1} : node_ref{node_kind::state, source};
        const auto source_rect = get_node_rect(layer, source_node);
        const auto destination_rect = get_node_rect(layer, {node_kind::state, destination});
        if(source == destination)
        {
            const ImVec2 top(source_rect.center().x, source_rect.min.y - SELF_LOOP_HIT_LIFT * zoom_);
            if(length_of(ImVec2(mouse.x - top.x, mouse.y - top.y)) < SELF_LOOP_HIT_RADIUS * zoom_)
            {
                return int(i);
            }
            continue;
        }
        const auto segment = make_link_segment(source_rect.min,
                                               source_rect.max,
                                               destination_rect.min,
                                               destination_rect.max,
                                               get_pair_offset(zoom_));
        // The heads are wider than the line: a click on one picks its arrow too.
        const float hit_distance = std::max(ARROW_HIT_DISTANCE, ARROW_HEAD_SIZE * std::max(zoom_, 0.6f));
        if(segment.valid && distance_to_segment(mouse, segment.from, segment.to) <= hit_distance)
        {
            return int(i);
        }
    }
    return -1;
}

void animation_graph_view::frame_all(const animation_layer_def& layer)
{
    math::vec2 lo = layer.entry_position;
    math::vec2 hi = layer.entry_position;
    const auto include = [&](const math::vec2& p, float half_w, float half_h)
    {
        lo = math::min(lo, p - math::vec2(half_w, half_h));
        hi = math::max(hi, p + math::vec2(half_w, half_h));
    };
    include(layer.entry_position, SPECIAL_NODE_WIDTH * 0.5f, SPECIAL_NODE_HEIGHT * 0.5f);
    include(layer.any_state_position, SPECIAL_NODE_WIDTH * 0.5f, SPECIAL_NODE_HEIGHT * 0.5f);
    for(const auto& state : layer.states)
    {
        include(state.editor_position, STATE_NODE_WIDTH * 0.5f, STATE_NODE_HEIGHT * 0.5f);
    }
    const math::vec2 extent = hi - lo + math::vec2(FRAME_MARGIN * 2.0f);
    if(size_.x <= 1.0f || size_.y <= 1.0f)
    {
        return;
    }
    zoom_ = std::clamp(std::min(size_.x / extent.x, size_.y / extent.y), MIN_ZOOM, MAX_FRAME_ZOOM);
    const math::vec2 center = (lo + hi) * 0.5f;
    pan_ = {size_.x * 0.5f / zoom_ - center.x, size_.y * 0.5f / zoom_ - center.y};
}

void animation_graph_view::draw_grid(ImDrawList* draw_list) const
{
    draw_list->AddRectFilled(origin_, ImVec2(origin_.x + size_.x, origin_.y + size_.y), COL_CANVAS);
    const float step = GRID_STEP * zoom_;
    if(step < 4.0f)
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
                           line % 4 == 0 ? COL_GRID_MAJOR : COL_GRID);
    }
    for(float y = offset_y; y < size_.y; y += step)
    {
        const int line = int(std::floor((y - pan_.y * zoom_) / step + 0.5f));
        draw_list->AddLine(ImVec2(origin_.x, origin_.y + y),
                           ImVec2(origin_.x + size_.x, origin_.y + y),
                           line % 4 == 0 ? COL_GRID_MAJOR : COL_GRID);
    }
}

void animation_graph_view::draw_arrow(ImDrawList* draw_list,
                                      const ImVec2& from,
                                      const ImVec2& to,
                                      ImU32 color,
                                      float thickness,
                                      int heads) const
{
    draw_list->AddLine(from, to, color, thickness);
    ImVec2 direction(to.x - from.x, to.y - from.y);
    const float length = length_of(direction);
    if(length < 1.0f)
    {
        return;
    }
    direction = ImVec2(direction.x / length, direction.y / length);
    const ImVec2 normal(-direction.y, direction.x);
    const float head = ARROW_HEAD_SIZE * std::max(zoom_, 0.6f);
    const ImVec2 middle((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
    // Several transitions between one pair show as stacked heads, like a multi-transition marker.
    for(int i = 0; i < heads; ++i)
    {
        const float shift = (float(i) - float(heads - 1) * 0.5f) * head * 1.3f;
        const ImVec2 tip(middle.x + direction.x * (head + shift), middle.y + direction.y * (head + shift));
        const ImVec2 back(tip.x - direction.x * head * 1.8f, tip.y - direction.y * head * 1.8f);
        draw_list->AddTriangleFilled(tip,
                                     ImVec2(back.x + normal.x * head, back.y + normal.y * head),
                                     ImVec2(back.x - normal.x * head, back.y - normal.y * head),
                                     color);
    }
}

void animation_graph_view::draw_transitions(ImDrawList* draw_list,
                                            const animation_layer_def& layer,
                                            const animation_graph_live& live,
                                            const animation_graph_selection& selection) const
{
    // Entry -> default state.
    const int default_state = get_default_state(layer);
    if(default_state >= 0)
    {
        const auto entry = get_node_rect(layer, {node_kind::entry, -1});
        const auto target = get_node_rect(layer, {node_kind::state, default_state});
        const auto segment = make_link_segment(entry.min, entry.max, target.min, target.max, 0.0f);
        if(segment.valid)
        {
            draw_arrow(draw_list, segment.from, segment.to, COL_ARROW_ENTRY, ENTRY_ARROW_THICKNESS * std::max(zoom_, MIN_LINE_ZOOM), 1);
        }
    }
    // Every transition's ends once; a pair draws once, with a head per transition.
    std::vector<std::pair<int, int>> ends;
    ends.reserve(layer.transitions.size());
    std::map<std::pair<int, int>, int> pair_counts;
    for(const auto& transition : layer.transitions)
    {
        const auto& end = ends.emplace_back(get_source_index(layer, transition), find_animation_state(layer, transition.destination));
        ++pair_counts[end];
    }
    std::map<std::pair<int, int>, bool> drawn;
    for(size_t i = 0; i < layer.transitions.size(); ++i)
    {
        const auto [source, destination] = ends[i];
        if(destination < 0 || source == -1)
        {
            continue;
        }
        const bool selected = selection.is(animation_graph_selection::kind::transition, int(i));
        const bool active = live.valid && live.transition == int(i);
        auto& was_drawn = drawn[ends[i]];
        if(was_drawn && !selected && !active)
        {
            continue;
        }
        was_drawn = true;
        const int heads = std::min(pair_counts[ends[i]], MAX_ARROW_HEADS);
        const ImU32 color = active ? COL_ACTIVE : (selected ? COL_SELECTED : COL_ARROW);
        const float thickness = (active || selected ? HIGHLIGHTED_TRANSITION_THICKNESS : TRANSITION_THICKNESS) * std::max(zoom_, MIN_LINE_ZOOM);
        const node_ref source_node = source == -2 ? node_ref{node_kind::any_state, -1} : node_ref{node_kind::state, source};
        const auto source_rect = get_node_rect(layer, source_node);
        const auto destination_rect = get_node_rect(layer, {node_kind::state, destination});
        if(source == destination)
        {
            // A state transitioning into itself: a loop over the node.
            const ImVec2 center = source_rect.center();
            const float lift = SELF_LOOP_LIFT * zoom_;
            const float spread = SELF_LOOP_SPREAD * zoom_;
            const ImVec2 p0(center.x - spread, source_rect.min.y);
            const ImVec2 p3(center.x + spread, source_rect.min.y);
            draw_list->AddBezierCubic(p0,
                                      ImVec2(p0.x - spread, p0.y - lift * 1.6f),
                                      ImVec2(p3.x + spread, p3.y - lift * 1.6f),
                                      p3,
                                      color,
                                      thickness);
            const ImVec2 tip(p3.x, p3.y - 1.0f);
            const float head = ARROW_HEAD_SIZE * std::max(zoom_, 0.6f);
            draw_list->AddTriangleFilled(tip,
                                         ImVec2(tip.x - head, tip.y - head * 1.6f),
                                         ImVec2(tip.x + head * 0.6f, tip.y - head * 1.8f),
                                         color);
            continue;
        }
        // Two-way pairs sit side by side, each offset to its own right.
        const auto segment = make_link_segment(source_rect.min,
                                               source_rect.max,
                                               destination_rect.min,
                                               destination_rect.max,
                                               get_pair_offset(zoom_));
        if(!segment.valid)
        {
            continue;
        }
        const ImVec2 from = segment.from;
        const ImVec2 to = segment.to;
        draw_arrow(draw_list, from, to, color, thickness, heads);
        if(active)
        {
            const float t = std::clamp(live.transition_progress, 0.0f, 1.0f);
            const ImVec2 dot(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
            draw_list->AddCircleFilled(dot, ACTIVE_DOT_RADIUS * std::max(zoom_, MIN_LINE_ZOOM), COL_ACTIVE);
        }
    }
}

void animation_graph_view::draw_node(ImDrawList* draw_list,
                                     const node_rect& rect,
                                     const char* label,
                                     const char* sublabel,
                                     ImU32 accent,
                                     bool selected,
                                     bool hovered,
                                     float progress,
                                     ImU32 progress_color) const
{
    const float rounding = NODE_ROUNDING * zoom_;
    draw_list->AddRectFilled(ImVec2(rect.min.x + NODE_SHADOW_OFFSET_X, rect.min.y + NODE_SHADOW_OFFSET_Y),
                             ImVec2(rect.max.x + NODE_SHADOW_OFFSET_X, rect.max.y + NODE_SHADOW_OFFSET_Y),
                             COL_NODE_SHADOW,
                             rounding);
    draw_list->AddRectFilled(rect.min, rect.max, hovered ? COL_NODE_HOVER : COL_NODE, rounding);
    // Accent strip on the left edge says what kind of node it is.
    draw_list->AddRectFilled(rect.min,
                             ImVec2(rect.min.x + NODE_ACCENT_WIDTH * zoom_, rect.max.y),
                             accent,
                             rounding,
                             ImDrawFlags_RoundCornersLeft);
    if(progress >= 0.0f)
    {
        const float inset = PROGRESS_INSET * zoom_;
        const float bar_height = std::max(PROGRESS_HEIGHT * zoom_, MIN_PROGRESS_HEIGHT);
        const float bar_width = (rect.max.x - rect.min.x) - inset * 2.0f;
        const ImVec2 bar_min(rect.min.x + inset, rect.max.y - inset * PROGRESS_BOTTOM_SHARE - bar_height);
        draw_list->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width, bar_min.y + bar_height), COL_PROGRESS_TRACK, PROGRESS_ROUNDING);
        draw_list->AddRectFilled(bar_min,
                                 ImVec2(bar_min.x + bar_width * std::clamp(progress, 0.0f, 1.0f), bar_min.y + bar_height),
                                 progress_color,
                                 PROGRESS_ROUNDING);
    }
    draw_list->AddRect(rect.min, rect.max, selected ? COL_SELECTED : COL_NODE_BORDER, rounding, 0, selected ? SELECTED_NODE_BORDER_THICKNESS : NODE_BORDER_THICKNESS);
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * zoom_;
    if(font_size < MIN_NODE_FONT_SIZE)
    {
        return;
    }
    const float height = rect.max.y - rect.min.y;
    const ImVec2 label_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label);
    const float text_x = rect.min.x + NODE_LABEL_INDENT * zoom_;
    const float max_width = rect.max.x - text_x - NODE_LABEL_MARGIN * zoom_;
    const float label_y = sublabel ? rect.min.y + height * NODE_LABEL_TOP_SHARE : rect.min.y + (height - label_size.y) * 0.5f;
    const std::string fitted_label = fit_text(font, font_size, label, max_width);
    draw_list->AddText(font, font_size, ImVec2(text_x, label_y), COL_NODE_TEXT, fitted_label.c_str());
    if(sublabel)
    {
        const float sub_size = font_size * NODE_SUBLABEL_SCALE;
        const std::string fitted_sublabel = fit_text(font, sub_size, sublabel, max_width);
        draw_list->AddText(font,
                           sub_size,
                           ImVec2(text_x, label_y + font_size + NODE_SUBLABEL_GAP * zoom_),
                           COL_NODE_SUBTEXT,
                           fitted_sublabel.c_str());
    }
}

void animation_graph_view::draw_nodes(ImDrawList* draw_list,
                                      const animation_layer_def& layer,
                                      const animation_graph_live& live,
                                      const animation_graph_selection& selection) const
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const node_ref hovered = ImGui::IsWindowHovered() ? hit_node(layer, mouse) : node_ref{node_kind::state, -1};
    const int default_state = get_default_state(layer);
    // The top state (the selected one) goes last, over the others.
    for(size_t order = 0; order < layer.states.size(); ++order)
    {
        const bool has_top = top_state_ >= 0 && size_t(top_state_) < layer.states.size();
        size_t i = order;
        if(has_top)
        {
            i = order + 1 == layer.states.size() ? size_t(top_state_) : (order >= size_t(top_state_) ? order + 1 : order);
        }
        const auto& state = layer.states[i];
        const bool is_current = live.valid && live.current_state == int(i);
        const bool is_next = live.valid && live.next_state == int(i);
        float progress = -1.0f;
        ImU32 progress_color = COL_PROGRESS;
        if(is_next)
        {
            progress = live.next_state_time - std::floor(live.next_state_time);
            progress_color = COL_PROGRESS_NEXT;
        }
        else if(is_current)
        {
            progress = live.state_time - std::floor(live.state_time);
        }
        const auto rect = get_node_rect(layer, {node_kind::state, int(i)});
        const std::string sublabel = get_motion_label(state);
        draw_node(draw_list,
                  rect,
                  state.name.c_str(),
                  sublabel.c_str(),
                  int(i) == default_state ? COL_DEFAULT_ACCENT : COL_STATE_ACCENT,
                  selection.is(animation_graph_selection::kind::state, int(i)),
                  hovered == node_ref{node_kind::state, int(i)},
                  progress,
                  progress_color);
        if(is_current || is_next)
        {
            draw_list->AddRect(ImVec2(rect.min.x - 2.0f, rect.min.y - 2.0f),
                               ImVec2(rect.max.x + 2.0f, rect.max.y + 2.0f),
                               is_next ? COL_PROGRESS_NEXT : COL_ACTIVE,
                               NODE_ROUNDING * zoom_ + 2.0f,
                               0,
                               1.5f);
        }
    }
    draw_node(draw_list,
              get_node_rect(layer, {node_kind::entry, -1}),
              ICON_MDI_PLAY " Entry",
              nullptr,
              COL_ENTRY_ACCENT,
              selection.what == animation_graph_selection::kind::entry,
              hovered == node_ref{node_kind::entry, -1},
              -1.0f,
              0);
    draw_node(draw_list,
              get_node_rect(layer, {node_kind::any_state, -1}),
              ICON_MDI_ASTERISK " Any State",
              nullptr,
              COL_ANY_ACCENT,
              selection.what == animation_graph_selection::kind::any_state,
              hovered == node_ref{node_kind::any_state, -1},
              -1.0f,
              0);
}

void animation_graph_view::draw_tooltip(const animation_controller& controller, const animation_layer_def& layer) const
{
    if(!ImGui::IsItemHovered() || linking_ || dragged_)
    {
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const node_ref node = hit_node(layer, mouse);
    if(node.kind == node_kind::state && node.index >= 0)
    {
        const auto& state = layer.states[size_t(node.index)];
        ImGui::SetTooltip("%s\n%s, speed %.2f%s%s",
                          state.name.c_str(),
                          get_motion_label(state).c_str(),
                          state.speed,
                          state.loop ? ", loops" : ", plays once",
                          node.index == get_default_state(layer) ? "\nLayer default state" : "");
        return;
    }
    if(node.kind != node_kind::state)
    {
        return;
    }
    const int index = hit_transition(layer, mouse);
    if(index < 0)
    {
        return;
    }
    const auto& transition = layer.transitions[size_t(index)];
    std::string text = (transition.source.empty() ? std::string("Any State") : transition.source) + " -> " +
                       transition.destination;
    for(const auto& condition : transition.conditions)
    {
        text += "\n  " + describe_condition(controller, condition);
    }
    if(transition.has_exit_time)
    {
        char exit_text[64];
        ImFormatString(exit_text, sizeof(exit_text), "\n  exit time %.2f", transition.exit_time);
        text += exit_text;
    }
    if(transition.conditions.empty() && !transition.has_exit_time)
    {
        text += "\n  never fires: add a condition or an exit time";
    }
    char duration_text[64];
    ImFormatString(duration_text, sizeof(duration_text), "\nCrossfade %.2f s", transition.duration);
    text += duration_text;
    ImGui::SetTooltip("%s", text.c_str());
}

auto animation_graph_view::draw(animation_controller& controller,
                                size_t layer_index,
                                const animation_graph_live& live,
                                animation_graph_selection& selection) -> result
{
    result out;
    if(layer_index >= controller.layers.size())
    {
        ImGui::TextDisabled("No layer.");
        return out;
    }
    auto& layer = controller.layers[layer_index];
    top_state_ = get_top_state(selection);
    origin_ = ImGui::GetCursorScreenPos();
    size_ = ImGui::GetContentRegionAvail();
    size_.x = std::max(size_.x, 50.0f);
    size_.y = std::max(size_.y, 50.0f);
    if(framed_layer_ != layer_index)
    {
        framed_layer_ = layer_index;
        frame_request_ = true;
        cancel_interaction();
    }
    if(frame_request_)
    {
        frame_request_ = false;
        // A layer shown for the first time with every state on one spot gets a grid layout.
        if(is_never_laid_out(layer))
        {
            apply_auto_layout(layer);
            out.begin_edit = true;
            out.layout_changed = true;
            out.finished = true;
        }
        frame_all(layer);
    }
    ImGui::InvisibleButton("##graph_canvas",
                           size_,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    handle_drop(out);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(origin_, ImVec2(origin_.x + size_.x, origin_.y + size_.y), true);
    draw_grid(draw_list);
    draw_transitions(draw_list, layer, live, selection);
    draw_nodes(draw_list, layer, live, selection);
    if(linking_ && is_valid_node(layer, link_source_))
    {
        const auto source_rect = get_node_rect(layer, link_source_);
        draw_arrow(draw_list, source_rect.center(), ImGui::GetIO().MousePos, COL_LINK, 2.0f, 1);
    }
    if(layer.states.empty())
    {
        const char* hint = "Right-click to create a state, or drop an animation clip here.";
        const ImVec2 hint_size = ImGui::CalcTextSize(hint);
        draw_list->AddText(ImVec2(origin_.x + (size_.x - hint_size.x) * 0.5f, origin_.y + size_.y * 0.5f),
                           COL_NODE_SUBTEXT,
                           hint);
    }
    draw_list->PopClipRect();
    draw_tooltip(controller, layer);
    handle_input(controller, layer_index, selection, out);
    draw_context_menus(controller, layer_index, selection, out);
    return out;
}

void animation_graph_view::handle_drop(result& out)
{
    if(!ImGui::BeginDragDropTarget())
    {
        return;
    }
    for(const auto& format : ex::get_suported_formats<animation_clip>())
    {
        if(auto payload = ImGui::AcceptDragDropPayload(format.c_str()))
        {
            std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
            out.dropped_clip_key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
            out.drop_position = to_graph(ImGui::GetIO().MousePos);
            break;
        }
    }
    ImGui::EndDragDropTarget();
}

void animation_graph_view::handle_input(animation_controller& controller,
                                        size_t layer_index,
                                        animation_graph_selection& selection,
                                        result& out)
{
    auto& layer = controller.layers[layer_index];
    // A link whose source went away (undo, delete) ends.
    if(linking_ && !is_valid_node(layer, link_source_))
    {
        linking_ = false;
    }
    handle_zoom_and_pan();
    if(linking_ && ImGui::IsKeyPressed(shortcuts::item_cancel))
    {
        linking_ = false;
    }
    handle_click(layer, layer_index, selection, out);
    handle_node_drag(layer, out);
    handle_context_click(layer, selection);
    handle_keys(layer, selection, out);
}

void animation_graph_view::handle_zoom_and_pan()
{
    auto& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if(ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        const math::vec2 anchor = to_graph(mouse);
        zoom_ = std::clamp(zoom_ * std::pow(ZOOM_STEP, io.MouseWheel), MIN_ZOOM, MAX_ZOOM);
        pan_ = {(mouse.x - origin_.x) / zoom_ - anchor.x, (mouse.y - origin_.y) / zoom_ - anchor.y};
    }
    const bool pan_drag = ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                                                   (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)));
    if(pan_drag)
    {
        pan_.x += io.MouseDelta.x / zoom_;
        pan_.y += io.MouseDelta.y / zoom_;
    }
}

void animation_graph_view::link_to(animation_layer_def& layer,
                                   const node_ref& node,
                                   animation_graph_selection& selection,
                                   result& out)
{
    linking_ = false;
    if(node.kind != node_kind::state || node.index < 0)
    {
        return;
    }
    animation_transition_def transition;
    transition.source = link_source_.kind == node_kind::any_state ? std::string{} : layer.states[size_t(link_source_.index)].name;
    transition.destination = layer.states[size_t(node.index)].name;
    // A transition from a state starts with an exit time so it fires without setup; an Any State
    // one needs a condition anyway.
    transition.has_exit_time = !transition.source.empty();
    layer.transitions.push_back(transition);
    selection = {animation_graph_selection::kind::transition, int(layer.transitions.size() - 1)};
    out.set_committed();
}

void animation_graph_view::handle_click(animation_layer_def& layer,
                                        size_t layer_index,
                                        animation_graph_selection& selection,
                                        result& out)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if(!ImGui::IsItemHovered() || ImGui::GetIO().KeyAlt)
    {
        return;
    }
    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        const node_ref node = hit_node(layer, mouse);
        if(node.kind == node_kind::state && node.index >= 0)
        {
            selection = {animation_graph_selection::kind::state, node.index};
            out.rename_requested = true;
        }
        return;
    }
    if(!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        return;
    }
    const node_ref node = hit_node(layer, mouse);
    if(linking_)
    {
        link_to(layer, node, selection, out);
        return;
    }
    if(node.index >= 0 || node.kind != node_kind::state)
    {
        selection.what = node.kind == node_kind::state   ? animation_graph_selection::kind::state
                         : node.kind == node_kind::entry ? animation_graph_selection::kind::entry
                                                         : animation_graph_selection::kind::any_state;
        selection.index = node.index;
        dragging_ = node;
        dragged_ = false;
        drag_grab_ = to_graph(mouse) - get_node_position(layer, node);
        return;
    }
    const int transition = hit_transition(layer, mouse);
    selection = transition >= 0 ? animation_graph_selection{animation_graph_selection::kind::transition, transition}
                                : animation_graph_selection{animation_graph_selection::kind::layer, int(layer_index)};
}

void animation_graph_view::handle_node_drag(animation_layer_def& layer, result& out)
{
    auto& io = ImGui::GetIO();
    const bool dragging_node = dragging_.kind != node_kind::state || dragging_.index >= 0;
    if(!dragging_node)
    {
        return;
    }
    if(!is_valid_node(layer, dragging_))
    {
        cancel_interaction();
        return;
    }
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if(dragged_)
        {
            out.finished = true;
            out.layout_changed = true;
        }
        dragging_ = {node_kind::state, -1};
        dragged_ = false;
        return;
    }
    if(io.KeyAlt)
    {
        return;
    }
    math::vec2 position = to_graph(io.MousePos) - drag_grab_;
    if(io.KeyCtrl)
    {
        position = math::round(position / SNAP_STEP) * SNAP_STEP;
    }
    const math::vec2 previous = get_node_position(layer, dragging_);
    if(math::distance(position, previous) > 0.01f && ImGui::IsMouseDragging(ImGuiMouseButton_Left, NODE_DRAG_THRESHOLD))
    {
        out.begin_edit = out.begin_edit || !dragged_;
        dragged_ = true;
        set_node_position(layer, dragging_, position);
        out.layout_changed = true;
    }
}

void animation_graph_view::handle_context_click(animation_layer_def& layer, animation_graph_selection& selection)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if(!ImGui::IsItemHovered() || !ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
       ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right))
    {
        return;
    }
    linking_ = false;
    context_position_ = to_graph(mouse);
    context_node_ = hit_node(layer, mouse);
    if(context_node_.kind == node_kind::state && context_node_.index >= 0)
    {
        selection = {animation_graph_selection::kind::state, context_node_.index};
        ImGui::OpenPopup("##graph_node_menu");
        return;
    }
    if(context_node_.kind == node_kind::any_state)
    {
        selection = {animation_graph_selection::kind::any_state, -1};
        ImGui::OpenPopup("##graph_node_menu");
        return;
    }
    if(context_node_.kind == node_kind::entry)
    {
        selection = {animation_graph_selection::kind::entry, -1};
        return;
    }
    const int transition = hit_transition(layer, mouse);
    if(transition >= 0)
    {
        selection = {animation_graph_selection::kind::transition, transition};
        ImGui::OpenPopup("##graph_transition_menu");
        return;
    }
    ImGui::OpenPopup("##graph_canvas_menu");
}

void animation_graph_view::handle_keys(animation_layer_def& layer, animation_graph_selection& selection, result& out)
{
    // The graph's keys work while the canvas itself has focus (the last click was on it): not while
    // the sidebar, the inspector or one of the canvas's menus, which holds a node index, has it.
    const bool canvas_focused = ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput;
    if(!canvas_focused)
    {
        return;
    }
    if(ImGui::IsKeyPressed(ImGuiKey_F))
    {
        frame_all(layer);
    }
    if(!ImGui::IsKeyPressed(shortcuts::delete_item))
    {
        return;
    }
    if(selection.what == animation_graph_selection::kind::state && selection.index >= 0 &&
       size_t(selection.index) < layer.states.size())
    {
        remove_state(layer, size_t(selection.index));
        selection = {};
        out.set_committed();
    }
    else if(selection.what == animation_graph_selection::kind::transition && selection.index >= 0 &&
            size_t(selection.index) < layer.transitions.size())
    {
        layer.transitions.erase(layer.transitions.begin() + selection.index);
        selection = {};
        out.set_committed();
    }
}

void animation_graph_view::draw_context_menus(animation_controller& controller,
                                              size_t layer_index,
                                              animation_graph_selection& selection,
                                              result& out)
{
    auto& layer = controller.layers[layer_index];
    const auto add_state = [&](animation_motion_type motion, const std::string& base)
    {
        animation_state_def state;
        state.name = make_unique_state_name(layer, base);
        state.motion = motion;
        state.editor_position = context_position_;
        layer.states.push_back(state);
        if(layer.states.size() == 1)
        {
            layer.default_state = state.name;
        }
        selection = {animation_graph_selection::kind::state, int(layer.states.size() - 1)};
        out.set_committed();
    };
    if(ImGui::BeginPopup("##graph_canvas_menu"))
    {
        if(ImGui::BeginMenu(ICON_MDI_PLUS " Create State"))
        {
            if(ImGui::MenuItem("Empty"))
            {
                add_state(animation_motion_type::clip, "New State");
            }
            if(ImGui::MenuItem("Blend 1D"))
            {
                add_state(animation_motion_type::blend_1d, "Blend 1D");
            }
            if(ImGui::MenuItem("Blend 2D Directional"))
            {
                add_state(animation_motion_type::blend_2d_directional, "Blend 2D");
            }
            if(ImGui::MenuItem("Blend 2D Cartesian"))
            {
                add_state(animation_motion_type::blend_2d_cartesian, "Blend 2D");
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if(ImGui::MenuItem(ICON_MDI_FIT_TO_PAGE_OUTLINE " Frame All", "F"))
        {
            frame_all(layer);
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("##graph_node_menu"))
    {
        const bool is_state = context_node_.kind == node_kind::state && context_node_.index >= 0 &&
                              size_t(context_node_.index) < layer.states.size();
        if(ImGui::MenuItem(ICON_MDI_ARROW_RIGHT_BOLD " Make Transition"))
        {
            linking_ = true;
            link_source_ = context_node_;
        }
        if(is_state)
        {
            const auto& state = layer.states[size_t(context_node_.index)];
            const bool is_default = context_node_.index == get_default_state(layer);
            if(ImGui::MenuItem(ICON_MDI_RENAME_BOX " Rename", "F2"))
            {
                out.rename_requested = true;
            }
            if(ImGui::MenuItem(ICON_MDI_STAR " Set as Layer Default State", nullptr, false, !is_default))
            {
                layer.default_state = state.name;
                out.set_committed();
            }
            if(ImGui::MenuItem(ICON_MDI_CONTENT_DUPLICATE " Duplicate"))
            {
                animation_state_def copy = state;
                copy.name = make_unique_state_name(layer, state.name);
                copy.editor_position = state.editor_position + math::vec2(DUPLICATE_OFFSET, DUPLICATE_OFFSET);
                layer.states.push_back(copy);
                selection = {animation_graph_selection::kind::state, int(layer.states.size() - 1)};
                out.set_committed();
            }
            ImGui::Separator();
            if(ImGui::MenuItem(ICON_MDI_DELETE " Delete", "Del"))
            {
                remove_state(layer, size_t(context_node_.index));
                selection = {};
                out.set_committed();
            }
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("##graph_transition_menu"))
    {
        if(ImGui::MenuItem(ICON_MDI_DELETE " Delete Transition", "Del") &&
           selection.what == animation_graph_selection::kind::transition && selection.index >= 0 &&
           size_t(selection.index) < layer.transitions.size())
        {
            layer.transitions.erase(layer.transitions.begin() + selection.index);
            selection = {};
            out.set_committed();
        }
        ImGui::EndPopup();
    }
}

} // namespace unravel
