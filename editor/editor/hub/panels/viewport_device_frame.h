#pragma once
#include <editor/imgui/integration/imgui.h>

#include <engine/settings/settings.h>
#include <imgui/imgui_internal.h>

/// A phone or a tablet drawn around the image of a view, so that a preset of a device reads as one. Only the look
/// changes: the view renders what its preset asks for, and the image, the pointer and the picking keep to the
/// screen rectangle of the layout.
namespace unravel::viewport_device_frame
{

/// Where a device goes in a view, in screen pixels.
struct layout
{
    settings::resolution_settings::device_type device{settings::resolution_settings::device_type::phone};
    /// The outline of the device, without the buttons on its edges.
    ImRect body{};
    /// Where the image of the view goes.
    ImRect screen{};
    float body_rounding{};
    float screen_rounding{};
    /// The short side of the screen. Every part of the device is sized after it.
    float unit{};
    /// The device is turned a quarter to the left of upright: its top edge is at the left.
    bool is_landscape{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Lay out the largest device of the given kind whose screen has the given aspect and that fits the area.
/// </summary>
/// <param name="screen_aspect">Width over height of the image the screen shows; above 1 turns the device to
/// landscape</param>
/// <param name="margin">Room kept free on every side, e.g. for a toolbar that floats over the view</param>
//-----------------------------------------------------------------------------
auto compute_layout(settings::resolution_settings::device_type device,
                    float screen_aspect,
                    const ImRect& area,
                    float margin) -> layout;

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the device with the texture on its screen into the current window, and add an item the size of the screen,
/// so that the caller's hover checks and item rectangle refer to the image as they would for ImGui::Image(). A null
/// texture leaves the screen dark.
/// </summary>
//-----------------------------------------------------------------------------
void draw(const layout& frame, ImTextureID texture);

/// Outline the device, for a view that marks a mode (playing, editing a prefab) around its image.
void draw_outline(const layout& frame, ImU32 color, float thickness);

} // namespace unravel::viewport_device_frame
