#pragma once
#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/physics/backend/bullet/bullet_backend.h>

namespace unravel
{

/**
 * @class physics_system
 * @brief Manages the physics operations using the specified backend.
 */
class physics_system
{
public:
    using backend_type = bullet_backend; ///< The backend type used for physics operations.


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

    /**
     * @brief Called when a physics component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when a physics component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);

    static void apply_explosion_force(physics_component& comp,
                                      float explosion_force,
                                      const math::vec3& explosion_position,
                                      float explosion_radius,
                                      float upwards_modifier,
                                      force_mode mode);

    static void apply_force(physics_component& comp, const math::vec3& force, force_mode mode);

    /**
     * @brief Applies a torque to the specified physics component.
     * @param comp The physics component.
     * @param torque The torque vector.
     */
    static void apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode);

    /**
     * @brief Clears kinematic velocities for the specified physics component.
     * @param comp The physics component.
     */
    static void clear_kinematic_velocities(physics_component& comp);

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

private:
    /**
     * @brief Updates the physics system for each frame.
     * @param ctx The context for the update.
     * @param dt The delta time for the frame.
     */
    void on_frame_update(rtti::context& ctx, delta_t dt);

    /**
     * @brief Called when playback begins.
     * @param ctx The context for the playback.
     */
    void on_play_begin(rtti::context& ctx);

    /**
     * @brief Called when playback ends.
     * @param ctx The context for the playback.
     */
    void on_play_end(rtti::context& ctx);

    /**
     * @brief Called when playback is paused.
     * @param ctx The context for the playback.
     */
    void on_pause(rtti::context& ctx);

    /**
     * @brief Called when playback is resumed.
     * @param ctx The context for the playback.
     */
    void on_resume(rtti::context& ctx);

    /**
     * @brief Skips the next frame update.
     * @param ctx The context for the update.
     */
    void on_skip_next_frame(rtti::context& ctx);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0); ///< Sentinel value to manage shared resources.

    backend_type backend_; ///< The backend used for physics operations.
};

} // namespace unravel
