#pragma once

#include <math/math.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief The bones a humanoid skeleton is matched by when bone names differ between rigs.
 *
 * The same roles as Unity's humanoid avatar and Godot's humanoid skeleton profile: a clip made for
 * one humanoid rig plays on another whatever its bones are called, how many spine bones it has or
 * whether it has shoulder and neck bones.
 */
enum class humanoid_bone : uint8_t
{
    hips,
    spine,
    chest,
    /// The spine bone the arms and the neck hang from.
    upper_chest,
    neck,
    head,
    left_shoulder,
    left_upper_arm,
    left_lower_arm,
    left_hand,
    right_shoulder,
    right_upper_arm,
    right_lower_arm,
    right_hand,
    left_upper_leg,
    left_lower_leg,
    left_foot,
    left_toes,
    right_upper_leg,
    right_lower_leg,
    right_foot,
    right_toes,
    count
};

constexpr size_t HUMANOID_BONE_COUNT = size_t(humanoid_bone::count);

/// Bone index per humanoid role; -1 where the skeleton has no such bone.
using humanoid_map = std::array<int, HUMANOID_BONE_COUNT>;

/// A map that found no roles.
constexpr auto make_empty_humanoid_map() -> humanoid_map
{
    humanoid_map map{};
    for(int& bone : map)
    {
        bone = -1;
    }
    return map;
}

/**
 * @brief Finds the humanoid roles of a skeleton from its structure and rest pose.
 *
 * The hips are the bone two legs hang down from and the spine rises from; the spine runs up to the
 * bone the arms branch sideways from; the head ends the chain above it; a first arm joint that
 * starts near the spine is a shoulder. Names decide left from right (`_l`, `.R`, `Left`...), else
 * the way the toes point does, and mark twist joints a limb does not bend at (`lShldrTwist`).
 * Works for any naming convention and bone count.
 *
 * @param names Bone names.
 * @param parents Parent index per bone, -1 for roots; parents come before their children.
 * @param rest_positions Rest positions in an upright frame (+Y up), in any unit.
 * @return The bone per role; all -1 when no hips are found (not a humanoid).
 */
auto find_humanoid_bones(const std::vector<std::string>& names,
                         const std::vector<int>& parents,
                         const std::vector<math::vec3>& rest_positions) -> humanoid_map;

/// Display name of a role, for tools ("Left Upper Arm").
auto get_humanoid_bone_name(humanoid_bone bone) -> const char*;

/// Number of roles a map found.
auto count_humanoid_bones(const humanoid_map& map) -> size_t;

/**
 * @brief A role set by hand, for a skeleton whose shape misleads the finder (a hand role the rig
 * animates on another bone than the one found). An empty bone name takes the role away.
 */
struct humanoid_bone_override
{
    friend auto operator==(const humanoid_bone_override& lhs, const humanoid_bone_override& rhs) -> bool = default;

    humanoid_bone role{humanoid_bone::hips};
    /// Name of the bone that plays the role; empty for none.
    std::string bone;
};

/**
 * @brief Puts hand-set roles over the found ones.
 *
 * Bones are looked up by name, then by name without FBX namespace. A bone moved to a role leaves
 * any other role it held. An override naming a bone the skeleton lacks changes nothing.
 */
void apply_humanoid_overrides(const std::vector<humanoid_bone_override>& overrides,
                              const std::vector<std::string>& names,
                              humanoid_map& map);

/**
 * @brief A turn of one role's bone in a skeleton's retarget pose, in character space.
 *
 * The character's axes are its side (towards its left), up and forward. Right-side roles are
 * mirrored, so equal values move both sides alike: a rotation of (0, 0, 20) on both upper arms
 * raises both by 20 degrees.
 */
struct humanoid_pose_offset
{
    friend auto operator==(const humanoid_pose_offset& lhs, const humanoid_pose_offset& rhs) -> bool = default;

    humanoid_bone role{humanoid_bone::left_upper_arm};
    /// Euler angles in degrees about the side, up and forward axes.
    math::vec3 rotation{0.0f, 0.0f, 0.0f};
};

/**
 * @brief The character's side axis (towards its left), level: from the upper arms, else the
 * shoulders, else the upper legs. Zero when the skeleton has none of them.
 *
 * @param roles The skeleton's roles.
 * @param globals Global transforms per bone, in an upright frame (+Y up).
 */
auto find_humanoid_side_axis(const humanoid_map& roles, const std::vector<math::transform>& globals) -> math::vec3;

/**
 * @brief Poses a skeleton in a T-pose: upper and lower arms level along the side axis, upper and
 * lower legs straight down. Each limb bone swings about its joint and carries its children.
 *
 * Two skeletons straightened this way can share motion whatever pose they rest in (a T-pose and an
 * A-pose). The same skeleton is straightened the same way, so motion between copies of one
 * skeleton does not change.
 *
 * @param parents Parent index per bone, -1 for roots; parents come before their children.
 * @param roles The skeleton's roles.
 * @param globals Global transforms per bone in an upright frame, changed in place.
 */
void align_humanoid_to_t_pose(const std::vector<int>& parents, const humanoid_map& roles, std::vector<math::transform>& globals);

/**
 * @brief Turns role bones by character-space offsets (see humanoid_pose_offset); each turn carries
 * the bone's children. Offsets on roles the skeleton lacks change nothing.
 *
 * @param parents Parent index per bone, -1 for roots; parents come before their children.
 * @param roles The skeleton's roles.
 * @param offsets The turns, applied in order.
 * @param globals Global transforms per bone in an upright frame, changed in place.
 */
void apply_humanoid_pose_offsets(const std::vector<int>& parents,
                                 const humanoid_map& roles,
                                 const std::vector<humanoid_pose_offset>& offsets,
                                 std::vector<math::transform>& globals);

} // namespace unravel
