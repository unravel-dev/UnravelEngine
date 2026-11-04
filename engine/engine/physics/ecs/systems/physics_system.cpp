#include "physics_system.h"
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/systems/ragdoll_poses.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/settings/boot_config.h>
#include <engine/settings/settings.h>

#include <logging/logging.h>

namespace unravel
{
namespace
{
template<typename Function>
void for_each_active_character(entt::registry& registry, Function&& function)
{
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto /*entity*/, auto& transform, auto& character, auto& /*active*/)
        {
            function(transform, character);
        });
}

/**
 * @brief Places a character without it reading as a teleport: the sync flag the backend tests stays as it was, so a
 *        script's write still reaches the backend and this one never does.
 */
void place_character(transform_component& transform, const math::vec3& position)
{
    const bool has_pending_sync = transform.is_dirty(dirty_ids::physics);
    transform.set_position_global(position);
    transform.set_dirty(dirty_ids::physics, has_pending_sync);
}

/// Ends interpolation for a character that turned it off, where the character really is.
void release_character(transform_component& transform, character_controller_component& character, physics_backend& backend)
{
    auto& motion = character.get_motion();
    if(!motion.is_tracked)
    {
        return;
    }
    if(transform.get_position_global() == motion.shown)
    {
        place_character(transform, motion.step_end);
    }
    if(motion.queued_move != math::vec3{0.0f})
    {
        backend.move_character(character, motion.queued_move);
    }
    motion = {};
}

void begin_character_frame(entt::registry& registry, physics_backend& backend, float frame_seconds, float max_queued_seconds)
{
    for_each_active_character(registry,
                              [&](transform_component& transform, character_controller_component& character)
                              {
                                  if(!character.get_interpolate())
                                  {
                                      release_character(transform, character, backend);
                                      return;
                                  }
                                  auto& motion = character.get_motion();
                                  const math::vec3 position = transform.get_position_global();
                                  if(!motion.is_tracked || position != motion.shown)
                                  {
                                      motion.place_at(position);
                                  }
                                  motion.add_queued_time(frame_seconds, max_queued_seconds);
                              });
}

void put_characters_at_their_steps(entt::registry& registry)
{
    for_each_active_character(registry,
                              [](transform_component& transform, character_controller_component& character)
                              {
                                  const auto& motion = character.get_motion();
                                  if(character.get_interpolate() && motion.is_tracked)
                                  {
                                      place_character(transform, motion.step_end);
                                  }
                              });
}

void hand_moves_to_step(entt::registry& registry, physics_backend& backend, float step_seconds)
{
    for_each_active_character(registry,
                              [&](transform_component& /*transform*/, character_controller_component& character)
                              {
                                  auto& motion = character.get_motion();
                                  if(!character.get_interpolate() || !motion.is_tracked)
                                  {
                                      return;
                                  }
                                  const math::vec3 move = motion.take_step_move(step_seconds);
                                  if(move != math::vec3{0.0f})
                                  {
                                      backend.move_character(character, move);
                                  }
                              });
}

void note_step_start(entt::registry& registry)
{
    for_each_active_character(registry,
                              [](transform_component& transform, character_controller_component& character)
                              {
                                  character.get_motion().step_start = transform.get_position_global();
                              });
}

void note_step_end(entt::registry& registry)
{
    for_each_active_character(registry,
                              [](transform_component& transform, character_controller_component& character)
                              {
                                  character.get_motion().step_end = transform.get_position_global();
                              });
}

/// A contact callback that moved a character teleported it: nothing lies between where it was and where it went.
void adopt_contact_teleports(entt::registry& registry)
{
    for_each_active_character(registry,
                              [](transform_component& transform, character_controller_component& character)
                              {
                                  auto& motion = character.get_motion();
                                  const math::vec3 position = transform.get_position_global();
                                  if(position != motion.step_end)
                                  {
                                      motion.step_start = position;
                                      motion.step_end = position;
                                  }
                              });
}

void show_characters(entt::registry& registry, float step_fraction)
{
    for_each_active_character(registry,
                              [&](transform_component& transform, character_controller_component& character)
                              {
                                  auto& motion = character.get_motion();
                                  if(!character.get_interpolate() || !motion.is_tracked)
                                  {
                                      return;
                                  }
                                  place_character(transform, math::mix(motion.step_start, motion.step_end, step_fraction));
                                  motion.shown = transform.get_position_global();
                              });
}
} // namespace

void fixed_step_runner::advance(rtti::context& ctx,
                                physics_backend& backend,
                                float frame_seconds,
                                float step_seconds,
                                int max_steps)
{
    auto& ev = ctx.get_cached<events>();
    auto& registry = *ctx.get_cached<ecs>().get_scene().registry;

    elapsed_ += frame_seconds;

    // Clamp leftover to at most one extra fixed step worth after the max burst.
    const float max_elapsed = step_seconds * static_cast<float>(max_steps + 1);

    begin_character_frame(registry, backend, frame_seconds, max_elapsed);
    if(elapsed_ >= step_seconds && max_steps > 0)
    {
        put_characters_at_their_steps(registry);
    }

    int steps = 0;
    while(elapsed_ >= step_seconds && steps < max_steps)
    {
        APP_SCOPE_PERF("Physics/Fixed Update");
        delta_t step_dt(step_seconds);
        is_in_fixed_update_ = true;
        ev.on_frame_fixed_update(ctx, step_dt);
        is_in_fixed_update_ = false;

        hand_moves_to_step(registry, backend, step_seconds);
        backend.sync_to_physics(ctx, step_dt);
        note_step_start(registry);
        backend.simulate(step_dt);
        backend.sync_from_physics(ctx);
        note_step_end(registry);
        backend.dispatch_contacts(ctx);
        adopt_contact_teleports(registry);

        elapsed_ -= step_seconds;
        steps++;
    }

    if(elapsed_ > max_elapsed)
    {
        elapsed_ = max_elapsed;
    }

    show_characters(registry, math::min(elapsed_ / step_seconds, 1.0f));
}

void fixed_step_runner::reset() noexcept
{
    elapsed_ = 0.0f;
}

auto fixed_step_runner::is_in_fixed_update() const noexcept -> bool
{
    return is_in_fixed_update_;
}

auto fixed_step_runner::get_step_fraction(float step_seconds) const noexcept -> float
{
    return step_seconds > 0.0f ? math::clamp(elapsed_ / step_seconds, 0.0f, 1.0f) : 1.0f;
}

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
    ev.on_frame_update.connect(sentinel_, frame_update_priority::physics, this, &physics_system::on_frame_update);

    ev.on_play_begin.connect(sentinel_, 10, this, &physics_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &physics_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &physics_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &physics_system::on_resume);

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
    step_runner_.reset();
    registry.view<character_controller_component>().each(
        [](auto /*entity*/, auto& character)
        {
            character.get_motion() = {};
        });
    backend_->on_play_begin(ctx);
    if(ctx.has<settings>())
    {
        backend_->set_gravity(ctx.get<settings>().physics.gravity);
    }
}

void physics_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    backend_->on_play_end(ctx);
    step_runner_.reset();

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

void physics_system::on_frame_update(rtti::context& ctx, const frame_times& times)
{
    APP_SCOPE_PERF("Physics/System Update");

    auto& play = ctx.get_cached<play_mode>();

    if(!play.is_simulation_running())
    {
        return;
    }

    delta_t dt = times.scaled;
    // First sim frame (incl. mid-frame enter_running): do not accumulate a hitch into fixed steps.
    if(play.frames_running() == 0 || play.is_simulation_paused())
    {
        dt = {};
    }

    float fixed_time_step = 1.0f / 50.0f;
    int max_subs_steps = 3;

    if(ctx.has<settings>())
    {
        auto& ss = ctx.get<settings>();
        fixed_time_step = ss.physics.fixed_timestep;
        max_subs_steps = ss.physics.max_fixed_steps;
    }

    auto& registry = *ctx.get_cached<ecs>().get_scene().registry;
    // The animation has posed the bones for this frame: ragdolls read it before the steps and pose the bones after.
    // A paused or frozen game steps nothing, but the animation still poses the bones every frame, so the ragdolls pose
    // them again where physics left them.
    capture_ragdoll_animations(registry, dt.count());
    if(dt > delta_t::zero())
    {
        step_runner_.advance(ctx, *backend_, dt.count(), fixed_time_step, max_subs_steps);
    }
    show_ragdolls(registry, step_runner_.get_step_fraction(fixed_time_step), dt.count());
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

void physics_system::apply_force_at_position(physics_component& comp,
                                             const math::vec3& force,
                                             const math::vec3& position,
                                             force_mode mode)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->apply_force_at_position(comp, force, position, mode);
    }
}

void physics_system::set_body_awake(physics_component& comp, bool awake)
{
    auto& ctx = engine::context();
    if(auto* backend = ctx.get_cached<physics_system>().get_backend())
    {
        backend->set_body_awake(comp, awake);
    }
}

void physics_system::on_create_joint_component(entt::registry& r, entt::entity e)
{
    joint_component::on_create_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_create_joint_component(r, e);
        }
    }
}

void physics_system::on_destroy_joint_component(entt::registry& r, entt::entity e)
{
    joint_component::on_destroy_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_destroy_joint_component(r, e);
        }
    }
}

void physics_system::on_create_ragdoll_component(entt::registry& r, entt::entity e)
{
    ragdoll_component::on_create_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_create_ragdoll_component(r, e);
        }
    }
}

void physics_system::on_destroy_ragdoll_component(entt::registry& r, entt::entity e)
{
    ragdoll_component::on_destroy_component(r, e);
    auto& ctx = engine::context();
    if(ctx.has<physics_system>())
    {
        if(auto* backend = ctx.get_cached<physics_system>().get_backend())
        {
            backend->on_destroy_ragdoll_component(r, e);
        }
    }
}

void physics_system::set_gravity(const math::vec3& gravity)
{
    if(backend_)
    {
        backend_->set_gravity(gravity);
    }
}

auto physics_system::get_gravity() const -> math::vec3
{
    auto& ctx = engine::context();
    const bool is_playing = ctx.has<play_mode>() && ctx.get_cached<play_mode>().is_simulation_running();
    if(backend_ && is_playing)
    {
        return backend_->get_gravity();
    }
    if(ctx.has<settings>())
    {
        return ctx.get<settings>().physics.gravity;
    }
    return {0.0f, -9.8f, 0.0f};
}

void physics_system::explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask)
{
    if(backend_)
    {
        backend_->explode(center, radius, falloff, impulse_per_area, layer_mask);
    }
}

auto physics_system::shape_cast(const physics_compound_shape& shape,
                                const math::vec3& origin,
                                const math::quat& rotation,
                                const math::vec3& direction,
                                float max_distance,
                                int layer_mask,
                                bool query_sensors) const -> hpp::optional<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->shape_cast(shape, origin, rotation, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::shape_cast_all(const physics_compound_shape& shape,
                                    const math::vec3& origin,
                                    const math::quat& rotation,
                                    const math::vec3& direction,
                                    float max_distance,
                                    int layer_mask,
                                    bool query_sensors) const -> physics_vector<raycast_hit>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->shape_cast_all(shape, origin, rotation, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::shape_overlap(const physics_compound_shape& shape,
                                   const math::vec3& origin,
                                   const math::quat& rotation,
                                   int layer_mask,
                                   bool query_sensors) const -> physics_vector<entt::entity>
{
    if(!backend_)
    {
        return {};
    }
    return backend_->shape_overlap(shape, origin, rotation, layer_mask, query_sensors);
}

auto physics_system::get_world_stats() const -> physics_backend::world_stats
{
    return backend_ ? backend_->get_world_stats() : physics_backend::world_stats{};
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
    auto& system = ctx.get_cached<physics_system>();
    if(comp.get_interpolate() && !system.step_runner_.is_in_fixed_update())
    {
        comp.get_motion().queued_move += displacement;
    }
    else if(auto* backend = system.get_backend())
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
        // Moves queued before a warp were for where the character was.
        auto& motion = comp.get_motion();
        motion.queued_move = math::vec3{0.0f};
        motion.queued_time = 0.0f;
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
