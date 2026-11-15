#include "material.h"

#include <graphics/texture.h>
#include <graphics/uniform.h>

#include <algorithm>
#include <cmath>

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

auto material::get_cull_type() const -> cull_type
{
    return cull_type_;
}

void material::set_cull_type(cull_type val)
{
    if(cull_type_ == val)
    {
        return;
    }
    cull_type_ = val;
    mark_changed(material_property_id::cull_type);
}

auto material::get_content_version(render_change::mask changes) const -> uint64_t
{
    render_change_values versions{};
    add_content_versions(versions);
    uint64_t version = 0;
    for_each_aspect(changes,
                    [&](uint32_t i)
                    {
                        version += versions[i];
                    });
    return version;
}

void material::add_change_keys(render_change_values& keys) const
{
    const auto& state = state_revisions_.get_each();
    const auto& instance_values = instance_values_revisions_.get_each();
    for(uint32_t i = 0; i < render_change::count; ++i)
    {
        keys[i] += state[i] + instance_values[i];
    }
    add_content_versions(keys);
}

void material::add_content_versions(render_change_values& /*keys*/) const
{
}

void material::copy_unreflected_values(const material& /*source*/)
{
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


auto material::write_instance_constants(math::vec4* out) const -> uint32_t
{
    return 0;
}

} // namespace unravel
