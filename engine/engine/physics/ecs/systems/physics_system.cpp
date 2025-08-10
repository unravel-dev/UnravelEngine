#include "physics_system.h"
#include <engine/defaults/defaults.h>
#include <engine/events.h>

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/profiler/profiler.h>

#include <logging/logging.h>

namespace unravel
{

void physics_system::on_create_component(entt::registry& r, entt::entity e)
{
    physics_component::on_create_component(r, e);
    backend_type::on_create_component(r, e);
}
void physics_system::on_destroy_component(entt::registry& r, entt::entity e)
{
    physics_component::on_destroy_component(r, e);
    backend_type::on_destroy_component(r, e);
}

auto physics_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ev = ctx.get_cached<events>();
    ev.on_frame_update.connect(sentinel_, this, &physics_system::on_frame_update);

    ev.on_play_begin.connect(sentinel_, 10, this, &physics_system::on_play_begin);
    ev.on_play_end.connect(sentinel_, -10, this, &physics_system::on_play_end);
    ev.on_pause.connect(sentinel_, 10, this, &physics_system::on_pause);
    ev.on_resume.connect(sentinel_, -10, this, &physics_system::on_resume);
    ev.on_skip_next_frame.connect(sentinel_, -10, this, &physics_system::on_skip_next_frame);

    backend_.init();

    return true;
}

auto physics_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    backend_.deinit();

    return true;
}

void physics_system::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.ctx().emplace<physics_system*>(this);
    backend_.on_play_begin(ctx);
}

void physics_system::on_play_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    backend_.on_play_end(ctx);

    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    registry.ctx().erase<physics_system*>();
}

void physics_system::on_pause(rtti::context& ctx)
{
    backend_.on_pause(ctx);
}

void physics_system::on_resume(rtti::context& ctx)
{
    backend_.on_resume(ctx);
}

void physics_system::on_skip_next_frame(rtti::context& ctx)
{
    backend_.on_skip_next_frame(ctx);
}

void physics_system::on_frame_update(rtti::context& ctx, delta_t dt)
{
    APP_SCOPE_PERF("Physics/System Update");

    auto& ev = ctx.get_cached<events>();

    if(ev.is_playing && !ev.is_paused)
    {
        backend_.on_frame_update(ctx, dt);
    }
}
void physics_system::apply_explosion_force(physics_component& comp,
                                           float explosion_force,
                                           const math::vec3& explosion_position,
                                           float explosion_radius,
                                           float upwards_modifier,
                                           force_mode mode)
{
    backend_type::apply_explosion_force(comp,
                                        explosion_force,
                                        explosion_position,
                                        explosion_radius,
                                        upwards_modifier,
                                        mode);
}

void physics_system::apply_force(physics_component& comp, const math::vec3& force, force_mode mode)
{
    backend_type::apply_force(comp, force, mode);
}

void physics_system::apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode)
{
    backend_type::apply_torque(comp, torque, mode);
}

void physics_system::clear_kinematic_velocities(physics_component& comp)
{
    backend_type::clear_kinematic_velocities(comp);
}

auto physics_system::ray_cast(const math::vec3& origin,
                              const math::vec3& direction,
                              float max_distance,
                              int layer_mask,
                              bool query_sensors) const -> hpp::optional<raycast_hit>
{
    return backend_type::ray_cast(origin, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::ray_cast_all(const math::vec3& origin,
                                  const math::vec3& direction,
                                  float max_distance,
                                  int layer_mask,
                                  bool query_sensors) const -> physics_vector<raycast_hit>
{
    return backend_type::ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float radius,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) const -> hpp::optional<raycast_hit>
{
    return backend_type::sphere_cast(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast_all(const math::vec3& origin,
                                     const math::vec3& direction,
                                     float radius,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) const -> physics_vector<raycast_hit>
{
    return backend_type::sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors) const
    -> physics_vector<entt::entity>
{
    return backend_type::sphere_overlap(origin, radius, layer_mask, query_sensors);
}

} // namespace unravel
