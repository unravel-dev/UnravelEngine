#include "decal_component.h"

#include <algorithm>

namespace unravel
{

auto decal_component::get_material() const -> const asset_handle<material>&
{
    return material_.get_shared_material();
}

void decal_component::set_material(const asset_handle<material>& material)
{
    if(material_.get_shared_material() == material)
    {
        return;
    }
    material_.set_shared_material(material);
    material_.update_resolved();
}

auto decal_component::get_material_instance() const -> const material_instance&
{
    return material_;
}

auto decal_component::get_material_instance() -> material_instance&
{
    return material_;
}

void decal_component::set_material_instance(const material_instance& instance)
{
    const auto shared = material_.get_shared_material();
    material_ = instance;
    // The instance's own shared material is the component's: the overrides follow the material picked.
    material_.set_shared_material(shared);
    material_.update_resolved();
}

auto decal_component::get_resolved_material() const -> material::sptr
{
    if(const auto& resolved = material_.get_resolved())
    {
        return resolved;
    }
    return material_.get_shared_material().try_get();
}

auto decal_component::get_local_bounds() const -> math::bbox
{
    const math::vec3 half(size.x * 0.5f, size.y * 0.5f, depth * 0.5f);
    return math::bbox(-half, half);
}

auto decal_component::get_box_to_world(const math::transform& world) const -> math::mat4
{
    return world.get_matrix() * glm::scale(math::mat4(1.0f), math::vec3(size.x, size.y, depth));
}

void decal_component::resize(const math::vec2& face)
{
    size = math::max(face, math::vec2(0.001f));
    depth = std::max(std::min(size.x, size.y) * 0.5f, 0.05f);
}

auto decal_component::get_play_fade() const -> float
{
    return age > 0.0f || is_transient ? get_lifetime_fade() : 1.0f;
}

auto decal_component::get_lifetime_fade() const -> float
{
    float fade = 1.0f;
    if(fade_in > 0.0f)
    {
        fade = std::min(fade, age / fade_in);
    }
    if(lifetime > 0.0f)
    {
        const float fade_start = fade_in + lifetime;
        if(age > fade_start)
        {
            fade = fade_out > 0.0f ? std::min(fade, 1.0f - (age - fade_start) / fade_out) : 0.0f;
        }
    }
    return std::clamp(fade, 0.0f, 1.0f);
}

auto decal_component::has_faded_out() const -> bool
{
    return lifetime > 0.0f && age >= fade_in + lifetime + std::max(fade_out, 0.0f);
}

auto decal_component::get_normalized_age() const -> float
{
    const float total = fade_in + lifetime + std::max(fade_out, 0.0f);
    return lifetime > 0.0f && total > 0.0f ? std::clamp(age / total, 0.0f, 1.0f) : 0.0f;
}

} // namespace unravel
