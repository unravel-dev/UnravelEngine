#include "physics_system.h"
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>

#include <logging/logging.h>

namespace unravel
{
void physics_system::on_create_component(entt::registry& r, entt::entity e)
{
    physics_component::on_create_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_create_component(r, e);
        }
    }
}

void physics_system::on_destroy_component(entt::registry& r, entt::entity e)
{
    physics_component::on_destroy_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_destroy_component(r, e);
        }
    }
}

auto physics_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    physics_backend_type backend_type = physics_backend_type::auto_detect;
    if(ctx.has<boot_config>())
    {
        backend_type = ctx.get<boot_config>().physics;
    }
    APPLOG_INFO("{}::{}: creating physics backend {} (selected: {})",
                hpp::type_name_str(*this),
                __func__,
                physics_backend_to_string(resolve_physics_backend(backend_type)),
                physics_backend_to_string(backend_type));
    backend_ = create_physics_backend(backend_type);
    if(!backend_)
    {
        APPLOG_ERROR("{}::{}: failed to create physics backend", hpp::type_name_str(*this), __func__);
        return false;
    }

    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, this, &physics_system::on_frame_update);

    ev.on_play_begin.connect(sentinel_, 10, this, &physics_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &physics_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &physics_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &physics_system::on_resume);
    ev.on_skip_next_frame.connect(sentinel_, -10, this, &physics_system::on_skip_next_frame);

    backend_->init();

    return true;
}

auto physics_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(backend_)
    {
        backend_->deinit();
        backend_.reset();
    }

    return true;
}

void physics_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.ctx().emplace<physics_system*>(this);
    elapsed_ = 0.0f;
    backend_->on_play_begin(ctx);
}

void physics_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    backend_->on_play_end(ctx);
    elapsed_ = 0.0f;

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.ctx().erase<physics_system*>();
}

void physics_system::on_pause(rtti::context& ctx)
{
    backend_->on_pause(ctx);
}

void physics_system::on_resume(rtti::context& ctx)
{
    backend_->on_resume(ctx);
}

void physics_system::on_skip_next_frame(rtti::context& ctx)
{
    delta_t step(1.0f / 60.0f);
    on_frame_update(ctx, step);
}

void physics_system::on_frame_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Physics/System Update");

    auto& play = ctx.get_cached<play_mode>();

    if(!play.is_simulation_running() || play.is_paused())
    {
        return;
    }

    // First sim frame (incl. mid-frame enter_running): do not accumulate a hitch into fixed steps.
    if(play.frames_running() == 0)
    {
        dt = {};
    }

    if(dt <= delta_t::zero())
    {
        return;
    }

    float fixed_time_step = 1.0f / 50.0f;
    int max_subs_steps = 3;

    if(ctx.has<settings>())
    {
        auto& ss = ctx.get<settings>();
        fixed_time_step = ss.physics.fixed_timestep;
        max_subs_steps = ss.physics.max_fixed_steps;
    }

    auto& ev = ctx.get_cached<events>();

    elapsed_ += dt.count();

    // Clamp leftover to at most one extra fixed step worth after the max burst.
    const float max_elapsed = fixed_time_step * static_cast<float>(max_subs_steps + 1);

    int steps = 0;
    while(elapsed_ >= fixed_time_step && steps < max_subs_steps)
    {
        APP_SCOPE_PERF("Physics/Fixed Update");
        delta_t step_dt(fixed_time_step);
        ev.on_frame_fixed_update(ctx, step_dt);

        backend_->sync_to_physics(ctx, step_dt);
        backend_->simulate(step_dt);
        backend_->sync_from_physics(ctx);
        backend_->dispatch_contacts(ctx);

        elapsed_ -= fixed_time_step;
        steps++;
    }

    if(elapsed_ > max_elapsed)
    {
        elapsed_ = max_elapsed;
    }
}

void physics_system::apply_explosion_force(physics_component& comp,
                                           float explosion_force,
                                           const math::vec3& explosion_position,
                                           float explosion_radius,
                                           float upwards_modifier,
                                           force_mode mode)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->apply_explosion_force(comp,
                                       explosion_force,
                                       explosion_position,
                                       explosion_radius,
                                       upwards_modifier,
                                       mode);
    }
}

void physics_system::apply_force(physics_component& comp, const math::vec3& force, force_mode mode)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->apply_force(comp, force, mode);
    }
}

void physics_system::apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->apply_torque(comp, torque, mode);
    }
}

void physics_system::clear_kinematic_velocities(physics_component& comp)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->clear_kinematic_velocities(comp);
    }
}

void physics_system::on_create_cc_component(entt::registry& r, entt::entity e)
{
    character_controller_component::on_create_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_create_cc_component(r, e);
        }
    }
}

void physics_system::on_destroy_cc_component(entt::registry& r, entt::entity e)
{
    character_controller_component::on_destroy_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_destroy_cc_component(r, e);
        }
    }
}

void physics_system::move_character(character_controller_component& comp, const math::vec3& displacement)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->move_character(comp, displacement);
    }
}

void physics_system::jump_character(character_controller_component& comp, const math::vec3& direction)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->jump_character(comp, direction);
    }
}

void physics_system::apply_impulse_character(character_controller_component& comp, const math::vec3& impulse)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->apply_impulse_character(comp, impulse);
    }
}

void physics_system::warp_character(character_controller_component& comp, const math::vec3& position)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->warp_character(comp, position);
    }
}

void physics_system::set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->set_character_linear_velocity(comp, velocity);
    }
}

auto physics_system::ray_cast(const math::vec3& origin,
                              const math::vec3& direction,
                              float max_distance,
                              int layer_mask,
                              bool query_sensors) const -> hpp::optional<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->ray_cast(origin, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::ray_cast_all(const math::vec3& origin,
                                  const math::vec3& direction,
                                  float max_distance,
                                  int layer_mask,
                                  bool query_sensors) const -> physics_vector<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float radius,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) const -> hpp::optional<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->sphere_cast(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast_all(const math::vec3& origin,
                                     const math::vec3& direction,
                                     float radius,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) const -> physics_vector<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors) const
    -> physics_vector<entt::entity>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->sphere_overlap(origin, radius, layer_mask, query_sensors);
}

void physics_system::draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd)
{
    if(backend_)
    {
        backend_->draw_system_gizmos(ctx, cam, dd);
    }
}

auto physics_system::get_backend() -> physics_backend*
{
    return backend_.get();
}

auto physics_system::get_backend() const -> const physics_backend*
{
    return backend_.get();
}

} // namespace unravel
