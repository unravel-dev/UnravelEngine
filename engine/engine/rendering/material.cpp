#include "material.h"

#include <graphics/texture.h>
#include <graphics/uniform.h>

namespace unravel
{

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


} // namespace unravel
