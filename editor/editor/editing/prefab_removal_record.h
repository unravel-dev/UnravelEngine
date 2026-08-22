#pragma once

#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/ecs.h>

#include <set>
#include <vector>

namespace unravel
{

/**
 * @brief What marking an entity as removed from a prefab instance changed.
 *
 * The bookkeeping does not live on the deleted entity - it lives on the instance that contained
 * it - so restoring the entity restores none of it. Without this an undone deletion brings the
 * entity back while the instance still lists it as removed, and the next resync deletes it again.
 *
 * Lists rather than single ids, because a removal covers the whole deleted subtree: a resync
 * recreates any record it cannot match, so a removal that only named the subtree's root would
 * bring the root's children back as orphans.
 *
 * Its own header rather than a member of prefab_override_context, so an undo action can carry
 * one without an editor panel header ending up in the action's interface.
 */
struct prefab_removal_record
{
    /// The instance whose prefab_component recorded the removal.
    entt::uhandle container{};

    /// Nested prefab instances the removal recorded, keyed by instance id.
    std::vector<hpp::uuid> removed_instances{};

    /// Ordinary entities the removal recorded, keyed by prefab uid.
    std::vector<hpp::uuid> removed_entities{};

    /// Overrides the removal dropped along with the entities, and which half each was in.
    std::set<prefab_property_override_data> erased_overrides{};
};

} // namespace unravel
