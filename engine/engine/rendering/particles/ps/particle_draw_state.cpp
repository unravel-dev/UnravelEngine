#include "particle_draw_state.h"

#include <bgfx/bgfx.h>
#include <math/math.h>

namespace unravel
{
namespace ps_soa
{

auto to_sort_kind(sort_mode mode, blend_mode blend) -> gpu_sort_kind
{
    switch(mode)
    {
        case sort_mode::none:
            return gpu_sort_kind::slot;
        case sort_mode::view_distance:
            return gpu_sort_kind::distance;
        case sort_mode::view_depth:
            return gpu_sort_kind::depth;
        case sort_mode::oldest_first:
            return gpu_sort_kind::oldest_first;
        case sort_mode::youngest_first:
            return gpu_sort_kind::youngest_first;
        default:
            return blend == blend_mode::normal || blend == blend_mode::premultiplied ? gpu_sort_kind::distance
                                                                                     : gpu_sort_kind::append;
    }
}

auto blend_state(blend_mode mode) -> uint64_t
{
    switch(mode)
    {
        case blend_mode::additive:
            return BGFX_STATE_BLEND_ADD;
        case blend_mode::multiply:
            return BGFX_STATE_BLEND_MULTIPLY;
        default:
            // Normal and premultiplied: the shaders premultiply what the texture has not.
            return BGFX_STATE_BLEND_NORMAL;
    }
}

auto transmittance_blend_state(blend_mode mode) -> uint64_t
{
    if(mode == blend_mode::additive)
    {
        return BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                              BGFX_STATE_BLEND_ONE,
                                              BGFX_STATE_BLEND_ZERO,
                                              BGFX_STATE_BLEND_ONE);
    }
    return BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                          BGFX_STATE_BLEND_ZERO,
                                          BGFX_STATE_BLEND_INV_SRC_ALPHA);
}

auto get_blend_factor_rgba(float weight) -> uint32_t
{
    constexpr float unorm8_max = 255.0f;
    const uint32_t channel = uint32_t(math::clamp(weight, 0.0f, 1.0f) * unorm8_max + 0.5f);
    return channel << 24 | channel << 16 | channel << 8 | channel;
}

} // namespace ps_soa
} // namespace unravel
