#pragma once
#include <editor/imgui/integration/imgui.h>

#include <imgui/imgui_internal.h>

namespace unravel::camera_preview_overlay
{

struct state
{
    /// Only the title row shows; the card keeps its corner.
    bool is_collapsed{};
};

/// What the card shows of a camera. The strings are borrowed for the call.
struct preview_desc
{
    /// Name of the camera entity.
    const char* name{};
    /// What the camera rendered, 0 while it has no output.
    ImTextureID texture{};
    /// Size the camera renders at, in pixels; 0 when unknown.
    ImVec2 resolution{};
    /// Short readout of the projection, such as "FOV 60" or "Ortho 5.0".
    const char* projection{};
};

struct request
{
    /// The button that moves the camera to the scene view was pressed.
    bool is_align_pressed{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the preview of a camera as a card at the bottom right corner of view_rect, in the look of
/// the viewport overlays: a title row with the camera name, the buttons to align the camera with
/// the view and to collapse the card, then the output fitted to its aspect with the resolution
/// and projection on it. The card is a child window, so a click on it never reaches the picking
/// or the gizmo underneath. A view too small for the card shows nothing.
/// </summary>
/// <param name="id">Unique identifier of the card within the current window</param>
/// <param name="view_rect">Screen rectangle of the viewport image the card floats over</param>
//-----------------------------------------------------------------------------
auto draw(const char* id, const preview_desc& desc, const ImRect& view_rect, state& preview_state) -> request;

} // namespace unravel::camera_preview_overlay
