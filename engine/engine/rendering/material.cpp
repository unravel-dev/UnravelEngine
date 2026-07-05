#include "material.h"

#include "batch_key.h"

#include <graphics/texture.h>
#include <graphics/uniform.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
constexpr float k_deferred_dither_alpha = 0.25f;
constexpr float k_legacy_opaque_cutoff_sentinel = 0.25f;
} // namespace

auto material::clone() const -> material::sptr
{
    auto mat = std::make_shared<material>(*this);
    return mat;
}

auto material::default_color_map() -> asset_handle<gfx::texture>&
{
    static asset_handle<gfx::texture> texture;
    return texture;
}

auto material::default_normal_map() -> asset_handle<gfx::texture>&
{
    static asset_handle<gfx::texture> texture;
    return texture;
}

auto material::submit(gpu_program* program) const -> bool
{
    return false;
}

auto material::get_cull_type() const -> cull_type
{
    return cull_type_;
}

void material::set_cull_type(cull_type val)
{
    cull_type_ = val;
}

auto material::get_render_states(bool apply_cull, bool depth_write, bool depth_test) const -> uint64_t
{
    // Set render states.
    uint64_t states = 0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

    if(depth_write)
    {
        states |= BGFX_STATE_WRITE_Z;
    }

    if(depth_test)
    {
        states |= BGFX_STATE_DEPTH_TEST_LESS;
    }

    if(apply_cull)
    {
        auto cull_type = get_cull_type();
        if(cull_type == cull_type::counter_clockwise)
        {
            states |= BGFX_STATE_CULL_CCW;
        }
        if(cull_type == cull_type::clockwise)
        {
            states |= BGFX_STATE_CULL_CW;
        }
    }

    return states;
}


auto pbr_material::clone() const -> material::sptr
{
    auto mat = std::make_shared<pbr_material>(*this);
    return mat;
}

void pbr_material::set_alpha_mode(alpha_mode mode)
{
    alpha_mode_ = mode;
    sync_surface_alpha_channel();
}

void pbr_material::set_alpha_cutoff(float cutoff)
{
    alpha_cutoff_ = std::clamp(cutoff, 0.0f, 1.0f);
    if(alpha_mode_ == alpha_mode::mask && alpha_cutoff_ <= 0.0f)
    {
        alpha_cutoff_ = 0.5f;
    }
    sync_surface_alpha_channel();
}

void pbr_material::sync_surface_alpha_channel()
{
    surface_data_.w = alpha_mode_ == alpha_mode::mask ? alpha_cutoff_ : k_deferred_dither_alpha;
}

auto pbr_material::uses_alpha_cutout() const -> bool
{
    return alpha_mode_ == alpha_mode::mask;
}

auto pbr_material::casts_shadow() const -> bool
{
    return alpha_mode_ != alpha_mode::blend;
}

auto pbr_material::make_shadow_cutout_state() const -> shadow_cutout_state
{
    shadow_cutout_state state;
    const auto& color_map = get_color_map();
    const auto& albedo = color_map ? color_map : default_color_map();
    state.color_map = albedo.get();
    state.alpha_test_value = alpha_cutoff_;
    state.base_color = get_base_color();
    state.tiling = get_tiling();
    return state;
}

void pbr_material::infer_alpha_mode_from_legacy_cutoff()
{
    if(std::abs(alpha_cutoff_ - k_legacy_opaque_cutoff_sentinel) > 0.001f
       || std::abs(surface_data_.w - k_legacy_opaque_cutoff_sentinel) > 0.001f)
    {
        if(std::abs(alpha_cutoff_ - k_legacy_opaque_cutoff_sentinel) <= 0.001f)
        {
            alpha_cutoff_ = surface_data_.w;
        }
        set_alpha_mode(alpha_mode::mask);
        return;
    }

    set_alpha_mode(alpha_mode::opaque);
}

} // namespace unravel
