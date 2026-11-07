#pragma once

#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/physics_types.h>
#include <engine/rendering/camera.h>
#include <graphics/debugdraw.h>

#include <memory>

namespace unravel
{

/**
 * @class physics_backend
 * @brief Physics engine adapter selected at runtime via create_physics_backend.
 *
 * Owns world lifetime, body creation, queries, and contact dispatch.
 * Fixed-step accumulation and body-type sync policy live in physics_system.
 */
class physics_backend
{
public:
    virtual ~physics_backend() = default;

    virtual void init() = 0;
    virtual void deinit() = 0;

    virtual void on_play_begin(rtti::context& ctx) = 0;
    virtual void on_play_end(rtti::context& ctx) = 0;
    virtual void on_pause(rtti::context& ctx) = 0;
    virtual void on_resume(rtti::context& ctx) = 0;

    /**
     * @brief Push dirty ECS state into the backend for one fixed step.
     * @param step_dt Fixed step length (used for kinematic velocity derivation).
     */
    virtual void sync_to_physics(rtti::context& ctx, delta_t step_dt) = 0;

    /**
     * @brief Advance the backend simulation by exactly one fixed step.
     */
    virtual void simulate(delta_t step_dt) = 0;

    /**
     * @brief Pull dynamic (and other backend-owned) state back into ECS.
     */
    virtual void sync_from_physics(rtti::context& ctx) = 0;

    /**
     * @brief Dispatch enter/exit contact events accumulated during simulate.
     */
    virtual void dispatch_contacts(rtti::context& ctx) = 0;

    virtual void apply_explosion_force(physics_component& comp,
                                       float explosion_force,
                                       const math::vec3& explosion_position,
                                       float explosion_radius,
                                       float upwards_modifier,
                                       force_mode mode) = 0;
    virtual void apply_force(physics_component& comp, const math::vec3& force, force_mode mode) = 0;
    /// Applies a force (or impulse, by @p mode) at a world point, so it also turns the body.
    virtual void apply_force_at_position(physics_component& comp,
                                         const math::vec3& force,
                                         const math::vec3& position,
                                         force_mode mode) = 0;
    virtual void apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode) = 0;
    virtual void clear_kinematic_velocities(physics_component& comp) = 0;
    /// Wakes a dynamic body, or puts it to sleep together with its island: the bodies it touches sleep with it, even
    /// ones that may not fall asleep on their own.
    virtual void set_body_awake(physics_component& comp, bool awake) = 0;

    /// World gravity while playing; the project setting seeds it at play start.
    virtual void set_gravity(const math::vec3& gravity) = 0;
    virtual auto get_gravity() const -> math::vec3 = 0;

    /**
     * @brief Pushes every dynamic body within @p radius of @p center away from it, by the area each one turns to it.
     * @param falloff Distance beyond the radius over which the push fades to nothing.
     * @param impulse_per_area Newton seconds per square meter facing the center; negative pulls in.
     */
    virtual void explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask) = 0;

    /**
     * @brief Sweeps a box, capsule or sphere and returns the first hit. The shape is placed at @p origin turned by
     *        @p rotation; its own center and rotation offset it from there.
     *
     * Every cast takes a unit @p direction and a finite @p max_distance (physics_system sees to both) and reports the
     * contact point, the surface normal and the distance travelled along the direction.
     */
    virtual auto shape_cast(const physics_compound_shape& shape,
                            const math::vec3& origin,
                            const math::quat& rotation,
                            const math::vec3& direction,
                            float max_distance,
                            int layer_mask,
                            bool query_sensors) -> hpp::optional<raycast_hit> = 0;

    /// Like shape_cast, every hit, nearest first.
    virtual auto shape_cast_all(const physics_compound_shape& shape,
                                const math::vec3& origin,
                                const math::quat& rotation,
                                const math::vec3& direction,
                                float max_distance,
                                int layer_mask,
                                bool query_sensors) -> physics_vector<raycast_hit> = 0;

    /// Entities whose shapes touch a box, capsule or sphere placed like a shape cast's start.
    virtual auto shape_overlap(const physics_compound_shape& shape,
                               const math::vec3& origin,
                               const math::quat& rotation,
                               int layer_mask,
                               bool query_sensors) -> physics_vector<entt::entity> = 0;

    virtual auto ray_cast(const math::vec3& origin,
                          const math::vec3& direction,
                          float max_distance,
                          int layer_mask,
                          bool query_sensors) -> hpp::optional<raycast_hit> = 0;

    virtual auto ray_cast_all(const math::vec3& origin,
                              const math::vec3& direction,
                              float max_distance,
                              int layer_mask,
                              bool query_sensors) -> physics_vector<raycast_hit> = 0;

    virtual auto sphere_cast(const math::vec3& origin,
                             const math::vec3& direction,
                             float radius,
                             float max_distance,
                             int layer_mask,
                             bool query_sensors) -> hpp::optional<raycast_hit> = 0;

    virtual auto sphere_cast_all(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float radius,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) -> physics_vector<raycast_hit> = 0;

    virtual auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
        -> physics_vector<entt::entity> = 0;

    virtual void on_create_component(entt::registry& r, entt::entity e) = 0;
    virtual void on_destroy_component(entt::registry& r, entt::entity e) = 0;

    virtual void on_create_cc_component(entt::registry& r, entt::entity e) = 0;
    virtual void on_destroy_cc_component(entt::registry& r, entt::entity e) = 0;

    /// Joints are made in the next sync, once their bodies exist.
    virtual void on_create_joint_component(entt::registry& r, entt::entity e) = 0;
    virtual void on_destroy_joint_component(entt::registry& r, entt::entity e) = 0;

    /// Ragdoll bodies are made in the next sync, from the animated pose physics_system captured.
    virtual void on_create_ragdoll_component(entt::registry& r, entt::entity e) = 0;
    virtual void on_destroy_ragdoll_component(entt::registry& r, entt::entity e) = 0;

    virtual void move_character(character_controller_component& comp, const math::vec3& displacement) = 0;
    virtual void jump_character(character_controller_component& comp, const math::vec3& direction) = 0;
    virtual void apply_impulse_character(character_controller_component& comp, const math::vec3& impulse) = 0;
    virtual void warp_character(character_controller_component& comp, const math::vec3& position) = 0;
    virtual void set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity) = 0;
    virtual void sync_character_runtime_state(character_controller_component& comp) = 0;

    virtual void draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd) = 0;
    virtual void draw_gizmo(rtti::context& ctx, physics_component& comp, const camera& cam, gfx::dd_raii& dd) = 0;
    virtual void draw_gizmo(rtti::context& ctx,
                            character_controller_component& comp,
                            const camera& cam,
                            gfx::dd_raii& dd) = 0;

    /// Counts of what the running world holds, for tools. A backend that cannot count something reports 0.
    struct world_stats
    {
        int body_count{};
        int awake_body_count{};
        int shape_count{};
        int contact_count{};
        int joint_count{};
        int island_count{};
        /// Milliseconds the last step took.
        float step_ms{};
    };
    /// What the running world holds now; all zero when no world runs.
    virtual auto get_world_stats() const -> world_stats = 0;
};

/**
 * @brief The threads a backend steps a world on, the calling one included: one per performance core but one, at
 * least one; one in the single-threaded mode, where a backend starts no threads.
 */
auto get_physics_worker_count() -> int;

/**
 * @brief Create a physics backend instance.
 * @param type Backend to create. auto_detect resolves to the engine default.
 * @return Backend instance, or nullptr if the type is unavailable in this build.
 */
auto create_physics_backend(physics_backend_type type = physics_backend_type::auto_detect)
    -> std::unique_ptr<physics_backend>;

} // namespace unravel
