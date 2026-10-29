#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <hpp/optional.hpp>
#include <reflection/reflection.h>

#include <algorithm>
#include <vector>

namespace unravel
{
namespace
{

using managed_range = dotnetpp_backend::managed_interface::float_range;

/// @p value as an @p Enum when the reflection lists it among the enum's values. Scripts pass enums as plain ints,
/// and a cast int could name no value at all, or one that is not an authoring choice; those are refused with a
/// warning naming @p property.
template<typename Enum>
auto to_reflected_enum(int value, const char* property) -> hpp::optional<Enum>
{
    for(auto [id, data] : entt::resolve<Enum>().data())
    {
        const auto candidate = data.get({});
        const auto* as_enum = candidate.template try_cast<Enum>();
        if(as_enum != nullptr && static_cast<int>(*as_enum) == value)
        {
            return *as_enum;
        }
    }
    APPLOG_WARNING("ParticleEmitterComponent.{}: {} is not one of its values; ignored.", property, value);
    return hpp::nullopt;
}

//------------------------------

auto internal_m2n_particle_emitter_get_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_enabled();
    }
    return false;
}

void internal_m2n_particle_emitter_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_enabled(enabled);
    }
}

auto internal_m2n_particle_emitter_get_max_particles(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_max_particles();
    }
    return 0;
}

void internal_m2n_particle_emitter_set_max_particles(entt::entity id, uint32_t max_particles)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_max_particles(max_particles);
    }
}

auto internal_m2n_particle_emitter_get_shape(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_shape());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_shape(entt::entity id, int shape)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::emitter_shape>(shape, "shape"))
        {
            comp->set_shape(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_direction(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_direction());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_direction(entt::entity id, int direction)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::emitter_direction>(direction, "direction"))
        {
            comp->set_direction(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_gravity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_gravity_scale();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_gravity_scale(entt::entity id, float scale)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_gravity_scale(scale);
    }
}

auto internal_m2n_particle_emitter_get_emission_rate(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_rate();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_emission_rate(entt::entity id, float rate)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_rate(rate);
    }
}

auto internal_m2n_particle_emitter_get_spawn_along_path(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_spawn_along_path();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_spawn_along_path(entt::entity id, float motion)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_spawn_along_path(motion);
    }
}

auto internal_m2n_particle_emitter_get_drag(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_drag();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_drag(entt::entity id, float drag)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_drag(drag);
    }
}

auto internal_m2n_particle_emitter_get_inherit_velocity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_inherit_velocity();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_inherit_velocity(entt::entity id, float fraction)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_inherit_velocity(fraction);
    }
}

auto internal_m2n_particle_emitter_get_direction_spread(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_direction_spread();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_direction_spread(entt::entity id, float degrees)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_direction_spread(degrees);
    }
}

auto internal_m2n_particle_emitter_get_use_fixed_seed(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_use_fixed_seed();
    }
    return false;
}

void internal_m2n_particle_emitter_set_use_fixed_seed(entt::entity id, bool use_fixed_seed)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_use_fixed_seed(use_fixed_seed);
    }
}

auto internal_m2n_particle_emitter_get_random_seed(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_random_seed();
    }
    return 0;
}

void internal_m2n_particle_emitter_set_random_seed(entt::entity id, uint32_t seed)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_random_seed(seed);
    }
}

auto internal_m2n_particle_emitter_get_sort_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_sort_mode());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_sort_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::sort_mode>(value, "sortMode"))
        {
            comp->set_sort_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_primitive(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_primitive());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_primitive(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::particle_primitive>(value, "primitive"))
        {
            comp->set_primitive(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_half_resolution(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_half_resolution();
    }
    return false;
}

void internal_m2n_particle_emitter_set_half_resolution(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_half_resolution(value);
    }
}

auto internal_m2n_particle_emitter_get_write_velocity(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_write_velocity();
    }
    return false;
}

void internal_m2n_particle_emitter_set_write_velocity(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_write_velocity(value);
    }
}

auto internal_m2n_particle_emitter_get_velocity_alpha_threshold(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_velocity_alpha_threshold();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_velocity_alpha_threshold(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_velocity_alpha_threshold(value);
    }
}

auto internal_m2n_particle_emitter_get_random_between_gradients(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_random_between_gradients();
    }
    return false;
}

void internal_m2n_particle_emitter_set_random_between_gradients(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_random_between_gradients(value);
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_use_frame_curve(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture_sheet_use_frame_curve();
    }
    return false;
}

void internal_m2n_particle_emitter_set_texture_sheet_use_frame_curve(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_use_frame_curve(value);
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_random_row(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture_sheet_random_row();
    }
    return false;
}

void internal_m2n_particle_emitter_set_texture_sheet_random_row(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_random_row(value);
    }
}

auto internal_m2n_particle_emitter_get_rate_over_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_rate_over_distance();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_rate_over_distance(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_rate_over_distance(value);
    }
}

auto internal_m2n_particle_emitter_get_cone_angle(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_cone_angle();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_cone_angle(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_cone_angle(value);
    }
}

auto internal_m2n_particle_emitter_get_torus_thickness(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_torus_thickness();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_torus_thickness(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_torus_thickness(value);
    }
}

auto internal_m2n_particle_emitter_get_shape_mesh(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_shape_mesh().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_shape_mesh(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        comp->set_shape_mesh(am.get_asset<mesh>(uid));
    }
}

auto internal_m2n_particle_emitter_get_render_mesh(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_render_mesh().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_render_mesh(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        comp->set_render_mesh(am.get_asset<mesh>(uid));
    }
}

auto internal_m2n_particle_emitter_get_render_material(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_render_material().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_render_material(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        comp->set_render_material(am.get_asset<material>(uid));
    }
}

auto internal_m2n_particle_emitter_get_mesh_cast_shadows(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_mesh_cast_shadows();
    }
    return true;
}

void internal_m2n_particle_emitter_set_mesh_cast_shadows(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_mesh_cast_shadows(value);
    }
}

auto internal_m2n_particle_emitter_get_mesh_spawn_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_shape_mesh_mode());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_mesh_spawn_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::mesh_spawn_mode>(value, "shapeMeshMode"))
        {
            comp->set_shape_mesh_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_turbulence_strength(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_turbulence_strength();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_turbulence_strength(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_turbulence_strength(value);
    }
}

auto internal_m2n_particle_emitter_get_turbulence_frequency(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_turbulence_frequency();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_turbulence_frequency(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_turbulence_frequency(value);
    }
}

auto internal_m2n_particle_emitter_get_turbulence_speed(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_turbulence_speed();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_turbulence_speed(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_turbulence_speed(value);
    }
}

auto internal_m2n_particle_emitter_get_max_draw_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_max_draw_distance();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_max_draw_distance(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_max_draw_distance(value);
    }
}

auto internal_m2n_particle_emitter_get_draw_distance_fade(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_draw_distance_fade();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_draw_distance_fade(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_draw_distance_fade(value);
    }
}

auto internal_m2n_particle_emitter_get_normal_texture(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_normal_texture().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_normal_texture(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& am = engine::context().get_cached<asset_manager>();
        comp->set_normal_texture(am.get_asset<gfx::texture>(uid));
    }
}

auto internal_m2n_particle_emitter_get_cutout_alpha_threshold(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_cutout_alpha_threshold();
    }
    return 0.05f;
}

void internal_m2n_particle_emitter_set_cutout_alpha_threshold(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_cutout_alpha_threshold(value);
    }
}

auto internal_m2n_particle_emitter_get_lighting_wrap(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_lighting_wrap();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_lighting_wrap(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lighting_wrap(value);
    }
}

auto internal_m2n_particle_emitter_get_receive_shadows(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_receive_shadows();
    }
    return false;
}

void internal_m2n_particle_emitter_set_receive_shadows(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_receive_shadows(value);
    }
}

auto internal_m2n_particle_emitter_get_lighting(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_lighting());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_lighting(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::particle_lighting>(value, "lighting"))
        {
            comp->set_lighting(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_light_count(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_light_count();
    }
    return 0u;
}

void internal_m2n_particle_emitter_set_light_count(entt::entity id, uint32_t value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_light_count(value);
    }
}

auto internal_m2n_particle_emitter_get_light_intensity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_light_intensity();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_light_intensity(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_light_intensity(value);
    }
}

auto internal_m2n_particle_emitter_get_light_range(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_light_range();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_light_range(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_light_range(value);
    }
}

auto internal_m2n_particle_emitter_get_light_color_from_particle(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_light_color_from_particle();
    }
    return false;
}

void internal_m2n_particle_emitter_set_light_color_from_particle(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_light_color_from_particle(value);
    }
}

auto internal_m2n_particle_emitter_get_restitution(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_restitution();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_restitution(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_restitution(value);
    }
}

auto internal_m2n_particle_emitter_get_friction(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_friction();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_friction(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_friction(value);
    }
}

auto internal_m2n_particle_emitter_get_collision_radius_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_collision_radius_scale();
    }
    return 1.0f;
}

void internal_m2n_particle_emitter_set_collision_radius_scale(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_collision_radius_scale(value);
    }
}

auto internal_m2n_particle_emitter_get_max_bounces(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_max_bounces();
    }
    return 0u;
}

void internal_m2n_particle_emitter_set_max_bounces(entt::entity id, uint32_t value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_max_bounces(value);
    }
}

auto internal_m2n_particle_emitter_get_trail_length(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_trail_length();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_trail_length(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_trail_length(value);
    }
}

auto internal_m2n_particle_emitter_get_ribbon_texture_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_ribbon_texture_mode());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_ribbon_texture_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::ribbon_texture_mode>(value, "ribbonTextureMode"))
        {
            comp->set_ribbon_texture_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_ribbon_tile_time(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_ribbon_tile_time();
    }
    return 0.25f;
}

void internal_m2n_particle_emitter_set_ribbon_tile_time(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_ribbon_tile_time(value);
    }
}

auto internal_m2n_particle_emitter_get_ribbon_facing(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_ribbon_facing());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_ribbon_facing(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::ribbon_facing>(value, "ribbonFacing"))
        {
            comp->set_ribbon_facing(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_trail_width_end(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_trail_width_end();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_trail_width_end(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_trail_width_end(value);
    }
}

auto internal_m2n_particle_emitter_get_bounds_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_bounds_mode());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_bounds_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::bounds_mode>(value, "boundsMode"))
        {
            comp->set_bounds_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_fixed_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_fixed_bounds();
    }
    return math::bbox::empty;
}

void internal_m2n_particle_emitter_set_fixed_bounds(entt::entity id, const math::bbox& bounds)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_fixed_bounds(bounds);
    }
}

auto internal_m2n_particle_emitter_get_opacity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_opacity();
    }
    return 1.0f;
}

void internal_m2n_particle_emitter_set_opacity(entt::entity id, float opacity)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_opacity(opacity);
    }
}

auto internal_m2n_particle_emitter_get_acceleration(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_acceleration();
    }
    return math::vec3{0.0f, 0.0f, 0.0f};
}

void internal_m2n_particle_emitter_set_acceleration(entt::entity id, const math::vec3& force)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_acceleration(force);
    }
}

auto internal_m2n_particle_emitter_get_shape_scale(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_shape_scale();
    }
    return math::vec3{1.0f, 1.0f, 1.0f};
}

void internal_m2n_particle_emitter_set_shape_scale(entt::entity id, const math::vec3& scale)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_shape_scale(scale);
    }
}

auto internal_m2n_particle_emitter_get_duration(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_duration().count();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_duration(entt::entity id, float duration)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_duration(std::chrono::duration<float>(duration));
    }
}

auto internal_m2n_particle_emitter_get_lifetime_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_lifetime();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_lifetime_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_start_speed(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_start_speed();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_start_speed(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_start_speed(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_motion_easing(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_motion_easing());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_motion_easing(entt::entity id, int easing)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<bx::Easing::Enum>(easing, "motionEasing"))
        {
            comp->set_motion_easing(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_num_particles(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_num_particles();
    }
    return 0;
}

auto internal_m2n_particle_emitter_is_playing(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_playing();
    }
    return false;
}

auto internal_m2n_particle_emitter_is_paused(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_paused();
    }
    return false;
}

auto internal_m2n_particle_emitter_get_texture(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_texture(entt::entity id, const hpp::uuid& texture)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto handle = am.get_asset<gfx::texture>(texture);
        comp->set_texture(handle);
    }
}

void internal_m2n_particle_emitter_play(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->play();
    }
}

void internal_m2n_particle_emitter_stop(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop();
    }
}

void internal_m2n_particle_emitter_stop_and_reset(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_and_reset();
    }
}

void internal_m2n_particle_emitter_pause(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->pause();
    }
}

void internal_m2n_particle_emitter_resume(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->resume();
    }
}

void internal_m2n_particle_emitter_reset_emitter(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->reset_emitter();
    }
}

auto internal_m2n_particle_emitter_get_loop(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_loop();
    }
    return true; // Default to true
}

void internal_m2n_particle_emitter_set_loop(entt::entity id, bool loop)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_loop(loop);
    }
}

auto internal_m2n_particle_emitter_get_blend_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_blend_mode());
    }
    return static_cast<int>(ps_soa::blend_mode::normal);
}

void internal_m2n_particle_emitter_set_blend_mode(entt::entity id, int mode)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::blend_mode>(mode, "blendMode"))
        {
            comp->set_blend_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_sort_backend(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_sort_backend());
    }
    return static_cast<int>(ps_soa::particle_sort_backend::cpu);
}

void internal_m2n_particle_emitter_set_sort_backend(entt::entity id, int backend)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::particle_sort_backend>(backend, "sortBackend"))
        {
            comp->set_sort_backend(*valid);
        }
    }
}

//------------------------------


auto internal_m2n_particle_emitter_get_culling_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_culling_mode());
    }
    return {};
}

void internal_m2n_particle_emitter_set_culling_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<particle_emitter_component::culling_mode>(value, "cullingMode"))
        {
            comp->set_culling_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_simulation_space(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_simulation_space());
    }
    return {};
}

void internal_m2n_particle_emitter_set_simulation_space(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::simulation_space>(value, "simulationSpace"))
        {
            comp->set_simulation_space(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_start_delay(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_start_delay().count();
    }
    return {};
}

void internal_m2n_particle_emitter_set_start_delay(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_start_delay(std::chrono::duration<float>(value));
    }
}

auto internal_m2n_particle_emitter_get_spawn_location(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_spawn_location());
    }
    return {};
}

void internal_m2n_particle_emitter_set_spawn_location(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::spawn_location>(value, "spawnLocation"))
        {
            comp->set_spawn_location(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_shape_position(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_shape_position();
    }
    return {};
}

void internal_m2n_particle_emitter_set_shape_position(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_shape_position(value);
    }
}

auto internal_m2n_particle_emitter_get_size_axes(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_size_axes();
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_axes(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_axes(value);
    }
}

auto internal_m2n_particle_emitter_get_rotation(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_start_rotation();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_rotation(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_start_rotation(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_spin_speed(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_spin_speed();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_spin_speed(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_spin_speed(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_random_spin_direction(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_random_spin_direction();
    }
    return false;
}

void internal_m2n_particle_emitter_set_random_spin_direction(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_random_spin_direction(value);
    }
}

auto internal_m2n_particle_emitter_get_soft_fade_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_soft_fade_distance();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_soft_fade_distance(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_soft_fade_distance(value);
    }
}

auto internal_m2n_particle_emitter_get_prewarm(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_prewarm();
    }
    return false;
}

void internal_m2n_particle_emitter_set_prewarm(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_prewarm(value);
    }
}

auto internal_m2n_particle_emitter_get_auto_play(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_auto_play();
    }
    return true;
}

void internal_m2n_particle_emitter_set_auto_play(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_auto_play(value);
    }
}

auto internal_m2n_particle_emitter_get_sort_priority(entt::entity id) -> int32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_sort_priority();
    }
    return 0;
}

void internal_m2n_particle_emitter_set_sort_priority(entt::entity id, int32_t value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_sort_priority(value);
    }
}

auto internal_m2n_particle_emitter_get_frame_blending(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_frame_blending();
    }
    return false;
}

void internal_m2n_particle_emitter_set_frame_blending(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_frame_blending(value);
    }
}

auto internal_m2n_particle_emitter_get_size_by_speed_multiplier(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_size_by_speed_multiplier();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_by_speed_multiplier(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_by_speed_multiplier(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_size_by_speed_speed_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_size_by_speed_speed_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_by_speed_speed_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_by_speed_speed_range(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_color_intensity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_color_intensity();
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_intensity(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_intensity(value);
    }
}

auto internal_m2n_particle_emitter_get_color_by_speed_speed_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_color_by_speed_speed_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_by_speed_speed_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_by_speed_speed_range(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_lifetime_by_emitter_speed_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime_by_emitter_speed_range(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_render_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_render_mode());
    }
    return {};
}

void internal_m2n_particle_emitter_set_render_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::render_mode>(value, "renderMode"))
        {
            comp->set_render_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_align_to_direction(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_align_to_direction();
    }
    return {};
}

void internal_m2n_particle_emitter_set_align_to_direction(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_align_to_direction(value);
    }
}

auto internal_m2n_particle_emitter_get_pivot(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_pivot();
    }
    return {};
}

void internal_m2n_particle_emitter_set_pivot(entt::entity id, const math::vec2& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_pivot(value);
    }
}

auto internal_m2n_particle_emitter_get_texture_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_texture_mode());
    }
    return {};
}

void internal_m2n_particle_emitter_set_texture_mode(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::texture_mode>(value, "textureMode"))
        {
            comp->set_texture_mode(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_tiles(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture_sheet_tiles();
    }
    return {};
}

void internal_m2n_particle_emitter_set_texture_sheet_tiles(entt::entity id, const math::vec2& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_tiles(value);
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_cycles(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture_sheet_cycles();
    }
    return {};
}

void internal_m2n_particle_emitter_set_texture_sheet_cycles(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_cycles(value);
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_randomize(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture_sheet_randomize();
    }
    return {};
}

void internal_m2n_particle_emitter_set_texture_sheet_randomize(entt::entity id, bool value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_randomize(value);
    }
}

//------------------------------
// Gradients cross as arrays of keys. Managed code passes and receives them as raw bytes
// (byte[] <-> the key structs below), which carry any blittable layout.

/// Managed GradientKey.
struct gradient_key
{
    float time{};
    dotnetpp_backend::managed_interface::color color{};
};

/// Managed CurveKey.
struct curve_value_key
{
    float time{};
    float value{};
};

/// Managed MinMaxKey.
struct curve_range_key
{
    float time{};
    float min{};
    float max{};
};

/// Managed Vector3Key.
struct curve_vector3_key
{
    float time{};
    dotnetpp_backend::managed_interface::vector3 value{};
};

/// Managed EmitParams (ps_soa::emit_params with managed vector and color types).
struct managed_emit_params
{
    dotnetpp_backend::managed_interface::vector3 position{};
    dotnetpp_backend::managed_interface::vector3 velocity{};
    dotnetpp_backend::managed_interface::color color{};
    float size{1.0f};
    float lifetime{1.0f};
    uint32_t mask{};
};

/// @p base with the points of @p keys: keys that stay where @p base has keys keep their interpolation and
/// tangents, so a curve read by a script and written back is unchanged.
template<typename T, typename Key, typename ToElement>
auto make_gradient(const std::vector<Key>& keys, ToElement&& to_element, const math::gradient<T>& base) -> math::gradient<T>
{
    typename math::gradient<T>::points_t points;
    points.reserve(keys.size());
    for(const Key& key : keys)
    {
        points.push_back({key.time, to_element(key)});
    }
    std::sort(points.begin(), points.end());
    math::gradient<T> gradient = base;
    gradient.set_points_keeping_shape(points);
    return gradient;
}

auto to_value_keys(const math::gradient<float>& curve) -> std::vector<curve_value_key>
{
    std::vector<curve_value_key> keys;
    for(const auto& point : curve.get_points())
    {
        keys.push_back({point.progress, point.element});
    }
    return keys;
}

auto to_value_curve(const std::vector<curve_value_key>& keys, const math::gradient<float>& base) -> math::gradient<float>
{
    return make_gradient<float>(keys,
                                [](const curve_value_key& key)
                                {
                                    return key.value;
                                },
                                base);
}

auto to_color_keys(const math::gradient<math::color>& gradient) -> std::vector<gradient_key>
{
    using converter = dotnet::managed_interface::converter;
    std::vector<gradient_key> keys;
    for(const auto& point : gradient.get_points())
    {
        keys.push_back({point.progress,
                        converter::convert<math::color, dotnetpp_backend::managed_interface::color>(point.element)});
    }
    return keys;
}

auto to_color_gradient(const std::vector<gradient_key>& keys, const math::gradient<math::color>& base)
    -> math::gradient<math::color>
{
    using converter = dotnet::managed_interface::converter;
    return make_gradient<math::color>(keys,
                                      [](const gradient_key& key)
                                      {
                                          return converter::convert<dotnetpp_backend::managed_interface::color,
                                                                    math::color>(key.color);
                                      },
                                      base);
}

auto to_range_keys(const math::gradient<frange_t>& gradient) -> std::vector<curve_range_key>
{
    std::vector<curve_range_key> keys;
    for(const auto& point : gradient.get_points())
    {
        keys.push_back({point.progress, point.element.min, point.element.max});
    }
    return keys;
}

auto to_range_gradient(const std::vector<curve_range_key>& keys, const math::gradient<frange_t>& base)
    -> math::gradient<frange_t>
{
    return make_gradient<frange_t>(keys,
                                   [](const curve_range_key& key)
                                   {
                                       return frange_t(key.min, key.max);
                                   },
                                   base);
}

auto to_vector3_keys(const math::gradient<math::vec3>& curve) -> std::vector<curve_vector3_key>
{
    using converter = dotnet::managed_interface::converter;
    std::vector<curve_vector3_key> keys;
    for(const auto& point : curve.get_points())
    {
        keys.push_back(
            {point.progress, converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(point.element)});
    }
    return keys;
}

auto to_vector3_curve(const std::vector<curve_vector3_key>& keys, const math::gradient<math::vec3>& base)
    -> math::gradient<math::vec3>
{
    using converter = dotnet::managed_interface::converter;
    return make_gradient<math::vec3>(keys,
                                     [](const curve_vector3_key& key)
                                     {
                                         return converter::convert<dotnetpp_backend::managed_interface::vector3,
                                                                   math::vec3>(key.value);
                                     },
                                     base);
}

auto internal_m2n_particle_emitter_get_color_gradient(entt::entity id) -> std::vector<gradient_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_color_keys(comp->get_color_gradient());
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_gradient(entt::entity id, const std::vector<gradient_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_gradient(to_color_gradient(keys, comp->get_color_gradient()));
    }
}

auto internal_m2n_particle_emitter_get_color_by_speed_gradient(entt::entity id) -> std::vector<gradient_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_color_keys(comp->get_color_by_speed_gradient());
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_by_speed_gradient(entt::entity id, const std::vector<gradient_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_by_speed_gradient(to_color_gradient(keys, comp->get_color_by_speed_gradient()));
    }
}

auto internal_m2n_particle_emitter_get_speed_over_life(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_speed_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_speed_over_life(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_speed_over_life(to_value_curve(keys, comp->get_speed_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_size_over_life(entt::entity id) -> std::vector<curve_range_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_range_keys(comp->get_size_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_over_life(entt::entity id, const std::vector<curve_range_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_over_life(to_range_gradient(keys, comp->get_size_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_curve(entt::entity id)
    -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_lifetime_by_emitter_speed_curve());
    }
    return {};
}

void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_curve(entt::entity id,
                                                                         const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime_by_emitter_speed_curve(to_value_curve(keys, comp->get_lifetime_by_emitter_speed_curve()));
    }
}

auto internal_m2n_particle_emitter_get_stretch_length_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_stretch_length_scale();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_stretch_length_scale(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_stretch_length_scale(value);
    }
}

auto internal_m2n_particle_emitter_get_stretch_camera_velocity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_stretch_camera_velocity_scale();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_stretch_camera_velocity_scale(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_stretch_camera_velocity_scale(value);
    }
}

auto internal_m2n_particle_emitter_get_facing_axis(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_facing_axis();
    }
    return math::vec3{0.0f, 1.0f, 0.0f};
}

void internal_m2n_particle_emitter_set_facing_axis(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_facing_axis(value);
    }
}

auto internal_m2n_particle_emitter_get_stretch_speed_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_stretch_speed_scale();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_stretch_speed_scale(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_stretch_speed_scale(value);
    }
}

auto internal_m2n_particle_emitter_get_camera_offset(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_camera_offset();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_camera_offset(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_camera_offset(value);
    }
}

auto internal_m2n_particle_emitter_get_max_screen_size(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_max_screen_size();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_max_screen_size(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_max_screen_size(value);
    }
}

auto internal_m2n_particle_emitter_get_taa_response(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_taa_response();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_taa_response(entt::entity id, float value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_taa_response(value);
    }
}

auto internal_m2n_particle_emitter_get_camera_fade_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_camera_fade_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_camera_fade_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_camera_fade_range(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_subpixel_fade(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_subpixel_fade();
    }
    return false;
}

void internal_m2n_particle_emitter_set_subpixel_fade(entt::entity id, bool fade)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_subpixel_fade(fade);
    }
}

auto internal_m2n_particle_emitter_get_size_axes_over_life(entt::entity id) -> std::vector<curve_vector3_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_vector3_keys(comp->get_size_axes_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_axes_over_life(entt::entity id, const std::vector<curve_vector3_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_axes_over_life(to_vector3_curve(keys, comp->get_size_axes_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_color_gradient_b(entt::entity id) -> std::vector<gradient_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_color_keys(comp->get_color_gradient_b());
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_gradient_b(entt::entity id, const std::vector<gradient_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_gradient_b(to_color_gradient(keys, comp->get_color_gradient_b()));
    }
}

auto internal_m2n_particle_emitter_get_intensity_over_life(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_intensity_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_intensity_over_life(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_intensity_over_life(to_value_curve(keys, comp->get_intensity_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_spin_over_life(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_spin_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_spin_over_life(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_spin_over_life(to_value_curve(keys, comp->get_spin_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_texture_sheet_frame_curve(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_texture_sheet_frame_curve());
    }
    return {};
}

void internal_m2n_particle_emitter_set_texture_sheet_frame_curve(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_texture_sheet_frame_curve(to_value_curve(keys, comp->get_texture_sheet_frame_curve()));
    }
}

/// Managed ParticleBurst.
struct burst_key
{
    float time{};
    float count_min{};
    float count_max{};
    uint32_t cycles{};
    float interval{};
    float probability{};
};

auto internal_m2n_particle_emitter_get_bursts(entt::entity id) -> std::vector<burst_key>
{
    std::vector<burst_key> keys;
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        for(const auto& burst : comp->get_bursts())
        {
            keys.push_back({burst.time, burst.count.min, burst.count.max, burst.cycles, burst.interval, burst.probability});
        }
    }
    return keys;
}

void internal_m2n_particle_emitter_set_bursts(entt::entity id, const std::vector<burst_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        std::vector<ps_soa::emitter_burst> bursts;
        for(const auto& key : keys)
        {
            bursts.push_back({key.time, frange_t(key.count_min, key.count_max), key.cycles, key.interval, key.probability});
        }
        comp->set_bursts(bursts);
    }
}

auto internal_m2n_particle_emitter_get_turbulence_over_life(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_turbulence_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_turbulence_over_life(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_turbulence_over_life(to_value_curve(keys, comp->get_turbulence_over_life()));
    }
}

auto internal_m2n_particle_emitter_get_collision(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_collision());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_collision(entt::entity id, int value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        if(const auto valid = to_reflected_enum<ps_soa::collision_mode>(value, "collision"))
        {
            comp->set_collision(*valid);
        }
    }
}

auto internal_m2n_particle_emitter_get_collision_planes(entt::entity id) -> std::vector<math::vec4>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_collision_planes();
    }
    return {};
}

void internal_m2n_particle_emitter_set_collision_planes(entt::entity id, const std::vector<math::vec4>& planes)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_collision_planes(planes);
    }
}

/// Managed ParticleSubEmitter (ints where C# would marshal a bool differently from its layout).
struct sub_emitter_key
{
    int32_t trigger{};
    entt::entity target{entt::null};
    float count_min{};
    float count_max{};
    float probability{};
    float inherit_velocity{};
    int32_t inherit_color{};
    int32_t inherit_size{};
};

auto internal_m2n_particle_emitter_get_sub_emitters(entt::entity id) -> std::vector<sub_emitter_key>
{
    std::vector<sub_emitter_key> keys;
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        for(const auto& sub_emitter : comp->get_sub_emitters())
        {
            keys.push_back({static_cast<int32_t>(sub_emitter.trigger),
                            sub_emitter.target ? sub_emitter.target.entity() : entt::entity(entt::null),
                            sub_emitter.count.min,
                            sub_emitter.count.max,
                            sub_emitter.probability,
                            sub_emitter.inherit_velocity,
                            sub_emitter.inherit_color ? 1 : 0,
                            sub_emitter.inherit_size ? 1 : 0});
        }
    }
    return keys;
}

void internal_m2n_particle_emitter_set_sub_emitters(entt::entity id, const std::vector<sub_emitter_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        std::vector<particle_sub_emitter> sub_emitters;
        for(const auto& key : keys)
        {
            const auto trigger = to_reflected_enum<ps_soa::particle_event_type>(key.trigger, "subEmitters trigger");
            if(!trigger)
            {
                continue;
            }
            particle_sub_emitter sub_emitter;
            sub_emitter.trigger = *trigger;
            sub_emitter.target = get_entity_from_id(key.target);
            sub_emitter.count = frange_t(key.count_min, key.count_max);
            sub_emitter.probability = key.probability;
            sub_emitter.inherit_velocity = key.inherit_velocity;
            sub_emitter.inherit_color = key.inherit_color != 0;
            sub_emitter.inherit_size = key.inherit_size != 0;
            sub_emitters.push_back(sub_emitter);
        }
        comp->set_sub_emitters(sub_emitters);
    }
}

auto internal_m2n_particle_emitter_get_erosion_over_life(entt::entity id) -> std::vector<curve_value_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_value_keys(comp->get_erosion_over_life());
    }
    return {};
}

void internal_m2n_particle_emitter_set_erosion_over_life(entt::entity id, const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_erosion_over_life(to_value_curve(keys, comp->get_erosion_over_life()));
    }
}

//------------------------------

auto internal_m2n_particle_emitter_get_world_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_world_bounds();
    }
    return math::bbox::empty;
}

auto internal_m2n_particle_emitter_is_stopped(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_stopped();
    }
    return true;
}

auto internal_m2n_particle_emitter_is_simulated(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_simulated();
    }
    return false;
}

void internal_m2n_particle_emitter_play_child_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->play_child_emitters();
    }
}

void internal_m2n_particle_emitter_stop_child_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_child_emitters();
    }
}

void internal_m2n_particle_emitter_stop_and_reset_child_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_and_reset_child_emitters();
    }
}

void internal_m2n_particle_emitter_pause_child_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->pause_child_emitters();
    }
}

void internal_m2n_particle_emitter_resume_child_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->resume_child_emitters();
    }
}

void internal_m2n_particle_emitter_emit(entt::entity id, uint32_t count, const managed_emit_params& params)
{
    using converter = dotnet::managed_interface::converter;
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        ps_soa::emit_params native;
        native.position = converter::convert<dotnetpp_backend::managed_interface::vector3, math::vec3>(params.position);
        native.velocity = converter::convert<dotnetpp_backend::managed_interface::vector3, math::vec3>(params.velocity);
        native.color = converter::convert<dotnetpp_backend::managed_interface::color, math::color>(params.color);
        native.size = params.size;
        native.lifetime = params.lifetime;
        native.mask = params.mask;
        comp->emit(count, native);
    }
}

} // namespace

void register_particle_emitter_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.ParticleEmitterComponent");
    reg.add_internal_call("internal_m2n_particle_emitter_get_enabled", 
                            script_internal_call(internal_m2n_particle_emitter_get_enabled));
    reg.add_internal_call("internal_m2n_particle_emitter_set_enabled", 
                            script_internal_call(internal_m2n_particle_emitter_set_enabled));
    reg.add_internal_call("internal_m2n_particle_emitter_get_max_particles", 
                            script_internal_call(internal_m2n_particle_emitter_get_max_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_max_particles", 
                            script_internal_call(internal_m2n_particle_emitter_set_max_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_shape", 
                            script_internal_call(internal_m2n_particle_emitter_get_shape));
    reg.add_internal_call("internal_m2n_particle_emitter_set_shape", 
                            script_internal_call(internal_m2n_particle_emitter_set_shape));
    reg.add_internal_call("internal_m2n_particle_emitter_get_direction", 
                            script_internal_call(internal_m2n_particle_emitter_get_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_direction", 
                            script_internal_call(internal_m2n_particle_emitter_set_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_gravity_scale", 
                            script_internal_call(internal_m2n_particle_emitter_get_gravity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_gravity_scale", 
                            script_internal_call(internal_m2n_particle_emitter_set_gravity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_emission_rate", 
                            script_internal_call(internal_m2n_particle_emitter_get_emission_rate));
    reg.add_internal_call("internal_m2n_particle_emitter_set_emission_rate", 
                            script_internal_call(internal_m2n_particle_emitter_set_emission_rate));
    reg.add_internal_call("internal_m2n_particle_emitter_get_spawn_along_path", 
                            script_internal_call(internal_m2n_particle_emitter_get_spawn_along_path));
    reg.add_internal_call("internal_m2n_particle_emitter_set_spawn_along_path", 
                            script_internal_call(internal_m2n_particle_emitter_set_spawn_along_path));
    reg.add_internal_call("internal_m2n_particle_emitter_get_drag",
                            script_internal_call(internal_m2n_particle_emitter_get_drag));
    reg.add_internal_call("internal_m2n_particle_emitter_set_drag",
                            script_internal_call(internal_m2n_particle_emitter_set_drag));
    reg.add_internal_call("internal_m2n_particle_emitter_get_inherit_velocity",
                            script_internal_call(internal_m2n_particle_emitter_get_inherit_velocity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_inherit_velocity",
                            script_internal_call(internal_m2n_particle_emitter_set_inherit_velocity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_direction_spread",
                            script_internal_call(internal_m2n_particle_emitter_get_direction_spread));
    reg.add_internal_call("internal_m2n_particle_emitter_set_direction_spread",
                            script_internal_call(internal_m2n_particle_emitter_set_direction_spread));
    reg.add_internal_call("internal_m2n_particle_emitter_get_use_fixed_seed",
                            script_internal_call(internal_m2n_particle_emitter_get_use_fixed_seed));
    reg.add_internal_call("internal_m2n_particle_emitter_set_use_fixed_seed",
                            script_internal_call(internal_m2n_particle_emitter_set_use_fixed_seed));
    reg.add_internal_call("internal_m2n_particle_emitter_get_random_seed",
                            script_internal_call(internal_m2n_particle_emitter_get_random_seed));
    reg.add_internal_call("internal_m2n_particle_emitter_set_random_seed",
                            script_internal_call(internal_m2n_particle_emitter_set_random_seed));
    reg.add_internal_call("internal_m2n_particle_emitter_get_sort_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_sort_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_sort_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_sort_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_primitive",
                            script_internal_call(internal_m2n_particle_emitter_get_primitive));
    reg.add_internal_call("internal_m2n_particle_emitter_set_primitive",
                            script_internal_call(internal_m2n_particle_emitter_set_primitive));
    reg.add_internal_call("internal_m2n_particle_emitter_get_half_resolution",
                            script_internal_call(internal_m2n_particle_emitter_get_half_resolution));
    reg.add_internal_call("internal_m2n_particle_emitter_set_half_resolution",
                            script_internal_call(internal_m2n_particle_emitter_set_half_resolution));
    reg.add_internal_call("internal_m2n_particle_emitter_get_write_velocity",
                            script_internal_call(internal_m2n_particle_emitter_get_write_velocity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_write_velocity",
                            script_internal_call(internal_m2n_particle_emitter_set_write_velocity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_velocity_alpha_threshold",
                            script_internal_call(internal_m2n_particle_emitter_get_velocity_alpha_threshold));
    reg.add_internal_call("internal_m2n_particle_emitter_set_velocity_alpha_threshold",
                            script_internal_call(internal_m2n_particle_emitter_set_velocity_alpha_threshold));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_axes_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_size_axes_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_axes_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_size_axes_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_random_between_gradients",
                            script_internal_call(internal_m2n_particle_emitter_get_random_between_gradients));
    reg.add_internal_call("internal_m2n_particle_emitter_set_random_between_gradients",
                            script_internal_call(internal_m2n_particle_emitter_set_random_between_gradients));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_gradient_b",
                            script_internal_call(internal_m2n_particle_emitter_get_color_gradient_b));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_gradient_b",
                            script_internal_call(internal_m2n_particle_emitter_set_color_gradient_b));
    reg.add_internal_call("internal_m2n_particle_emitter_get_intensity_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_intensity_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_intensity_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_intensity_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_spin_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_spin_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_spin_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_spin_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_use_frame_curve",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_use_frame_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_use_frame_curve",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_use_frame_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_frame_curve",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_frame_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_frame_curve",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_frame_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_random_row",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_random_row));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_random_row",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_random_row));
    reg.add_internal_call("internal_m2n_particle_emitter_get_rate_over_distance",
                            script_internal_call(internal_m2n_particle_emitter_get_rate_over_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_set_rate_over_distance",
                            script_internal_call(internal_m2n_particle_emitter_set_rate_over_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_get_bursts",
                            script_internal_call(internal_m2n_particle_emitter_get_bursts));
    reg.add_internal_call("internal_m2n_particle_emitter_set_bursts",
                            script_internal_call(internal_m2n_particle_emitter_set_bursts));
    reg.add_internal_call("internal_m2n_particle_emitter_get_cone_angle",
                            script_internal_call(internal_m2n_particle_emitter_get_cone_angle));
    reg.add_internal_call("internal_m2n_particle_emitter_set_cone_angle",
                            script_internal_call(internal_m2n_particle_emitter_set_cone_angle));
    reg.add_internal_call("internal_m2n_particle_emitter_get_torus_thickness",
                            script_internal_call(internal_m2n_particle_emitter_get_torus_thickness));
    reg.add_internal_call("internal_m2n_particle_emitter_set_torus_thickness",
                            script_internal_call(internal_m2n_particle_emitter_set_torus_thickness));
    reg.add_internal_call("internal_m2n_particle_emitter_get_shape_mesh",
                            script_internal_call(internal_m2n_particle_emitter_get_shape_mesh));
    reg.add_internal_call("internal_m2n_particle_emitter_set_shape_mesh",
                            script_internal_call(internal_m2n_particle_emitter_set_shape_mesh));
    reg.add_internal_call("internal_m2n_particle_emitter_get_render_mesh",
                            script_internal_call(internal_m2n_particle_emitter_get_render_mesh));
    reg.add_internal_call("internal_m2n_particle_emitter_set_render_mesh",
                            script_internal_call(internal_m2n_particle_emitter_set_render_mesh));
    reg.add_internal_call("internal_m2n_particle_emitter_get_render_material",
                            script_internal_call(internal_m2n_particle_emitter_get_render_material));
    reg.add_internal_call("internal_m2n_particle_emitter_set_render_material",
                            script_internal_call(internal_m2n_particle_emitter_set_render_material));
    reg.add_internal_call("internal_m2n_particle_emitter_get_mesh_cast_shadows",
                            script_internal_call(internal_m2n_particle_emitter_get_mesh_cast_shadows));
    reg.add_internal_call("internal_m2n_particle_emitter_set_mesh_cast_shadows",
                            script_internal_call(internal_m2n_particle_emitter_set_mesh_cast_shadows));
    reg.add_internal_call("internal_m2n_particle_emitter_get_mesh_spawn_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_mesh_spawn_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_mesh_spawn_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_mesh_spawn_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_turbulence_strength",
                            script_internal_call(internal_m2n_particle_emitter_get_turbulence_strength));
    reg.add_internal_call("internal_m2n_particle_emitter_set_turbulence_strength",
                            script_internal_call(internal_m2n_particle_emitter_set_turbulence_strength));
    reg.add_internal_call("internal_m2n_particle_emitter_get_turbulence_frequency",
                            script_internal_call(internal_m2n_particle_emitter_get_turbulence_frequency));
    reg.add_internal_call("internal_m2n_particle_emitter_set_turbulence_frequency",
                            script_internal_call(internal_m2n_particle_emitter_set_turbulence_frequency));
    reg.add_internal_call("internal_m2n_particle_emitter_get_turbulence_speed",
                            script_internal_call(internal_m2n_particle_emitter_get_turbulence_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_set_turbulence_speed",
                            script_internal_call(internal_m2n_particle_emitter_set_turbulence_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_get_turbulence_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_turbulence_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_turbulence_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_turbulence_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_max_draw_distance",
                            script_internal_call(internal_m2n_particle_emitter_get_max_draw_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_set_max_draw_distance",
                            script_internal_call(internal_m2n_particle_emitter_set_max_draw_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_get_draw_distance_fade",
                            script_internal_call(internal_m2n_particle_emitter_get_draw_distance_fade));
    reg.add_internal_call("internal_m2n_particle_emitter_set_draw_distance_fade",
                            script_internal_call(internal_m2n_particle_emitter_set_draw_distance_fade));
    reg.add_internal_call("internal_m2n_particle_emitter_get_normal_texture",
                            script_internal_call(internal_m2n_particle_emitter_get_normal_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_set_normal_texture",
                            script_internal_call(internal_m2n_particle_emitter_set_normal_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_get_cutout_alpha_threshold",
                            script_internal_call(internal_m2n_particle_emitter_get_cutout_alpha_threshold));
    reg.add_internal_call("internal_m2n_particle_emitter_set_cutout_alpha_threshold",
                            script_internal_call(internal_m2n_particle_emitter_set_cutout_alpha_threshold));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lighting_wrap",
                            script_internal_call(internal_m2n_particle_emitter_get_lighting_wrap));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lighting_wrap",
                            script_internal_call(internal_m2n_particle_emitter_set_lighting_wrap));
    reg.add_internal_call("internal_m2n_particle_emitter_get_receive_shadows",
                            script_internal_call(internal_m2n_particle_emitter_get_receive_shadows));
    reg.add_internal_call("internal_m2n_particle_emitter_set_receive_shadows",
                            script_internal_call(internal_m2n_particle_emitter_set_receive_shadows));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lighting",
                            script_internal_call(internal_m2n_particle_emitter_get_lighting));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lighting",
                            script_internal_call(internal_m2n_particle_emitter_set_lighting));
    reg.add_internal_call("internal_m2n_particle_emitter_get_light_count",
                            script_internal_call(internal_m2n_particle_emitter_get_light_count));
    reg.add_internal_call("internal_m2n_particle_emitter_set_light_count",
                            script_internal_call(internal_m2n_particle_emitter_set_light_count));
    reg.add_internal_call("internal_m2n_particle_emitter_get_light_intensity",
                            script_internal_call(internal_m2n_particle_emitter_get_light_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_light_intensity",
                            script_internal_call(internal_m2n_particle_emitter_set_light_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_light_range",
                            script_internal_call(internal_m2n_particle_emitter_get_light_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_light_range",
                            script_internal_call(internal_m2n_particle_emitter_set_light_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_light_color_from_particle",
                            script_internal_call(internal_m2n_particle_emitter_get_light_color_from_particle));
    reg.add_internal_call("internal_m2n_particle_emitter_set_light_color_from_particle",
                            script_internal_call(internal_m2n_particle_emitter_set_light_color_from_particle));
    reg.add_internal_call("internal_m2n_particle_emitter_get_restitution",
                            script_internal_call(internal_m2n_particle_emitter_get_restitution));
    reg.add_internal_call("internal_m2n_particle_emitter_set_restitution",
                            script_internal_call(internal_m2n_particle_emitter_set_restitution));
    reg.add_internal_call("internal_m2n_particle_emitter_get_friction",
                            script_internal_call(internal_m2n_particle_emitter_get_friction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_friction",
                            script_internal_call(internal_m2n_particle_emitter_set_friction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_collision_radius_scale",
                            script_internal_call(internal_m2n_particle_emitter_get_collision_radius_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_collision_radius_scale",
                            script_internal_call(internal_m2n_particle_emitter_set_collision_radius_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_max_bounces",
                            script_internal_call(internal_m2n_particle_emitter_get_max_bounces));
    reg.add_internal_call("internal_m2n_particle_emitter_set_max_bounces",
                            script_internal_call(internal_m2n_particle_emitter_set_max_bounces));
    reg.add_internal_call("internal_m2n_particle_emitter_get_collision",
                            script_internal_call(internal_m2n_particle_emitter_get_collision));
    reg.add_internal_call("internal_m2n_particle_emitter_set_collision",
                            script_internal_call(internal_m2n_particle_emitter_set_collision));
    reg.add_internal_call("internal_m2n_particle_emitter_get_collision_planes",
                            script_internal_call(internal_m2n_particle_emitter_get_collision_planes));
    reg.add_internal_call("internal_m2n_particle_emitter_set_collision_planes",
                            script_internal_call(internal_m2n_particle_emitter_set_collision_planes));
    reg.add_internal_call("internal_m2n_particle_emitter_get_sub_emitters",
                            script_internal_call(internal_m2n_particle_emitter_get_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_set_sub_emitters",
                            script_internal_call(internal_m2n_particle_emitter_set_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_get_trail_length",
                            script_internal_call(internal_m2n_particle_emitter_get_trail_length));
    reg.add_internal_call("internal_m2n_particle_emitter_set_trail_length",
                            script_internal_call(internal_m2n_particle_emitter_set_trail_length));
    reg.add_internal_call("internal_m2n_particle_emitter_get_ribbon_texture_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_ribbon_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_ribbon_texture_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_ribbon_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_ribbon_tile_time",
                            script_internal_call(internal_m2n_particle_emitter_get_ribbon_tile_time));
    reg.add_internal_call("internal_m2n_particle_emitter_set_ribbon_tile_time",
                            script_internal_call(internal_m2n_particle_emitter_set_ribbon_tile_time));
    reg.add_internal_call("internal_m2n_particle_emitter_get_ribbon_facing",
                            script_internal_call(internal_m2n_particle_emitter_get_ribbon_facing));
    reg.add_internal_call("internal_m2n_particle_emitter_set_ribbon_facing",
                            script_internal_call(internal_m2n_particle_emitter_set_ribbon_facing));
    reg.add_internal_call("internal_m2n_particle_emitter_get_trail_width_end",
                            script_internal_call(internal_m2n_particle_emitter_get_trail_width_end));
    reg.add_internal_call("internal_m2n_particle_emitter_set_trail_width_end",
                            script_internal_call(internal_m2n_particle_emitter_set_trail_width_end));
    reg.add_internal_call("internal_m2n_particle_emitter_get_bounds_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_bounds_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_bounds_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_bounds_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_fixed_bounds",
                            script_internal_call(internal_m2n_particle_emitter_get_fixed_bounds));
    reg.add_internal_call("internal_m2n_particle_emitter_set_fixed_bounds",
                            script_internal_call(internal_m2n_particle_emitter_set_fixed_bounds));
    reg.add_internal_call("internal_m2n_particle_emitter_get_opacity", 
                            script_internal_call(internal_m2n_particle_emitter_get_opacity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_opacity", 
                            script_internal_call(internal_m2n_particle_emitter_set_opacity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_acceleration", 
                            script_internal_call(internal_m2n_particle_emitter_get_acceleration));
    reg.add_internal_call("internal_m2n_particle_emitter_set_acceleration", 
                            script_internal_call(internal_m2n_particle_emitter_set_acceleration));
    reg.add_internal_call("internal_m2n_particle_emitter_get_shape_scale", 
                            script_internal_call(internal_m2n_particle_emitter_get_shape_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_shape_scale", 
                            script_internal_call(internal_m2n_particle_emitter_set_shape_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_duration",
                            script_internal_call(internal_m2n_particle_emitter_get_duration));
    reg.add_internal_call("internal_m2n_particle_emitter_set_duration",
                            script_internal_call(internal_m2n_particle_emitter_set_duration));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime_range",
                            script_internal_call(internal_m2n_particle_emitter_get_lifetime_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime_range",
                            script_internal_call(internal_m2n_particle_emitter_set_lifetime_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_start_speed",
                            script_internal_call(internal_m2n_particle_emitter_get_start_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_set_start_speed",
                            script_internal_call(internal_m2n_particle_emitter_set_start_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_get_motion_easing", 
                            script_internal_call(internal_m2n_particle_emitter_get_motion_easing));
    reg.add_internal_call("internal_m2n_particle_emitter_set_motion_easing", 
                            script_internal_call(internal_m2n_particle_emitter_set_motion_easing));
    reg.add_internal_call("internal_m2n_particle_emitter_get_num_particles", 
                            script_internal_call(internal_m2n_particle_emitter_get_num_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_is_playing", 
                            script_internal_call(internal_m2n_particle_emitter_is_playing));
    reg.add_internal_call("internal_m2n_particle_emitter_is_paused", 
                            script_internal_call(internal_m2n_particle_emitter_is_paused));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture", 
                            script_internal_call(internal_m2n_particle_emitter_get_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture", 
                            script_internal_call(internal_m2n_particle_emitter_set_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_play", 
                            script_internal_call(internal_m2n_particle_emitter_play));
    reg.add_internal_call("internal_m2n_particle_emitter_stop", 
                            script_internal_call(internal_m2n_particle_emitter_stop));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_and_reset", 
                            script_internal_call(internal_m2n_particle_emitter_stop_and_reset));
    reg.add_internal_call("internal_m2n_particle_emitter_pause", 
                            script_internal_call(internal_m2n_particle_emitter_pause));
    reg.add_internal_call("internal_m2n_particle_emitter_resume", 
                            script_internal_call(internal_m2n_particle_emitter_resume));
    reg.add_internal_call("internal_m2n_particle_emitter_reset_emitter", 
                            script_internal_call(internal_m2n_particle_emitter_reset_emitter));
    reg.add_internal_call("internal_m2n_particle_emitter_get_loop", 
                            script_internal_call(internal_m2n_particle_emitter_get_loop));
    reg.add_internal_call("internal_m2n_particle_emitter_set_loop", 
                            script_internal_call(internal_m2n_particle_emitter_set_loop));
    reg.add_internal_call("internal_m2n_particle_emitter_get_blend_mode", 
                            script_internal_call(internal_m2n_particle_emitter_get_blend_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_blend_mode", 
                            script_internal_call(internal_m2n_particle_emitter_set_blend_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_sort_backend",
                            script_internal_call(internal_m2n_particle_emitter_get_sort_backend));
    reg.add_internal_call("internal_m2n_particle_emitter_set_sort_backend",
                            script_internal_call(internal_m2n_particle_emitter_set_sort_backend));
    reg.add_internal_call("internal_m2n_particle_emitter_get_culling_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_culling_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_culling_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_culling_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_simulation_space",
                            script_internal_call(internal_m2n_particle_emitter_get_simulation_space));
    reg.add_internal_call("internal_m2n_particle_emitter_set_simulation_space",
                            script_internal_call(internal_m2n_particle_emitter_set_simulation_space));
    reg.add_internal_call("internal_m2n_particle_emitter_get_start_delay",
                            script_internal_call(internal_m2n_particle_emitter_get_start_delay));
    reg.add_internal_call("internal_m2n_particle_emitter_set_start_delay",
                            script_internal_call(internal_m2n_particle_emitter_set_start_delay));
    reg.add_internal_call("internal_m2n_particle_emitter_get_spawn_location",
                            script_internal_call(internal_m2n_particle_emitter_get_spawn_location));
    reg.add_internal_call("internal_m2n_particle_emitter_set_spawn_location",
                            script_internal_call(internal_m2n_particle_emitter_set_spawn_location));
    reg.add_internal_call("internal_m2n_particle_emitter_get_shape_position",
                            script_internal_call(internal_m2n_particle_emitter_get_shape_position));
    reg.add_internal_call("internal_m2n_particle_emitter_set_shape_position",
                            script_internal_call(internal_m2n_particle_emitter_set_shape_position));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_axes",
                            script_internal_call(internal_m2n_particle_emitter_get_size_axes));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_axes",
                            script_internal_call(internal_m2n_particle_emitter_set_size_axes));
    reg.add_internal_call("internal_m2n_particle_emitter_get_rotation",
                            script_internal_call(internal_m2n_particle_emitter_get_rotation));
    reg.add_internal_call("internal_m2n_particle_emitter_set_rotation",
                            script_internal_call(internal_m2n_particle_emitter_set_rotation));
    reg.add_internal_call("internal_m2n_particle_emitter_get_spin_speed",
                            script_internal_call(internal_m2n_particle_emitter_get_spin_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_set_spin_speed",
                            script_internal_call(internal_m2n_particle_emitter_set_spin_speed));
    reg.add_internal_call("internal_m2n_particle_emitter_get_random_spin_direction",
                            script_internal_call(internal_m2n_particle_emitter_get_random_spin_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_random_spin_direction",
                            script_internal_call(internal_m2n_particle_emitter_set_random_spin_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_soft_fade_distance",
                            script_internal_call(internal_m2n_particle_emitter_get_soft_fade_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_set_soft_fade_distance",
                            script_internal_call(internal_m2n_particle_emitter_set_soft_fade_distance));
    reg.add_internal_call("internal_m2n_particle_emitter_get_prewarm",
                            script_internal_call(internal_m2n_particle_emitter_get_prewarm));
    reg.add_internal_call("internal_m2n_particle_emitter_set_prewarm",
                            script_internal_call(internal_m2n_particle_emitter_set_prewarm));
    reg.add_internal_call("internal_m2n_particle_emitter_get_auto_play",
                            script_internal_call(internal_m2n_particle_emitter_get_auto_play));
    reg.add_internal_call("internal_m2n_particle_emitter_set_auto_play",
                            script_internal_call(internal_m2n_particle_emitter_set_auto_play));
    reg.add_internal_call("internal_m2n_particle_emitter_get_sort_priority",
                            script_internal_call(internal_m2n_particle_emitter_get_sort_priority));
    reg.add_internal_call("internal_m2n_particle_emitter_set_sort_priority",
                            script_internal_call(internal_m2n_particle_emitter_set_sort_priority));
    reg.add_internal_call("internal_m2n_particle_emitter_get_frame_blending",
                            script_internal_call(internal_m2n_particle_emitter_get_frame_blending));
    reg.add_internal_call("internal_m2n_particle_emitter_set_frame_blending",
                            script_internal_call(internal_m2n_particle_emitter_set_frame_blending));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_by_speed_multiplier",
                            script_internal_call(internal_m2n_particle_emitter_get_size_by_speed_multiplier));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_by_speed_multiplier",
                            script_internal_call(internal_m2n_particle_emitter_set_size_by_speed_multiplier));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_by_speed_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_get_size_by_speed_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_by_speed_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_set_size_by_speed_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_intensity",
                            script_internal_call(internal_m2n_particle_emitter_get_color_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_intensity",
                            script_internal_call(internal_m2n_particle_emitter_set_color_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_by_speed_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_get_color_by_speed_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_by_speed_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_set_color_by_speed_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range",
                            script_internal_call(internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_render_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_render_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_render_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_render_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_align_to_direction",
                            script_internal_call(internal_m2n_particle_emitter_get_align_to_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_align_to_direction",
                            script_internal_call(internal_m2n_particle_emitter_set_align_to_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_pivot",
                            script_internal_call(internal_m2n_particle_emitter_get_pivot));
    reg.add_internal_call("internal_m2n_particle_emitter_set_pivot",
                            script_internal_call(internal_m2n_particle_emitter_set_pivot));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_mode",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_mode",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_tiles",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_tiles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_tiles",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_tiles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_cycles",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_cycles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_cycles",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_cycles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_randomize",
                            script_internal_call(internal_m2n_particle_emitter_get_texture_sheet_randomize));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_randomize",
                            script_internal_call(internal_m2n_particle_emitter_set_texture_sheet_randomize));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_gradient",
                            script_internal_call(internal_m2n_particle_emitter_get_color_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_gradient",
                            script_internal_call(internal_m2n_particle_emitter_set_color_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_by_speed_gradient",
                            script_internal_call(internal_m2n_particle_emitter_get_color_by_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_by_speed_gradient",
                            script_internal_call(internal_m2n_particle_emitter_set_color_by_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_stretch_length_scale",
                            script_internal_call(internal_m2n_particle_emitter_get_stretch_length_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_stretch_length_scale",
                            script_internal_call(internal_m2n_particle_emitter_set_stretch_length_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_stretch_camera_velocity_scale",
                            script_internal_call(internal_m2n_particle_emitter_get_stretch_camera_velocity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_stretch_camera_velocity_scale",
                            script_internal_call(internal_m2n_particle_emitter_set_stretch_camera_velocity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_facing_axis",
                            script_internal_call(internal_m2n_particle_emitter_get_facing_axis));
    reg.add_internal_call("internal_m2n_particle_emitter_set_facing_axis",
                            script_internal_call(internal_m2n_particle_emitter_set_facing_axis));
    reg.add_internal_call("internal_m2n_particle_emitter_get_stretch_speed_scale",
                            script_internal_call(internal_m2n_particle_emitter_get_stretch_speed_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_stretch_speed_scale",
                            script_internal_call(internal_m2n_particle_emitter_set_stretch_speed_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_camera_offset",
                            script_internal_call(internal_m2n_particle_emitter_get_camera_offset));
    reg.add_internal_call("internal_m2n_particle_emitter_set_camera_offset",
                            script_internal_call(internal_m2n_particle_emitter_set_camera_offset));
    reg.add_internal_call("internal_m2n_particle_emitter_get_max_screen_size",
                            script_internal_call(internal_m2n_particle_emitter_get_max_screen_size));
    reg.add_internal_call("internal_m2n_particle_emitter_set_max_screen_size",
                            script_internal_call(internal_m2n_particle_emitter_set_max_screen_size));
    reg.add_internal_call("internal_m2n_particle_emitter_get_taa_response",
                            script_internal_call(internal_m2n_particle_emitter_get_taa_response));
    reg.add_internal_call("internal_m2n_particle_emitter_set_taa_response",
                            script_internal_call(internal_m2n_particle_emitter_set_taa_response));
    reg.add_internal_call("internal_m2n_particle_emitter_get_camera_fade_range",
                            script_internal_call(internal_m2n_particle_emitter_get_camera_fade_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_camera_fade_range",
                            script_internal_call(internal_m2n_particle_emitter_set_camera_fade_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_subpixel_fade",
                            script_internal_call(internal_m2n_particle_emitter_get_subpixel_fade));
    reg.add_internal_call("internal_m2n_particle_emitter_set_subpixel_fade",
                            script_internal_call(internal_m2n_particle_emitter_set_subpixel_fade));
    reg.add_internal_call("internal_m2n_particle_emitter_get_erosion_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_erosion_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_erosion_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_erosion_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_speed_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_speed_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_speed_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_speed_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_over_life",
                            script_internal_call(internal_m2n_particle_emitter_get_size_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_over_life",
                            script_internal_call(internal_m2n_particle_emitter_set_size_over_life));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_curve",
                            script_internal_call(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_curve",
                            script_internal_call(internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_curve));
    reg.add_internal_call("internal_m2n_particle_emitter_get_world_bounds",
                            script_internal_call(internal_m2n_particle_emitter_get_world_bounds));
    reg.add_internal_call("internal_m2n_particle_emitter_is_stopped",
                            script_internal_call(internal_m2n_particle_emitter_is_stopped));
    reg.add_internal_call("internal_m2n_particle_emitter_is_simulated",
                            script_internal_call(internal_m2n_particle_emitter_is_simulated));
    reg.add_internal_call("internal_m2n_particle_emitter_play_child_emitters",
                            script_internal_call(internal_m2n_particle_emitter_play_child_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_child_emitters",
                            script_internal_call(internal_m2n_particle_emitter_stop_child_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_and_reset_child_emitters",
                            script_internal_call(internal_m2n_particle_emitter_stop_and_reset_child_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_pause_child_emitters",
                            script_internal_call(internal_m2n_particle_emitter_pause_child_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_resume_child_emitters",
                            script_internal_call(internal_m2n_particle_emitter_resume_child_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_emit",
                            script_internal_call(internal_m2n_particle_emitter_emit));
}

} // namespace unravel
