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

#include <cmath>

namespace unravel
{
namespace
{
/// Writes a pose whatever its distance from the one held; the transform still drops an exactly equal one.
constexpr float EXACT_WRITE_EPSILON = 0.0f;

/// The kinds of movers the runner shows between steps this frame: a pass skips a kind with none.
struct tracked_movers
{
    bool has_characters{};
    bool has_bodies{};
};

auto is_interpolated(const character_controller_component& character) noexcept -> bool
{
    return character.get_interpolate();
}

/// Kinematic and static bodies follow their transforms, so only a dynamic body is ever shown between steps.
auto is_interpolated(const physics_component& body) noexcept -> bool
{
    return body.get_interpolate() && body.get_body_type() == rigidbody_type::dynamic;
}

/// A character's rotation is left to scripts: only its position is shown between steps.
auto is_turning(const character_controller_component& /*character*/) noexcept -> bool
{
    return false;
}

auto is_turning(const physics_component& /*body*/) noexcept -> bool
{
    return true;
}

template<typename Component, typename Function>
void for_each_tracked(entt::registry& registry, Function& function)
{
    registry.view<transform_component, Component, active_component>().each(
        [&](auto /*entity*/, transform_component& transform, Component& component, auto& /*active*/)
        {
            if(is_interpolated(component) && component.get_motion().is_tracked)
            {
                function(transform, component);
            }
        });
}

/// Runs a function on every character and dynamic body shown between steps.
template<typename Function>
void for_each_tracked_mover(entt::registry& registry, const tracked_movers& tracked, Function&& function)
{
    if(tracked.has_characters)
    {
        for_each_tracked<character_controller_component>(registry, function);
    }
    if(tracked.has_bodies)
    {
        for_each_tracked<physics_component>(registry, function);
    }
}

/// Whether the transform holds a pose exactly; the rotation counts only for a mover that turns.
auto is_at_pose(const transform_component& transform,
                const math::vec3& position,
                const math::quat& rotation,
                bool turns) -> bool
{
    if(transform.get_position_global() != position)
    {
        return false;
    }
    return !turns || transform.get_rotation_global() == rotation;
}

void write_pose(transform_component& transform, const math::vec3& position, const math::quat& rotation, bool turns)
{
    if(turns)
    {
        transform.set_position_and_rotation_global(position, rotation, EXACT_WRITE_EPSILON);
        return;
    }
    transform.set_position_global(position);
}

/**
 * @brief Places a mover without it reading as a teleport: the sync flag the backend tests stays as it was, so a
 *        script's write still reaches the backend and this one never does. A pose the transform holds is not written.
 *
 * The global transform is resolved before the flag is put back. A transform raises its consumer flags only when its
 * lazy global turns dirty, so a flag put back while the global stays dirty would miss every later write of the frame,
 * local ones included.
 */
void place_pose(transform_component& transform, const math::vec3& position, const math::quat& rotation, bool turns)
{
    if(is_at_pose(transform, position, rotation, turns))
    {
        return;
    }
    const bool has_pending_sync = transform.is_dirty(dirty_ids::physics);
    write_pose(transform, position, rotation, turns);
    transform.resolve_transform_global();
    transform.set_dirty(dirty_ids::physics, has_pending_sync);
}

/// The backend places the mover where its transform is at the next step.
void request_sync(transform_component& transform)
{
    transform.set_dirty(dirty_ids::physics, true);
}

void note_shown(const transform_component& transform, stepped_motion& motion)
{
    motion.shown_position = transform.get_position_global();
    motion.shown_rotation = transform.get_rotation_global();
    motion.shown_local_position = transform.get_position_local();
    motion.shown_local_rotation = transform.get_rotation_local();
}

/// Starts over at the transform's pose, as if the mover had always been there.
void restart_at_transform(const transform_component& transform, stepped_motion& motion)
{
    motion.step_start_position = transform.get_position_global();
    motion.step_start_rotation = transform.get_rotation_global();
    motion.step_end_position = motion.step_start_position;
    motion.step_end_rotation = motion.step_start_rotation;
    note_shown(transform, motion);
    motion.is_tracked = true;
}

/// Someone wrote the mover's own transform since it was drawn: its pose in its parent's space changed.
auto is_written_since_shown(const transform_component& transform, const stepped_motion& motion, bool turns) -> bool
{
    if(transform.get_position_local() != motion.shown_local_position)
    {
        return true;
    }
    return turns && transform.get_rotation_local() != motion.shown_local_rotation;
}

/**
 * @brief How the parent turned since the mover was drawn: the change of the global rotation without the mover's own
 *        local one, which a script may have turned in the same frame (a character's rotation is its scripts').
 *
 * A parent that did not turn gives the identity exactly, so a parent that only moves leaves the step rotations as
 * they were.
 */
auto get_parent_turn(const transform_component& transform, const stepped_motion& motion) -> math::quat
{
    const math::quat parent_rotation = transform.get_rotation_global() * glm::inverse(transform.get_rotation_local());
    const math::quat shown_parent_rotation = motion.shown_rotation * glm::inverse(motion.shown_local_rotation);
    if(parent_rotation == shown_parent_rotation)
    {
        return math::identity<math::quat>();
    }
    return math::normalize(parent_rotation * glm::inverse(shown_parent_rotation));
}

/// Moves the step poses rigidly as a parent moved the drawn pose, so the mover keeps the travel it made since.
void carry_step_poses(const transform_component& transform, stepped_motion& motion)
{
    const math::quat parent_turn = get_parent_turn(transform, motion);
    const math::vec3 position = transform.get_position_global();
    motion.step_start_position = position + parent_turn * (motion.step_start_position - motion.shown_position);
    motion.step_end_position = position + parent_turn * (motion.step_end_position - motion.shown_position);
    motion.step_start_rotation = parent_turn * motion.step_start_rotation;
    motion.step_end_rotation = parent_turn * motion.step_end_rotation;
    note_shown(transform, motion);
}

/// A parent moved the mover since it was drawn: the step poses move along, and the backend places the carried pose.
void follow_parent(transform_component& transform, stepped_motion& motion, bool turns)
{
    if(is_at_pose(transform, motion.shown_position, motion.shown_rotation, turns))
    {
        return;
    }
    carry_step_poses(transform, motion);
    request_sync(transform);
}

/**
 * @brief Catches up with what happened to a mover's transform since it was drawn: tracking starts where it is, a
 *        write starts it over there, and a parent's move carries its step poses along. The backend follows the last
 *        two.
 */
void follow_transform(transform_component& transform, stepped_motion& motion, bool turns)
{
    if(!motion.is_tracked)
    {
        restart_at_transform(transform, motion);
        return;
    }
    if(is_written_since_shown(transform, motion, turns))
    {
        restart_at_transform(transform, motion);
        request_sync(transform);
        return;
    }
    follow_parent(transform, motion, turns);
}

/**
 * @brief Leaves a mover whose interpolation ends where it really is: at the last step's end, carried along with a
 *        parent that moved since. A transform written since it was drawn (inside a fixed step too) keeps what it holds.
 */
void show_real_pose(transform_component& transform, stepped_motion& motion, bool turns)
{
    if(is_written_since_shown(transform, motion, turns))
    {
        return;
    }
    follow_parent(transform, motion, turns);
    place_pose(transform, motion.step_end_position, motion.step_end_rotation, turns);
}

/// Ends interpolation for a character that turned it off, where the character really is, and hands on queued moves.
void release_character(transform_component& transform,
                       character_controller_component& character,
                       physics_backend& backend)
{
    auto& motion = character.get_motion();
    if(!motion.is_tracked)
    {
        return;
    }
    show_real_pose(transform, motion, is_turning(character));
    if(motion.queued_move != math::vec3{0.0f})
    {
        backend.move_character(character, motion.queued_move);
    }
    motion = {};
}

/// Ends interpolation for a body that turned it off or stopped being dynamic, where the body really is.
void release_body(transform_component& transform, physics_component& body)
{
    auto& motion = body.get_motion();
    if(!motion.is_tracked)
    {
        return;
    }
    show_real_pose(transform, motion, is_turning(body));
    motion = {};
}

/// @return Whether the character is shown between steps this frame.
auto begin_character_frame(transform_component& transform,
                           character_controller_component& character,
                           physics_backend& backend,
                           float frame_seconds,
                           float max_queued_seconds) -> bool
{
    if(!is_interpolated(character))
    {
        release_character(transform, character, backend);
        return false;
    }
    auto& motion = character.get_motion();
    follow_transform(transform, motion, is_turning(character));
    motion.add_queued_time(frame_seconds, max_queued_seconds);
    return true;
}

/// @return Whether the body is shown between steps this frame.
auto begin_body_frame(transform_component& transform, physics_component& body) -> bool
{
    if(!is_interpolated(body))
    {
        release_body(transform, body);
        return false;
    }
    follow_transform(transform, body.get_motion(), is_turning(body));
    return true;
}

auto begin_frame(entt::registry& registry, physics_backend& backend, float frame_seconds, float max_queued_seconds)
    -> tracked_movers
{
    tracked_movers tracked;
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto /*entity*/, auto& transform, auto& character, auto& /*active*/)
        {
            const bool is_shown =
                begin_character_frame(transform, character, backend, frame_seconds, max_queued_seconds);
            tracked.has_characters = tracked.has_characters || is_shown;
        });
    registry.view<transform_component, physics_component, active_component>().each(
        [&](auto /*entity*/, auto& transform, auto& body, auto& /*active*/)
        {
            const bool is_shown = begin_body_frame(transform, body);
            tracked.has_bodies = tracked.has_bodies || is_shown;
        });
    return tracked;
}

/// Before the steps, every mover's transform holds where it really is, which fixed updates read and the backend syncs.
void put_at_step_ends(entt::registry& registry, const tracked_movers& tracked)
{
    for_each_tracked_mover(registry,
                           tracked,
                           [](transform_component& transform, auto& component)
                           {
                               const stepped_motion& motion = component.get_motion();
                               place_pose(transform,
                                          motion.step_end_position,
                                          motion.step_end_rotation,
                                          is_turning(component));
                           });
}

void hand_moves_to_step(entt::registry& registry,
                        const tracked_movers& tracked,
                        physics_backend& backend,
                        float step_seconds)
{
    if(!tracked.has_characters)
    {
        return;
    }
    auto hand_move = [&](transform_component& /*transform*/, character_controller_component& character)
    {
        const math::vec3 move = character.get_motion().take_step_move(step_seconds);
        if(move != math::vec3{0.0f})
        {
            backend.move_character(character, move);
        }
    };
    for_each_tracked<character_controller_component>(registry, hand_move);
}

/// Reads the poses a step begins at, after the backend synced the transforms written since the last step.
void note_step_starts(entt::registry& registry, const tracked_movers& tracked)
{
    for_each_tracked_mover(registry,
                           tracked,
                           [](transform_component& transform, auto& component)
                           {
                               stepped_motion& motion = component.get_motion();
                               motion.step_start_position = transform.get_position_global();
                               motion.step_start_rotation = transform.get_rotation_global();
                           });
}

/// Reads the poses a step ended at. The backend writes an interpolated mover's pose exactly, however little it moved.
void note_step_ends(entt::registry& registry, const tracked_movers& tracked)
{
    for_each_tracked_mover(registry,
                           tracked,
                           [](transform_component& transform, auto& component)
                           {
                               stepped_motion& motion = component.get_motion();
                               motion.step_end_position = transform.get_position_global();
                               motion.step_end_rotation = transform.get_rotation_global();
                           });
}

/// A contact callback that moved a mover teleported it: nothing lies between where it was and where it went.
void adopt_contact_teleports(entt::registry& registry, const tracked_movers& tracked)
{
    for_each_tracked_mover(registry,
                           tracked,
                           [](transform_component& transform, auto& component)
                           {
                               stepped_motion& motion = component.get_motion();
                               const bool turns = is_turning(component);
                               if(!is_at_pose(transform, motion.step_end_position, motion.step_end_rotation, turns))
                               {
                                   restart_at_transform(transform, motion);
                                   request_sync(transform);
                               }
                           });
}

/// One fixed step after its fixed update: queued moves handed over, the world synced and stepped, the poses read.
void simulate_step(rtti::context& ctx,
                   physics_backend& backend,
                   entt::registry& registry,
                   const tracked_movers& tracked,
                   float step_seconds)
{
    const delta_t step_dt(step_seconds);
    hand_moves_to_step(registry, tracked, backend, step_seconds);
    backend.sync_to_physics(ctx, step_dt);
    note_step_starts(registry, tracked);
    backend.simulate(step_dt);
    backend.sync_from_physics(ctx);
    note_step_ends(registry, tracked);
    backend.dispatch_contacts(ctx);
    adopt_contact_teleports(registry, tracked);
}

/**
 * @brief Where a mover is shown between its step poses. Equal poses are taken as they are: mixing a pose with itself
 *        is not exact, and a resting mover would be rewritten every frame.
 */
auto get_shown_position(const stepped_motion& motion, float step_fraction) -> math::vec3
{
    if(motion.step_start_position == motion.step_end_position)
    {
        return motion.step_end_position;
    }
    return math::mix(motion.step_start_position, motion.step_end_position, step_fraction);
}

auto get_shown_rotation(const stepped_motion& motion, float step_fraction) -> math::quat
{
    if(motion.step_start_rotation == motion.step_end_rotation)
    {
        return motion.step_end_rotation;
    }
    return math::slerp(motion.step_start_rotation, motion.step_end_rotation, step_fraction);
}

void show_movers(entt::registry& registry, const tracked_movers& tracked, float step_fraction)
{
    for_each_tracked_mover(registry,
                           tracked,
                           [&](transform_component& transform, auto& component)
                           {
                               stepped_motion& motion = component.get_motion();
                               place_pose(transform,
                                          get_shown_position(motion, step_fraction),
                                          get_shown_rotation(motion, step_fraction),
                                          is_turning(component));
                               note_shown(transform, motion);
                           });
}

/// Whether game time pays for another step and the frame may still run one.
auto is_step_due(float elapsed_seconds, float step_seconds, int steps_left) noexcept -> bool
{
    return steps_left > 0 && elapsed_seconds >= step_seconds;
}

/// How far game time has come through the next fixed step, 0..1. Without a step length nothing waits.
auto compute_step_fraction(float elapsed_seconds, float step_seconds) noexcept -> float
{
    return step_seconds > 0.0f ? math::clamp(elapsed_seconds / step_seconds, 0.0f, 1.0f) : 1.0f;
}

struct fixed_step_settings
{
    float step_seconds{};
    int max_steps{};
};

/// A fixed step that is not a positive time (a hand-edited settings file) would make every shown pose NaN.
auto is_usable_step(float step_seconds) noexcept -> bool
{
    return step_seconds > 0.0f && std::isfinite(step_seconds);
}

void warn_unusable_step_once(float step_seconds, float fallback_seconds)
{
    static bool has_warned = false;
    if(has_warned)
    {
        return;
    }
    has_warned = true;
    APPLOG_WARNING("The physics fixed timestep {} is not a positive time, so {} is used instead.",
                   step_seconds,
                   fallback_seconds);
}

/// The fixed step and the most steps a frame runs, from the project settings, with the default for a step unfit to use.
auto get_fixed_step_settings(rtti::context& ctx) -> fixed_step_settings
{
    const settings::physics_settings defaults{};
    const settings::physics_settings& physics = ctx.has<settings>() ? ctx.get<settings>().physics : defaults;
    if(is_usable_step(physics.fixed_timestep))
    {
        return {physics.fixed_timestep, physics.max_fixed_steps};
    }
    warn_unusable_step_once(physics.fixed_timestep, defaults.fixed_timestep);
    return {defaults.fixed_timestep, physics.max_fixed_steps};
}

/// A cast the backends can run: a unit direction and a finite length.
struct cast_query
{
    math::vec3 direction{};
    float max_distance{};
};

auto is_finite(const math::vec3& value) noexcept -> bool
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

/// A length a direction or rotation can be divided by.
auto is_usable_length(float length) noexcept -> bool
{
    return length > 0.0f && std::isfinite(length);
}

/// NaN fails too.
auto is_usable_distance(float distance) noexcept -> bool
{
    return distance >= 0.0f;
}

auto is_usable_radius(float radius) noexcept -> bool
{
    return radius >= 0.0f && std::isfinite(radius);
}

/**
 * @brief Makes a cast fit for the backends, whose libraries assert on bad input: the direction turns unit length and
 *        the length is cut to MAX_QUERY_DISTANCE.
 * @return Nothing for a cast that can hit nothing: a zero or non-finite direction, a non-finite origin, or a NaN or
 *         negative distance.
 */
auto make_cast_query(const math::vec3& origin, const math::vec3& direction, float max_distance)
    -> hpp::optional<cast_query>
{
    const float length = math::length(direction);
    if(!is_finite(origin) || !is_usable_length(length) || !is_usable_distance(max_distance))
    {
        return {};
    }
    return cast_query{direction / length, math::min(max_distance, MAX_QUERY_DISTANCE)};
}

/// A shape query's rotation made unit length, or nothing for a zero or non-finite one.
auto make_query_rotation(const math::quat& rotation) -> hpp::optional<math::quat>
{
    const float length = math::length(rotation);
    if(!is_usable_length(length))
    {
        return {};
    }
    return rotation / length;
}
} // namespace

void fixed_step_runner::advance(rtti::context& ctx,
                                physics_backend& backend,
                                float frame_seconds,
                                float step_seconds,
                                int max_steps)
{
    auto& registry = *ctx.get_cached<ecs>().get_scene().registry;
    elapsed_ += frame_seconds;
    // Game time left for later frames is capped at max_steps + 1 steps, a little more than one frame's steps catch up:
    // beyond that a slow frame rate would only fall further behind, so the rest is dropped. Queued character moves
    // span no more than that either.
    const float max_elapsed = step_seconds * static_cast<float>(max_steps + 1);
    const tracked_movers tracked = begin_frame(registry, backend, frame_seconds, max_elapsed);
    if(is_step_due(elapsed_, step_seconds, max_steps))
    {
        put_at_step_ends(registry, tracked);
    }
    for(int steps = 0; is_step_due(elapsed_, step_seconds, max_steps - steps); ++steps)
    {
        APP_SCOPE_PERF("Physics/Fixed Update");
        run_fixed_update(ctx, delta_t(step_seconds));
        simulate_step(ctx, backend, registry, tracked, step_seconds);
        elapsed_ -= step_seconds;
    }
    elapsed_ = math::min(elapsed_, max_elapsed);
    step_fraction_ = compute_step_fraction(elapsed_, step_seconds);
    show_movers(registry, tracked, step_fraction_);
}

void fixed_step_runner::run_fixed_update(rtti::context& ctx, delta_t step)
{
    is_in_fixed_update_ = true;
    ctx.get_cached<events>().on_frame_fixed_update(ctx, step);
    is_in_fixed_update_ = false;
}

void fixed_step_runner::reset() noexcept
{
    elapsed_ = 0.0f;
    step_fraction_ = 0.0f;
}

auto fixed_step_runner::is_in_fixed_update() const noexcept -> bool
{
    return is_in_fixed_update_;
}

auto fixed_step_runner::get_step_fraction() const noexcept -> float
{
    return step_fraction_;
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
    ev.on_frame_update.connect(sentinel_,
                               frame_update_priority::physics_late,
                               this,
                               &physics_system::on_frame_late_update);

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
    registry.view<physics_component>().each(
        [](auto /*entity*/, auto& body)
        {
            body.get_motion() = {};
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

    const fixed_step_settings steps = get_fixed_step_settings(ctx);
    auto& registry = *ctx.get_cached<ecs>().get_scene().registry;
    // The animation has posed the bones for this frame: ragdolls read it before the steps and pose the bones after.
    // A paused or frozen game steps nothing, but the animation still poses the bones every frame, so the ragdolls pose
    // them again where physics left them.
    capture_ragdoll_animations(registry, dt.count());
    if(dt > delta_t::zero())
    {
        step_runner_.advance(ctx, *backend_, dt.count(), steps.step_seconds, steps.max_steps);
    }
    show_ragdolls(registry, step_runner_.get_step_fraction(), dt.count());
}

auto physics_system::get_step_fraction() const noexcept -> float
{
    return step_runner_.get_step_fraction();
}

void physics_system::on_frame_late_update(rtti::context& ctx, const frame_times& /*times*/)
{
    APP_SCOPE_PERF("Physics/System Late Update");
    if(!ctx.get_cached<play_mode>().is_simulation_running())
    {
        return;
    }
    show_moved_ragdolls(*ctx.get_cached<ecs>().get_scene().registry);
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
    return get_default_gravity();
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
    const auto query = make_cast_query(origin, direction, max_distance);
    const auto query_rotation = make_query_rotation(rotation);
    if(!backend_ || !query || !query_rotation)
    {
        return {};
    }
    return backend_->shape_cast(shape,
                                origin,
                                *query_rotation,
                                query->direction,
                                query->max_distance,
                                layer_mask,
                                query_sensors);
}

auto physics_system::shape_cast_all(const physics_compound_shape& shape,
                                    const math::vec3& origin,
                                    const math::quat& rotation,
                                    const math::vec3& direction,
                                    float max_distance,
                                    int layer_mask,
                                    bool query_sensors) const -> physics_vector<raycast_hit>
{
    const auto query = make_cast_query(origin, direction, max_distance);
    const auto query_rotation = make_query_rotation(rotation);
    if(!backend_ || !query || !query_rotation)
    {
        return {};
    }
    return backend_->shape_cast_all(shape,
                                    origin,
                                    *query_rotation,
                                    query->direction,
                                    query->max_distance,
                                    layer_mask,
                                    query_sensors);
}

auto physics_system::shape_overlap(const physics_compound_shape& shape,
                                   const math::vec3& origin,
                                   const math::quat& rotation,
                                   int layer_mask,
                                   bool query_sensors) const -> physics_vector<entt::entity>
{
    const auto query_rotation = make_query_rotation(rotation);
    if(!backend_ || !is_finite(origin) || !query_rotation)
    {
        return {};
    }
    return backend_->shape_overlap(shape, origin, *query_rotation, layer_mask, query_sensors);
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
    const auto query = make_cast_query(origin, direction, max_distance);
    if(!backend_ || !query)
    {
        return {};
    }
    return backend_->ray_cast(origin, query->direction, query->max_distance, layer_mask, query_sensors);
}

auto physics_system::ray_cast_all(const math::vec3& origin,
                                  const math::vec3& direction,
                                  float max_distance,
                                  int layer_mask,
                                  bool query_sensors) const -> physics_vector<raycast_hit>
{
    const auto query = make_cast_query(origin, direction, max_distance);
    if(!backend_ || !query)
    {
        return {};
    }
    return backend_->ray_cast_all(origin, query->direction, query->max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float radius,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) const -> hpp::optional<raycast_hit>
{
    const auto query = make_cast_query(origin, direction, max_distance);
    if(!backend_ || !query || !is_usable_radius(radius))
    {
        return {};
    }
    return backend_->sphere_cast(origin, query->direction, radius, query->max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_cast_all(const math::vec3& origin,
                                     const math::vec3& direction,
                                     float radius,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) const -> physics_vector<raycast_hit>
{
    const auto query = make_cast_query(origin, direction, max_distance);
    if(!backend_ || !query || !is_usable_radius(radius))
    {
        return {};
    }
    return backend_->sphere_cast_all(origin, query->direction, radius, query->max_distance, layer_mask, query_sensors);
}

auto physics_system::sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors) const
    -> physics_vector<entt::entity>
{
    if(!backend_ || !is_finite(origin) || !is_usable_radius(radius))
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
