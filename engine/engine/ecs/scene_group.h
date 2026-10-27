#pragma once

#include <cstdint>

namespace unravel
{

/// Identifies the loaded scene a root entity, and with it everything under it, belongs to.
using scene_group_id = uint32_t;

/// Roots of the scene scene::load_from loaded, and roots created outside an additive load.
constexpr scene_group_id MAIN_SCENE_GROUP = 0;
/// Roots that scene::load_from keeps; see scene::make_persistent.
constexpr scene_group_id PERSISTENT_SCENE_GROUP = 1;
/// The first id scene::reserve_scene_group hands out. Ids are never reused.
constexpr scene_group_id FIRST_ADDITIVE_SCENE_GROUP = 2;

/**
 * @struct scene_group_context
 * @brief Registry context entry: the group new roots join, and the next additive group id.
 */
struct scene_group_context
{
    /// The group a root created now joins.
    scene_group_id new_root_group{MAIN_SCENE_GROUP};
    /// The id the next additive load reserves.
    scene_group_id next_additive_group{FIRST_ADDITIVE_SCENE_GROUP};
};

} // namespace unravel
