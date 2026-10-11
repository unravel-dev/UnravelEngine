#include "inspector_curve_editor.h"
#include "inspector_container_widgets.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <unordered_map>

namespace unravel::curve_editor
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size, so the editor follows the UI scale. Pixels are used only
// for hairlines, which should not grow with the font.
constexpr float CURVE_PREVIEW_HEIGHT_EM = 1.9f;
constexpr float CURVE_PREVIEW_ROUNDING_EM = 0.2f;
constexpr float CURVE_CANVAS_PADDING_EM = 0.6f;
constexpr float CURVE_CANVAS_MIN_HEIGHT_EM = 8.0f;
constexpr float CURVE_KEY_RADIUS_EM = 0.24f;
constexpr float CURVE_KEY_ACTIVE_RADIUS_EM = 0.38f;
constexpr float CURVE_KEY_PICK_RADIUS_EM = 0.65f;
constexpr float CURVE_TANGENT_LENGTH_EM = 2.0f;
constexpr float CURVE_TANGENT_RADIUS_EM = 0.2f;
constexpr float CURVE_GRID_SPACING_EM = 4.5f;
constexpr float CURVE_LABEL_INSET_EM = 0.25f;
constexpr float CURVE_TOOLBAR_GAP_EM = 0.4f;
constexpr float CURVE_CHIP_PADDING_EM = 0.5f;
constexpr float CURVE_FIELD_WIDTH_EM = 5.0f;
/// Share of the viewport the editor window takes when it first opens.
constexpr float CURVE_WINDOW_SHARE_X = 0.46f;
constexpr float CURVE_WINDOW_SHARE_Y = 0.52f;

constexpr float CURVE_LINE_THICKNESS = 2.0f;
constexpr float CURVE_HAIRLINE_THICKNESS = 1.0f;
constexpr float CURVE_SELECTION_THICKNESS = 1.5f;

constexpr ImU32 CURVE_GRID_COLOR = IM_COL32(255, 255, 255, 18);
constexpr ImU32 CURVE_AXIS_COLOR = IM_COL32(255, 255, 255, 54);
constexpr ImU32 CURVE_CANVAS_COLOR = IM_COL32(0, 0, 0, 56);
constexpr ImU32 CURVE_CHIP_OFF_COLOR = IM_COL32(255, 255, 255, 18);
constexpr ImU32 CURVE_SELECTION_FILL_COLOR = IM_COL32(255, 255, 255, 26);
constexpr float CURVE_MUTED_ALPHA = 0.55f;
/// Alpha of a channel that is switched off: still drawn, so it can be found again.
constexpr float CURVE_HIDDEN_CHANNEL_ALPHA = 0.12f;

/// One sample of the plotted line per this many pixels. Dense enough that a smooth segment reads
/// as a curve rather than as a chain of chords.
constexpr float CURVE_PIXELS_PER_SAMPLE = 3.0f;
/// Samples across the gradient strip; its bands are flat fills, so this is the band count.
constexpr int CURVE_STRIP_SAMPLES = 128;
constexpr float CURVE_MARKER_HEIGHT_EM = 1.6f;
constexpr float CURVE_MARKER_WIDTH_EM = 0.9f;
constexpr float CURVE_STRIP_MIN_HEIGHT_EM = 2.5f;
/// A drag shorter than this many pixels is a click, not a rubber band.
constexpr float CURVE_DRAG_THRESHOLD = 4.0f;
/// Padding added around the keys when the view is framed to them, as a share of their extent.
constexpr float CURVE_FRAME_MARGIN = 0.12f;
/// The same, for the inline strip, which has no axes to spare: a line sitting exactly on the
/// strip's edge is half cut away by the clip rect.
constexpr float CURVE_PREVIEW_MARGIN = 0.14f;
/// A domain or value range narrower than this is widened, so the projection never divides by zero.
constexpr float CURVE_MIN_VIEW_EXTENT = 1e-4f;
constexpr float CURVE_ZOOM_PER_WHEEL_NOTCH = 1.15f;
/// Default step of the numeric fields when the property does not name one.
constexpr float CURVE_DEFAULT_STEP = 0.01f;

constexpr const char* CURVE_INTERPOLATION_NAMES[] = {"Constant", "Linear", "Smooth"};
constexpr const char* CURVE_TANGENT_NAMES[] = {"Auto", "Flat", "Free"};

auto get_muted_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, CURVE_MUTED_ALPHA);
}

auto get_accent_color(float alpha = 1.0f) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

/// One key of one channel.
struct key_ref
{
    size_t channel{};
    size_t key{};

    auto operator==(const key_ref& other) const -> bool
    {
        return channel == other.channel && key == other.key;
    }
};

enum class drag_mode
{
    none,
    move_keys,
    move_tangent_in,
    move_tangent_out,
    pan,
    rubber_band
};

/// What an open editor remembers between frames.
///
/// Too much for ImGuiStorage, which holds scalars: the selection is a list and the view is a
/// rectangle. Keyed by the id of the inline preview, so two curves in one inspector keep their
/// own view and selection - which the gradient widget this replaces did not.
struct editor_state
{
    bool is_open{};
    bool has_view{};
    /// The part of the curve the canvas shows.
    float domain_min{};
    float domain_max{1.0f};
    float value_min{};
    float value_max{1.0f};

    std::vector<key_ref> selection;
    /// Channels switched off in the toolbar, by index.
    std::vector<bool> hidden_channels;

    drag_mode drag{drag_mode::none};
    key_ref drag_anchor{};
    ImVec2 rubber_band_origin{};
    /// An edit is in flight, so its end is worth recording as one undo entry.
    bool is_editing{};
    /// A color is shown as a gradient strip; this switches to its four channel curves.
    bool show_curves{};
    /// Marker being dragged along the gradient strip, or -1.
    int dragged_marker{-1};
};

auto get_states() -> std::unordered_map<ImGuiID, editor_state>&
{
    static std::unordered_map<ImGuiID, editor_state> states;
    return states;
}

// ---------------------------------------------------------------------------------
// Sampling and framing

void sample_channels(const curve_view& view, float position, std::vector<float>& out_values)
{
    out_values.resize(view.channels.size());
    for(size_t index = 0; index < view.channels.size(); ++index)
    {
        out_values[index] = view.channels[index].channel->sample(position);
    }
}

auto sample_color(const curve_view& view, float position) -> ImVec4
{
    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
    const size_t count = std::min<size_t>(view.channels.size(), 4);
    for(size_t index = 0; index < count; ++index)
    {
        (&color.x)[index] = view.channels[index].channel->sample(position);
    }
    return color;
}

/// Lowest and highest key position and value across every channel.
struct curve_extent
{
    bool has_keys{};
    float position_min{};
    float position_max{};
    float value_min{};
    float value_max{};
};

auto measure(const curve_view& view) -> curve_extent
{
    curve_extent extent;
    for(const auto& channel_view : view.channels)
    {
        for(const auto& key : channel_view.channel->get_keys())
        {
            if(!extent.has_keys)
            {
                extent.position_min = extent.position_max = key.position;
                extent.value_min = extent.value_max = key.value;
                extent.has_keys = true;
                continue;
            }
            extent.position_min = std::min(extent.position_min, key.position);
            extent.position_max = std::max(extent.position_max, key.position);
            extent.value_min = std::min(extent.value_min, key.value);
            extent.value_max = std::max(extent.value_max, key.value);
        }
    }
    return extent;
}

void frame_view(editor_state& state, const curve_view& view)
{
    const curve_extent extent = measure(view);
    if(!extent.has_keys)
    {
        state.domain_min = 0.0f;
        state.domain_max = 1.0f;
        state.value_min = view.has_value_limits ? view.value_min : 0.0f;
        state.value_max = view.has_value_limits ? view.value_max : 1.0f;
    }
    else
    {
        const float position_margin = std::max((extent.position_max - extent.position_min) * CURVE_FRAME_MARGIN,
                                               CURVE_MIN_VIEW_EXTENT);
        const float value_margin =
            std::max((extent.value_max - extent.value_min) * CURVE_FRAME_MARGIN, CURVE_MIN_VIEW_EXTENT);
        state.domain_min = extent.position_min - position_margin;
        state.domain_max = extent.position_max + position_margin;
        state.value_min = extent.value_min - value_margin;
        state.value_max = extent.value_max + value_margin;
    }
    if(state.domain_max - state.domain_min < CURVE_MIN_VIEW_EXTENT)
    {
        state.domain_max = state.domain_min + 1.0f;
    }
    if(state.value_max - state.value_min < CURVE_MIN_VIEW_EXTENT)
    {
        state.value_max = state.value_min + 1.0f;
    }
    state.has_view = true;
}

/// The step between grid lines: the 1, 2 or 5 times a power of ten that lands nearest the wanted
/// spacing, so the labels stay readable numbers at any zoom.
auto get_grid_step(float range, float wanted_divisions) -> float
{
    const float rough = range / std::max(wanted_divisions, 1.0f);
    if(rough <= 0.0f)
    {
        return 1.0f;
    }
    const float magnitude = std::pow(10.0f, std::floor(std::log10(rough)));
    const float normalized = rough / magnitude;
    if(normalized <= 1.0f)
    {
        return magnitude;
    }
    if(normalized <= 2.0f)
    {
        return 2.0f * magnitude;
    }
    if(normalized <= 5.0f)
    {
        return 5.0f * magnitude;
    }
    return 10.0f * magnitude;
}

/// Maps between curve space and the canvas.
struct projection
{
    ImRect plot;
    float domain_min{};
    float domain_max{1.0f};
    float value_min{};
    float value_max{1.0f};

    auto to_screen(float position, float value) const -> ImVec2
    {
        const float x = (position - domain_min) / (domain_max - domain_min);
        const float y = (value - value_min) / (value_max - value_min);
        return ImVec2(plot.Min.x + x * plot.GetWidth(), plot.Max.y - y * plot.GetHeight());
    }

    auto to_position(float screen_x) const -> float
    {
        return domain_min + (screen_x - plot.Min.x) / plot.GetWidth() * (domain_max - domain_min);
    }

    auto to_value(float screen_y) const -> float
    {
        return value_min + (plot.Max.y - screen_y) / plot.GetHeight() * (value_max - value_min);
    }
};

auto make_projection(const editor_state& state, const ImRect& plot) -> projection
{
    projection proj;
    proj.plot = plot;
    proj.domain_min = state.domain_min;
    proj.domain_max = state.domain_max;
    proj.value_min = state.value_min;
    proj.value_max = state.value_max;
    return proj;
}

// ---------------------------------------------------------------------------------
// Preview

/// The curve as one strip: a gradient for a color, the plotted lines for anything else.
void draw_preview_content(ImDrawList* draw_list, const ImRect& bounds, const curve_view& view)
{
    const curve_extent extent = measure(view);
    if(!extent.has_keys)
    {
        return;
    }

    if(view.is_color)
    {
        const float width = bounds.GetWidth() / float(CURVE_STRIP_SAMPLES);
        for(int sample = 0; sample < CURVE_STRIP_SAMPLES; ++sample)
        {
            const float alpha_start = float(sample) / float(CURVE_STRIP_SAMPLES);
            const float alpha_end = float(sample + 1) / float(CURVE_STRIP_SAMPLES);
            const float span = extent.position_max - extent.position_min;
            const ImU32 start = ImGui::ColorConvertFloat4ToU32(
                sample_color(view, extent.position_min + span * alpha_start));
            const ImU32 end =
                ImGui::ColorConvertFloat4ToU32(sample_color(view, extent.position_min + span * alpha_end));
            const ImVec2 band_min(bounds.Min.x + width * float(sample), bounds.Min.y);
            const ImVec2 band_max(band_min.x + width + 1.0f, bounds.Max.y);
            draw_list->AddRectFilledMultiColor(band_min, band_max, start, end, end, start);
        }
        return;
    }

    projection proj;
    proj.plot = bounds;
    proj.domain_min = extent.position_min;
    proj.domain_max = extent.position_max;
    proj.value_min = extent.value_min;
    proj.value_max = extent.value_max;
    if(proj.domain_max - proj.domain_min < CURVE_MIN_VIEW_EXTENT)
    {
        proj.domain_max = proj.domain_min + 1.0f;
    }
    // A flat curve plots down the middle of the strip rather than along its floor.
    if(proj.value_max - proj.value_min < CURVE_MIN_VIEW_EXTENT)
    {
        proj.value_min -= 0.5f;
        proj.value_max += 0.5f;
    }
    // Room above the highest value and below the lowest, so a line that reaches either end is
    // drawn inside the strip instead of along its edge where the clip rect cuts it in half.
    const float value_margin = (proj.value_max - proj.value_min) * CURVE_PREVIEW_MARGIN;
    proj.value_min -= value_margin;
    proj.value_max += value_margin;

    const int samples = std::max(int(bounds.GetWidth() / CURVE_PIXELS_PER_SAMPLE), 2);
    std::vector<ImVec2> points;
    points.reserve(size_t(samples) + 1);
    for(const auto& channel_view : view.channels)
    {
        if(channel_view.channel->is_empty())
        {
            continue;
        }
        points.clear();
        for(int sample = 0; sample <= samples; ++sample)
        {
            const float alpha = float(sample) / float(samples);
            const float position = proj.domain_min + (proj.domain_max - proj.domain_min) * alpha;
            points.push_back(proj.to_screen(position, channel_view.channel->sample(position)));
        }
        draw_list->AddPolyline(points.data(),
                               int(points.size()),
                               channel_view.color,
                               ImDrawFlags_None,
                               CURVE_HAIRLINE_THICKNESS);
    }
}

void draw_preview_frame(ImDrawList* draw_list, const ImRect& bounds, const curve_view& view, bool is_hovered)
{
    const float rounding = to_pixels(CURVE_PREVIEW_ROUNDING_EM);
    if(view.is_color)
    {
        // Under a color the checkerboard says which part of the strip is transparent.
        ImGui::RenderColorRectWithAlphaCheckerboard(draw_list,
                                                    bounds.Min,
                                                    bounds.Max,
                                                    0,
                                                    bounds.GetHeight() * 0.5f,
                                                    ImVec2(0.0f, 0.0f),
                                                    rounding);
    }
    else
    {
        draw_list->AddRectFilled(bounds.Min, bounds.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    }

    draw_list->PushClipRect(bounds.Min, bounds.Max, true);
    draw_preview_content(draw_list, bounds, view);
    draw_list->PopClipRect();

    const ImU32 border = is_hovered ? get_accent_color() : ImGui::GetColorU32(ImGuiCol_Border);
    draw_list->AddRect(bounds.Min, bounds.Max, border, rounding, 0, CURVE_HAIRLINE_THICKNESS);
}

// ---------------------------------------------------------------------------------
// Canvas

void draw_grid(ImDrawList* draw_list, const projection& proj)
{
    char label[32];
    const float wanted_x = proj.plot.GetWidth() / to_pixels(CURVE_GRID_SPACING_EM);
    const float wanted_y = proj.plot.GetHeight() / to_pixels(CURVE_GRID_SPACING_EM);
    const float step_x = get_grid_step(proj.domain_max - proj.domain_min, wanted_x);
    const float step_y = get_grid_step(proj.value_max - proj.value_min, wanted_y);
    const float inset = to_pixels(CURVE_LABEL_INSET_EM);
    const ImU32 label_color = get_muted_color();

    for(float position = std::ceil(proj.domain_min / step_x) * step_x; position <= proj.domain_max; position += step_x)
    {
        const float x = proj.to_screen(position, 0.0f).x;
        const bool is_axis = std::fabs(position) < step_x * 0.5f;
        draw_list->AddLine(ImVec2(x, proj.plot.Min.y),
                           ImVec2(x, proj.plot.Max.y),
                           is_axis ? CURVE_AXIS_COLOR : CURVE_GRID_COLOR,
                           CURVE_HAIRLINE_THICKNESS);
        std::snprintf(label, sizeof(label), "%.3g", position);
        draw_list->AddText(ImVec2(x + inset, proj.plot.Max.y - ImGui::GetTextLineHeight()), label_color, label);
    }

    for(float value = std::ceil(proj.value_min / step_y) * step_y; value <= proj.value_max; value += step_y)
    {
        const float y = proj.to_screen(0.0f, value).y;
        const bool is_axis = std::fabs(value) < step_y * 0.5f;
        draw_list->AddLine(ImVec2(proj.plot.Min.x, y),
                           ImVec2(proj.plot.Max.x, y),
                           is_axis ? CURVE_AXIS_COLOR : CURVE_GRID_COLOR,
                           CURVE_HAIRLINE_THICKNESS);
        std::snprintf(label, sizeof(label), "%.3g", value);
        draw_list->AddText(ImVec2(proj.plot.Min.x + inset, y - ImGui::GetTextLineHeight()), label_color, label);
    }
}

/// Whether a key's incoming tangent shapes anything: only when a segment arrives at it and that
/// segment is smooth. The first key of a channel never has one.
auto has_in_tangent(const math::curve_channel& channel, size_t index) -> bool
{
    const auto& keys = channel.get_keys();
    return index > 0 && index < keys.size() &&
           keys[index - 1].interpolation == math::curve_interpolation::smooth;
}

/// The same for the outgoing tangent, which the last key never has.
auto has_out_tangent(const math::curve_channel& channel, size_t index) -> bool
{
    const auto& keys = channel.get_keys();
    return index + 1 < keys.size() && keys[index].interpolation == math::curve_interpolation::smooth;
}

auto is_channel_visible(const editor_state& state, size_t channel) -> bool
{
    return channel >= state.hidden_channels.size() || !state.hidden_channels[channel];
}

auto is_selected(const editor_state& state, const key_ref& reference) -> bool
{
    return std::find(state.selection.begin(), state.selection.end(), reference) != state.selection.end();
}

/// Where a key's tangent handle sits: a fixed distance along the slope, so a steep tangent does
/// not fling its handle off the canvas.
auto get_tangent_handle(const projection& proj, const math::curve_key& key, float tangent, bool is_outgoing)
    -> ImVec2
{
    const ImVec2 anchor = proj.to_screen(key.position, key.value);
    const float length = to_pixels(CURVE_TANGENT_LENGTH_EM);
    // The slope is in curve space; converting it through the projection gives the screen slope.
    const float x_scale = proj.plot.GetWidth() / (proj.domain_max - proj.domain_min);
    const float y_scale = proj.plot.GetHeight() / (proj.value_max - proj.value_min);
    ImVec2 direction(x_scale, -tangent * y_scale);
    const float magnitude = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if(magnitude > 0.0f)
    {
        direction.x /= magnitude;
        direction.y /= magnitude;
    }
    const float sign = is_outgoing ? 1.0f : -1.0f;
    return ImVec2(anchor.x + direction.x * length * sign, anchor.y + direction.y * length * sign);
}

/// The tangent a handle dragged to @p screen means, back in curve space.
auto get_tangent_from_handle(const projection& proj, const math::curve_key& key, const ImVec2& screen, bool is_outgoing)
    -> float
{
    const ImVec2 anchor = proj.to_screen(key.position, key.value);
    float dx = screen.x - anchor.x;
    const float dy = screen.y - anchor.y;
    if(!is_outgoing)
    {
        dx = -dx;
    }
    if(std::fabs(dx) < 1.0f)
    {
        dx = dx < 0.0f ? -1.0f : 1.0f;
    }
    const float x_scale = proj.plot.GetWidth() / (proj.domain_max - proj.domain_min);
    const float y_scale = proj.plot.GetHeight() / (proj.value_max - proj.value_min);
    const float slope = (is_outgoing ? -dy : dy) / dx;
    return slope * x_scale / y_scale;
}

void draw_curves(ImDrawList* draw_list, const projection& proj, const curve_view& view, const editor_state& state)
{
    const int samples = std::max(int(proj.plot.GetWidth() / CURVE_PIXELS_PER_SAMPLE), 2);
    std::vector<ImVec2> points;
    points.reserve(size_t(samples) + 1);

    for(size_t index = 0; index < view.channels.size(); ++index)
    {
        const auto& channel_view = view.channels[index];
        if(channel_view.channel->is_empty())
        {
            continue;
        }
        ImVec4 color = ImGui::ColorConvertU32ToFloat4(channel_view.color);
        if(!is_channel_visible(state, index))
        {
            color.w = CURVE_HIDDEN_CHANNEL_ALPHA;
        }
        const ImU32 line_color = ImGui::ColorConvertFloat4ToU32(color);

        points.clear();
        for(int sample = 0; sample <= samples; ++sample)
        {
            const float alpha = float(sample) / float(samples);
            const float position = proj.domain_min + (proj.domain_max - proj.domain_min) * alpha;
            points.push_back(proj.to_screen(position, channel_view.channel->sample(position)));
        }
        draw_list->AddPolyline(points.data(), int(points.size()), line_color, ImDrawFlags_None, CURVE_LINE_THICKNESS);
    }
}

void draw_keys(ImDrawList* draw_list,
               const projection& proj,
               const curve_view& view,
               const editor_state& state,
               const key_ref* hovered)
{
    const float radius = to_pixels(CURVE_KEY_RADIUS_EM);
    const float active_radius = to_pixels(CURVE_KEY_ACTIVE_RADIUS_EM);
    const float tangent_radius = to_pixels(CURVE_TANGENT_RADIUS_EM);

    for(size_t channel = 0; channel < view.channels.size(); ++channel)
    {
        if(!is_channel_visible(state, channel))
        {
            continue;
        }
        const auto& channel_view = view.channels[channel];
        const auto& keys = channel_view.channel->get_keys();
        for(size_t index = 0; index < keys.size(); ++index)
        {
            const math::curve_key& key = keys[index];
            const key_ref reference{channel, index};
            const bool selected = is_selected(state, reference);
            const bool is_hovered = hovered != nullptr && *hovered == reference;
            const ImVec2 anchor = proj.to_screen(key.position, key.value);

            if(selected && has_in_tangent(*channel_view.channel, index))
            {
                const ImVec2 handle = get_tangent_handle(proj, key, key.in_tangent, false);
                draw_list->AddLine(anchor, handle, get_muted_color(), CURVE_HAIRLINE_THICKNESS);
                draw_list->AddCircleFilled(handle, tangent_radius, ImGui::GetColorU32(ImGuiCol_Text));
            }
            if(selected && has_out_tangent(*channel_view.channel, index))
            {
                const ImVec2 handle = get_tangent_handle(proj, key, key.out_tangent, true);
                draw_list->AddLine(anchor, handle, get_muted_color(), CURVE_HAIRLINE_THICKNESS);
                draw_list->AddCircleFilled(handle, tangent_radius, ImGui::GetColorU32(ImGuiCol_Text));
            }

            const float drawn_radius = (selected || is_hovered) ? active_radius : radius;
            draw_list->AddCircleFilled(anchor, drawn_radius, channel_view.color);
            if(selected)
            {
                draw_list->AddCircle(anchor,
                                     drawn_radius + CURVE_SELECTION_THICKNESS,
                                     ImGui::GetColorU32(ImGuiCol_Text),
                                     0,
                                     CURVE_SELECTION_THICKNESS);
            }
        }
    }
}

/// The key, or tangent handle, nearest the pointer within picking distance.
struct pick_result
{
    bool has_key{};
    key_ref key{};
    bool is_tangent_in{};
    bool is_tangent_out{};
};

auto pick(const projection& proj, const curve_view& view, const editor_state& state, const ImVec2& mouse)
    -> pick_result
{
    pick_result result;
    const float pick_radius = to_pixels(CURVE_KEY_PICK_RADIUS_EM);
    float nearest = pick_radius * pick_radius;

    const auto consider = [&](const ImVec2& point, const key_ref& reference, bool tangent_in, bool tangent_out)
    {
        const float dx = point.x - mouse.x;
        const float dy = point.y - mouse.y;
        const float distance = dx * dx + dy * dy;
        if(distance > nearest)
        {
            return;
        }
        nearest = distance;
        result.has_key = true;
        result.key = reference;
        result.is_tangent_in = tangent_in;
        result.is_tangent_out = tangent_out;
    };

    for(size_t channel = 0; channel < view.channels.size(); ++channel)
    {
        if(!is_channel_visible(state, channel))
        {
            continue;
        }
        const auto& keys = view.channels[channel].channel->get_keys();
        for(size_t index = 0; index < keys.size(); ++index)
        {
            const math::curve_key& key = keys[index];
            const key_ref reference{channel, index};
            // Tangent handles are picked first: they sit away from the key, and only a selected
            // smooth key has them.
            if(is_selected(state, reference))
            {
                const auto& channel_keys = *view.channels[channel].channel;
                if(has_in_tangent(channel_keys, index))
                {
                    consider(get_tangent_handle(proj, key, key.in_tangent, false), reference, true, false);
                }
                if(has_out_tangent(channel_keys, index))
                {
                    consider(get_tangent_handle(proj, key, key.out_tangent, true), reference, false, true);
                }
            }
            consider(proj.to_screen(key.position, key.value), reference, false, false);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------
// Editing

void refresh_channel(const curve_view& view, size_t channel)
{
    view.channels[channel].channel->refresh_automatic_tangents();
}

void clamp_key_value(const curve_view& view, math::curve_key& key)
{
    if(view.has_value_limits)
    {
        key.value = std::clamp(key.value, view.value_min, view.value_max);
    }
}

/// On a linked curve one key exists on every channel at the same index, so picking one picks all
/// of them. Without this an interpolation change would reach one channel of a key and leave the
/// rest of it alone, which on a color reads as nothing happening at all.
auto expand_selection(const curve_view& view, const key_ref& reference) -> std::vector<key_ref>
{
    const bool linked = view.is_linked != nullptr && *view.is_linked;
    if(!linked)
    {
        return {reference};
    }
    std::vector<key_ref> expanded;
    for(size_t channel = 0; channel < view.channels.size(); ++channel)
    {
        if(reference.key < view.channels[channel].channel->get_key_count())
        {
            expanded.push_back({channel, reference.key});
        }
    }
    return expanded;
}

void select(editor_state& state, const curve_view& view, const key_ref& reference, bool additive)
{
    const auto expanded = expand_selection(view, reference);
    if(!additive)
    {
        state.selection = expanded;
        return;
    }
    // Ctrl over an already selected key takes the whole key out again, not one of its channels.
    if(is_selected(state, reference))
    {
        for(const key_ref& entry : expanded)
        {
            const auto at = std::find(state.selection.begin(), state.selection.end(), entry);
            if(at != state.selection.end())
            {
                state.selection.erase(at);
            }
        }
        return;
    }
    for(const key_ref& entry : expanded)
    {
        if(!is_selected(state, entry))
        {
            state.selection.push_back(entry);
        }
    }
}

/// Moves every selected key by a delta in curve space.
///
/// The selection already names every channel a key lives on, so the position moves once per entry.
/// The value only follows on the channel the pointer grabbed: on a linked curve the channels share
/// a position but keep their own values.
void move_selection(const curve_view& view, editor_state& state, float position_delta, float value_delta)
{
    const bool linked = view.is_linked != nullptr && *view.is_linked;
    std::vector<bool> touched(view.channels.size(), false);

    for(const key_ref& reference : state.selection)
    {
        auto* track = view.channels[reference.channel].channel;
        if(reference.key >= track->get_key_count())
        {
            continue;
        }
        math::curve_key key = track->get_key(reference.key);
        key.position += position_delta;
        if(!linked || reference.channel == state.drag_anchor.channel)
        {
            key.value += value_delta;
            clamp_key_value(view, key);
        }
        track->set_key(reference.key, key);
        touched[reference.channel] = true;
    }

    for(size_t channel = 0; channel < touched.size(); ++channel)
    {
        if(touched[channel])
        {
            refresh_channel(view, channel);
        }
    }
}

/**
 * @brief Adds a key at @p position, and reports it as the selection.
 *
 * A linked curve takes a key on every channel. Only the channel the pointer was nearest takes
 * @p value; the rest keep the value they already had there, so adding a key to a two-ended range
 * moves the end that was clicked and leaves the other where it was.
 *
 * @p keep_values makes every channel keep its value, for the gradient strip, where the pointer
 * says nothing about a value and a new key must not change the color the strip shows.
 */
void add_key_at(const curve_view& view, editor_state& state, float position, float value, bool keep_values)
{
    const bool linked = view.is_linked != nullptr && *view.is_linked;
    state.selection.clear();

    // The channel whose line passes nearest the click is the one the click was aimed at.
    size_t nearest = 0;
    float nearest_distance = std::numeric_limits<float>::max();
    for(size_t channel = 0; channel < view.channels.size(); ++channel)
    {
        if(!is_channel_visible(state, channel))
        {
            continue;
        }
        const float distance = std::fabs(view.channels[channel].channel->sample(position) - value);
        if(distance < nearest_distance)
        {
            nearest_distance = distance;
            nearest = channel;
        }
    }

    if(linked)
    {
        for(size_t channel = 0; channel < view.channels.size(); ++channel)
        {
            auto* track = view.channels[channel].channel;
            const bool takes_value = !keep_values && channel == nearest;
            math::curve_key key;
            key.value = takes_value ? value : track->sample(position);
            if(takes_value)
            {
                clamp_key_value(view, key);
            }
            const size_t index = track->add_key(position, key.value);
            refresh_channel(view, channel);
            state.selection.push_back({channel, index});
        }
        return;
    }

    auto* track = view.channels[nearest].channel;
    math::curve_key key;
    key.value = keep_values ? track->sample(position) : value;
    if(!keep_values)
    {
        clamp_key_value(view, key);
    }
    const size_t index = track->add_key(position, key.value);
    refresh_channel(view, nearest);
    state.selection.push_back({nearest, index});
}

void remove_selection(const curve_view& view, editor_state& state)
{
    // Grouped per channel and taken highest index first, so removing one key does not shift the
    // index of the next one still to go.
    std::vector<std::vector<size_t>> per_channel(view.channels.size());
    for(const key_ref& reference : state.selection)
    {
        per_channel[reference.channel].push_back(reference.key);
    }
    for(size_t channel = 0; channel < per_channel.size(); ++channel)
    {
        auto& indices = per_channel[channel];
        if(indices.empty())
        {
            continue;
        }
        std::sort(indices.begin(), indices.end(), std::greater<size_t>{});
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for(size_t index : indices)
        {
            view.channels[channel].channel->remove_key(index);
        }
        refresh_channel(view, channel);
    }
    state.selection.clear();
}

/// Applies @p apply to every selected key and reports whether anything changed.
template<typename Apply>
auto edit_selection(const curve_view& view, editor_state& state, Apply&& apply) -> bool
{
    bool changed = false;
    std::vector<bool> touched(view.channels.size(), false);
    for(const key_ref& reference : state.selection)
    {
        auto* track = view.channels[reference.channel].channel;
        if(reference.key >= track->get_key_count())
        {
            continue;
        }
        math::curve_key key = track->get_key(reference.key);
        const math::curve_key before = key;
        apply(key);
        clamp_key_value(view, key);
        if(!(key == before))
        {
            track->set_key(reference.key, key);
            touched[reference.channel] = true;
            changed = true;
        }
    }
    for(size_t channel = 0; channel < touched.size(); ++channel)
    {
        if(touched[channel])
        {
            refresh_channel(view, channel);
        }
    }
    return changed;
}

auto handle_canvas_input(const curve_view& view,
                         editor_state& state,
                         const projection& proj,
                         bool is_hovered,
                         const pick_result& picked) -> edit_result
{
    edit_result result;
    ImGuiIO& io = ImGui::GetIO();

    if(is_hovered && io.MouseWheel != 0.0f)
    {
        const float zoom = std::pow(CURVE_ZOOM_PER_WHEEL_NOTCH, -io.MouseWheel);
        const float at_position = proj.to_position(io.MousePos.x);
        const float at_value = proj.to_value(io.MousePos.y);
        if(!io.KeyShift)
        {
            state.domain_min = at_position + (state.domain_min - at_position) * zoom;
            state.domain_max = at_position + (state.domain_max - at_position) * zoom;
        }
        if(!io.KeyCtrl)
        {
            state.value_min = at_value + (state.value_min - at_value) * zoom;
            state.value_max = at_value + (state.value_max - at_value) * zoom;
        }
    }

    if(is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        state.drag = drag_mode::pan;
    }

    if(is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !picked.has_key)
    {
        add_key_at(view, state, proj.to_position(io.MousePos.x), proj.to_value(io.MousePos.y), false);
        state.is_editing = true;
        result.changed = true;
        result.edit_finished = true;
        state.is_editing = false;
        state.drag = drag_mode::none;
        return result;
    }

    if(is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if(picked.has_key)
        {
            if(io.KeyCtrl)
            {
                select(state, view, picked.key, true);
            }
            else
            {
                if(!is_selected(state, picked.key))
                {
                    select(state, view, picked.key, false);
                }
                state.drag_anchor = picked.key;
                state.drag = picked.is_tangent_in  ? drag_mode::move_tangent_in
                             : picked.is_tangent_out ? drag_mode::move_tangent_out
                                                     : drag_mode::move_keys;
                state.is_editing = true;
            }
        }
        else
        {
            if(!io.KeyCtrl)
            {
                state.selection.clear();
            }
            state.drag = drag_mode::rubber_band;
            state.rubber_band_origin = io.MousePos;
        }
    }

    switch(state.drag)
    {
        case drag_mode::pan:
        {
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                state.drag = drag_mode::none;
                break;
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            const float position_delta = io.MouseDelta.x / proj.plot.GetWidth() * (proj.domain_max - proj.domain_min);
            const float value_delta = io.MouseDelta.y / proj.plot.GetHeight() * (proj.value_max - proj.value_min);
            state.domain_min -= position_delta;
            state.domain_max -= position_delta;
            state.value_min += value_delta;
            state.value_max += value_delta;
            break;
        }
        case drag_mode::move_keys:
        {
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.drag = drag_mode::none;
                result.edit_finished = state.is_editing;
                state.is_editing = false;
                break;
            }
            if(io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
            {
                const float position_delta =
                    io.MouseDelta.x / proj.plot.GetWidth() * (proj.domain_max - proj.domain_min);
                const float value_delta = -io.MouseDelta.y / proj.plot.GetHeight() * (proj.value_max - proj.value_min);
                move_selection(view, state, position_delta, value_delta);
                result.changed = true;
            }
            break;
        }
        case drag_mode::move_tangent_in:
        case drag_mode::move_tangent_out:
        {
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.drag = drag_mode::none;
                result.edit_finished = state.is_editing;
                state.is_editing = false;
                break;
            }
            const bool is_outgoing = state.drag == drag_mode::move_tangent_out;
            auto* track = view.channels[state.drag_anchor.channel].channel;
            if(state.drag_anchor.key < track->get_key_count())
            {
                math::curve_key key = track->get_key(state.drag_anchor.key);
                const float tangent = get_tangent_from_handle(proj, key, io.MousePos, is_outgoing);
                // Dragging a handle is an explicit statement about the shape, so the key stops
                // following its neighbours.
                key.tangent_mode = math::curve_tangent_mode::free;
                (is_outgoing ? key.out_tangent : key.in_tangent) = tangent;
                track->set_key(state.drag_anchor.key, key);
                result.changed = true;
            }
            break;
        }
        case drag_mode::rubber_band:
        {
            const float dx = io.MousePos.x - state.rubber_band_origin.x;
            const float dy = io.MousePos.y - state.rubber_band_origin.y;
            if(dx * dx + dy * dy >= CURVE_DRAG_THRESHOLD * CURVE_DRAG_THRESHOLD)
            {
                ImRect band(state.rubber_band_origin, io.MousePos);
                if(band.Min.x > band.Max.x)
                {
                    std::swap(band.Min.x, band.Max.x);
                }
                if(band.Min.y > band.Max.y)
                {
                    std::swap(band.Min.y, band.Max.y);
                }
                state.selection.clear();
                for(size_t channel = 0; channel < view.channels.size(); ++channel)
                {
                    if(!is_channel_visible(state, channel))
                    {
                        continue;
                    }
                    const auto& keys = view.channels[channel].channel->get_keys();
                    for(size_t index = 0; index < keys.size(); ++index)
                    {
                        if(band.Contains(proj.to_screen(keys[index].position, keys[index].value)))
                        {
                            select(state, view, {channel, index}, true);
                        }
                    }
                }
            }
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                state.drag = drag_mode::none;
            }
            break;
        }
        case drag_mode::none:
            break;
    }

    if(is_hovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && !state.selection.empty())
    {
        remove_selection(view, state);
        result.changed = true;
        result.edit_finished = true;
    }
    if(is_hovered && ImGui::IsKeyPressed(ImGuiKey_F))
    {
        frame_view(state, view);
    }

    return result;
}

// ---------------------------------------------------------------------------------
// Chrome

/// A channel chip: the channel's color, its name, and whether it is shown.
auto draw_channel_chip(const char* name, ImU32 color, bool is_visible) -> bool
{
    const ImVec2 label_size = ImGui::CalcTextSize(name);
    const float padding = to_pixels(CURVE_CHIP_PADDING_EM);
    const ImVec2 size(label_size.x + padding * 3.0f + ImGui::GetFontSize() * 0.5f, ImGui::GetFrameHeight());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect bounds(min, ImVec2(min.x + size.x, min.y + size.y));

    ImGui::InvisibleButton(name, size);
    const bool is_pressed = ImGui::IsItemClicked();
    const bool is_hovered = ImGui::IsItemHovered();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    draw_list->AddRectFilled(bounds.Min,
                             bounds.Max,
                             is_hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : CURVE_CHIP_OFF_COLOR,
                             rounding);

    const float dot_radius = ImGui::GetFontSize() * 0.22f;
    const ImVec2 dot(bounds.Min.x + padding + dot_radius, (bounds.Min.y + bounds.Max.y) * 0.5f);
    if(is_visible)
    {
        draw_list->AddCircleFilled(dot, dot_radius, color);
    }
    else
    {
        draw_list->AddCircle(dot, dot_radius, color, 0, CURVE_HAIRLINE_THICKNESS);
    }
    draw_list->AddText(ImVec2(dot.x + dot_radius + padding, bounds.Min.y + ImGui::GetStyle().FramePadding.y),
                       is_visible ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color(),
                       name);

    ImGui::SetItemTooltipEx("%s", is_visible ? "Hide this channel" : "Show this channel");
    return is_pressed;
}

auto draw_toolbar(const curve_view& view, editor_state& state) -> edit_result
{
    edit_result result;
    const float gap = to_pixels(CURVE_TOOLBAR_GAP_EM);
    const bool shows_curves = !view.is_color || state.show_curves;

    if(view.is_color)
    {
        if(ImGui::Checkbox("Curves", &state.show_curves))
        {
            state.selection.clear();
        }
        ImGui::SetItemTooltipEx("%s",
                                "Shape the red, green, blue and alpha channels one by one.\n"
                                "Off shows the gradient the channels add up to.");
        ImGui::SameLine(0.0f, gap * 2.0f);
    }

    for(size_t channel = 0; shows_curves && channel < view.channels.size(); ++channel)
    {
        if(channel > 0 || view.is_color)
        {
            ImGui::SameLine(0.0f, gap);
        }
        if(draw_channel_chip(view.channels[channel].name, view.channels[channel].color, is_channel_visible(state, channel)))
        {
            state.hidden_channels.resize(view.channels.size(), false);
            state.hidden_channels[channel] = !state.hidden_channels[channel];
        }
    }

    if(view.is_linked != nullptr && view.channels.size() > 1)
    {
        if(shows_curves || view.is_color)
        {
            ImGui::SameLine(0.0f, gap * 2.0f);
        }
        bool linked = *view.is_linked;
        if(ImGui::Checkbox("Linked", &linked))
        {
            *view.is_linked = linked;
            result.changed = true;
            result.edit_finished = true;
        }
        ImGui::SetItemTooltipEx("%s",
                                "Keeps a key at the same position on every channel.\n"
                                "Switch it off to key one channel on its own.");
    }

    // Shapes the whole curve at once. The footer's combo shapes only what is selected, which is
    // no help when every key needs the same shape.
    ImGui::SameLine(0.0f, gap * 2.0f);
    ImGui::SetNextItemWidth(to_pixels(CURVE_FIELD_WIDTH_EM) * 1.7f);
    int shared = -1;
    for(const auto& channel_view : view.channels)
    {
        for(const auto& key : channel_view.channel->get_keys())
        {
            const int value = int(key.interpolation);
            shared = (shared < 0 || shared == value) ? value : -1;
            if(shared < 0)
            {
                break;
            }
        }
    }
    int all_interpolation = shared;
    if(ImGui::Combo("##curve_all_interpolation",
                    &all_interpolation,
                    CURVE_INTERPOLATION_NAMES,
                    IM_ARRAYSIZE(CURVE_INTERPOLATION_NAMES)))
    {
        for(size_t channel = 0; channel < view.channels.size(); ++channel)
        {
            auto keys = view.channels[channel].channel->get_keys();
            for(auto& key : keys)
            {
                key.interpolation = math::curve_interpolation(all_interpolation);
            }
            view.channels[channel].channel->set_keys(std::move(keys));
            refresh_channel(view, channel);
        }
        result.changed = true;
        result.edit_finished = true;
    }
    ImGui::SetItemTooltipEx("%s", "Shape of every key in the curve. Blank while the keys differ.");

    // Right-aligned actions.
    const float button_width = ImGui::GetFrameHeight();
    const float actions_width = button_width * 3.0f + gap * 2.0f;
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::GetCurrentWindow()->DC.CursorPos.x = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() -
                                                ImGui::GetStyle().WindowPadding.x - actions_width;

    const ImVec2 action_min = ImGui::GetCursorScreenPos();
    const auto action_rect = [&](int index)
    {
        const float x = action_min.x + (button_width + gap) * float(index);
        return ImRect(ImVec2(x, action_min.y), ImVec2(x + button_width, action_min.y + button_width));
    };
    ImGui::Dummy(ImVec2(actions_width, button_width));

    if(container_widgets::draw_icon_button({"##curve_frame", ICON_MDI_FIT_TO_PAGE_OUTLINE, "Frame the keys (F)"},
                                           action_rect(0)))
    {
        frame_view(state, view);
    }
    if(container_widgets::draw_icon_button({"##curve_reverse", ICON_MDI_SWAP_HORIZONTAL, "Reverse the curve"},
                                           action_rect(1)))
    {
        for(const auto& channel_view : view.channels)
        {
            const frange_t domain = channel_view.channel->get_domain();
            channel_view.channel->reverse(domain.min, domain.max);
        }
        result.changed = true;
        result.edit_finished = true;
    }
    if(container_widgets::draw_icon_button({"##curve_clear", ICON_MDI_DELETE_SWEEP, "Remove every key", true},
                                           action_rect(2)))
    {
        for(const auto& channel_view : view.channels)
        {
            channel_view.channel->clear();
        }
        state.selection.clear();
        result.changed = true;
        result.edit_finished = true;
    }
    return result;
}

/// The interpolation and tangent mode of the selection, and the numbers behind the selected key.
auto draw_footer(const curve_view& view, editor_state& state, const projection& proj, bool is_canvas_hovered)
    -> edit_result
{
    edit_result result;
    const float field_width = to_pixels(CURVE_FIELD_WIDTH_EM);
    const float step = view.value_step > 0.0f ? view.value_step : CURVE_DEFAULT_STEP;

    if(state.selection.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
        ImGui::TextUnformatted("Double click to add a key. Drag to move, right drag to pan, wheel to zoom.");
        ImGui::PopStyleColor();
    }
    else
    {
        const key_ref& primary = state.selection.front();
        const auto* track = view.channels[primary.channel].channel;
        const math::curve_key key = track->get_key(primary.key);

        ImGui::SetNextItemWidth(field_width * 1.6f);
        int interpolation = int(key.interpolation);
        if(ImGui::Combo("##curve_interpolation", &interpolation, CURVE_INTERPOLATION_NAMES, IM_ARRAYSIZE(CURVE_INTERPOLATION_NAMES)))
        {
            result.changed |= edit_selection(view,
                                            state,
                                            [interpolation](math::curve_key& edited)
                                            { edited.interpolation = math::curve_interpolation(interpolation); });
            result.edit_finished = result.changed;
        }
        ImGui::SetItemTooltipEx("%s", "How the segment leaving the selected key reaches the next one");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(field_width * 1.2f);
        int tangent_mode = int(key.tangent_mode);
        if(ImGui::Combo("##curve_tangents", &tangent_mode, CURVE_TANGENT_NAMES, IM_ARRAYSIZE(CURVE_TANGENT_NAMES)))
        {
            result.changed |= edit_selection(view,
                                             state,
                                             [tangent_mode](math::curve_key& edited)
                                             { edited.tangent_mode = math::curve_tangent_mode(tangent_mode); });
            result.edit_finished = result.changed;
        }
        ImGui::SetItemTooltipEx("%s",
                                "Auto follows the neighbouring keys, Flat arrives and leaves level,\n"
                                "Free keeps the tangents as they were dragged");

        float position = key.position;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field_width);
        if(ImGui::DragFloat("##curve_position", &position, step, 0.0f, 0.0f, "P %.3f"))
        {
            const float delta = position - key.position;
            move_selection(view, state, delta, 0.0f);
            result.changed = true;
        }
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltipEx("%s", "Position of the selected key");

        float value = key.value;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(field_width);
        const float value_low = view.has_value_limits ? view.value_min : 0.0f;
        const float value_high = view.has_value_limits ? view.value_max : 0.0f;
        if(ImGui::DragFloat("##curve_value", &value, step, value_low, value_high, "V %.3f"))
        {
            result.changed |= edit_selection(view, state, [value](math::curve_key& edited) { edited.value = value; });
        }
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltipEx("%s", "Value of the selected key on its own channel");
    }

    // The sample under the pointer, which is what the curve is actually for.
    if(is_canvas_hovered)
    {
        const float position = proj.to_position(ImGui::GetIO().MousePos.x);
        std::vector<float> values;
        sample_channels(view, position, values);
        std::string readout = "at " + std::to_string(position).substr(0, 5) + "  ";
        for(size_t index = 0; index < values.size(); ++index)
        {
            readout += std::string(view.channels[index].name) + " " + std::to_string(values[index]).substr(0, 5) + "  ";
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
        ImGui::TextUnformatted(readout.c_str());
        ImGui::PopStyleColor();
    }
    return result;
}


// ---------------------------------------------------------------------------------
// The gradient view
//
// Four channel curves say nothing about what a color does. A strip of the actual colors, with a
// marker per key and a picker for the selected one, is what a color gradient is for; the channel
// curves stay one click away for shaping alpha or a single component.

/// Color of the key at @p index, read from the channels rather than sampled, so a marker shows
/// exactly what its key holds.
auto get_key_color(const curve_view& view, size_t index) -> ImVec4
{
    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
    const size_t count = std::min<size_t>(view.channels.size(), 4);
    for(size_t channel = 0; channel < count; ++channel)
    {
        (&color.x)[channel] = view.channels[channel].channel->get_key(index).value;
    }
    return color;
}

void set_key_color(const curve_view& view, size_t index, const ImVec4& color)
{
    const size_t count = std::min<size_t>(view.channels.size(), 4);
    for(size_t channel = 0; channel < count; ++channel)
    {
        auto* track = view.channels[channel].channel;
        if(index >= track->get_key_count())
        {
            continue;
        }
        math::curve_key key = track->get_key(index);
        key.value = (&color.x)[channel];
        track->set_key(index, key);
    }
}

auto draw_gradient_view(const curve_view& view, editor_state& state, const ImVec2& size) -> edit_result
{
    edit_result result;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const float marker_height = to_pixels(CURVE_MARKER_HEIGHT_EM);
    const float marker_width = to_pixels(CURVE_MARKER_WIDTH_EM);
    const float strip_height = std::max(size.y - marker_height, to_pixels(CURVE_STRIP_MIN_HEIGHT_EM));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect strip(origin, ImVec2(origin.x + size.x, origin.y + strip_height));
    const ImRect markers(ImVec2(origin.x, strip.Max.y), ImVec2(strip.Max.x, strip.Max.y + marker_height));

    ImGui::InvisibleButton("##curve_gradient", ImVec2(size.x, strip_height + marker_height));
    const bool is_hovered = ImGui::IsItemHovered();

    auto* positions_channel = view.channels.empty() ? nullptr : view.channels.front().channel;
    const size_t key_count = positions_channel != nullptr ? positions_channel->get_key_count() : 0;
    frange_t domain = positions_channel != nullptr ? positions_channel->get_domain() : frange_t(0.0f, 1.0f);
    // A gradient with one key, or none, still needs a strip to drop keys onto.
    if(domain.max - domain.min < CURVE_MIN_VIEW_EXTENT)
    {
        domain = frange_t(0.0f, 1.0f);
    }
    const float span = domain.max - domain.min;
    const auto to_screen_x = [&](float position)
    { return strip.Min.x + (position - domain.min) / span * strip.GetWidth(); };
    const auto to_position = [&](float x)
    { return domain.min + (x - strip.Min.x) / strip.GetWidth() * span; };

    ImGui::RenderColorRectWithAlphaCheckerboard(draw_list,
                                                strip.Min,
                                                strip.Max,
                                                0,
                                                strip.GetHeight() * 0.25f,
                                                ImVec2(0.0f, 0.0f),
                                                ImGui::GetStyle().FrameRounding);
    draw_list->PushClipRect(strip.Min, strip.Max, true);
    const float band_width = strip.GetWidth() / float(CURVE_STRIP_SAMPLES);
    for(int sample = 0; sample < CURVE_STRIP_SAMPLES; ++sample)
    {
        const float start_alpha = float(sample) / float(CURVE_STRIP_SAMPLES);
        const float end_alpha = float(sample + 1) / float(CURVE_STRIP_SAMPLES);
        const ImU32 start = ImGui::ColorConvertFloat4ToU32(sample_color(view, domain.min + span * start_alpha));
        const ImU32 end = ImGui::ColorConvertFloat4ToU32(sample_color(view, domain.min + span * end_alpha));
        const ImVec2 band_min(strip.Min.x + band_width * float(sample), strip.Min.y);
        const ImVec2 band_max(band_min.x + band_width + 1.0f, strip.Max.y);
        draw_list->AddRectFilledMultiColor(band_min, band_max, start, end, end, start);
    }
    draw_list->PopClipRect();
    draw_list->AddRect(strip.Min,
                       strip.Max,
                       ImGui::GetColorU32(ImGuiCol_Border),
                       ImGui::GetStyle().FrameRounding,
                       0,
                       CURVE_HAIRLINE_THICKNESS);

    // A marker per key: a pointer at the strip, and the key's own color as a swatch under it.
    int hovered_marker = -1;
    for(size_t index = 0; index < key_count; ++index)
    {
        const float x = to_screen_x(positions_channel->get_key(index).position);
        const ImRect bounds(ImVec2(x - marker_width * 0.5f, markers.Min.y),
                            ImVec2(x + marker_width * 0.5f, markers.Max.y));
        if(is_hovered && io.MousePos.x >= bounds.Min.x && io.MousePos.x <= bounds.Max.x &&
           io.MousePos.y >= markers.Min.y)
        {
            hovered_marker = int(index);
        }
        const bool selected = is_selected(state, key_ref{0, index});
        const ImVec4 color = get_key_color(view, index);
        draw_list->AddTriangleFilled(ImVec2(x, markers.Min.y),
                                     ImVec2(x - marker_width * 0.4f, markers.Min.y + marker_width * 0.4f),
                                     ImVec2(x + marker_width * 0.4f, markers.Min.y + marker_width * 0.4f),
                                     selected ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color());
        const ImRect swatch(ImVec2(bounds.Min.x, markers.Min.y + marker_width * 0.4f),
                            ImVec2(bounds.Max.x, markers.Max.y));
        ImGui::RenderColorRectWithAlphaCheckerboard(draw_list,
                                                    swatch.Min,
                                                    swatch.Max,
                                                    ImGui::ColorConvertFloat4ToU32(color),
                                                    swatch.GetWidth() * 0.5f,
                                                    ImVec2(0.0f, 0.0f));
        draw_list->AddRect(swatch.Min,
                           swatch.Max,
                           selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_Border),
                           0.0f,
                           0,
                           selected ? CURVE_SELECTION_THICKNESS : CURVE_HAIRLINE_THICKNESS);
    }

    if(is_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_marker < 0)
    {
        add_key_at(view, state, to_position(io.MousePos.x), 0.0f, true);
        result.changed = true;
        result.edit_finished = true;
    }
    else if(is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_marker >= 0)
    {
        select(state, view, key_ref{0, size_t(hovered_marker)}, io.KeyCtrl);
        state.dragged_marker = hovered_marker;
        state.drag_anchor = key_ref{0, size_t(hovered_marker)};
        state.is_editing = true;
    }

    if(state.dragged_marker >= 0)
    {
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            state.dragged_marker = -1;
            result.edit_finished = state.is_editing;
            state.is_editing = false;
        }
        else if(io.MouseDelta.x != 0.0f)
        {
            move_selection(view, state, io.MouseDelta.x / strip.GetWidth() * span, 0.0f);
            result.changed = true;
        }
    }

    if(is_hovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && !state.selection.empty())
    {
        remove_selection(view, state);
        result.changed = true;
        result.edit_finished = true;
    }

    // The selected key: its color, and where it sits.
    if(!state.selection.empty() && state.selection.front().key < key_count)
    {
        const size_t index = state.selection.front().key;
        ImVec4 color = get_key_color(view, index);
        ImGui::SetNextItemWidth(to_pixels(CURVE_FIELD_WIDTH_EM) * 2.4f);
        if(ImGui::ColorEdit4("##curve_key_color",
                             &color.x,
                             ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
        {
            set_key_color(view, index, color);
            result.changed = true;
        }
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltipEx("%s", "Color of the selected key");

        const float current = positions_channel->get_key(index).position;
        float position = current;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(to_pixels(CURVE_FIELD_WIDTH_EM));
        const float step = view.value_step > 0.0f ? view.value_step : CURVE_DEFAULT_STEP;
        if(ImGui::DragFloat("##curve_key_position", &position, step, 0.0f, 0.0f, "P %.3f"))
        {
            move_selection(view, state, position - current, 0.0f);
            result.changed = true;
        }
        result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltipEx("%s", "Position of the selected key");
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
        ImGui::TextUnformatted("Double click the strip to add a key. Click a marker to pick its color.");
        ImGui::PopStyleColor();
    }
    return result;
}

auto draw_canvas(const curve_view& view, editor_state& state, const ImVec2& size) -> edit_result
{
    edit_result result;
    const float padding = to_pixels(CURVE_CANVAS_PADDING_EM);
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImRect canvas(canvas_min, ImVec2(canvas_min.x + size.x, canvas_min.y + size.y));
    const ImRect plot(ImVec2(canvas.Min.x + padding, canvas.Min.y + padding),
                      ImVec2(canvas.Max.x - padding, canvas.Max.y - padding));

    ImGui::InvisibleButton("##curve_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool is_hovered = ImGui::IsItemHovered();

    const projection proj = make_projection(state, plot);
    const pick_result picked = is_hovered ? pick(proj, view, state, ImGui::GetIO().MousePos) : pick_result{};
    result = handle_canvas_input(view, state, proj, is_hovered, picked);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(canvas.Min, canvas.Max, true);
    draw_list->AddRectFilled(canvas.Min, canvas.Max, CURVE_CANVAS_COLOR, ImGui::GetStyle().FrameRounding);
    draw_grid(draw_list, proj);
    draw_curves(draw_list, proj, view, state);
    draw_keys(draw_list, proj, view, state, picked.has_key ? &picked.key : nullptr);
    if(state.drag == drag_mode::rubber_band)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        draw_list->AddRectFilled(state.rubber_band_origin, mouse, CURVE_SELECTION_FILL_COLOR);
        draw_list->AddRect(state.rubber_band_origin, mouse, get_accent_color(), 0.0f, 0, CURVE_HAIRLINE_THICKNESS);
    }
    draw_list->PopClipRect();
    draw_list->AddRect(canvas.Min,
                       canvas.Max,
                       ImGui::GetColorU32(ImGuiCol_Border),
                       ImGui::GetStyle().FrameRounding,
                       0,
                       CURVE_HAIRLINE_THICKNESS);

    const edit_result footer = draw_footer(view, state, proj, is_hovered);
    result.changed |= footer.changed;
    result.edit_finished |= footer.edit_finished;
    return result;
}

auto draw_editor_window(const char* id, const curve_view& view, editor_state& state) -> edit_result
{
    edit_result result;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * CURVE_WINDOW_SHARE_X,
                                    viewport->WorkSize.y * CURVE_WINDOW_SHARE_Y),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    // Not a modal: a curve is judged against what it drives, so the viewport stays live while it
    // is edited.
    if(!ImGui::Begin(id, &state.is_open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        return result;
    }

    const edit_result toolbar = draw_toolbar(view, state);
    result.changed |= toolbar.changed;
    result.edit_finished |= toolbar.edit_finished;

    ImGui::Spacing();

    const float footer_height = ImGui::GetFrameHeightWithSpacing();
    const float strip_height = view.is_color ? get_preview_height() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
    const float canvas_height =
        std::max(ImGui::GetContentRegionAvail().y - footer_height - strip_height, to_pixels(CURVE_CANVAS_MIN_HEIGHT_EM));

    const ImVec2 body_size(ImGui::GetContentRegionAvail().x, canvas_height);
    const edit_result body = (view.is_color && !state.show_curves) ? draw_gradient_view(view, state, body_size)
                                                                   : draw_canvas(view, state, body_size);
    result.changed |= body.changed;
    result.edit_finished |= body.edit_finished;

    // In the curve view a color still needs its strip, to judge the channels against the result.
    if(view.is_color && state.show_curves)
    {
        const ImVec2 strip_min = ImGui::GetCursorScreenPos();
        const ImRect strip(strip_min,
                           ImVec2(strip_min.x + ImGui::GetContentRegionAvail().x, strip_min.y + get_preview_height()));
        ImGui::Dummy(ImVec2(strip.GetWidth(), strip.GetHeight()));
        draw_preview_frame(ImGui::GetWindowDrawList(), strip, view, false);
    }

    ImGui::End();
    return result;
}

} // namespace

auto get_preview_height() -> float
{
    return to_pixels(CURVE_PREVIEW_HEIGHT_EM);
}

auto draw(const char* id, const curve_view& view) -> edit_result
{
    edit_result result;
    ImGui::PushID(id);

    const ImVec2 size(ImGui::GetContentRegionAvail().x, get_preview_height());
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect bounds(min, ImVec2(min.x + size.x, min.y + size.y));
    ImGui::InvisibleButton("##curve_preview", size);

    const ImGuiID state_id = ImGui::GetItemID();
    editor_state& state = get_states()[state_id];
    if(!state.has_view)
    {
        frame_view(state, view);
    }
    state.hidden_channels.resize(view.channels.size(), false);

    const bool is_hovered = ImGui::IsItemHovered();
    if(ImGui::IsItemClicked())
    {
        state.is_open = true;
    }
    ImGui::SetItemTooltipEx("%s", "Click to edit this curve");
    draw_preview_frame(ImGui::GetWindowDrawList(), bounds, view, is_hovered);

    if(state.is_open)
    {
        // A window is identified by its name alone, so every curve needs its own: two curves
        // sharing one name would draw into the same window, where their widgets collide. The id
        // of the preview below is unique across the whole interface, which the property's name
        // is not - two components can both have a "Color over lifetime".
        const std::string label = view.title.empty() ? std::string("Curve") : view.title;
        const std::string title = std::string(ICON_MDI_CHART_BELL_CURVE_CUMULATIVE " ") + label + "##curve_editor_" +
                                  std::to_string(static_cast<unsigned long long>(state_id));
        result = draw_editor_window(title.c_str(), view, state);
    }

    ImGui::PopID();
    return result;
}

} // namespace unravel::curve_editor
