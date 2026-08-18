#pragma once

#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/ecs.h>

#include <set>

namespace unravel
{

/**
 * @brief What marking an entity as removed from a prefab instance changed.
 *
 * The bookkeeping does not live on the deleted entity - it lives on the instance that contained
 * it - so restoring the entity restores none of it. Without this an undone deletion brings the
 * entity back while the instance still lists it as removed, and the next resync deletes it again.
 *
 * Its own header rather than a member of prefab_override_context, so an undo action can carry
 * one without an editor panel header ending up in the action's interface.
 */
struct prefab_removal_record
{
    /// The instance whose prefab_component recorded the removal.
    entt::uhandle container{};

    /// Non-nil when a nested prefab instance was recorded, keyed by its instance id.
    hpp::uuid removed_instance{};

    /// Non-nil when an ordinary child was recorded, keyed by its prefab uid.
    hpp::uuid removed_entity{};

    /// Overrides the removal dropped along with the entity, and which half each was in.
    std::set<prefab_property_override_data> erased_overrides{};
    std::set<prefab_property_override_data> erased_inherited_overrides{};
};

} // namespace unravel
