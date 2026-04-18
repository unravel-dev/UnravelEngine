#pragma once

#include <cstddef>
#include <entt/entt.hpp>
#include <math/math.h>

namespace unravel
{

// For all position-based solvers below, the `pole` parameter is a world-space
// hint that tells the solver which side of the (root -> end) line the
// intermediate joints (e.g. the knee or elbow) should bend toward. Pass a
// zero-length vector to disable the constraint - CCD and FABRIK will then
// place intermediate joints wherever the raw solve puts them, while the
// analytical two-bone solver will fall back to the current bend direction.
//
// Argument order convention (shared across the three solvers):
//   (entity, target, pole) -> (chain structure, if any) -> tuning knobs.
auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         const math::vec3& pole,
                         size_t num_bones_in_chain,
                         int max_iterations = 10,
                         float threshold = 0.001f) -> bool;

auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            const math::vec3& pole,
                            size_t num_bones_in_chain,
                            int max_iterations = 10,
                            float threshold = 0.001f) -> bool;

auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const math::vec3& pole,
                              float weight = 1.0f,
                              float soften = 1.0f) -> bool;

auto ik_look_at_position(entt::handle end_effector,
                         const math::vec3& target,
                         float weight = 1.0f) -> bool;

} // namespace unravel
