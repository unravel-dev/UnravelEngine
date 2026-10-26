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

auto pbr_material::clone() const -> material::sptr
{
    auto mat = std::make_shared<pbr_material>(*this);
    return mat;
}

void pbr_material::add_content_versions(render_change_values& keys) const
{
    const auto add = [&keys](material_property_id property, const asset_handle<gfx::texture>& map)
    {
        add_identity_key(keys, get_material_property_traits(property).changes, map.version());
    };
    add(material_property_id::color_map, color_map_);
    add(material_property_id::normal_map, normal_map_);
    add(material_property_id::roughness_map, roughness_map_);
    add(material_property_id::metalness_map, metalness_map_);
    add(material_property_id::emissive_map, emissive_map_);
    add(material_property_id::ao_map, ao_map_);
}

auto pbr_material::write_instance_constants(math::vec4* out) const -> uint32_t
{
    // Picker colors are authored sRGB-encoded; lighting math runs in linear, so decode them here.
    const auto emissive = emissive_color_.to_linear().value;
    out[0] = base_color_.to_linear().value;
    out[1] = subsurface_color_.to_linear().value;
    out[2] = math::vec4(math::vec3(emissive) * emissive_intensity_, emissive.w);
    out[3] = surface_data_;
    return 4;
}

void pbr_material::set_alpha_mode(alpha_mode mode)
{
    // Synced even when unchanged: a legacy load sets the cutoff directly and relies on this.
    const bool is_changed = alpha_mode_ != mode;
    alpha_mode_ = mode;
    sync_surface_alpha_channel();
    if(is_changed)
    {
        mark_changed(material_property_id::alpha_mode);
    }
}

void pbr_material::set_alpha_cutoff(float cutoff)
{
    float value = std::clamp(cutoff, 0.0f, 1.0f);
    if(alpha_mode_ == alpha_mode::mask && value <= 0.0f)
    {
        value = 0.5f;
    }
    const bool is_changed = alpha_cutoff_ != value;
    alpha_cutoff_ = value;
    sync_surface_alpha_channel();
    if(is_changed)
    {
        mark_changed(material_property_id::alpha_cutoff);
    }
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
