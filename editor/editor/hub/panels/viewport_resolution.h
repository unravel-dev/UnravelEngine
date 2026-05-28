#pragma once
#include <editor/imgui/integration/imgui.h>

#include <context/context.hpp>
#include <engine/settings/settings.h>

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
/// Apply the resolution preset to the camera component using
/// compute_viewport_size. Only viewport size is modified; viewport position
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

//-----------------------------------------------------------------------------
/// <summary>
/// Draw the resolution selection drop-down for the current menu bar.
/// The selection is read and written through the supplied index parameter.
/// Returns true if the user changed the selection - the caller is
/// responsible for persisting the new index if desired (e.g. project
/// settings vs. a transient panel-local value). Adds an "Edit ..." entry
/// that opens the project settings panel scoped to the Resolution section.
/// </summary>
//-----------------------------------------------------------------------------
auto draw_menu(rtti::context& ctx, int& current_index) -> bool;

} // namespace unravel::viewport_resolution
