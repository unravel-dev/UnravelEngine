#pragma once
#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/physics/backend/physics_backend.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/physics_types.h>

#include <memory>

namespace unravel
{

/**
 * @class physics_system
 * @brief Owns fixed-step simulation and dispatches work to the compile-time physics backend.
 */
class physics_system
{
public:
    /**
     * @brief Initializes the physics system with the given context.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the physics system with the given context.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;

    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    static void apply_explosion_force(physics_component& comp,
                                      float explosion_force,
                                      const math::vec3& explosion_position,
                                      float explosion_radius,
                                      float upwards_modifier,
                                      force_mode mode);

    static void apply_force(physics_component& comp, const math::vec3& force, force_mode mode);
    static void apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode);
    static void clear_kinematic_velocities(physics_component& comp);

    static void on_create_cc_component(entt::registry& r, entt::entity e);
    static void on_destroy_cc_component(entt::registry& r, entt::entity e);

    static void move_character(character_controller_component& comp, const math::vec3& displacement);
    static void jump_character(character_controller_component& comp, const math::vec3& direction);
    static void apply_impulse_character(character_controller_component& comp, const math::vec3& impulse);
    static void warp_character(character_controller_component& comp, const math::vec3& position);
    static void set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity);

    auto ray_cast(const math::vec3& origin,
                  const math::vec3& direction,
                  float max_distance,
                  int layer_mask,
                  bool query_sensors) const -> hpp::optional<raycast_hit>;

    auto ray_cast_all(const math::vec3& origin,
                      const math::vec3& direction,
                      float max_distance,
                      int layer_mask,
                      bool query_sensors) const -> physics_vector<raycast_hit>;

    auto sphere_cast(const math::vec3& origin,
                     const math::vec3& direction,
                     float radius,
                     float max_distance,
                     int layer_mask,
                     bool query_sensors) const -> hpp::optional<raycast_hit>;

    auto sphere_cast_all(const math::vec3& origin,
                         const math::vec3& direction,
                         float radius,
                         float max_distance,
                         int layer_mask,
                         bool query_sensors) const -> physics_vector<raycast_hit>;

    auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors) const
        -> physics_vector<entt::entity>;

    /**
     * @brief Draws backend physics debug gizmos for the active world.
     */
    void draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd);

    auto get_backend() -> physics_backend*;
    auto get_backend() const -> const physics_backend*;

private:
    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_play_begin(rtti::context& ctx);
    void on_play_end(rtti::context& ctx);
    void on_pause(rtti::context& ctx);
    void on_resume(rtti::context& ctx);
    void on_skip_next_frame(rtti::context& ctx);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    std::unique_ptr<physics_backend> backend_;
    float elapsed_{};
};

} // namespace unravel
