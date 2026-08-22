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
/**
 * @brief What a reparent recorded or stripped, so undo can put it back.
 *
 * See prefab_override_context::mark_entity_reparented. A move within one instance records the
 * placement as local overrides; a move out of the instance that supplied the subtree makes it
 * this scene's content - prefab ids and slots dropped - and states removals on the instances
 * that supplied it so their replays do not bring it back.
 */
struct prefab_reparent_record
{
    struct removal_entry
    {
        entt::uhandle root{};
        prefab_statement_target target{};
        bool is_instance{};
    };
    struct stripped_slot
    {
        entt::uhandle root{};
        hpp::uuid instance_id{};
        hpp::uuid instance_document{};
    };

    std::vector<removal_entry> removals{};
    std::vector<std::pair<entt::uhandle, prefab_id_component>> stripped_ids{};
    std::vector<stripped_slot> stripped_slots{};
    entt::uhandle override_root{};
    std::vector<prefab_property_override_data> added_overrides{};
};

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
