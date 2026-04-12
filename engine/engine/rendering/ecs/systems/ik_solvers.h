#pragma once

#include <cstddef>
#include <entt/entt.hpp>
#include <math/math.h>

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

} // namespace unravel
