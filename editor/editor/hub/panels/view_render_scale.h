#pragma once

#include <context/context.hpp>
#include <hpp/optional.hpp>

#include <cstdint>

/// The render scale a viewport (the Game view, the Scene view) renders its scene at, picked in its toolbar. The
/// scene renders at that share of the view's resolution and is resampled to it; the view keeps its size, so
/// clicks, touches, the picking and the UI map to it as before.
namespace unravel::view_render_scale
{

/// Where a view's render scale comes from.
enum class source : std::uint8_t
{
    /// The project's graphics.render_scale, as the previewed profile sets it.
    project,
    /// None: the view's full resolution.
    full,
    /// The view's own scale.
    custom,
};

/// The scale a view's Custom pick starts at.
constexpr float DEFAULT_CUSTOM_RENDER_SCALE = 0.5f;

/// A view's pick.
struct state
{
    source scale_source{source::project};
    /// The scale Custom renders at, from MIN_RENDER_SCALE to 1.
    float custom_scale{DEFAULT_CUSTOM_RENDER_SCALE};
};

/// The render scale the view asks the rendering system for: empty for the project's.
auto get_render_scale(const state& view) -> hpp::optional<float>;

/// The dropdown of a viewport toolbar; call between panel_toolbar::begin_bar() and end_bar(). Returns true when
/// the pick changed.
/// @param is_compact Show the icon alone, without the scale.
auto draw_toolbar_dropdown(rtti::context& ctx, state& view, bool is_compact) -> bool;

} // namespace unravel::view_render_scale
