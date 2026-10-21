#include "render_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace unravel
{

auto get_render_size(const usize32_t& viewport_size, float render_scale) -> usize32_t
{
    // Written so that a scale that is not a number keeps the viewport's size.
    const bool is_scaled = render_scale < 1.0f;
    if(!is_scaled || viewport_size.width == 0 || viewport_size.height == 0)
    {
        return viewport_size;
    }
    const float scale = std::max(render_scale, MIN_RENDER_SCALE);
    const auto scale_side = [scale](std::uint32_t side)
    {
        return std::max(std::uint32_t(1), static_cast<std::uint32_t>(std::lround(static_cast<float>(side) * scale)));
    };
    return {scale_side(viewport_size.width), scale_side(viewport_size.height)};
}

auto get_view_render_size(const usize32_t& viewport_size, const usize32_t& render_resolution, float render_scale) -> usize32_t
{
    const bool has_resolution = render_resolution.width > 0 && render_resolution.height > 0;
    return get_render_size(has_resolution ? render_resolution : viewport_size, render_scale);
}

} // namespace unravel
