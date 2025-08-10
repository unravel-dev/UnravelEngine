#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <engine/physics/ecs/components/physics_component.h>
#include <engine/rendering/camera.h>
#include <graphics/debugdraw.h>
#include <hpp/small_vector.hpp>

namespace unravel
{
class camera;

template<typename T, size_t SmallSizeCapacity = 8>
using physics_vector = hpp::small_vector<T, SmallSizeCapacity>;

struct bullet_backend
{
    void init();
    void deinit();
    void on_frame_update(rtti::context& ctx, delta_t dt);
    void on_play_begin(rtti::context& ctx);
    void on_play_end(rtti::context& ctx);
    void on_pause(rtti::context& ctx);
    void on_resume(rtti::context& ctx);
    void on_skip_next_frame(rtti::context& ctx);

    static void apply_explosion_force(physics_component& comp,
                                      float explosion_force,
                                      const math::vec3& explosion_position,
                                      float explosion_radius,
                                      float upwards_modifier,
                                      force_mode mode);
    static void apply_force(physics_component& comp, const math::vec3& force, force_mode mode);
    static void apply_torque(physics_component& comp, const math::vec3& toruqe, force_mode mode);
    static void clear_kinematic_velocities(physics_component& comp);

    static auto ray_cast(const math::vec3& origin,
                         const math::vec3& direction,
                         float max_distance,
                         int layer_mask,
                         bool query_sensors) -> hpp::optional<raycast_hit>;

    static auto ray_cast_all(const math::vec3& origin,
                             const math::vec3& direction,
                             float max_distance,
                             int layer_mask,
                             bool query_sensors) -> physics_vector<raycast_hit>;

    static auto sphere_cast(const math::vec3& origin,
                            const math::vec3& direction,
                            float radius,
                            float max_distance,
                            int layer_mask,
                            bool query_sensors) -> hpp::optional<raycast_hit>;

    static auto sphere_cast_all(const math::vec3& origin,
                                const math::vec3& direction,
                                float radius,
                                float max_distance,
                                int layer_mask,
                                bool query_sensors) -> physics_vector<raycast_hit>;

    static auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
        -> physics_vector<entt::entity>;

    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);
    static void on_destroy_bullet_rigidbody_component(entt::registry& r, entt::entity e);

    static void on_create_active_component(entt::registry& r, entt::entity e);
    static void on_destroy_active_component(entt::registry& r, entt::entity e);

    static void draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd);
    static void draw_gizmo(rtti::context& ctx, physics_component& comp, const camera& cam, gfx::dd_raii& dd);
};
} // namespace unravel
