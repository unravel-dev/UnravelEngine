#include "viewport_device_frame.h"

#include <algorithm>
#include <cmath>

namespace unravel::viewport_device_frame
{
namespace
{
using device_type = settings::resolution_settings::device_type;

/// The proportions of a device, as fractions of the short side of its screen.
struct device_style
{
    float bezel;
    float screen_rounding;
    /// The metal band around the glass front.
    float rim;
    /// How far the buttons stand out of the edge.
    float button_depth;
    float camera_radius;
};

/// A stretch of an edge of the upright device, as fractions of that edge from its top or left end.
struct edge_span
{
    float start;
    float end;
};

// A current phone: a thin even bezel, round screen corners, and a punch-hole camera in the screen.
constexpr device_style PHONE_STYLE{0.045f, 0.1f, 0.012f, 0.012f, 0.022f};
// A tablet: a wider bezel around squarer corners, with the camera in the bezel.
constexpr device_style TABLET_STYLE{0.055f, 0.03f, 0.008f, 0.008f, 0.012f};
// The volume rocker and the power key: on the right edge of a phone, on the top edge of a tablet.
constexpr edge_span PHONE_VOLUME_SPAN{0.16f, 0.28f};
constexpr edge_span PHONE_POWER_SPAN{0.33f, 0.4f};
constexpr edge_span TABLET_VOLUME_SPAN{0.6f, 0.74f};
constexpr edge_span TABLET_POWER_SPAN{0.8f, 0.88f};
/// The phone's camera sits this far under the top edge of the screen, in short sides of the screen.
constexpr float PHONE_CAMERA_INSET = 0.045f;
// Smallest sizes in pixels, so that a small view still shows every part.
constexpr float MIN_BEZEL = 4.0f;
constexpr float MIN_RIM = 1.0f;
constexpr float MIN_BUTTON_DEPTH = 3.0f;
constexpr float MIN_CAMERA_RADIUS = 2.0f;
constexpr float MIN_UNIT = 1.0f;
constexpr int SHADOW_RINGS = 12;
constexpr int SHADOW_RING_ALPHA = 9;
/// Growth of each ring of the shadow and the drop of the whole shadow, in short sides of the screen.
constexpr float SHADOW_RING_STEP = 0.004f;
constexpr float SHADOW_OFFSET = 0.012f;
// The lens of the camera: a ring inside the hole and a small glint, as fractions of the hole's radius.
constexpr float CAMERA_RING_RADIUS = 0.55f;
constexpr float CAMERA_RING_THICKNESS = 0.2f;
constexpr float CAMERA_GLINT_RADIUS = 0.18f;
constexpr float CAMERA_GLINT_OFFSET = 0.25f;
// A dark device with a grey metal rim reads on both light and dark editor themes.
constexpr ImU32 BUTTON_COLOR = IM_COL32(52, 55, 61, 255);
constexpr ImU32 RIM_COLOR = IM_COL32(64, 67, 74, 255);
constexpr ImU32 RIM_EDGE_COLOR = IM_COL32(96, 100, 108, 255);
constexpr ImU32 GLASS_COLOR = IM_COL32(10, 10, 12, 255);
constexpr ImU32 SCREEN_OFF_COLOR = IM_COL32(0, 0, 0, 255);
constexpr ImU32 CAMERA_COLOR = IM_COL32(2, 2, 3, 255);
constexpr ImU32 CAMERA_RING_COLOR = IM_COL32(34, 38, 48, 255);
constexpr ImU32 CAMERA_GLINT_COLOR = IM_COL32(90, 110, 160, 120);

auto get_style(device_type device) -> const device_style&
{
    return device == device_type::tablet ? TABLET_STYLE : PHONE_STYLE;
}

auto to_pixels(const layout& frame, float units, float min_pixels) -> float
{
    return std::max(min_pixels, std::round(units * frame.unit));
}

/// Half the size of the upright device: x across it, y along it.
auto get_upright_half_size(const ImRect& rect, bool is_landscape) -> ImVec2
{
    const ImVec2 half = rect.GetSize() * 0.5f;
    return is_landscape ? ImVec2(half.y, half.x) : half;
}

/// A point given around the center of the upright device, where the view shows it: a landscape device is the upright
/// one turned a quarter to the left.
auto to_view(const layout& frame, const ImVec2& upright) -> ImVec2
{
    const ImVec2 center = frame.body.GetCenter();
    return frame.is_landscape ? ImVec2(center.x + upright.y, center.y - upright.x) : center + upright;
}

auto to_view(const layout& frame, const ImRect& upright) -> ImRect
{
    const ImVec2 a = to_view(frame, upright.Min);
    const ImVec2 b = to_view(frame, upright.Max);
    return {ImMin(a, b), ImMax(a, b)};
}

/// A key on an edge of the upright device; the body drawn after it covers its inner half.
auto make_button(const layout& frame, const edge_span& span) -> ImRect
{
    const ImVec2 half = get_upright_half_size(frame.body, frame.is_landscape);
    const float depth = to_pixels(frame, get_style(frame.device).button_depth, MIN_BUTTON_DEPTH);
    if(frame.device == device_type::tablet)
    {
        return {ImVec2(-half.x + span.start * half.x * 2.0f, -half.y - depth),
                ImVec2(-half.x + span.end * half.x * 2.0f, -half.y + depth)};
    }
    return {ImVec2(half.x - depth, -half.y + span.start * half.y * 2.0f),
            ImVec2(half.x + depth, -half.y + span.end * half.y * 2.0f)};
}

void draw_shadow(ImDrawList* draw_list, const layout& frame)
{
    const float step = std::max(1.0f, frame.unit * SHADOW_RING_STEP);
    const ImVec2 offset(0.0f, std::round(frame.unit * SHADOW_OFFSET));
    for(int ring = SHADOW_RINGS; ring >= 1; --ring)
    {
        const float grow = step * static_cast<float>(ring);
        const ImVec2 expand(grow, grow);
        draw_list->AddRectFilled(frame.body.Min - expand + offset,
                                 frame.body.Max + expand + offset,
                                 IM_COL32(0, 0, 0, SHADOW_RING_ALPHA),
                                 frame.body_rounding + grow);
    }
}

void draw_buttons(ImDrawList* draw_list, const layout& frame)
{
    const bool is_tablet = frame.device == device_type::tablet;
    const float rounding = to_pixels(frame, get_style(frame.device).button_depth, MIN_BUTTON_DEPTH);
    for(const edge_span& span : {is_tablet ? TABLET_VOLUME_SPAN : PHONE_VOLUME_SPAN,
                                 is_tablet ? TABLET_POWER_SPAN : PHONE_POWER_SPAN})
    {
        const ImRect button = to_view(frame, make_button(frame, span));
        draw_list->AddRectFilled(button.Min, button.Max, BUTTON_COLOR, rounding);
    }
}

void draw_body(ImDrawList* draw_list, const layout& frame)
{
    const float rim = to_pixels(frame, get_style(frame.device).rim, MIN_RIM);
    const ImVec2 inset(rim, rim);
    draw_list->AddRectFilled(frame.body.Min, frame.body.Max, RIM_COLOR, frame.body_rounding);
    draw_list->AddRectFilled(frame.body.Min + inset,
                             frame.body.Max - inset,
                             GLASS_COLOR,
                             std::max(frame.body_rounding - rim, 0.0f));
    draw_list->AddRect(frame.body.Min, frame.body.Max, RIM_EDGE_COLOR, frame.body_rounding);
}

void draw_screen(ImDrawList* draw_list, const layout& frame, ImTextureID texture)
{
    if(texture == ImTextureID{})
    {
        draw_list->AddRectFilled(frame.screen.Min, frame.screen.Max, SCREEN_OFF_COLOR, frame.screen_rounding);
        return;
    }
    draw_list->AddImageRounded(texture,
                               frame.screen.Min,
                               frame.screen.Max,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               IM_COL32_WHITE,
                               frame.screen_rounding);
}

/// The phone's punch hole under the top edge of its screen; the tablet's camera in the middle of its right bezel,
/// which is the top one while the tablet lies in landscape.
void draw_camera(ImDrawList* draw_list, const layout& frame)
{
    const ImVec2 screen_half = get_upright_half_size(frame.screen, frame.is_landscape);
    const ImVec2 body_half = get_upright_half_size(frame.body, frame.is_landscape);
    const ImVec2 upright_center = frame.device == device_type::tablet
                                      ? ImVec2((screen_half.x + body_half.x) * 0.5f, 0.0f)
                                      : ImVec2(0.0f, -screen_half.y + frame.unit * PHONE_CAMERA_INSET);
    const ImVec2 center = to_view(frame, upright_center);
    const float radius = to_pixels(frame, get_style(frame.device).camera_radius, MIN_CAMERA_RADIUS);
    const ImVec2 glint_offset(-radius * CAMERA_GLINT_OFFSET, -radius * CAMERA_GLINT_OFFSET);
    draw_list->AddCircleFilled(center, radius, CAMERA_COLOR);
    draw_list->AddCircle(center,
                         radius * CAMERA_RING_RADIUS,
                         CAMERA_RING_COLOR,
                         0,
                         std::max(1.0f, radius * CAMERA_RING_THICKNESS));
    draw_list->AddCircleFilled(center + glint_offset, radius * CAMERA_GLINT_RADIUS, CAMERA_GLINT_COLOR);
}
} // namespace

auto compute_layout(device_type device, float screen_aspect, const ImRect& area, float margin) -> layout
{
    layout frame{};
    frame.device = device;
    frame.is_landscape = screen_aspect > 1.0f;
    const device_style& style = get_style(device);
    const float aspect = std::max(screen_aspect, 0.01f);
    const float long_side = frame.is_landscape ? aspect : 1.0f / aspect;
    // The buttons count on both sides, so that the device stays in the middle of the area.
    const float edge = 2.0f * (style.bezel + style.button_depth);
    const ImVec2 upright_extent(1.0f + edge, long_side + edge);
    const ImVec2 extent = frame.is_landscape ? ImVec2(upright_extent.y, upright_extent.x) : upright_extent;
    const ImVec2 room = ImMax(area.GetSize() - ImVec2(margin, margin) * 2.0f, ImVec2(0.0f, 0.0f));
    frame.unit = std::max(MIN_UNIT, std::floor(std::min(room.x / extent.x, room.y / extent.y)));
    const float screen_long = std::round(frame.unit * long_side);
    const ImVec2 screen_size = frame.is_landscape ? ImVec2(screen_long, frame.unit) : ImVec2(frame.unit, screen_long);
    const ImVec2 screen_min = ImFloor(area.GetCenter() - screen_size * 0.5f);
    frame.screen = ImRect(screen_min, screen_min + screen_size);
    const float bezel = to_pixels(frame, style.bezel, MIN_BEZEL);
    frame.body = frame.screen;
    frame.body.Expand(bezel);
    frame.screen_rounding = std::round(frame.unit * style.screen_rounding);
    frame.body_rounding = frame.screen_rounding + bezel;
    return frame;
}

void draw(const layout& frame, ImTextureID texture)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_shadow(draw_list, frame);
    draw_buttons(draw_list, frame);
    draw_body(draw_list, frame);
    draw_screen(draw_list, frame, texture);
    draw_camera(draw_list, frame);
    ImGui::SetCursorScreenPos(frame.screen.Min);
    ImGui::Dummy(frame.screen.GetSize());
}

void draw_outline(const layout& frame, ImU32 color, float thickness)
{
    const float gap = thickness + 1.0f;
    const ImVec2 expand(gap, gap);
    ImGui::GetWindowDrawList()->AddRect(frame.body.Min - expand,
                                        frame.body.Max + expand,
                                        color,
                                        frame.body_rounding + gap,
                                        0,
                                        thickness);
}

} // namespace unravel::viewport_device_frame
