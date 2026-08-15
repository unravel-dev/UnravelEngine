#pragma once

#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <math/math.h>

namespace unravel
{

/**
 * @brief How the `pole` argument of a position solver is interpreted.
 *
 * The previous API used a zero-length vector as the "disabled" sentinel, which
 * made the world origin a magic location: a pole authored at (0,0,0) silently
 * turned the constraint off. The mode is explicit instead.
 */
enum class ik_pole_mode : uint8_t
{
    /// No pole constraint. Intermediate joints keep whatever bend the raw solve
    /// produced (CCD / FABRIK) or their current bend plane (two-bone).
    none,
    /// World-space point that intermediate joints should bend toward.
    point,
    /// World-space direction, measured from the chain root.
    direction,
};

/**
 * @brief Bend-direction hint for the intermediate joints of a chain.
 *
 * Default-constructs to "no constraint", so a value-initialised pole never
 * pins joints anywhere.
 *
 * The pole must come from a reference the solve does not move. Deriving it from
 * the end effector's own transform (or from anything that tracks it, such as a
 * manipulator gizmo snapped to the selection) closes a feedback loop: the solve
 * rotates the effector, the pole follows, the chain chases the pole, and the
 * limb spins - fastest past 90 degrees of bend, where the pole's component
 * perpendicular to the root->end axis collapses and flips sign. Use the
 * character root, a dedicated hint entity, or a value snapshotted when the
 * interaction began.
 */
struct ik_pole
{
    math::vec3 value{0.0f, 0.0f, 0.0f};
    ik_pole_mode mode{ik_pole_mode::none};

    /// World-space point the knee / elbow should bend toward.
    static auto from_point(const math::vec3& point) -> ik_pole
    {
        return ik_pole{point, ik_pole_mode::point};
    }

    /// World-space direction, measured from the chain root.
    static auto from_direction(const math::vec3& direction) -> ik_pole
    {
        return ik_pole{direction, ik_pole_mode::direction};
    }

    /**
     * @brief Legacy conversion: treat a zero-length vector as "disabled".
     *
     * Kept so the scripting API can preserve its documented `Vector3.zero`
     * convention. New C++ code should use from_point / from_direction.
     */
    static auto from_legacy_point(const math::vec3& point) -> ik_pole;

    auto is_enabled() const noexcept -> bool
    {
        return mode != ik_pole_mode::none;
    }
};

/**
 * @brief Shared tuning for the iterative solvers (CCD, FABRIK).
 */
struct ik_solver_params
{
    /// Upper bound on solver iterations. Values below 1 are clamped to 1.
    ///
    /// 20 rather than 10 because CCD needs it: on a limb-length chain whose
    /// target mainly requires the chain to shorten rather than swing, CCD is
    /// still ~60mm off after 10 sweeps and only settles around 20. FABRIK stops
    /// as soon as it is within `threshold`, so the larger budget costs it
    /// nothing. Check ik_result::reached rather than assuming convergence.
    int max_iterations{20};

    /// Convergence distance in world units. Values below 0 are clamped to 0.
    float threshold{0.001f};

    /// Pose blend against the incoming (animated) pose. 0 leaves the chain
    /// untouched, 1 applies the full solve. Blending happens in local rotation
    /// space, so the result is a pose interpolation - the effector does not
    /// travel linearly toward the target as the weight ramps.
    float weight{1.0f};
};

/**
 * @brief Outcome of a solve.
 *
 * `applied` and `reached` are deliberately separate: a caller needs to know an
 * out-of-reach target was clamped so it can fade the goal out, which a plain
 * success flag cannot express.
 */
struct ik_result
{
    /// A pose was written to the chain.
    bool applied{false};

    /// The effector converged on the target within the requested threshold.
    bool reached{false};

    /// Remaining error against the requested (unclamped) goal: world-space
    /// distance for the position solvers, radians for the rotation ones.
    float distance{0.0f};

    explicit operator bool() const noexcept
    {
        return applied;
    }
};

/**
 * @brief Tuning for the single-bone aim solver.
 *
 * The bone-local axes are explicit because there is no universal convention:
 * most imported rigs (Mixamo included) run bones along local +Y, while the
 * engine's own entities face local +Z. Guessing produces a twisted spine.
 * ik_resolve_bone_axis_local() derives the axis from the rig when the caller
 * does not know it.
 */
struct ik_aim_params
{
    /// Bone-local axis that ends up pointing at the target.
    math::vec3 forward_axis{0.0f, 0.0f, 1.0f};

    /// Bone-local axis kept as close to `world_up` as the aim allows.
    math::vec3 up_axis{0.0f, 1.0f, 0.0f};

    /// World-space up reference. Zero length keeps the bone's existing twist
    /// about the aim axis instead of forcing a roll.
    math::vec3 world_up{0.0f, 0.0f, 0.0f};

    /// Maximum deviation from the bone's current forward, in radians. Values
    /// <= 0 leave the aim unlimited.
    float max_angle_radians{0.0f};

    /// Blend against the current orientation, clamped to [0, 1].
    float weight{1.0f};
};

// -----------------------------------------------------------------------------
// Position solvers.
//
// Argument order convention (shared across the three solvers):
//   (entity, target, pole) -> (chain structure, if any) -> tuning knobs.
//
// `num_bones_in_chain` counts BONES, so the collected joint chain is one longer.
// The walk stops early at the skeleton root; a chain shorter than the solver
// needs reports `applied == false` rather than solving a truncated chain.
// -----------------------------------------------------------------------------

/**
 * @brief Cyclic-coordinate-descent solve of an arbitrary-length chain.
 *
 * Converges slowly when the target mainly requires the chain to retract rather
 * than swing, and settles to roughly a thousandth of chain length rather than
 * exactly on target. Prefer ik_set_position_two_bone for limbs (exact, single
 * pass) and FABRIK for longer chains; CCD is here for chains that need a
 * per-joint sweep.
 */
auto ik_set_position_ccd(entt::handle end_effector,
                         const math::vec3& target,
                         const ik_pole& pole,
                         size_t num_bones_in_chain,
                         const ik_solver_params& params = {}) -> ik_result;

/**
 * @brief FABRIK solve of an arbitrary-length chain.
 */
auto ik_set_position_fabrik(entt::handle end_effector,
                            const math::vec3& target,
                            const ik_pole& pole,
                            size_t num_bones_in_chain,
                            const ik_solver_params& params = {}) -> ik_result;

/**
 * @brief Analytical two-bone solve (hip-knee-foot, shoulder-elbow-hand).
 *
 * @param weight Pose blend against the incoming pose, clamped to [0, 1].
 * @param soften Fraction of chain length over which the reach eases out before
 *               lockout, clamped to [0, 1]. 0 (the default) reaches the target
 *               exactly whenever it is reachable; higher values trade that for
 *               a knee that never snaps straight. Softening always costs reach,
 *               so it must be opted into rather than defaulted on.
 */
auto ik_set_position_two_bone(entt::handle end_effector,
                              const math::vec3& target,
                              const ik_pole& pole,
                              float weight = 1.0f,
                              float soften = 0.0f) -> ik_result;

// -----------------------------------------------------------------------------
// Rotation goals.
// -----------------------------------------------------------------------------

/**
 * @brief Blends a bone toward a world-space orientation.
 *
 * Position solvers only orient the bones that drive the chain, never the
 * effector itself, so the effector's world orientation swings with the limb.
 * Call this after a position solve to pin a foot to a slope or keep an aimed
 * hand level - the same split as Unity's SetIKPosition / SetIKRotation.
 */
auto ik_set_rotation(entt::handle bone, const math::quat& rotation, float weight = 1.0f) -> ik_result;

/**
 * @brief Points a bone-local axis at a world-space target.
 */
auto ik_aim_at_position(entt::handle bone, const math::vec3& target, const ik_aim_params& params = {}) -> ik_result;

/**
 * @brief Points the entity's local +Z axis at `target`, preserving its twist.
 *
 * Thin wrapper over ik_aim_at_position using the engine's entity convention and
 * no up reference, so the bone swings onto the target without being rolled.
 * Prefer ik_aim_at_position for skeleton bones, whose axes are rig-specific.
 */
auto ik_look_at_position(entt::handle bone, const math::vec3& target, float weight = 1.0f) -> ik_result;

// -----------------------------------------------------------------------------
// Rig helpers.
// -----------------------------------------------------------------------------

/**
 * @brief Bone-local direction from `bone` toward its first child.
 *
 * The axis the bone visually runs ALONG. Falls back to +Z for leaf bones and for
 * entities without a usable child.
 *
 * Correct for pointing the bone itself at something (a barrel, a limb, a
 * tentacle). Wrong for a "face the target" aim on a spine, chest or head:
 * aiming a torso along its own length points it at the target and folds the
 * character in half. A bone's FACING axis is not derivable from the skeleton -
 * that is what a humanoid avatar mapping exists for - so pass it explicitly.
 */
auto ik_resolve_bone_axis_local(entt::handle bone) -> math::vec3;

/**
 * @brief Bone-local axis that currently points along `world_direction`.
 *
 * Answers "which way does this bone consider forward", which the skeleton alone
 * cannot: a rig authored with the chest's local -Z out of the front is
 * structurally identical to one with +Z out of the front, and importers do not
 * normalise it. Asking which local axis lines up with the character's world
 * forward resolves it for any rig without authoring.
 *
 * Sample it in a neutral pose. With `snap_to_cardinal` the result is one of the
 * six signed unit axes, so a spine slightly bent by the current animation frame
 * still resolves to the same clean axis.
 */
auto ik_resolve_facing_axis_local(entt::handle bone,
                                  const math::vec3& world_direction,
                                  bool snap_to_cardinal = true) -> math::vec3;

/**
 * @brief Returns the armature root local rotation for the model owning this bone.
 */
auto ik_get_facing_adjustment_rotation(entt::handle end_effector) -> math::quat;

} // namespace unravel
