/*
 * Validation suite for the IK solvers.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite ik
 *
 * These pin the correctness fixes from the 2026-08 IK review:
 *   - the pole constraint rotating each intermediate joint by its own angle,
 *     which broke bone lengths on any chain with 2+ intermediate joints and
 *     pulled the effector off target;
 *   - chain collection terminating on `bone_index == 0`, which addresses the
 *     mesh's skin influence list rather than the skeleton root;
 *   - `soften` defaulting to 1, so a straight limb never reached its target;
 *   - the missing effector-rotation goal;
 *   - the aim solver assuming a +Z bone axis and having no cone limit;
 *   - unvalidated goals writing NaN into the skeleton.
 */

#include "../tests.h"

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/components/ik_components.h>
#include <engine/animation/ecs/systems/rig_system.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/systems/ik_solvers.h>

#include <logging/logging.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n", what.c_str(), actual, expected, tolerance);
    }
}

void check_finite(const math::vec3& v, const std::string& what)
{
    check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z), what);
}

/**
 * @brief A joint chain in its own registry.
 *
 * The registry is heap-allocated so its address is stable: entt::handle stores a
 * registry pointer, and returning a by-value registry would leave every handle
 * in the struct dangling.
 */
struct test_chain
{
    std::unique_ptr<entt::registry> registry;
    /// Root-first, so joints.back() is the end effector.
    std::vector<entt::handle> joints;
    /// Non-bone ancestor above the skeleton root (the armature container).
    entt::handle container;

    auto transform(size_t index) const -> transform_component&
    {
        return joints[index].get<transform_component>();
    }

    auto position(size_t index) const -> math::vec3
    {
        return transform(index).get_position_global();
    }

    auto effector() const -> entt::handle
    {
        return joints.back();
    }

    auto effector_position() const -> math::vec3
    {
        return position(joints.size() - 1);
    }
};

auto make_registry() -> std::unique_ptr<entt::registry>
{
    auto registry = std::make_unique<entt::registry>();
    registry->on_construct<transform_component>().connect<&transform_component::on_create_component>();
    return registry;
}

auto spawn(entt::registry& registry, const math::vec3& position) -> entt::handle
{
    const auto entity = registry.create();
    auto& transform = registry.emplace<transform_component>(entity);
    transform.set_position_global(position);
    return entt::handle(registry, entity);
}

/**
 * @brief Builds a parented joint chain at the given world positions.
 *
 * @param bone_indices When non-empty, every joint gets a bone_component with the
 *                     matching index and the chain hangs off a non-bone
 *                     container - the arrangement an imported skinned rig has.
 */
auto make_chain(const std::vector<math::vec3>& positions, const std::vector<uint32_t>& bone_indices = {}) -> test_chain
{
    test_chain chain;
    chain.registry = make_registry();
    chain.container = spawn(*chain.registry, math::vec3(0.0f, 0.0f, 0.0f));
    for(size_t i = 0; i < positions.size(); ++i)
    {
        auto joint = spawn(*chain.registry, positions[i]);
        if(i < bone_indices.size())
        {
            chain.registry->emplace<bone_component>(joint.entity()).bone_index = bone_indices[i];
        }
        joint.get<transform_component>().set_parent(i == 0 ? chain.container : chain.joints[i - 1]);
        chain.joints.push_back(joint);
    }
    return chain;
}

auto make_leg(float knee_offset_z = 0.15f) -> test_chain
{
    return make_chain({{0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, knee_offset_z}, {0.0f, 0.0f, 0.0f}});
}

/// Component of `point` perpendicular to the chain axis, measured along `dir`.
auto side_offset(const math::vec3& point, const math::vec3& root, const math::vec3& end, const math::vec3& dir)
    -> float
{
    const math::vec3 axis = math::normalize(end - root);
    const math::vec3 flat_dir = math::normalize(dir - glm::dot(dir, axis) * axis);
    const math::vec3 offset = point - root;
    return glm::dot(offset - glm::dot(offset, axis) * axis, flat_dir);
}

// -----------------------------------------------------------------------------

void test_rejects_invalid_input()
{
    std::printf("test_rejects_invalid_input\n");
    const auto pole = ik_pole::from_point({0.0f, 1.0f, 2.0f});

    check(!ik_set_position_ccd({}, {0.0f, 0.0f, 0.0f}, pole, 2).applied, "ccd rejects an empty handle");
    check(!ik_set_position_fabrik({}, {0.0f, 0.0f, 0.0f}, pole, 2).applied, "fabrik rejects an empty handle");
    check(!ik_set_position_two_bone({}, {0.0f, 0.0f, 0.0f}, pole).applied, "two-bone rejects an empty handle");
    check(!ik_look_at_position({}, {0.0f, 0.0f, 1.0f}).applied, "look-at rejects an empty handle");
    check(!ik_set_rotation({}, math::identity<math::quat>()).applied, "set-rotation rejects an empty handle");

    auto registry = make_registry();
    const auto bare = entt::handle(*registry, registry->create());
    check(!ik_look_at_position(bare, {0.0f, 0.0f, 1.0f}).applied, "look-at rejects an entity without a transform");

    auto chain = make_leg();
    check(!ik_set_position_fabrik(chain.effector(), {0.2f, 0.2f, 0.2f}, pole, 0).applied,
          "a zero-length chain request is rejected");

    // A non-finite goal must be refused, not clamped: one NaN local rotation
    // propagates through the skinning palette and blanks the whole mesh.
    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const math::vec3 before = chain.effector_position();
    check(!ik_set_position_two_bone(chain.effector(), {nan_value, 0.0f, 0.0f}, pole).applied,
          "a NaN target is rejected");
    check(!ik_set_position_fabrik(chain.effector(), {0.2f, 0.2f, 0.2f}, ik_pole::from_point({nan_value, 0.0f, 0.0f}), 2)
               .applied,
          "a NaN pole is rejected");
    check(!ik_set_position_two_bone(chain.effector(), {0.2f, 0.2f, 0.2f}, pole, nan_value).applied,
          "a NaN weight is rejected");
    check_finite(chain.effector_position(), "the pose survives rejected goals");
    check_near(math::length(chain.effector_position() - before), 0.0f, 1e-5f, "rejected goals leave the pose untouched");
}

void test_two_bone_reaches_and_honors_pole()
{
    std::printf("test_two_bone_reaches_and_honors_pole\n");
    auto chain = make_leg();
    const math::vec3 target{0.4f, 0.2f, 0.1f};
    const auto result = ik_set_position_two_bone(chain.effector(), target, ik_pole::from_point({0.0f, 1.0f, 2.0f}));

    check(result.applied, "two-bone applies a reachable pose");
    check(result.reached, "two-bone reports a reachable target as reached");
    check_finite(chain.effector_position(), "two-bone effector is finite");
    check_finite(chain.position(1), "two-bone mid joint is finite");
    check_near(math::length(chain.effector_position() - target), 0.0f, 2e-3f, "two-bone lands on the target");
    check(chain.position(1).z > 0.05f, "two-bone knee bends toward the +Z pole");

    // A pole on the opposite side must flip the bend, not merely nudge it.
    auto mirrored = make_leg();
    ik_set_position_two_bone(mirrored.effector(), target, ik_pole::from_point({0.0f, 1.0f, -2.0f}));
    check(mirrored.position(1).z < -0.05f, "a -Z pole bends the knee the other way");
}

void test_two_bone_soften_costs_reach()
{
    std::printf("test_two_bone_soften_costs_reach\n");
    // Straight-down target at 96% of full extension: reachable, and inside the
    // 15% soften band. This is the common foot-IK case on a downhill step.
    const math::vec3 target{0.0f, 0.08f, 0.0f};

    auto exact = make_leg();
    const auto exact_result = ik_set_position_two_bone(exact.effector(), target, ik_pole::from_point({0.0f, 1.0f, 2.0f}));
    check(exact_result.reached, "soften 0 reaches a near-straight target");
    check_near(math::length(exact.effector_position() - target), 0.0f, 3e-3f, "soften 0 lands on the target");

    auto softened = make_leg();
    ik_set_position_two_bone(softened.effector(), target, ik_pole::from_point({0.0f, 1.0f, 2.0f}), 1.0f, 1.0f);
    const float shortfall = math::length(softened.effector_position() - target);
    // Documented tradeoff, not a bug - but it is why soften must not default on.
    check(shortfall > 0.01f, "soften 1 deliberately falls short of a near-straight target");
}

void test_two_bone_unreachable_stays_finite()
{
    std::printf("test_two_bone_unreachable_stays_finite\n");
    auto chain = make_leg();
    const math::vec3 target{0.0f, 2.0f, 20.0f};
    const auto result = ik_set_position_two_bone(chain.effector(), target, ik_pole::from_point({0.0f, 1.0f, 2.0f}));

    check(result.applied, "two-bone applies a stretched pose");
    check(!result.reached, "two-bone reports an out-of-reach target as not reached");
    check(result.distance > 1.0f, "two-bone reports how far short it fell");
    check_finite(chain.effector_position(), "stretched two-bone effector is finite");
    check(math::length(chain.effector_position() - chain.position(0)) < 2.1f,
          "stretched two-bone stays within chain length");
}

void test_two_bone_requires_three_joints()
{
    std::printf("test_two_bone_requires_three_joints\n");
    // Two bones only, and the walk stops at the skeleton root because the
    // container above it is not a bone. Falling back to another solver here would
    // silently change the reach, softening and blend semantics, so the call fails.
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}, {5u, 7u});
    check(!ik_set_position_two_bone(chain.effector(), {0.5f, 0.5f, 0.0f}, {}).applied,
          "two-bone refuses a chain shorter than three joints");
}

void test_weight_bounds()
{
    std::printf("test_weight_bounds\n");
    const math::vec3 target{0.4f, 0.2f, 0.1f};
    const auto pole = ik_pole::from_point({0.0f, 1.0f, 2.0f});

    auto untouched = make_leg();
    const math::vec3 rest = untouched.effector_position();
    check(!ik_set_position_two_bone(untouched.effector(), target, pole, 0.0f).applied, "weight 0 does not apply");
    check_near(math::length(untouched.effector_position() - rest), 0.0f, 1e-5f, "weight 0 leaves the pose untouched");

    auto full = make_leg();
    ik_set_position_two_bone(full.effector(), target, pole, 1.0f);
    const float full_error = math::length(full.effector_position() - target);

    auto half = make_leg();
    ik_set_position_two_bone(half.effector(), target, pole, 0.5f);
    const float half_error = math::length(half.effector_position() - target);

    check(half_error < math::length(rest - target), "weight 0.5 moves the effector toward the target");
    check(half_error > full_error, "weight 0.5 stops short of the full solve");

    // Weights outside [0, 1] are clamped rather than extrapolated.
    auto over = make_leg();
    ik_set_position_two_bone(over.effector(), target, pole, 4.0f);
    check_near(math::length(over.effector_position() - target), full_error, 1e-4f, "weight above 1 clamps to a full solve");

    // The iterative solvers gained the same knob; they had none before.
    auto ccd_untouched = make_leg();
    ik_solver_params zero_weight;
    zero_weight.weight = 0.0f;
    check(!ik_set_position_ccd(ccd_untouched.effector(), target, pole, 2, zero_weight).applied,
          "ccd weight 0 does not apply");
    check_near(math::length(ccd_untouched.effector_position() - rest), 0.0f, 1e-5f,
               "ccd weight 0 leaves the pose untouched");
}

void test_pole_preserves_reach_on_long_chains()
{
    std::printf("test_pole_preserves_reach_on_long_chains\n");
    // The regression: the pole pass used to rotate every intermediate joint onto
    // the pole half-plane by its OWN angle. That is only an isometry when there
    // is a single intermediate joint; with two or more it changes the distance
    // between adjacent joints, so the position set stops describing a chain of
    // fixed-length bones and the effector is left off target.
    const std::vector<math::vec3> four_bone{{0.0f, 4.0f, 0.0f},
                                            {0.0f, 3.0f, 0.0f},
                                            {0.0f, 2.0f, 0.0f},
                                            {0.0f, 1.0f, 0.0f},
                                            {0.0f, 0.0f, 0.0f}};
    const math::vec3 target{1.5f, 1.5f, 0.0f};
    const auto pole = ik_pole::from_point({0.0f, 2.0f, 5.0f});

    ik_solver_params params;
    params.max_iterations = 40;

    for(size_t bones = 2; bones <= 4; ++bones)
    {
        const std::vector<math::vec3> positions(four_bone.end() - static_cast<long>(bones) - 1, four_bone.end());

        auto fabrik_chain = make_chain(positions);
        const auto fabrik_result = ik_set_position_fabrik(fabrik_chain.effector(), target, pole, bones, params);
        check(fabrik_result.applied, "fabrik applies on a " + std::to_string(bones) + "-bone chain");
        check(fabrik_result.reached,
              "fabrik with a pole still reaches on a " + std::to_string(bones) + "-bone chain");

        auto ccd_chain = make_chain(positions);
        const auto ccd_result = ik_set_position_ccd(ccd_chain.effector(), target, pole, bones, params);
        check(ccd_result.applied, "ccd applies on a " + std::to_string(bones) + "-bone chain");
        check(ccd_result.distance < 0.05f,
              "ccd with a pole still reaches on a " + std::to_string(bones) + "-bone chain");

        // Every intermediate joint must sit on the pole side of the chain axis.
        const math::vec3 root = fabrik_chain.position(0);
        const math::vec3 end = fabrik_chain.effector_position();
        for(size_t i = 1; i + 1 < fabrik_chain.joints.size(); ++i)
        {
            check(side_offset(fabrik_chain.position(i), root, end, {0.0f, 0.0f, 1.0f}) > 0.0f,
                  "fabrik joint " + std::to_string(i) + " bends toward the pole on a " + std::to_string(bones) +
                      "-bone chain");
        }
    }
}

void test_pole_survives_degenerate_configurations()
{
    std::printf("test_pole_survives_degenerate_configurations\n");
    // Pole exactly on the chain axis: there is no half-plane to rotate into, so
    // the solver must leave the bend alone rather than produce NaN.
    auto chain = make_chain({{0.0f, 3.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
    const auto result =
        ik_set_position_fabrik(chain.effector(), {0.0f, 1.0f, 0.0f}, ik_pole::from_point({0.0f, 5.0f, 0.0f}), 3);
    check(result.applied, "fabrik applies with a collinear pole");
    for(size_t i = 0; i < chain.joints.size(); ++i)
    {
        check_finite(chain.position(i), "collinear-pole joint " + std::to_string(i) + " is finite");
    }

    // A direction pole is measured from the root rather than being a point, so
    // it stays valid however far the goal is dragged.
    auto directed = make_leg();
    ik_set_position_two_bone(directed.effector(), {0.4f, 0.2f, 0.1f}, ik_pole::from_direction({0.0f, 0.0f, 1.0f}));
    check(directed.position(1).z > 0.05f, "a direction pole bends the knee toward +Z");

    // The world origin is a legitimate pole point, not a disable sentinel.
    auto at_origin = make_chain({{0.0f, 2.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
    ik_set_position_two_bone(at_origin.effector(), {0.3f, 0.3f, 1.0f}, ik_pole::from_point({0.0f, 0.0f, 0.0f}));
    check(at_origin.position(1).z < 1.0f, "a pole at the world origin still constrains the bend");
}

void test_fabrik_and_ccd_reach()
{
    std::printf("test_fabrik_and_ccd_reach\n");
    const math::vec3 target{0.35f, 0.25f, 0.2f};
    const auto pole = ik_pole::from_point({0.0f, 1.0f, 2.0f});

    auto fabrik_chain = make_leg();
    const auto fabrik_result = ik_set_position_fabrik(fabrik_chain.effector(), target, pole, 2);
    check(fabrik_result.applied && fabrik_result.reached, "fabrik reaches with default iterations");

    auto ccd_chain = make_leg();
    const auto ccd_result = ik_set_position_ccd(ccd_chain.effector(), target, pole, 2);
    check(ccd_result.applied, "ccd applies");
    // CCD retracts a chain slowly: this same target is still ~60mm off after 10
    // sweeps, which is why the default budget is 20. It settles around a
    // thousandth of chain length rather than exactly on target.
    check_near(ccd_result.distance, 0.0f, 0.01f, "ccd converges within the default iteration budget");

    // Coincident joints used to divide by zero in the FABRIK placement step.
    auto collapsed = make_chain({{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
    check(ik_set_position_fabrik(collapsed.effector(), {0.2f, 0.2f, 0.2f}, pole, 2).applied,
          "fabrik applies with a zero-length bone");
    check_finite(collapsed.effector_position(), "fabrik with coincident joints stays finite");

    // Unreachable: report it instead of claiming success.
    auto stretched = make_leg();
    const auto stretched_result = ik_set_position_ccd(stretched.effector(), {0.0f, 2.0f, 20.0f}, pole, 2);
    check(stretched_result.applied && !stretched_result.reached, "ccd reports an out-of-reach target");
    check_finite(stretched.effector_position(), "stretched ccd effector is finite");
}

void test_chain_collection_ignores_skin_bone_index()
{
    std::printf("test_chain_collection_ignores_skin_bone_index\n");
    // bone_index addresses the mesh's skin influence list, which remove_empty_bones
    // prunes - index 0 is whichever bone the skinning data listed first, NOT the
    // skeleton root. Terminating on it truncated chains on perfectly ordinary rigs.
    // Here index 0 sits in the middle; a 3-bone request must walk straight past it.
    auto chain = make_chain({{0.0f, 3.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
                            {5u, 0u, 3u, 7u});

    // Reachable from the real root (distance 2.5 <= reach 3), unreachable from
    // the joint below it (distance 2.69 > reach 2), so this only passes when the
    // whole chain was collected.
    const math::vec3 target{2.5f, 3.0f, 0.0f};
    ik_solver_params params;
    params.max_iterations = 40;
    const auto result = ik_set_position_fabrik(chain.effector(), target, {}, 3, params);

    check(result.applied, "fabrik applies across a bone with skin index 0");
    check(result.reached, "the chain walked past the bone whose skin index is 0");

    // The walk must still stop at the skeleton root: the armature container above
    // it carries no bone_component, and rotating it would move the whole model.
    const math::quat container_rotation = chain.container.get<transform_component>().get_rotation_local();
    auto deep = make_chain({{0.0f, 3.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
                           {5u, 0u, 3u, 7u});
    ik_set_position_fabrik(deep.effector(), {1.0f, 1.0f, 1.0f}, {}, 10, params);
    const math::quat after = deep.container.get<transform_component>().get_rotation_local();
    check_near(std::fabs(glm::dot(container_rotation, after)), 1.0f, 1e-4f,
               "the walk stops at the skeleton root and leaves the armature container alone");
}

void test_repeated_solves_are_stable()
{
    std::printf("test_repeated_solves_are_stable\n");
    // Interactive tools and per-frame script IK re-solve the same goal on top of
    // the previous result every frame. A solver that is not idempotent drifts a
    // little each time, which reads as the chain slowly spinning. Deep bends are
    // the interesting case: that is where the pole's component perpendicular to
    // the root->end axis collapses and its sign can flip.
    const auto pole = ik_pole::from_point({0.0f, 1.0f, 2.0f});
    const int repeats = 50;

    // Target close to the root, so the leg folds well past 90 degrees.
    const math::vec3 folded{0.3f, 1.6f, 0.2f};

    auto two_bone = make_leg();
    ik_set_position_two_bone(two_bone.effector(), folded, pole);
    const math::vec3 tb_effector = two_bone.effector_position();
    const math::vec3 tb_knee = two_bone.position(1);
    for(int i = 0; i < repeats; ++i)
    {
        ik_set_position_two_bone(two_bone.effector(), folded, pole);
    }
    check_near(math::length(two_bone.effector_position() - tb_effector), 0.0f, 1e-4f,
               "two-bone effector does not drift when re-solved");
    check_near(math::length(two_bone.position(1) - tb_knee), 0.0f, 1e-4f,
               "two-bone knee does not drift when re-solved");

    // Three bones, so the pole pass has two intermediate joints to rotate.
    const std::vector<math::vec3> positions{{0.0f, 3.0f, 0.0f},
                                            {0.0f, 2.0f, 0.0f},
                                            {0.0f, 1.0f, 0.0f},
                                            {0.0f, 0.0f, 0.0f}};
    const math::vec3 deep{0.5f, 2.4f, 0.3f};

    auto fabrik_chain = make_chain(positions);
    ik_set_position_fabrik(fabrik_chain.effector(), deep, pole, 3);
    const math::vec3 fabrik_settled = fabrik_chain.effector_position();
    const math::vec3 fabrik_mid = fabrik_chain.position(1);
    for(int i = 0; i < repeats; ++i)
    {
        ik_set_position_fabrik(fabrik_chain.effector(), deep, pole, 3);
    }
    check_near(math::length(fabrik_chain.effector_position() - fabrik_settled), 0.0f, 1e-3f,
               "fabrik effector does not drift when re-solved");
    check_near(math::length(fabrik_chain.position(1) - fabrik_mid), 0.0f, 1e-3f,
               "fabrik chain does not rotate about its axis when re-solved");

    auto ccd_chain = make_chain(positions);
    ik_set_position_ccd(ccd_chain.effector(), deep, pole, 3);
    const math::vec3 ccd_settled = ccd_chain.effector_position();
    const math::vec3 ccd_mid = ccd_chain.position(1);
    for(int i = 0; i < repeats; ++i)
    {
        ik_set_position_ccd(ccd_chain.effector(), deep, pole, 3);
    }
    check_near(math::length(ccd_chain.effector_position() - ccd_settled), 0.0f, 1e-3f,
               "ccd effector does not drift when re-solved");
    check_near(math::length(ccd_chain.position(1) - ccd_mid), 0.0f, 1e-3f,
               "ccd chain does not rotate about its axis when re-solved");
}

void test_effector_rotation_goal()
{
    std::printf("test_effector_rotation_goal\n");
    // Position solvers never orient the effector - that is why a solved foot used
    // to swing with the leg on slopes. ik_set_rotation is the separate goal.
    auto chain = make_leg();
    ik_set_position_two_bone(chain.effector(), {0.4f, 0.2f, 0.1f}, ik_pole::from_point({0.0f, 1.0f, 2.0f}));

    const math::quat desired = math::angleAxis(math::radians(90.0f), math::vec3(0.0f, 1.0f, 0.0f));
    const auto result = ik_set_rotation(chain.effector(), desired, 1.0f);
    check(result.applied, "set-rotation applies");

    const math::vec3 forward = chain.transform(2).get_z_axis_global();
    check_near(forward.x, 1.0f, 1e-3f, "the effector adopted the requested orientation");

    // Half weight lands halfway along the arc.
    auto half = make_leg();
    const math::quat rest = half.transform(2).get_rotation_global();
    ik_set_rotation(half.effector(), desired, 0.5f);
    const math::quat blended = half.transform(2).get_rotation_global();
    const math::quat expected = math::slerp(rest, desired, 0.5f);
    check_near(std::fabs(glm::dot(blended, expected)), 1.0f, 1e-3f, "set-rotation blends by weight");

    check(!ik_set_rotation(half.effector(), desired, 0.0f).applied, "set-rotation at weight 0 does not apply");
}

void test_aim_respects_bone_axes()
{
    std::printf("test_aim_respects_bone_axes\n");
    // Most imported rigs run their bones along local +Y, so a solver that assumes
    // +Z twists the spine. The axis is a parameter.
    auto registry = make_registry();
    auto bone = spawn(*registry, {0.0f, 0.0f, 0.0f});

    ik_aim_params params;
    params.forward_axis = math::vec3(0.0f, 1.0f, 0.0f);
    const auto result = ik_aim_at_position(bone, {5.0f, 0.0f, 0.0f}, params);
    check(result.applied && result.reached, "aim applies along a +Y bone axis");

    const math::vec3 achieved = math::normalize(bone.get<transform_component>().get_y_axis_global());
    check_near(achieved.x, 1.0f, 1e-3f, "the bone's +Y axis points at the target");

    // The default wrapper keeps the engine's +Z entity convention.
    auto entity = spawn(*registry, {0.0f, 0.0f, 0.0f});
    check(ik_look_at_position(entity, {5.0f, 0.0f, 0.0f}).applied, "look-at applies");
    const math::vec3 along_z = math::normalize(entity.get<transform_component>().get_z_axis_global());
    check_near(along_z.x, 1.0f, 1e-3f, "look-at aims +Z at the target");
    check_near(math::length(along_z), 1.0f, 1e-4f, "look-at result is a unit axis");

    // Derive the axis from the rig instead of hardcoding it.
    auto rig = make_chain({{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}});
    const math::vec3 axis = ik_resolve_bone_axis_local(rig.joints[0]);
    check_near(axis.y, 1.0f, 1e-4f, "the bone axis is derived from the child offset");
    check_near(math::length(ik_resolve_bone_axis_local(rig.joints[1])), 1.0f, 1e-4f,
               "a leaf bone falls back to a unit axis");
}

void test_resolve_facing_axis()
{
    std::printf("test_resolve_facing_axis\n");
    // A rig authored with the chest's local -Z out of the front is structurally
    // identical to one with +Z out of the front, and importers do not normalise
    // it - so which axis "faces forward" cannot be read off the skeleton. Asking
    // which local axis currently points along the character's forward can.
    auto registry = make_registry();
    const math::vec3 character_forward{0.0f, 0.0f, 1.0f};

    auto aligned = spawn(*registry, {0.0f, 0.0f, 0.0f});
    const math::vec3 aligned_axis = ik_resolve_facing_axis_local(aligned, character_forward);
    check_near(aligned_axis.z, 1.0f, 1e-4f, "an unrotated bone faces along its own +Z");

    // Bone authored 180 degrees about Y: its local +Z points out of the back, so
    // the facing axis is -Z. Aiming +Z here is what spins a torso around.
    auto flipped = spawn(*registry, {0.0f, 0.0f, 0.0f});
    flipped.get<transform_component>().set_rotation_local(
        math::angleAxis(math::radians(180.0f), math::vec3(0.0f, 1.0f, 0.0f)));
    const math::vec3 flipped_axis = ik_resolve_facing_axis_local(flipped, character_forward);
    check_near(flipped_axis.z, -1.0f, 1e-4f, "a back-to-front bone resolves its facing axis to -Z");

    // Bone whose local +Y runs along the character's forward (the case that folds
    // a spine when GetBoneAxis is used as the aim axis).
    auto pitched = spawn(*registry, {0.0f, 0.0f, 0.0f});
    pitched.get<transform_component>().set_rotation_local(
        math::angleAxis(math::radians(-90.0f), math::vec3(1.0f, 0.0f, 0.0f)));
    const math::vec3 pitched_axis = ik_resolve_facing_axis_local(pitched, character_forward);
    check_near(std::fabs(pitched_axis.y), 1.0f, 1e-4f, "a pitched bone resolves its facing axis to +/-Y");

    // Snapping keeps a slightly bent bone on the same clean axis.
    auto bent = spawn(*registry, {0.0f, 0.0f, 0.0f});
    bent.get<transform_component>().set_rotation_local(
        math::angleAxis(math::radians(12.0f), math::vec3(1.0f, 0.0f, 0.0f)));
    const math::vec3 bent_axis = ik_resolve_facing_axis_local(bent, character_forward);
    check_near(bent_axis.z, 1.0f, 1e-4f, "a slightly bent bone still snaps to +Z");
    check_near(math::length(bent_axis), 1.0f, 1e-4f, "the snapped axis is a unit vector");

    // Unsnapped keeps the exact direction.
    const math::vec3 exact = ik_resolve_facing_axis_local(bent, character_forward, false);
    check_near(math::length(exact), 1.0f, 1e-4f, "the unsnapped axis is a unit vector");
    check(std::fabs(exact.y) > 0.1f, "the unsnapped axis keeps the bone's actual tilt");

    // Degenerate input falls back rather than producing NaN.
    const math::vec3 degenerate = ik_resolve_facing_axis_local(bent, math::vec3(0.0f, 0.0f, 0.0f));
    check_finite(degenerate, "a zero-length direction falls back to a finite axis");
}

void test_aim_cone_limit()
{
    std::printf("test_aim_cone_limit\n");
    auto registry = make_registry();
    auto bone = spawn(*registry, {0.0f, 0.0f, 0.0f});

    // Target 90 degrees off the bone's forward, limited to 30.
    ik_aim_params params;
    params.max_angle_radians = math::radians(30.0f);
    const auto result = ik_aim_at_position(bone, {5.0f, 0.0f, 0.0f}, params);

    check(result.applied, "a cone-limited aim still applies");
    check(!result.reached, "a cone-limited aim reports it did not reach the target");
    const math::vec3 achieved = math::normalize(bone.get<transform_component>().get_z_axis_global());
    check_near(std::acos(glm::clamp(achieved.z, -1.0f, 1.0f)), math::radians(30.0f), 1e-3f,
               "the swing stops exactly at the cone limit");
    check_near(result.distance, math::radians(60.0f), 1e-3f, "the residual angle is reported in radians");
}

void test_aim_up_reference()
{
    std::printf("test_aim_up_reference\n");
    auto registry = make_registry();

    // With no up reference the bone keeps its twist: aiming along its existing
    // forward must be a no-op rather than snapping to some synthesised basis.
    auto twisted = spawn(*registry, {0.0f, 0.0f, 0.0f});
    const math::quat roll = math::angleAxis(math::radians(40.0f), math::vec3(0.0f, 0.0f, 1.0f));
    twisted.get<transform_component>().set_rotation_local(roll);
    ik_aim_at_position(twisted, {0.0f, 0.0f, 5.0f}, {});
    check_near(std::fabs(glm::dot(twisted.get<transform_component>().get_rotation_global(), roll)),
               1.0f,
               1e-3f,
               "aiming along the current forward with no up reference preserves twist");

    // With an up reference the roll is resolved against it.
    auto rolled = spawn(*registry, {0.0f, 0.0f, 0.0f});
    rolled.get<transform_component>().set_rotation_local(roll);
    ik_aim_params params;
    params.world_up = math::vec3(0.0f, 1.0f, 0.0f);
    ik_aim_at_position(rolled, {0.0f, 0.0f, 5.0f}, params);
    const math::vec3 up = math::normalize(rolled.get<transform_component>().get_y_axis_global());
    check_near(up.y, 1.0f, 1e-3f, "an up reference rolls the bone upright");
}


// -----------------------------------------------------------------------------
// Rig modifier components (rig_system)
// -----------------------------------------------------------------------------

constexpr float RIG_FRAME = 1.0f / 60.0f;

auto capture_locals(const test_chain& chain) -> std::vector<math::transform>
{
    std::vector<math::transform> locals;
    for(size_t i = 0; i < chain.joints.size(); ++i)
    {
        locals.push_back(chain.transform(i).get_transform_local());
    }
    return locals;
}

auto locals_match(const test_chain& chain, const std::vector<math::transform>& locals) -> bool
{
    for(size_t i = 0; i < chain.joints.size(); ++i)
    {
        const auto& now = chain.transform(i).get_transform_local();
        if(math::length(now.get_position() - locals[i].get_position()) > 1e-6f ||
           std::fabs(std::fabs(math::dot(now.get_rotation(), locals[i].get_rotation())) - 1.0f) > 1e-6f)
        {
            return false;
        }
    }
    return true;
}

void test_rig_two_bone_reaches_and_restores()
{
    std::printf("test_rig_two_bone_reaches_and_restores\n");
    auto chain = make_leg();
    auto target = spawn(*chain.registry, {0.4f, 0.2f, 0.1f});
    auto pole = spawn(*chain.registry, {0.0f, 1.0f, 2.0f});
    auto& component = chain.registry->emplace<two_bone_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.pole = pole;
    const auto before = capture_locals(chain);

    rig_system rig;
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    check(component.debug.solved, "the two-bone component solved");
    check_near(math::length(chain.effector_position() - target.get<transform_component>().get_position_global()),
               0.0f,
               2e-3f,
               "the foot reaches the target entity");
    check(component.debug.joints.size() == 3, "the gizmo gets the three joints");

    rig.after_skinning(*chain.registry);
    check(locals_match(chain, before), "after skinning the animated pose is back, untouched");

    // A weight curve no playing clip provides keeps the modifier off.
    component.weight_curve = "HandReach";
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    check(!component.debug.solved, "a missing weight curve gates the solve off");
    check(locals_match(chain, before), "nothing moved without the curve");
    rig.after_skinning(*chain.registry);
}

void test_rig_chain_ik_reaches()
{
    std::printf("test_rig_chain_ik_reaches\n");
    auto chain = make_chain({{0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.5f, 0.0f}, {0.0f, 2.0f, 0.0f}});
    auto target = spawn(*chain.registry, {0.9f, 1.2f, 0.4f});
    auto& component = chain.registry->emplace<chain_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.chain_length = 4;
    const auto before = capture_locals(chain);

    rig_system rig;
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    check_near(math::length(chain.effector_position() - math::vec3(0.9f, 1.2f, 0.4f)), 0.0f, 5e-3f, "the chain tip reaches the target");
    check(component.debug.joints.size() == 5, "the gizmo gets every joint");
    rig.after_skinning(*chain.registry);
    check(locals_match(chain, before), "chain pose restored after skinning");
}

void test_rig_chain_ik_eases_to_a_swapped_target()
{
    std::printf("test_rig_chain_ik_eases_to_a_swapped_target\n");
    // A tentacle that switches from wandering to reaching for someone swaps its target entity: with
    // smoothing the goal starts at the tip and eases toward each new target instead of snapping.
    constexpr float smoothing = 0.2f;
    constexpr float frame = 0.1f;
    const float alpha = 1.0f - std::exp(-frame / smoothing);
    auto chain = make_chain({{0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.5f, 0.0f}});
    const math::vec3 wander_point{0.6f, 1.2f, 0.0f};
    const math::vec3 reach_point{-0.6f, 1.2f, 0.3f};
    auto wander = spawn(*chain.registry, wander_point);
    auto reach = spawn(*chain.registry, reach_point);
    auto& component = chain.registry->emplace<chain_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = wander;
    component.chain_length = 3;
    component.smoothing = smoothing;
    const math::vec3 tip = chain.effector_position();

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(frame));
        rig.after_skinning(*chain.registry);
    };
    step();
    check(math::length(component.goal.position - (tip + (wander_point - tip) * alpha)) < 1e-4f,
          "a starting solve eases from the animated tip");
    for(int i = 0; i < 40; ++i)
    {
        step();
    }
    check(math::length(component.goal.position - wander_point) < 1e-3f, "the goal settles on its target");

    component.target = reach;
    step();
    check(math::length(component.goal.position - (wander_point + (reach_point - wander_point) * alpha)) < 1e-3f,
          "a swapped target is approached, not jumped to");

    component.smoothing = 0.0f;
    step();
    check(math::length(component.goal.position - reach_point) < 1e-5f, "without smoothing the goal is the target");
}

void test_rig_look_at_turns_toward_target()
{
    std::printf("test_rig_look_at_turns_toward_target\n");
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.0f, 1.4f, 0.0f}, {0.0f, 1.6f, 0.0f}});
    auto target = spawn(*chain.registry, {2.0f, 1.6f, 2.0f});
    auto& component = chain.registry->emplace<look_at_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.chain_length = 3;
    component.smoothing = 0.0f;
    component.max_bone_angle = 90.0f;
    component.forward_axis = {0.0f, 0.0f, 1.0f};

    rig_system rig;
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    const math::vec3 facing = chain.transform(2).get_rotation_global() * math::vec3(0.0f, 0.0f, 1.0f);
    const math::vec3 wanted = math::normalize(math::vec3(2.0f, 1.6f, 2.0f) - chain.position(2));
    check(math::dot(math::normalize(facing), wanted) > 0.999f, "the head faces the target");
    const math::vec3 base_facing = chain.transform(0).get_rotation_global() * math::vec3(0.0f, 0.0f, 1.0f);
    const float base_angle = math::degrees(std::acos(math::clamp(math::dot(math::normalize(base_facing), math::vec3(0, 0, 1)), -1.0f, 1.0f)));
    check(base_angle > 5.0f && base_angle < 40.0f, "the lowest bone takes only its share of the turn");
    rig.after_skinning(*chain.registry);

    // A target behind the character fades the look out instead of twisting the neck round.
    target.get<transform_component>().set_position_global({0.0f, 1.6f, -2.0f});
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    check(component.smoothed_weight < 1e-3f, "a target behind the character switches the look off");
    rig.after_skinning(*chain.registry);
}

void test_rig_spring_bone_falls_and_holds_lengths()
{
    std::printf("test_rig_spring_bone_falls_and_holds_lengths\n");
    const auto run = [](float stiffness) -> std::pair<float, float>
    {
        auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.2f, 1.0f, 0.0f}, {0.4f, 1.0f, 0.0f}, {0.6f, 1.0f, 0.0f}});
        auto& component = chain.registry->emplace<spring_bone_component>(chain.joints.front().entity());
        component.set_owner(chain.joints.front());
        component.stiffness = stiffness;
        component.damping = 0.2f;
        component.gravity = {0.0f, -9.81f, 0.0f};
        rig_system rig;
        for(int frame = 0; frame < 120; ++frame)
        {
            rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
            rig.after_skinning(*chain.registry);
        }
        const auto& joints = component.debug.joints;
        float worst_length_error = 0.0f;
        for(size_t i = 0; i + 1 < joints.size(); ++i)
        {
            worst_length_error = std::max(worst_length_error, std::fabs(math::length(joints[i + 1] - joints[i]) - 0.2f));
        }
        check_finite(joints.back(), "the spring tip stays finite");
        return {joints.back().y, worst_length_error};
    };
    const auto [limp_tip, limp_error] = run(0.0f);
    const auto [stiff_tip, stiff_error] = run(60.0f);
    check(limp_tip < 0.5f, "a chain without stiffness hangs down under gravity");
    check(stiff_tip > limp_tip + 0.2f, "stiffness holds the chain toward its animated pose");
    check(limp_error < 1e-3f && stiff_error < 1e-3f, "bone lengths survive the simulation");
}

void test_rig_parents_solve_before_children()
{
    std::printf("test_rig_parents_solve_before_children\n");
    // A look-at that turns the chest moves the shoulder: the hand IK on that arm has to solve after
    // it, whatever order the components were added in.
    auto registry = make_registry();
    const auto attach = [&](const math::vec3& position, entt::handle parent)
    {
        auto joint = spawn(*registry, position);
        if(parent)
        {
            joint.get<transform_component>().set_parent(parent);
        }
        return joint;
    };
    auto hips = attach({0.0f, 1.0f, 0.0f}, {});
    auto chest = attach({0.0f, 1.4f, 0.0f}, hips);
    auto neck = attach({0.0f, 1.6f, 0.0f}, chest);
    auto head = attach({0.0f, 1.7f, 0.0f}, neck);
    auto upper_arm = attach({0.2f, 1.4f, 0.0f}, chest);
    auto forearm = attach({0.5f, 1.4f, 0.05f}, upper_arm);
    auto hand = attach({0.8f, 1.4f, 0.0f}, forearm);
    const math::vec3 grip{0.5f, 1.3f, 0.2f};
    auto hand_target = spawn(*registry, grip);
    auto look_target = spawn(*registry, {2.0f, 1.6f, 2.0f});

    auto& reach = registry->emplace<two_bone_ik_component>(hand.entity());
    reach.set_owner(hand);
    reach.target = hand_target;
    auto& look = registry->emplace<look_at_ik_component>(head.entity());
    look.set_owner(head);
    look.target = look_target;
    look.chain_length = 3;
    look.smoothing = 0.0f;
    look.max_bone_angle = 90.0f;
    look.forward_axis = {0.0f, 0.0f, 1.0f};
    const math::quat chest_before = chest.get<transform_component>().get_rotation_global();

    rig_system rig;
    rig.before_skinning(*registry, delta_t(RIG_FRAME));
    const float chest_turn = std::fabs(math::dot(chest.get<transform_component>().get_rotation_global(), chest_before));
    check(chest_turn < 0.9999f, "the look-at turned the chest");
    check_near(math::length(hand.get<transform_component>().get_position_global() - grip),
               0.0f,
               2e-3f,
               "the hand still reaches its target after the chest turned");
    rig.after_skinning(*registry);
}

void test_rig_lost_target_fades_out()
{
    std::printf("test_rig_lost_target_fades_out\n");
    auto chain = make_leg();
    auto target = spawn(*chain.registry, {0.4f, 0.2f, 0.1f});
    auto& component = chain.registry->emplace<two_bone_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.smoothing = 0.2f;
    const math::vec3 animated = chain.effector_position();

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
        const math::vec3 solved = chain.effector_position();
        rig.after_skinning(*chain.registry);
        return solved;
    };
    for(int i = 0; i < 120; ++i)
    {
        step();
    }
    check(component.goal.weight > 0.99f, "a smoothed weight eases in to the component weight");

    component.target = {};
    const math::vec3 released = step();
    check(component.debug.solved, "a lost target does not drop the solve at once");
    check(component.debug.effective_weight > 0.5f && component.debug.effective_weight < 1.0f,
          "the weight fades out from where it was");
    check(math::length(released - animated) > 0.1f, "the limb does not snap back to the animation");
    for(int i = 0; i < 300; ++i)
    {
        step();
    }
    check(!component.debug.solved && !component.goal.valid, "once faded out the modifier stops and resets");
}

void test_rig_switched_off_rests_and_resumes()
{
    std::printf("test_rig_switched_off_rests_and_resumes\n");
    auto chain = make_leg();
    auto target = spawn(*chain.registry, {0.4f, 0.2f, 0.1f});
    auto& component = chain.registry->emplace<two_bone_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.smoothing = 0.2f;
    const math::vec3 animated = chain.effector_position();

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
        const math::vec3 solved = chain.effector_position();
        rig.after_skinning(*chain.registry);
        return solved;
    };
    for(int i = 0; i < 120; ++i)
    {
        step();
    }
    component.weight = 0.0f;
    step();
    check(component.debug.solved && component.debug.effective_weight > 0.0f, "a switched-off rig fades out, not snaps");
    for(int i = 0; i < 300; ++i)
    {
        step();
    }
    const math::vec3 rested = step();
    check(!component.debug.solved && !component.goal.valid, "once faded out a switched-off rig no longer runs");
    check_near(math::length(rested - animated), 0.0f, 1e-5f, "a resting rig leaves the animated pose alone");

    component.weight = 1.0f;
    step();
    check(component.debug.solved && component.goal.weight > 0.0f, "switched back on, the rig runs and fades in");
}

/// A leg like make_leg()'s under its own container in `registry`, offset along X; the foot last.
auto add_leg(entt::registry& registry, float x, entt::handle parent = {}) -> std::vector<entt::handle>
{
    auto container = spawn(registry, {x, 0.0f, 0.0f});
    if(parent)
    {
        container.get<transform_component>().set_parent(parent);
    }
    std::vector<entt::handle> joints;
    for(const auto& point : {math::vec3(x, 2.0f, 0.0f), math::vec3(x, 1.0f, 0.15f), math::vec3(x, 0.0f, 0.0f)})
    {
        auto joint = spawn(registry, point);
        joint.get<transform_component>().set_parent(joints.empty() ? container : joints.back());
        joints.push_back(joint);
    }
    return joints;
}

auto add_two_bone(entt::registry& registry, entt::handle effector, entt::handle target) -> two_bone_ik_component&
{
    auto& component = registry.emplace<two_bone_ik_component>(effector.entity());
    component.set_owner(effector);
    component.target = target;
    component.smoothing = 0.0f;
    return component;
}

auto get_position(entt::handle entity) -> math::vec3
{
    return entity.get<transform_component>().get_position_global();
}

/// More characters than the rig system runs on one thread (32), so the modifiers run on the pool.
constexpr int PARALLEL_CHARACTERS = 40;
constexpr float CHARACTER_SPACING = 3.0f;

void test_rig_characters_solve_in_parallel_as_one_alone()
{
    std::printf("test_rig_characters_solve_in_parallel_as_one_alone\n");
    const math::vec3 target_offset(0.4f, 0.2f, 0.1f);
    rig_system rig;
    auto alone = make_leg();
    add_two_bone(*alone.registry, alone.effector(), spawn(*alone.registry, target_offset));
    rig.before_skinning(*alone.registry, delta_t(RIG_FRAME));
    const math::vec3 expected_knee = alone.position(1);
    const math::vec3 expected_foot = alone.effector_position();
    rig.after_skinning(*alone.registry);

    auto registry = make_registry();
    std::vector<std::vector<entt::handle>> legs;
    for(int i = 0; i < PARALLEL_CHARACTERS; ++i)
    {
        const float x = CHARACTER_SPACING * float(i + 1);
        legs.push_back(add_leg(*registry, x));
        add_two_bone(*registry, legs.back().back(), spawn(*registry, target_offset + math::vec3(x, 0.0f, 0.0f)));
    }
    rig.before_skinning(*registry, delta_t(RIG_FRAME));
    bool is_every_leg_solved = true;
    for(int i = 0; i < PARALLEL_CHARACTERS; ++i)
    {
        const math::vec3 offset(CHARACTER_SPACING * float(i + 1), 0.0f, 0.0f);
        is_every_leg_solved = is_every_leg_solved && math::length(get_position(legs[i][1]) - offset - expected_knee) < 1e-4f &&
                              math::length(get_position(legs[i][2]) - offset - expected_foot) < 1e-4f;
    }
    check(is_every_leg_solved, "every character reaches the pose one character alone reaches");
    rig.after_skinning(*registry);
    bool is_every_leg_restored = true;
    for(int i = 0; i < PARALLEL_CHARACTERS; ++i)
    {
        const float x = CHARACTER_SPACING * float(i + 1);
        is_every_leg_restored = is_every_leg_restored && math::length(get_position(legs[i][2]) - math::vec3(x, 0.0f, 0.0f)) < 1e-5f;
    }
    check(is_every_leg_restored, "every character gets its animated pose back after skinning");
}

void test_rig_aiming_at_another_character_sees_its_solved_pose()
{
    std::printf("test_rig_aiming_at_another_character_sees_its_solved_pose\n");
    auto registry = make_registry();
    for(int i = 0; i < PARALLEL_CHARACTERS; ++i)
    {
        const float x = CHARACTER_SPACING * float(i + 2);
        const auto leg = add_leg(*registry, x);
        add_two_bone(*registry, leg.back(), spawn(*registry, {x + 0.4f, 0.2f, 0.1f}));
    }
    // The watcher is made first: in one shallowest-first list it would run before the leg it reaches for.
    const auto watcher = add_leg(*registry, 1.0f);
    const auto leader = add_leg(*registry, 0.0f);
    add_two_bone(*registry, leader.back(), spawn(*registry, {0.6f, 0.5f, 0.3f}));
    add_two_bone(*registry, watcher.back(), leader.back());

    rig_system rig;
    rig.before_skinning(*registry, delta_t(RIG_FRAME));
    const math::vec3 leader_foot = get_position(leader.back());
    check(math::length(leader_foot) > 0.5f, "the leader's foot left its animated pose");
    check_near(math::length(get_position(watcher.back()) - leader_foot),
               0.0f,
               1e-3f,
               "a rig reaching for another character's bone reaches where that bone was solved to");
    rig.after_skinning(*registry);
}

void test_rig_writing_above_its_character_still_solves()
{
    std::printf("test_rig_writing_above_its_character_still_solves\n");
    auto registry = make_registry();
    for(int i = 0; i < PARALLEL_CHARACTERS; ++i)
    {
        const float x = CHARACTER_SPACING * float(i + 2);
        const auto leg = add_leg(*registry, x);
        add_two_bone(*registry, leg.back(), spawn(*registry, {x + 0.4f, 0.2f, 0.1f}));
    }
    // A character under a shared parent whose chain reaches past the character root into that
    // parent: no thread may write the parent while others pose below it.
    auto world = spawn(*registry, {0.0f, -1.0f, 0.0f});
    const auto leg = add_leg(*registry, 0.0f, world);
    const auto character = leg.front().get<transform_component>().get_parent();
    registry->emplace<animation_component>(character.entity());
    auto& chain = registry->emplace<chain_ik_component>(leg.back().entity());
    chain.set_owner(leg.back());
    chain.target = spawn(*registry, {0.5f, 0.4f, 0.2f});
    chain.chain_length = 4;

    rig_system rig;
    rig.before_skinning(*registry, delta_t(RIG_FRAME));
    check(chain.debug.solved, "the chain solves");
    check_near(math::length(get_position(leg.back()) - math::vec3(0.5f, 0.4f, 0.2f)), 0.0f, 2e-3f, "the chain reaches its target");
    const float turned = std::fabs(math::dot(world.get<transform_component>().get_rotation_local(), math::identity<math::quat>()));
    check(turned < 1.0f - 1e-6f, "the chain turns the shared parent it reaches into");
    rig.after_skinning(*registry);
    const float restored = std::fabs(math::dot(world.get<transform_component>().get_rotation_local(), math::identity<math::quat>()));
    check_near(restored, 1.0f, 1e-6f, "the shared parent gets its pose back");
}

void test_rig_look_at_follows_a_new_forward_axis()
{
    std::printf("test_rig_look_at_follows_a_new_forward_axis\n");
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.0f, 1.4f, 0.0f}, {0.0f, 1.6f, 0.0f}});
    auto target = spawn(*chain.registry, {2.0f, 1.6f, 2.0f});
    auto& component = chain.registry->emplace<look_at_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.smoothing = 0.0f;
    component.forward_axis = {0.0f, 0.0f, 1.0f};

    rig_system rig;
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    rig.after_skinning(*chain.registry);
    check(!component.resolved_axes.empty() && component.resolved_axes.front() == math::vec3(0.0f, 0.0f, 1.0f),
          "the bones look along the forward axis");

    component.forward_axis = {1.0f, 0.0f, 0.0f};
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    rig.after_skinning(*chain.registry);
    check(!component.resolved_axes.empty() && component.resolved_axes.front() == math::vec3(1.0f, 0.0f, 0.0f),
          "a changed forward axis is picked up while running");
}

void test_rig_spring_moves_every_frame_above_the_step_rate()
{
    std::printf("test_rig_spring_moves_every_frame_above_the_step_rate\n");
    // Faster frames than the fixed step show the joints part way between steps: a falling chain
    // moves on every frame instead of holding a step for several frames and jumping.
    constexpr float fast_frame = 1.0f / 240.0f;
    constexpr int warm_up_frames = 8;
    constexpr int measured_frames = 24;
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.2f, 1.0f, 0.0f}, {0.4f, 1.0f, 0.0f}});
    auto& component = chain.registry->emplace<spring_bone_component>(chain.joints.front().entity());
    component.set_owner(chain.joints.front());
    component.stiffness = 0.0f;
    component.gravity = {0.0f, -9.81f, 0.0f};

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(fast_frame));
        rig.after_skinning(*chain.registry);
        return component.debug.joints.back().y;
    };
    float previous = 0.0f;
    for(int i = 0; i < warm_up_frames; ++i)
    {
        previous = step();
    }
    int held_frames = 0;
    for(int i = 0; i < measured_frames; ++i)
    {
        const float tip = step();
        held_frames += std::fabs(tip - previous) < 1e-7f ? 1 : 0;
        previous = tip;
    }
    check(held_frames == 0, "the spring tip moves on every frame");
}

void test_rig_recovers_from_a_target_that_is_not_a_number()
{
    std::printf("test_rig_recovers_from_a_target_that_is_not_a_number\n");
    auto chain = make_chain({{0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.5f, 0.0f}});
    const math::vec3 reach_point{0.6f, 1.0f, 0.2f};
    auto target = spawn(*chain.registry, reach_point);
    auto& component = chain.registry->emplace<chain_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.chain_length = 3;
    component.smoothing = 0.05f;

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
        const math::vec3 solved = chain.effector_position();
        rig.after_skinning(*chain.registry);
        return solved;
    };
    for(int i = 0; i < 30; ++i)
    {
        step();
    }
    const float not_a_number = std::numeric_limits<float>::quiet_NaN();
    target.get<transform_component>().set_position_global({not_a_number, 1.0f, 0.0f});
    check_finite(step(), "a target that is not a number leaves the chain finite");
    check_finite(component.goal.position, "the goal never takes the bad position");

    const math::vec3 next_point{-0.5f, 1.1f, 0.3f};
    target.get<transform_component>().set_position_global(next_point);
    math::vec3 solved{};
    for(int i = 0; i < 120; ++i)
    {
        solved = step();
    }
    check_near(math::length(solved - next_point), 0.0f, 5e-3f, "the chain reaches again once the target is valid");
}

void test_rig_spring_restarts_after_a_teleport()
{
    std::printf("test_rig_spring_restarts_after_a_teleport\n");
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.2f, 1.0f, 0.0f}, {0.4f, 1.0f, 0.0f}});
    auto& component = chain.registry->emplace<spring_bone_component>(chain.joints.front().entity());
    component.set_owner(chain.joints.front());
    component.gravity = {0.0f, 0.0f, 0.0f};

    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
        rig.after_skinning(*chain.registry);
    };
    for(int i = 0; i < 10; ++i)
    {
        step();
    }
    const math::vec3 jump{10.0f, 0.0f, 0.0f};
    chain.container.get<transform_component>().set_position_global(jump);
    step();
    const auto& joints = component.debug.joints;
    check(joints.size() == 3, "the spring reports every joint");
    check(joints.back().x > joints.front().x + 0.3f,
          "after a teleport the chain starts from its animated pose instead of trailing back");
}

} // namespace

// ---------------------------------------------------------------------------------

void test_two_bone_turning_the_bend_plane_keeps_the_knee_on_its_hinge()
{
    std::printf("test_two_bone_turning_the_bend_plane_keeps_the_knee_on_its_hinge\n");
    // The target stays where the foot is and the pole turns the bend plane 30 degrees about the leg:
    // the limb turns as one piece, so the knee's own rotation does not change at all.
    auto chain = make_leg();
    const math::quat knee_before = chain.transform(1).get_rotation_local();
    const math::vec3 pole_direction = math::angleAxis(math::radians(30.0f), math::vec3(0.0f, 1.0f, 0.0f)) * math::vec3(0.0f, 0.0f, 1.0f);
    const auto result = ik_set_position_two_bone(chain.effector(), chain.effector_position(), ik_pole::from_direction(pole_direction));
    check(result.applied, "the solve runs");
    const math::quat knee_after = chain.transform(1).get_rotation_local();
    const float knee_change = math::degrees(2.0f * std::acos(math::clamp(std::fabs(math::dot(knee_before, knee_after)), 0.0f, 1.0f)));
    check(knee_change < 0.05f, "the knee keeps its own rotation (" + std::to_string(knee_change) + " degrees)");
    const math::vec3 knee = chain.position(1);
    check_near(math::dot(math::normalize(math::vec3(knee.x, 0.0f, knee.z)), pole_direction), 1.0f, 1e-3f,
               "and bends toward the turned pole");
}

void test_rig_goal_follows_a_moving_target_at_once()
{
    std::printf("test_rig_goal_follows_a_moving_target_at_once\n");
    // Smoothing eases where a goal starts and a target swap, never a target that moves: a hand on a
    // wheel in a moving car does not trail behind it.
    auto chain = make_leg();
    auto target = spawn(*chain.registry, {0.4f, 0.2f, 0.1f});
    auto& component = chain.registry->emplace<two_bone_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.smoothing = 0.1f;
    rig_system rig;
    const auto step = [&]()
    {
        rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
        rig.after_skinning(*chain.registry);
    };
    for(int i = 0; i < 120; ++i)
    {
        step();
    }
    for(int i = 0; i < 5; ++i)
    {
        auto& target_transform = target.get<transform_component>();
        target_transform.set_position_global(target_transform.get_position_global() + math::vec3(0.0f, 0.0f, 0.05f));
        step();
        check_near(math::length(component.goal.position - target_transform.get_position_global()), 0.0f, 1e-4f,
                   "the goal is on the moving target");
    }
    auto other = spawn(*chain.registry, {-0.4f, 0.2f, 0.1f});
    const math::vec3 before_swap = component.goal.position;
    component.target = other;
    step();
    check(math::length(component.goal.position - before_swap) < math::length(get_position(other) - before_swap) * 0.5f,
          "a new target is eased over to");
}

void test_rig_look_at_bone_limit_zero_holds_the_bones()
{
    std::printf("test_rig_look_at_bone_limit_zero_holds_the_bones\n");
    auto chain = make_chain({{0.0f, 1.0f, 0.0f}, {0.0f, 1.5f, 0.0f}, {0.0f, 1.7f, 0.0f}});
    auto target = spawn(*chain.registry, {2.0f, 1.7f, 0.0f});
    auto& component = chain.registry->emplace<look_at_ik_component>(chain.effector().entity());
    component.set_owner(chain.effector());
    component.target = target;
    component.smoothing = 0.0f;
    component.max_bone_angle = 0.0f;
    const math::quat before = chain.transform(2).get_rotation_global();
    rig_system rig;
    rig.before_skinning(*chain.registry, delta_t(RIG_FRAME));
    const math::quat solved = chain.transform(2).get_rotation_global();
    rig.after_skinning(*chain.registry);
    check_near(std::fabs(math::dot(before, solved)), 1.0f, 1e-6f, "a bone limit of 0 turns nothing");
}

auto run_ik_suite(rtti::context& /*ctx*/) -> int
{
    test_rejects_invalid_input();
    test_two_bone_reaches_and_honors_pole();
    test_two_bone_soften_costs_reach();
    test_two_bone_unreachable_stays_finite();
    test_two_bone_requires_three_joints();
    test_weight_bounds();
    test_pole_preserves_reach_on_long_chains();
    test_pole_survives_degenerate_configurations();
    test_fabrik_and_ccd_reach();
    test_chain_collection_ignores_skin_bone_index();
    test_repeated_solves_are_stable();
    test_effector_rotation_goal();
    test_aim_respects_bone_axes();
    test_resolve_facing_axis();
    test_aim_cone_limit();
    test_aim_up_reference();
    test_rig_two_bone_reaches_and_restores();
    test_rig_chain_ik_reaches();
    test_rig_chain_ik_eases_to_a_swapped_target();
    test_rig_look_at_turns_toward_target();
    test_rig_spring_bone_falls_and_holds_lengths();
    test_rig_parents_solve_before_children();
    test_rig_lost_target_fades_out();
    test_rig_switched_off_rests_and_resumes();
    test_rig_characters_solve_in_parallel_as_one_alone();
    test_rig_aiming_at_another_character_sees_its_solved_pose();
    test_rig_writing_above_its_character_still_solves();
    test_rig_look_at_follows_a_new_forward_axis();
    test_rig_spring_moves_every_frame_above_the_step_rate();
    test_rig_recovers_from_a_target_that_is_not_a_number();
    test_rig_spring_restarts_after_a_teleport();
    test_two_bone_turning_the_bend_plane_keeps_the_knee_on_its_hinge();
    test_rig_goal_follows_a_moving_target_at_once();
    test_rig_look_at_bone_limit_zero_holds_the_bones();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("ik solvers", run_ik_suite)
