#include "thumbnail_invalidation.h"

#include "thumbnail_manager.h"

#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/prefab.h>

#include <deque>
#include <unordered_set>

namespace unravel::asset_deps
{

namespace
{

//------------------------------------------------------------------------------
// BFS over the dependency graph starting from `root`, regenerating the
// thumbnail of every loaded transitive dependent. `root` itself is NOT
// touched here — the caller decides whether to regen or remove it.
//
// `visited` must already contain `root` (so we don't re-visit it via a cycle)
// and is populated with every UUID we touch — useful to the caller for the
// prefab mass-invalidation pass.
//------------------------------------------------------------------------------
void walk_and_regen_dependents(asset_manager& am,
                               thumbnail_manager& tm,
                               const hpp::uuid& root,
                               std::unordered_set<hpp::uuid>& visited)
{
    std::deque<hpp::uuid> queue;
    queue.push_back(root);

    while(!queue.empty())
    {
        const auto current = queue.front();
        queue.pop_front();

        for(const auto& dep : find_loaded_dependents(am, current))
        {
            if(visited.insert(dep).second)
            {
                tm.regenerate_thumbnail(dep);
                queue.push_back(dep);
            }
        }
    }
}

//------------------------------------------------------------------------------
// Conservative prefab fallback: mark every loaded prefab thumbnail we haven't
// already touched. Used by both cascade entry points when the triggering
// asset type can affect prefab visuals.
//------------------------------------------------------------------------------
void mass_invalidate_prefab_thumbnails(asset_manager& am,
                                       thumbnail_manager& tm,
                                       const std::unordered_set<hpp::uuid>& visited)
{
    am.for_each_asset<prefab>(
        [&](const auto& kvp) -> void
        {
            const auto& handle = kvp.second;
            const auto& uid = handle.uid();
            if(uid.is_nil() || visited.count(uid) != 0)
            {
                return;
            }
            tm.regenerate_thumbnail(uid);
        });
}

} // namespace

void cascade_thumbnail_regen(asset_manager& am,
                             thumbnail_manager& tm,
                             const hpp::uuid& changed,
                             bool mass_invalidate_prefabs)
{
    if(changed.is_nil())
    {
        return;
    }

    std::unordered_set<hpp::uuid> visited;
    visited.insert(changed);
    tm.regenerate_thumbnail(changed);

    walk_and_regen_dependents(am, tm, changed, visited);

    if(mass_invalidate_prefabs)
    {
        mass_invalidate_prefab_thumbnails(am, tm, visited);
    }
}

// Note: `removed_uids` and `dependent_uids` are intentionally distinct sets:
// the former gets thumbnails dropped, the latter regenerated.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void cascade_thumbnail_remove(asset_manager& am,
                              thumbnail_manager& tm,
                              const std::set<hpp::uuid>& removed_uids,
                              const std::set<hpp::uuid>& dependent_uids,
                              bool mass_invalidate_prefabs)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    std::unordered_set<hpp::uuid> visited;

    // Removed assets: drop their thumbnails entirely. They no longer exist, so
    // a regen would be a wasted render against a missing asset.
    for(const auto& uid : removed_uids)
    {
        if(uid.is_nil())
        {
            continue;
        }
        tm.remove_thumbnail(uid);
        visited.insert(uid);
    }

    // Dependents are still loaded — refresh their thumbnails so the missing
    // dependency is reflected (e.g. a material whose normal map was deleted
    // will now render with an empty slot).
    for(const auto& uid : dependent_uids)
    {
        if(uid.is_nil() || !visited.insert(uid).second)
        {
            continue;
        }
        tm.regenerate_thumbnail(uid);
    }

    if(mass_invalidate_prefabs)
    {
        mass_invalidate_prefab_thumbnails(am, tm, visited);
    }
}

} // namespace unravel::asset_deps
