#pragma once

#include <uuid/uuid.h>

#include <set>

namespace unravel
{
class asset_manager;
struct thumbnail_manager;

namespace asset_deps
{

//------------------------------------------------------------------------------
// Cascade thumbnail regeneration across the in-memory dependency graph.
//
// Marks the thumbnail of `changed` for regeneration, and recursively does the
// same for every currently-loaded asset that (transitively) references it via
// `asset_deps::find_loaded_dependents`. Materials referencing a changed
// texture, meshes referencing a changed material, models referencing a
// changed mesh — all get refreshed.
//
// Prefabs are an opaque serialized buffer at runtime, so we don't walk them
// for individual UUID references. When `mass_invalidate_prefabs` is true,
// every loaded prefab thumbnail is marked dirty as a conservative
// catch-all — the thumbnail pipeline only actually re-renders the prefabs
// the user is currently viewing, so the cost is bounded.
//
// Pass `mass_invalidate_prefabs = true` for renderable asset changes
// (texture / material / mesh / model / animation), false otherwise.
//------------------------------------------------------------------------------
void cascade_thumbnail_regen(asset_manager& am,
                             thumbnail_manager& tm,
                             const hpp::uuid& changed,
                             bool mass_invalidate_prefabs);

//------------------------------------------------------------------------------
// Same cascade, but for *deletions*: the removed assets' own thumbnails are
// removed (not regenerated, because the assets are gone), while every entry
// in `dependent_uids` gets regen-flagged. Without this, a material whose
// texture was just deleted would keep showing its stale thumbnail.
//
// The caller MUST compute `dependent_uids` (typically via
// `asset_deps::find_transitive_loaded_dependents`) BEFORE the removed assets
// have been unloaded. `asset_manager::unload_asset` clears the shared
// `asset_link_t`, which causes every other handle that shared it (e.g. a
// material's texture slot) to report a nil UUID — at that point dependent
// enumeration would silently miss them. By doing the walk pre-unload the
// caller captures the still-valid reference chain.
//
// `mass_invalidate_prefabs` has the same meaning as for regen.
//------------------------------------------------------------------------------
void cascade_thumbnail_remove(asset_manager& am,
                              thumbnail_manager& tm,
                              const std::set<hpp::uuid>& removed_uids,
                              const std::set<hpp::uuid>& dependent_uids,
                              bool mass_invalidate_prefabs);

} // namespace asset_deps
} // namespace unravel
