#pragma once

#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
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
    virtual void apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode) = 0;
    virtual void clear_kinematic_velocities(physics_component& comp) = 0;

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
};

/**
 * @brief Create a physics backend instance.
 * @param type Backend to create. Must be compiled into this build.
 * @return Backend instance, or nullptr if the type is unavailable in this build.
 */
auto create_physics_backend(physics_backend_type type = physics_backend_type::bullet)
    -> std::unique_ptr<physics_backend>;

} // namespace unravel
