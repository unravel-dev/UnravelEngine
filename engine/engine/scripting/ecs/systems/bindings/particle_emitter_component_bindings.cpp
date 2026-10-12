#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>

#include <algorithm>
#include <vector>

namespace unravel
{
namespace
{

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
        comp->set_shape(static_cast<ps_soa::emitter_shape>(shape));
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
        comp->set_direction(static_cast<ps_soa::emitter_direction>(direction));
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

auto internal_m2n_particle_emitter_get_temporal_motion(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_temporal_motion();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_temporal_motion(entt::entity id, float motion)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_temporal_motion(motion);
    }
}

auto internal_m2n_particle_emitter_get_velocity_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_velocity_damping();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_velocity_damping(entt::entity id, float damping)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_velocity_damping(damping);
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

auto internal_m2n_particle_emitter_get_force_over_lifetime(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_force_over_lifetime();
    }
    return math::vec3{0.0f, 0.0f, 0.0f};
}

void internal_m2n_particle_emitter_set_force_over_lifetime(entt::entity id, const math::vec3& force)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_force_over_lifetime(force);
    }
}

auto internal_m2n_particle_emitter_get_emission_shape_scale(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_shape_scale();
    }
    return math::vec3{1.0f, 1.0f, 1.0f};
}

void internal_m2n_particle_emitter_set_emission_shape_scale(entt::entity id, const math::vec3& scale)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_shape_scale(scale);
    }
}

auto internal_m2n_particle_emitter_get_emission_lifetime(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_lifetime().count();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_emission_lifetime(entt::entity id, float lifetime)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_lifetime(std::chrono::duration<float>(lifetime));
    }
}

auto internal_m2n_particle_emitter_get_lifetime(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_lifetime().count();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_lifetime(entt::entity id, float lifetime)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime(std::chrono::duration<float>(lifetime));
    }
}

auto internal_m2n_particle_emitter_get_position_easing(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_position_easing());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_position_easing(entt::entity id, int easing)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_position_easing(static_cast<bx::Easing::Enum>(easing));
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
        comp->set_blend_mode(static_cast<ps_soa::blend_mode>(mode));
    }
}

auto internal_m2n_particle_emitter_get_simulation_backend(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_simulation_backend());
    }
    return static_cast<int>(ps_soa::particle_sim_backend::cpu);
}

void internal_m2n_particle_emitter_set_simulation_backend(entt::entity id, int backend)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_simulation_backend(static_cast<ps_soa::particle_sim_backend>(backend));
    }
}

//------------------------------

using managed_range = dotnetpp_backend::managed_interface::float_range;

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
        comp->set_culling_mode(static_cast<particle_emitter_component::culling_mode>(value));
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
        comp->set_simulation_space(static_cast<ps_soa::simulation_space>(value));
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
        comp->set_spawn_location(static_cast<ps_soa::spawn_location>(value));
    }
}

auto internal_m2n_particle_emitter_get_emission_shape_position(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_shape_position();
    }
    return {};
}

void internal_m2n_particle_emitter_set_emission_shape_position(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_shape_position(value);
    }
}

auto internal_m2n_particle_emitter_get_initial_scale_3d(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_initial_scale_3d();
    }
    return {};
}

void internal_m2n_particle_emitter_set_initial_scale_3d(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_initial_scale_3d(value);
    }
}

auto internal_m2n_particle_emitter_get_size_by_speed_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_size_by_speed_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_by_speed_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_by_speed_range(frange_t(value.min, value.max));
    }
}

auto internal_m2n_particle_emitter_get_size_by_speed_velocity_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_size_by_speed_velocity_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_size_by_speed_velocity_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_size_by_speed_velocity_range(frange_t(value.min, value.max));
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

auto internal_m2n_particle_emitter_get_color_by_speed_velocity_range(entt::entity id) -> managed_range
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        const auto& range = comp->get_color_by_speed_velocity_range();
        return {range.min, range.max};
    }
    return {};
}

void internal_m2n_particle_emitter_set_color_by_speed_velocity_range(entt::entity id, managed_range value)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_color_by_speed_velocity_range(frange_t(value.min, value.max));
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
        comp->set_render_mode(static_cast<ps_soa::render_mode>(value));
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
        comp->set_texture_mode(static_cast<ps_soa::texture_mode>(value));
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

template<typename T, typename Key, typename ToElement>
auto make_gradient(const std::vector<Key>& keys, ToElement&& to_element) -> math::gradient<T>
{
    typename math::gradient<T>::points_t points;
    points.reserve(keys.size());
    for(const Key& key : keys)
    {
        points.push_back({key.time, to_element(key)});
    }
    std::sort(points.begin(), points.end());
    math::gradient<T> gradient;
    gradient.set_points(points);
    return gradient;
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

auto to_color_gradient(const std::vector<gradient_key>& keys) -> math::gradient<math::color>
{
    using converter = dotnet::managed_interface::converter;
    return make_gradient<math::color>(keys,
                                      [](const gradient_key& key)
                                      {
                                          return converter::convert<dotnetpp_backend::managed_interface::color,
                                                                    math::color>(key.color);
                                      });
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

auto to_range_gradient(const std::vector<curve_range_key>& keys) -> math::gradient<frange_t>
{
    return make_gradient<frange_t>(keys,
                                   [](const curve_range_key& key)
                                   {
                                       return frange_t(key.min, key.max);
                                   });
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
        comp->set_color_gradient(to_color_gradient(keys));
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
        comp->set_color_by_speed_gradient(to_color_gradient(keys));
    }
}

auto internal_m2n_particle_emitter_get_velocity_gradient(entt::entity id) -> std::vector<curve_range_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_range_keys(comp->get_velocity_gradient());
    }
    return {};
}

void internal_m2n_particle_emitter_set_velocity_gradient(entt::entity id, const std::vector<curve_range_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_velocity_gradient(to_range_gradient(keys));
    }
}

auto internal_m2n_particle_emitter_get_scale_gradient(entt::entity id) -> std::vector<curve_range_key>
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return to_range_keys(comp->get_scale_gradient());
    }
    return {};
}

void internal_m2n_particle_emitter_set_scale_gradient(entt::entity id, const std::vector<curve_range_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_scale_gradient(to_range_gradient(keys));
    }
}

auto internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_gradient(entt::entity id)
    -> std::vector<curve_value_key>
{
    std::vector<curve_value_key> keys;
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        for(const auto& point : comp->get_lifetime_by_emitter_speed_gradient().get_points())
        {
            keys.push_back({point.progress, point.element});
        }
    }
    return keys;
}

void internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_gradient(entt::entity id,
                                                                         const std::vector<curve_value_key>& keys)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime_by_emitter_speed_gradient(make_gradient<float>(keys,
                                                                          [](const curve_value_key& key)
                                                                          {
                                                                              return key.value;
                                                                          }));
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

void internal_m2n_particle_emitter_play_sub_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->play_sub_emitters();
    }
}

void internal_m2n_particle_emitter_stop_sub_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_sub_emitters();
    }
}

void internal_m2n_particle_emitter_stop_and_reset_sub_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_and_reset_sub_emitters();
    }
}

void internal_m2n_particle_emitter_pause_sub_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->pause_sub_emitters();
    }
}

void internal_m2n_particle_emitter_resume_sub_emitters(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->resume_sub_emitters();
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
                            dotnet_internal_call(internal_m2n_particle_emitter_get_enabled));
    reg.add_internal_call("internal_m2n_particle_emitter_set_enabled", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_enabled));
    reg.add_internal_call("internal_m2n_particle_emitter_get_max_particles", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_max_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_max_particles", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_max_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_shape", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_shape));
    reg.add_internal_call("internal_m2n_particle_emitter_set_shape", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_shape));
    reg.add_internal_call("internal_m2n_particle_emitter_get_direction", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_direction", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_gravity_scale", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_gravity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_gravity_scale", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_gravity_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_emission_rate", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_emission_rate));
    reg.add_internal_call("internal_m2n_particle_emitter_set_emission_rate", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_emission_rate));
    reg.add_internal_call("internal_m2n_particle_emitter_get_temporal_motion", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_temporal_motion));
    reg.add_internal_call("internal_m2n_particle_emitter_set_temporal_motion", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_temporal_motion));
    reg.add_internal_call("internal_m2n_particle_emitter_get_velocity_damping", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_velocity_damping));
    reg.add_internal_call("internal_m2n_particle_emitter_set_velocity_damping", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_velocity_damping));
    reg.add_internal_call("internal_m2n_particle_emitter_get_opacity", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_opacity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_opacity", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_opacity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_force_over_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_force_over_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_set_force_over_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_force_over_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_get_emission_shape_scale", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_emission_shape_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_set_emission_shape_scale", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_emission_shape_scale));
    reg.add_internal_call("internal_m2n_particle_emitter_get_emission_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_emission_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_set_emission_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_emission_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_lifetime));
    reg.add_internal_call("internal_m2n_particle_emitter_get_position_easing", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_position_easing));
    reg.add_internal_call("internal_m2n_particle_emitter_set_position_easing", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_position_easing));
    reg.add_internal_call("internal_m2n_particle_emitter_get_num_particles", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_num_particles));
    reg.add_internal_call("internal_m2n_particle_emitter_is_playing", 
                            dotnet_internal_call(internal_m2n_particle_emitter_is_playing));
    reg.add_internal_call("internal_m2n_particle_emitter_is_paused", 
                            dotnet_internal_call(internal_m2n_particle_emitter_is_paused));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_texture));
    reg.add_internal_call("internal_m2n_particle_emitter_play", 
                            dotnet_internal_call(internal_m2n_particle_emitter_play));
    reg.add_internal_call("internal_m2n_particle_emitter_stop", 
                            dotnet_internal_call(internal_m2n_particle_emitter_stop));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_and_reset", 
                            dotnet_internal_call(internal_m2n_particle_emitter_stop_and_reset));
    reg.add_internal_call("internal_m2n_particle_emitter_pause", 
                            dotnet_internal_call(internal_m2n_particle_emitter_pause));
    reg.add_internal_call("internal_m2n_particle_emitter_resume", 
                            dotnet_internal_call(internal_m2n_particle_emitter_resume));
    reg.add_internal_call("internal_m2n_particle_emitter_reset_emitter", 
                            dotnet_internal_call(internal_m2n_particle_emitter_reset_emitter));
    reg.add_internal_call("internal_m2n_particle_emitter_get_loop", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_loop));
    reg.add_internal_call("internal_m2n_particle_emitter_set_loop", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_loop));
    reg.add_internal_call("internal_m2n_particle_emitter_get_blend_mode", 
                            dotnet_internal_call(internal_m2n_particle_emitter_get_blend_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_blend_mode", 
                            dotnet_internal_call(internal_m2n_particle_emitter_set_blend_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_simulation_backend",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_simulation_backend));
    reg.add_internal_call("internal_m2n_particle_emitter_set_simulation_backend",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_simulation_backend));
    reg.add_internal_call("internal_m2n_particle_emitter_get_culling_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_culling_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_culling_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_culling_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_simulation_space",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_simulation_space));
    reg.add_internal_call("internal_m2n_particle_emitter_set_simulation_space",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_simulation_space));
    reg.add_internal_call("internal_m2n_particle_emitter_get_start_delay",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_start_delay));
    reg.add_internal_call("internal_m2n_particle_emitter_set_start_delay",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_start_delay));
    reg.add_internal_call("internal_m2n_particle_emitter_get_spawn_location",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_spawn_location));
    reg.add_internal_call("internal_m2n_particle_emitter_set_spawn_location",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_spawn_location));
    reg.add_internal_call("internal_m2n_particle_emitter_get_emission_shape_position",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_emission_shape_position));
    reg.add_internal_call("internal_m2n_particle_emitter_set_emission_shape_position",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_emission_shape_position));
    reg.add_internal_call("internal_m2n_particle_emitter_get_initial_scale_3d",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_initial_scale_3d));
    reg.add_internal_call("internal_m2n_particle_emitter_set_initial_scale_3d",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_initial_scale_3d));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_by_speed_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_size_by_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_by_speed_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_size_by_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_size_by_speed_velocity_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_size_by_speed_velocity_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_size_by_speed_velocity_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_size_by_speed_velocity_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_intensity",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_color_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_intensity",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_color_intensity));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_by_speed_velocity_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_color_by_speed_velocity_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_by_speed_velocity_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_color_by_speed_velocity_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_range));
    reg.add_internal_call("internal_m2n_particle_emitter_get_render_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_render_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_render_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_render_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_align_to_direction",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_align_to_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_set_align_to_direction",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_align_to_direction));
    reg.add_internal_call("internal_m2n_particle_emitter_get_pivot",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_pivot));
    reg.add_internal_call("internal_m2n_particle_emitter_set_pivot",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_pivot));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_mode",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_texture_mode));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_tiles",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_texture_sheet_tiles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_tiles",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_texture_sheet_tiles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_cycles",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_texture_sheet_cycles));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_cycles",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_texture_sheet_cycles));
    reg.add_internal_call("internal_m2n_particle_emitter_get_texture_sheet_randomize",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_texture_sheet_randomize));
    reg.add_internal_call("internal_m2n_particle_emitter_set_texture_sheet_randomize",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_texture_sheet_randomize));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_color_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_color_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_color_by_speed_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_color_by_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_color_by_speed_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_color_by_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_velocity_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_velocity_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_velocity_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_velocity_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_scale_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_scale_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_scale_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_scale_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_lifetime_by_emitter_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_gradient",
                            dotnet_internal_call(internal_m2n_particle_emitter_set_lifetime_by_emitter_speed_gradient));
    reg.add_internal_call("internal_m2n_particle_emitter_get_world_bounds",
                            dotnet_internal_call(internal_m2n_particle_emitter_get_world_bounds));
    reg.add_internal_call("internal_m2n_particle_emitter_is_stopped",
                            dotnet_internal_call(internal_m2n_particle_emitter_is_stopped));
    reg.add_internal_call("internal_m2n_particle_emitter_is_simulated",
                            dotnet_internal_call(internal_m2n_particle_emitter_is_simulated));
    reg.add_internal_call("internal_m2n_particle_emitter_play_sub_emitters",
                            dotnet_internal_call(internal_m2n_particle_emitter_play_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_sub_emitters",
                            dotnet_internal_call(internal_m2n_particle_emitter_stop_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_stop_and_reset_sub_emitters",
                            dotnet_internal_call(internal_m2n_particle_emitter_stop_and_reset_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_pause_sub_emitters",
                            dotnet_internal_call(internal_m2n_particle_emitter_pause_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_resume_sub_emitters",
                            dotnet_internal_call(internal_m2n_particle_emitter_resume_sub_emitters));
    reg.add_internal_call("internal_m2n_particle_emitter_emit",
                            dotnet_internal_call(internal_m2n_particle_emitter_emit));
}

} // namespace unravel
