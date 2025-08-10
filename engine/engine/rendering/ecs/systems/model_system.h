#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/scene.h>
#include <math/math.h>
#include <hpp/span.hpp>

namespace unravel
{

auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         size_t num_bones_in_chain,
                         float threshold = 0.001f,
                         int max_iterations = 10) -> bool;

auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            size_t num_bones_in_chain,
                            float threshold = 0.001f,
                            int max_iterations = 10) -> bool;

auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const math::vec3& forward,
                              float weight = 1.0f,
                              float soften = 1.0f,
                              int max_iterations = 10) -> bool;

auto ik_look_at_position(entt::handle end_effector,
                         const math::vec3& target,
                         float weight = 1.0f) -> bool;

class model_system
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;
    void on_play_begin(rtti::context& ctx);
    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);
    void on_frame_update(scene& scn, delta_t dt);
    void on_frame_before_render(scene& scn, delta_t dt);

private:
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
};
} // namespace unravel
