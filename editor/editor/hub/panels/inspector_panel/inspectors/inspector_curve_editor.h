#pragma once

#include <imgui/imgui.h>
#include <math/curve.h>

#include <string>
#include <vector>

/// The curve editor: an inline preview that expands into a graph with keys, tangents and a
/// gradient strip.
///
/// Drawing only, and deliberately blind to the value type a curve holds. The inspector in
/// inspector_curve.h builds a @ref curve_view out of math::curve<T> and applies the result, the
/// way inspector_assets.cpp feeds the asset picker. A new value type reaches this editor by
/// specializing math::curve_traits, never by changing anything here.
namespace unravel::curve_editor
{

/// One channel of the curve being edited, named and colored for the plot.
struct channel_view
{
    math::curve_channel* channel{};
    const char* name{};
    ImU32 color{};
};

/// What the editor needs to know about a curve. The channels are edited in place.
struct curve_view
{
    std::vector<channel_view> channels;
    /// Also present the curve as a gradient strip: the channels are a color.
    bool is_color{};
    /// Whether a key exists at the same position on every channel. Edited by the link toggle.
    bool* is_linked{};
    /// Limits of the value axis, taken from the property's own meta attributes when it has them.
    bool has_value_limits{};
    float value_min{};
    float value_max{};
    /// Step of the numeric fields, from the property's meta attributes. 0 picks a default.
    float value_step{};
    /// Name of the property being edited, for the title of the window it opens in. Two curves
    /// open at once are two windows, so this does not have to be unique.
    std::string title;
};

/// What the editor did to the curve this frame, in the inspector's terms.
struct edit_result
{
    /// The curve differs from what it was when the frame started.
    bool changed{};
    /// The interaction that changed it has ended, so the change is worth recording.
    bool edit_finished{};
};

/// Height of the inline preview, in pixels.
auto get_preview_height() -> float;

/**
 * @brief Draws the inline preview, and the editor it expands into while that is open.
 *
 * Takes the room of one preview strip; the editor opens in a window of its own. @p id has to be
 * unique within the window, as any ImGui id does.
 */
auto draw(const char* id, const curve_view& view) -> edit_result;

} // namespace unravel::curve_editor
