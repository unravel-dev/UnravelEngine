#pragma once

#include "inspector.h"

#include <context/context.hpp>
#include <entt/entity/handle.hpp>

namespace unravel
{

/// Whether an instance root sits inside another instance.
auto is_nested_instance(entt::handle entity) -> bool;

/**
 * @brief Draws the aggregated "Changes" section for everything from `root` down.
 *
 * One group per prefab instance in hierarchy order, each with its override tree, removals and
 * additions, attributed to whoever made them; per-row Revert / Restore / Delete; a per-group
 * Revert; and a deep "Revert All Changes" behind a confirmation.
 *
 * `root` may be either:
 * - an **instance root** - its own changes form the first group, "This instance"; or
 * - an **authoring root** - the content of the prefab being edited, which carries no
 *   prefab_component and has no group of its own. Its nested instances' changes are the
 *   document's authoring, and this is the one place they can all be seen and reverted from.
 *
 * Returns `changed` and `edit_finished` when anything was reverted or restored. Does not sync
 * and does not save: the caller decides (editing_manager::sync_after_override_change, and for
 * an authoring root outside prefab mode, a save).
 */
auto draw_prefab_changes(rtti::context& ctx, entt::handle root) -> inspect_result;

} // namespace unravel
