#pragma once

#include <engine/engine_export.h>

#include <base/basetypes.hpp>

namespace unravel
{

/// The smallest render scale: a quarter of a view's width and height.
constexpr float MIN_RENDER_SCALE = 0.25f;
/// The step the settings offer the render scale in: finer steps save little and resize every target.
constexpr float RENDER_SCALE_STEP = 0.05f;

/**
 * @brief The size a view of `viewport_size` renders its scene at with `render_scale`: the viewport scaled
 * (from MIN_RENDER_SCALE), at least a texel each way.
 *
 * A scale of 1 or more, a scale that is not a number and an empty viewport give the viewport's size.
 */
ENGINE_EXPORT auto get_render_size(const usize32_t& viewport_size, float render_scale) -> usize32_t;

/**
 * @brief The size a camera view renders its scene at: its render resolution at `render_scale` (get_render_size).
 * @param render_resolution The resolution the view renders; empty, or with an empty side, for its viewport's size.
 */
ENGINE_EXPORT auto get_view_render_size(const usize32_t& viewport_size, const usize32_t& render_resolution, float render_scale)
    -> usize32_t;

} // namespace unravel
