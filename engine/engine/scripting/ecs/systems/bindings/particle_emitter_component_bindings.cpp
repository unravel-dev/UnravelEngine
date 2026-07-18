#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>

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
}

} // namespace unravel
