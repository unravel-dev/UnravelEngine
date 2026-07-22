#pragma once
#include <engine/engine_export.h>

#include <engine/physics/backend/physics_backend.h>

namespace unravel
{

/**
 * @class bullet_backend
 * @brief Bullet3 implementation of physics_backend.
 */
class bullet_backend : public physics_backend
{
public:
    void init() override;
    void deinit() override;
    void on_play_begin(rtti::context& ctx) override;
    void on_play_end(rtti::context& ctx) override;
    void on_pause(rtti::context& ctx) override;
    void on_resume(rtti::context& ctx) override;

    void sync_to_physics(rtti::context& ctx, delta_t step_dt) override;
    void simulate(delta_t step_dt) override;
    void sync_from_physics(rtti::context& ctx) override;
    void dispatch_contacts(rtti::context& ctx) override;

    void apply_explosion_force(physics_component& comp,
                               float explosion_force,
                               const math::vec3& explosion_position,
                               float explosion_radius,
                               float upwards_modifier,
                               force_mode mode) override;
    void apply_force(physics_component& comp, const math::vec3& force, force_mode mode) override;
    void apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode) override;
    void clear_kinematic_velocities(physics_component& comp) override;

    auto ray_cast(const math::vec3& origin,
                  const math::vec3& direction,
                  float max_distance,
                  int layer_mask,
                  bool query_sensors) -> hpp::optional<raycast_hit> override;

    auto ray_cast_all(const math::vec3& origin,
                      const math::vec3& direction,
                      float max_distance,
                      int layer_mask,
                      bool query_sensors) -> physics_vector<raycast_hit> override;

    auto sphere_cast(const math::vec3& origin,
                     const math::vec3& direction,
                     float radius,
                     float max_distance,
                     int layer_mask,
                     bool query_sensors) -> hpp::optional<raycast_hit> override;

    auto sphere_cast_all(const math::vec3& origin,
                         const math::vec3& direction,
                         float radius,
                         float max_distance,
                         int layer_mask,
                         bool query_sensors) -> physics_vector<raycast_hit> override;

    auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
        -> physics_vector<entt::entity> override;

    void on_create_component(entt::registry& r, entt::entity e) override;
    void on_destroy_component(entt::registry& r, entt::entity e) override;

    void on_create_cc_component(entt::registry& r, entt::entity e) override;
    void on_destroy_cc_component(entt::registry& r, entt::entity e) override;

    void move_character(character_controller_component& comp, const math::vec3& displacement) override;
    void jump_character(character_controller_component& comp, const math::vec3& direction) override;
    void apply_impulse_character(character_controller_component& comp, const math::vec3& impulse) override;
    void warp_character(character_controller_component& comp, const math::vec3& position) override;
    void set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity) override;
    void sync_character_runtime_state(character_controller_component& comp) override;

    void draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd) override;
    void draw_gizmo(rtti::context& ctx, physics_component& comp, const camera& cam, gfx::dd_raii& dd) override;
    void draw_gizmo(rtti::context& ctx,
                    character_controller_component& comp,
                    const camera& cam,
                    gfx::dd_raii& dd) override;

    static void on_create_active_component(entt::registry& r, entt::entity e);
    static void on_destroy_active_component(entt::registry& r, entt::entity e);
    static void on_destroy_bullet_rigidbody_component(entt::registry& r, entt::entity e);
    static void on_destroy_bullet_cc_component(entt::registry& r, entt::entity e);
};

} // namespace unravel
