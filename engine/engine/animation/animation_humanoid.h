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

} // namespace unravel
