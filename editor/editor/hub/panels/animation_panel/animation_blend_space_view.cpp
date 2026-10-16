#include "animation_blend_space_view.h"

#include <filesystem/filesystem.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

namespace unravel
{

namespace
{
constexpr float PLOT_1D_HEIGHT = 84.0f;
/// 1D plots: the axis line sits this far below the top, under the range and parameter names.
constexpr float AXIS_1D_OFFSET = 30.0f;
/// Point labels of a 2D plot get at most this much width.
constexpr float MAX_2D_LABEL_WIDTH = 96.0f;
constexpr float PLOT_PADDING = 18.0f;
constexpr float POINT_RADIUS = 5.0f;
/// Extra room around the points, as a fraction of their spread.
constexpr float RANGE_MARGIN = 0.15f;
constexpr float POINT_HIT_RADIUS = 9.0f;
/// Live weight ring around a point: its radius grows by this much at weight 1.
constexpr float WEIGHT_RING_GROWTH = 18.0f;
/// Weights below this draw no ring.
constexpr float MIN_SHOWN_WEIGHT = 0.001f;
/// Ctrl-drag snaps point positions to this step.
constexpr float POINT_SNAP_STEP = 0.1f;
/// Mouse travel, in pixels, before a press on a point starts dragging it.
constexpr float POINT_DRAG_THRESHOLD = 2.0f;
/// Labels never get less room than this, in pixels.
constexpr float MIN_LABEL_ROOM = 24.0f;
/// 1D plots: the plot reaches this far above and below the axis line.
constexpr float AXIS_1D_HALF_HEIGHT = 8.0f;
/// Thickness of the 1D axis line.
constexpr float AXIS_1D_THICKNESS = 2.0f;
/// Gap between the plot and its range labels, and the top of the 1D header row, in pixels.
constexpr float RANGE_LABEL_GAP = 2.0f;
constexpr float HEADER_TOP = 4.0f;
/// The parameter marker: its ring radius and thickness.
constexpr float MARKER_RADIUS = 6.0f;
constexpr float MARKER_THICKNESS = 2.0f;

const ImU32 COL_PLOT = IM_COL32(28, 29, 32, 255);
const ImU32 COL_PLOT_BORDER = IM_COL32(70, 74, 82, 255);
const ImU32 COL_AXIS = IM_COL32(64, 68, 76, 255);
const ImU32 COL_POINT = IM_COL32(200, 204, 212, 255);
const ImU32 COL_POINT_SELECTED = IM_COL32(80, 170, 255, 255);
const ImU32 COL_WEIGHT = IM_COL32(90, 200, 255, 110);
const ImU32 COL_MARKER = IM_COL32(255, 200, 80, 255);
const ImU32 COL_MARKER_LINE = IM_COL32(255, 200, 80, 90);
const ImU32 COL_LABEL = IM_COL32(170, 176, 186, 255);

struct axis_range
{
    float min{-1.0f};
    float max{1.0f};
};

auto make_range(float lo, float hi, bool include_zero) -> axis_range
{
    if(include_zero)
    {
        lo = std::min(lo, 0.0f);
        hi = std::max(hi, 0.0f);
    }
    if(hi - lo < 1e-3f)
    {
        lo -= 1.0f;
        hi += 1.0f;
    }
    const float margin = (hi - lo) * RANGE_MARGIN;
    return {lo - margin, hi + margin};
}

auto is_name_separator(char c) -> bool
{
    return c == '_' || c == '-' || c == '.' || c == ' ';
}

/// Clip names without the prefix every point shares ("Rig_Idle_Loop", "Rig_Walk_Loop" -> "Idle_Loop",
/// "Walk_Loop"), cut at a word boundary so no label starts mid-word.
auto make_point_labels(const animation_state_def& state) -> std::vector<std::string>
{
    std::vector<std::string> labels;
    for(const auto& point : state.points)
    {
        labels.push_back(point.clip ? fs::path(point.clip.id()).stem().string() : std::string("(none)"));
    }
    if(labels.size() < 2)
    {
        return labels;
    }
    size_t prefix = labels.front().size();
    for(const auto& label : labels)
    {
        prefix = std::min(prefix, label.size());
        for(size_t i = 0; i < prefix; ++i)
        {
            if(label[i] != labels.front()[i])
            {
                prefix = i;
                break;
            }
        }
    }
    while(prefix > 0 && !is_name_separator(labels.front()[prefix - 1]))
    {
        --prefix;
    }
    for(auto& label : labels)
    {
        if(prefix < label.size())
        {
            label = label.substr(prefix);
        }
    }
    return labels;
}
} // namespace

void animation_blend_space_view::cancel_interaction()
{
    dragging_point_ = -1;
    dragged_ = false;
    dragging_marker_ = false;
}

auto animation_blend_space_view::draw(animation_state_def& state,
                                      const std::vector<float>& weights,
                                      const math::vec2* parameter,
                                      int selected_point) -> result
{
    result out;
    const bool is_2d = state.motion != animation_motion_type::blend_1d;
    const float width = std::max(ImGui::GetContentRegionAvail().x, 120.0f);
    const float height = is_2d ? width : PLOT_1D_HEIGHT;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##blend_space_plot", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    // A drag lasts while the plot holds the mouse: a release, or the plot going away, ends it.
    const bool held = ImGui::IsItemActive();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float lo_x = 0.0f;
    float hi_x = 0.0f;
    float lo_y = 0.0f;
    float hi_y = 0.0f;
    for(size_t i = 0; i < state.points.size(); ++i)
    {
        const auto& p = state.points[i].position;
        lo_x = i == 0 ? p.x : std::min(lo_x, p.x);
        hi_x = i == 0 ? p.x : std::max(hi_x, p.x);
        lo_y = i == 0 ? p.y : std::min(lo_y, p.y);
        hi_y = i == 0 ? p.y : std::max(hi_y, p.y);
    }
    if(parameter)
    {
        lo_x = std::min(lo_x, parameter->x);
        hi_x = std::max(hi_x, parameter->x);
        lo_y = std::min(lo_y, parameter->y);
        hi_y = std::max(hi_y, parameter->y);
    }
    axis_range range_x = make_range(lo_x, hi_x, state.motion == animation_motion_type::blend_2d_directional);
    axis_range range_y = make_range(lo_y, hi_y, state.motion == animation_motion_type::blend_2d_directional);
    if(dragging_point_ >= 0 || dragging_marker_)
    {
        range_x = {frozen_range_.min.x, frozen_range_.max.x};
        range_y = {frozen_range_.min.y, frozen_range_.max.y};
    }
    else
    {
        frozen_range_ = {{range_x.min, range_y.min}, {range_x.max, range_y.max}};
    }

    const ImVec2 plot_min(origin.x + PLOT_PADDING, origin.y + (is_2d ? PLOT_PADDING : AXIS_1D_OFFSET - AXIS_1D_HALF_HEIGHT));
    const ImVec2 plot_max(origin.x + width - PLOT_PADDING, is_2d ? origin.y + height - PLOT_PADDING : origin.y + AXIS_1D_OFFSET + AXIS_1D_HALF_HEIGHT);
    const float axis_1d_y = origin.y + AXIS_1D_OFFSET;
    const auto to_screen = [&](const math::vec2& p) -> ImVec2
    {
        const float tx = (p.x - range_x.min) / (range_x.max - range_x.min);
        if(!is_2d)
        {
            return ImVec2(plot_min.x + tx * (plot_max.x - plot_min.x), axis_1d_y);
        }
        const float ty = (p.y - range_y.min) / (range_y.max - range_y.min);
        return ImVec2(plot_min.x + tx * (plot_max.x - plot_min.x), plot_max.y - ty * (plot_max.y - plot_min.y));
    };
    const auto to_param = [&](const ImVec2& s) -> math::vec2
    {
        const float tx = (s.x - plot_min.x) / std::max(plot_max.x - plot_min.x, 1.0f);
        const float ty = (plot_max.y - s.y) / std::max(plot_max.y - plot_min.y, 1.0f);
        return {range_x.min + tx * (range_x.max - range_x.min), is_2d ? range_y.min + ty * (range_y.max - range_y.min) : 0.0f};
    };

    draw_list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), COL_PLOT, 4.0f);
    draw_list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), COL_PLOT_BORDER, 4.0f);
    // Zero axes.
    {
        const ImVec2 zero = to_screen({0.0f, 0.0f});
        if(zero.x >= plot_min.x && zero.x <= plot_max.x)
        {
            draw_list->AddLine(ImVec2(zero.x, plot_min.y), ImVec2(zero.x, plot_max.y), COL_AXIS);
        }
        if(is_2d && zero.y >= plot_min.y && zero.y <= plot_max.y)
        {
            draw_list->AddLine(ImVec2(plot_min.x, zero.y), ImVec2(plot_max.x, zero.y), COL_AXIS);
        }
        if(!is_2d)
        {
            draw_list->AddLine(ImVec2(plot_min.x, axis_1d_y), ImVec2(plot_max.x, axis_1d_y), COL_AXIS, AXIS_1D_THICKNESS);
        }
    }
    // Range and parameter name: a header row on 1D plots, under the plot on 2D ones.
    const float range_y_pos = is_2d ? plot_max.y + RANGE_LABEL_GAP : origin.y + HEADER_TOP;
    const char* parameter_x_name = state.parameter_x.empty() ? "X" : state.parameter_x.c_str();
    char range_text[64];
    ImFormatString(range_text, sizeof(range_text), "%.2f", range_x.min);
    draw_list->AddText(ImVec2(plot_min.x, range_y_pos), COL_LABEL, range_text);
    ImFormatString(range_text, sizeof(range_text), "%.2f", range_x.max);
    draw_list->AddText(ImVec2(plot_max.x - ImGui::CalcTextSize(range_text).x, range_y_pos), COL_LABEL, range_text);
    draw_list->AddText(ImVec2((plot_min.x + plot_max.x) * 0.5f - ImGui::CalcTextSize(parameter_x_name).x * 0.5f, range_y_pos),
                       COL_LABEL,
                       parameter_x_name);
    if(is_2d)
    {
        ImFormatString(range_text, sizeof(range_text), "%.2f", range_y.max);
        draw_list->AddText(ImVec2(origin.x + RANGE_LABEL_GAP, plot_min.y - ImGui::GetFontSize()), COL_LABEL, range_text);
        draw_list->AddText(ImVec2(origin.x + RANGE_LABEL_GAP, (plot_min.y + plot_max.y) * 0.5f),
                           COL_LABEL,
                           state.parameter_y.empty() ? "Y" : state.parameter_y.c_str());
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered_point = -1;
    for(size_t i = 0; i < state.points.size(); ++i)
    {
        const ImVec2 p = to_screen(state.points[i].position);
        const float dx = mouse.x - p.x;
        const float dy = mouse.y - p.y;
        if(hovered && dx * dx + dy * dy <= POINT_HIT_RADIUS * POINT_HIT_RADIUS)
        {
            hovered_point = int(i);
        }
    }

    const auto labels = make_point_labels(state);
    // Points side by side (a 1D track, a row of a 2D plane) alternate their labels between two
    // places - under the axis and the line below on 1D, under and over the point on 2D - so a
    // label may span the gap to the next point of its own place.
    std::vector<float> label_room(state.points.size(), is_2d ? MAX_2D_LABEL_WIDTH : width);
    std::vector<int> label_row(state.points.size(), 0);
    std::vector<size_t> order(state.points.size());
    std::iota(order.begin(), order.end(), size_t(0));
    const float row_height = ImGui::GetFontSize() * 1.5f;
    // Rows top to bottom (a row: the points less than a row height below its first one), then left
    // to right inside a row.
    const auto get_screen_x = [&](size_t point)
    {
        return to_screen(state.points[point].position).x;
    };
    const auto get_screen_y = [&](size_t point)
    {
        return to_screen(state.points[point].position).y;
    };
    std::sort(order.begin(),
              order.end(),
              [&](size_t lhs, size_t rhs)
              {
                  return get_screen_y(lhs) < get_screen_y(rhs);
              });
    for(size_t start = 0; start < order.size();)
    {
        size_t end = start + 1;
        const float row_y = get_screen_y(order[start]);
        while(end < order.size() && get_screen_y(order[end]) - row_y < row_height)
        {
            ++end;
        }
        std::sort(order.begin() + std::ptrdiff_t(start),
                  order.begin() + std::ptrdiff_t(end),
                  [&](size_t lhs, size_t rhs)
                  {
                      return get_screen_x(lhs) < get_screen_x(rhs);
                  });
        for(size_t k = start; k < end; ++k)
        {
            const float x = get_screen_x(order[k]);
            float half_room = width;
            if(k >= start + 2)
            {
                half_room = std::min(half_room, x - get_screen_x(order[k - 2]));
            }
            if(k + 2 < end)
            {
                half_room = std::min(half_room, get_screen_x(order[k + 2]) - x);
            }
            label_room[order[k]] = std::clamp(2.0f * half_room - 6.0f, MIN_LABEL_ROOM, label_room[order[k]]);
            label_row[order[k]] = int((k - start) % 2);
        }
        start = end;
    }
    for(size_t i = 0; i < state.points.size(); ++i)
    {
        const ImVec2 p = to_screen(state.points[i].position);
        const float weight = i < weights.size() ? weights[i] : 0.0f;
        if(weight > MIN_SHOWN_WEIGHT)
        {
            draw_list->AddCircleFilled(p, POINT_RADIUS + weight * WEIGHT_RING_GROWTH, COL_WEIGHT);
        }
        const bool selected = int(i) == selected_point;
        const bool point_hovered = int(i) == hovered_point;
        const ImU32 color = selected ? COL_POINT_SELECTED : COL_POINT;
        if(is_2d)
        {
            draw_list->AddCircleFilled(p, point_hovered ? POINT_RADIUS + 1.5f : POINT_RADIUS, color);
        }
        else
        {
            const float r = point_hovered ? POINT_RADIUS + 1.5f : POINT_RADIUS + 0.5f;
            draw_list->AddQuadFilled(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), color);
        }
        const std::string label = fit_text(ImGui::GetFont(), ImGui::GetFontSize(), labels[i], label_room[i]);
        const float label_y = !is_2d          ? p.y + POINT_RADIUS + 4.0f + float(label_row[i]) * ImGui::GetFontSize()
                              : label_row[i] == 0 ? p.y + POINT_RADIUS + 1.0f
                                                  : p.y - POINT_RADIUS - 1.0f - ImGui::GetFontSize();
        const float label_width = ImGui::CalcTextSize(label.c_str()).x;
        const float label_x = std::clamp(p.x - label_width * 0.5f, origin.x + 2.0f, origin.x + width - label_width - 2.0f);
        draw_list->AddText(ImVec2(label_x, label_y), COL_LABEL, label.c_str());
        if(point_hovered)
        {
            ImGui::SetTooltip("%s\n(%.2f, %.2f)%s",
                              labels[i].c_str(),
                              state.points[i].position.x,
                              state.points[i].position.y,
                              weight > 0.0f ? (std::string("\nweight ") + std::to_string(weight)).c_str() : "");
        }
    }

    if(parameter)
    {
        const ImVec2 m = to_screen(*parameter);
        draw_list->AddLine(ImVec2(m.x, plot_min.y), ImVec2(m.x, plot_max.y), COL_MARKER_LINE);
        if(is_2d)
        {
            draw_list->AddLine(ImVec2(plot_min.x, m.y), ImVec2(plot_max.x, m.y), COL_MARKER_LINE);
        }
        draw_list->AddCircle(m, MARKER_RADIUS, COL_MARKER, 0, MARKER_THICKNESS);
    }

    if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if(hovered_point >= 0)
        {
            dragging_point_ = hovered_point;
            dragged_ = false;
            out.clicked_point = hovered_point;
        }
        else if(parameter)
        {
            dragging_marker_ = true;
        }
    }
    if(dragging_point_ >= 0 && held && size_t(dragging_point_) < state.points.size())
    {
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left, POINT_DRAG_THRESHOLD))
        {
            if(!dragged_)
            {
                out.begin_edit = true;
                dragged_ = true;
            }
            math::vec2 position = to_param(mouse);
            if(ImGui::GetIO().KeyCtrl)
            {
                position = math::round(position / POINT_SNAP_STEP) * POINT_SNAP_STEP;
            }
            if(!is_2d)
            {
                position.y = 0.0f;
            }
            state.points[size_t(dragging_point_)].position = position;
            out.changed = true;
        }
    }
    else if(dragging_point_ >= 0)
    {
        out.finished = dragged_;
        dragging_point_ = -1;
        dragged_ = false;
    }
    if(dragging_marker_ && held)
    {
        out.preview_moved = true;
        out.preview_parameter = to_param(mouse);
    }
    else
    {
        dragging_marker_ = false;
    }
    return out;
}

} // namespace unravel
