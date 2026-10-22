#pragma once
#include <editor/imgui/integration/imgui.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/settings/settings.h>
#include <hpp/optional.hpp>

namespace unravel
{
class camera_component;
} // namespace unravel

namespace unravel::viewport_resolution
{

//-----------------------------------------------------------------------------
/// <summary>
/// Compute the camera viewport size to use for a resolution preset.
/// - Free aspect (aspect == 0): returns avail_size unchanged.
/// - Fixed resolution (width > 0 && height > 0): returns the exact pixel
///   dimensions configured in the preset.
/// - Aspect-only (aspect > 0, no fixed pixels): fits the aspect ratio
///   inside avail_size.
/// </summary>
//-----------------------------------------------------------------------------
auto compute_viewport_size(const settings::resolution_settings::resolution& res,
                           ImVec2 avail_size) -> ImVec2;

//-----------------------------------------------------------------------------
/// <summary>
/// Compute a viewport size that always fits within avail_size by aspect
/// ratio (ignoring any fixed pixel dimensions in the preset). This is
/// useful when the on-screen image rectangle must match the camera
/// viewport (e.g. for picking and gizmos).
/// </summary>
//-----------------------------------------------------------------------------
auto compute_fitted_size(const settings::resolution_settings::resolution& res,
                         ImVec2 avail_size) -> ImVec2;

//-----------------------------------------------------------------------------
/// <summary>
/// compute_viewport_size in whole pixels: the size the cameras render at.
/// </summary>
//-----------------------------------------------------------------------------
auto compute_viewport_pixels(const settings::resolution_settings::resolution& res,
                             ImVec2 avail_size) -> usize32_t;

//-----------------------------------------------------------------------------
/// <summary>
/// The pixels a preset renders a view's scene at: its own for a preset with pixels; empty
/// otherwise, for the view's size.
/// </summary>
//-----------------------------------------------------------------------------
auto get_render_resolution(const settings::resolution_settings::resolution& res) -> usize32_t;

//-----------------------------------------------------------------------------
/// <summary>
/// Apply the resolution preset to the camera component using
/// compute_viewport_pixels. Only viewport size is modified; viewport position
/// is left untouched.
/// </summary>
//-----------------------------------------------------------------------------
void apply_to_camera(camera_component& camera_comp,
                     const settings::resolution_settings::resolution& res,
                     ImVec2 avail_size);

//-----------------------------------------------------------------------------
/// <summary>
/// Get the resolution preset at the given index (clamped against the
/// configured presets). Returns nullptr if no resolutions are configured.
/// </summary>
//-----------------------------------------------------------------------------
auto get_resolution(rtti::context& ctx, int index)
    -> const settings::resolution_settings::resolution*;

/// Whether the preset is a phone or a tablet, which turns with the view's orientation.
auto is_handheld(const settings::resolution_settings::resolution& res) -> bool;

/// Whether a view shows the preset inside a frame of its device: a phone or a tablet with a shape.
auto has_device_frame(const settings::resolution_settings::resolution& res) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// The preset as a view uses it: a phone or a tablet turned to the orientation, its pixels and its aspect with it.
/// Any other preset stays as it is.
/// </summary>
//-----------------------------------------------------------------------------
auto get_oriented(const settings::resolution_settings::resolution& res,
                  settings::resolution_settings::screen_orientation orientation)
    -> settings::resolution_settings::resolution;

/// get_resolution() turned to the orientation, or nothing without settings or presets.
auto get_oriented_resolution(rtti::context& ctx, int index, settings::resolution_settings::screen_orientation orientation)
    -> hpp::optional<settings::resolution_settings::resolution>;

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the resolution selection dropdown of a floating viewport toolbar; call between
/// panel_toolbar::begin_bar() and end_bar(). The popover lists the free presets, then the presets of
/// each device in a section of its own, and turns phones and tablets between portrait and landscape.
/// The selection is read and written through the supplied index and orientation. Returns true if the
/// user changed either - the caller is responsible for persisting them if desired (e.g. project
/// settings vs. a transient panel-local value). Adds an "Edit ..." entry that opens the project
/// settings panel scoped to the Resolution section.
/// </summary>
/// <param name="is_compact">Show the icon alone, without the name of the preset</param>
//-----------------------------------------------------------------------------
auto draw_toolbar_dropdown(rtti::context& ctx,
                           int& current_index,
                           settings::resolution_settings::screen_orientation& orientation,
                           bool is_compact) -> bool;

} // namespace unravel::viewport_resolution
