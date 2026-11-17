#include "decal_component.h"

#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material_graph/graph_material.h>

#include <algorithm>
#include <cmath>

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

auto decal_component::get_distance_fade(float distance) const -> float
{
    if(max_distance <= 0.0f)
    {
        return 1.0f;
    }
    return std::clamp((max_distance - distance) / (max_distance * 0.1f), 0.0f, 1.0f);
}

auto decal_component::draws_at(const math::transform& world, const math::vec3& point, const math::vec3& normal) const -> bool
{
    const math::mat4 box_to_world = get_box_to_world(world);
    const math::vec3 local = math::vec3(glm::inverse(box_to_world) * math::vec4(point, 1.0f));
    if(math::any(math::greaterThan(math::abs(local), math::vec3(0.5f))))
    {
        return false;
    }
    // As the decal's shader fades it: by the angle between the surface's normal and the way back to the projector.
    const float end = std::clamp(std::max(angle_fade.x, angle_fade.y), 0.0f, 180.0f);
    const math::vec3 projection = math::vec3(box_to_world[2]);
    if(math::length(projection) <= 0.0f || math::length(normal) <= 0.0f)
    {
        return false;
    }
    const float facing = math::dot(math::normalize(normal), -math::normalize(projection));
    return facing > std::cos(math::radians(end));
}

namespace
{
/// Whether @p surface takes decals of @p receiver_layers: its Receive Decals, and its entity on one of the layers.
auto receives_decal_layers(entt::handle surface, const layer_mask& receiver_layers) -> bool
{
    const auto* model = surface.try_get<model_component>();
    if(model == nullptr || !model->receives_decals())
    {
        return false;
    }
    const auto* layer = surface.try_get<layer_component>();
    const int surface_layers = layer != nullptr ? layer->layers.mask : int(layer_reserved::default_layer);
    return (receiver_layers.mask & surface_layers) != 0;
}

/// Whether @p decal shows anything: a loaded Decal material and some opacity left at @p distance from the view.
auto is_decal_shown(const decal_component& decal, float distance) -> bool
{
    if(find_loaded_material_domain(decal.get_material()) != material_domain::decal)
    {
        return false;
    }
    return std::clamp(decal.opacity, 0.0f, 1.0f) * decal.get_play_fade() * decal.get_distance_fade(distance) > 0.0f;
}
} // namespace

auto find_decal_at(scene& scn,
                   entt::handle surface,
                   const math::vec3& point,
                   const math::vec3& normal,
                   const math::vec3& view_position) -> entt::handle
{
    entt::handle best;
    int best_order = 0;
    float best_volume = 0.0f;
    scn.registry->view<transform_component, decal_component, active_component>().each(
        [&](entt::entity entity, transform_component& transform_comp, decal_component& decal, active_component&)
        {
            const auto& world = transform_comp.get_transform_global();
            if(!receives_decal_layers(surface, decal.receiver_layers) ||
               !is_decal_shown(decal, math::length(world.get_position() - view_position)) || !decal.draws_at(world, point, normal))
            {
                return;
            }
            const math::mat4 box_to_world = decal.get_box_to_world(world);
            const float volume = math::length(math::vec3(box_to_world[0])) * math::length(math::vec3(box_to_world[1])) *
                                 math::length(math::vec3(box_to_world[2]));
            const bool is_above = !best || decal.sort_order > best_order || (decal.sort_order == best_order && volume < best_volume);
            if(is_above)
            {
                best = scn.create_handle(entity);
                best_order = decal.sort_order;
                best_volume = volume;
            }
        });
    return best;
}

} // namespace unravel
