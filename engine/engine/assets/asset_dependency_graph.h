#pragma once
#include <engine/engine_export.h>

#include <uuid/uuid.h>

#include <vector>

namespace unravel
{
class asset_manager;
class material;
class mesh;

namespace asset_deps
{

//------------------------------------------------------------------------------
// Forward-dependency enumeration.
//
// Returns the UUIDs that the given asset directly references. Designed as a
// set of type-overloaded free functions (rather than a class virtual or a
// template) so that:
//
//   1. Each asset type stays free of "I know about the dep graph" coupling.
//   2. The same signatures can be reused later when migrating to a persistent
//      forward-dependency list stored at compile time (e.g. on asset_meta or
//      asset_manifest). At that point we'd populate the list once during
//      compilation by calling these same functions on the fully-loaded asset.
//
// All functions skip nil UUIDs so the result never contains a "this slot is
// empty" marker.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto get_referenced_uids(const material& m) -> std::vector<hpp::uuid>;
ENGINE_EXPORT auto get_referenced_uids(const mesh& m) -> std::vector<hpp::uuid>;

//------------------------------------------------------------------------------
// Reverse-dependency lookup over the currently-loaded asset set.
//
// Walks every renderable-asset container registered with `am` (materials,
// meshes) and returns the UUIDs of those whose `get_referenced_uids`
// contains `uid`.
//
// Note: `model` is intentionally absent — it isn't a top-level asset_manager
// storage type, it lives inside model_component on prefabs/scenes. Prefab
// thumbnails are handled separately via the mass-invalidation path in
// `cascade_thumbnail_regen`.
//
// This intentionally inspects only assets that are *already in memory*
// (`asset_handle::peek()` — no forced loads, no deferred-task submission).
// Unloaded dependents are correctly handled by the editor's lazy thumbnail
// regeneration: they'll re-render against the new dependency content the next
// time their thumbnail is requested.
//
// Cost: O(N_loaded × N_deps_per_asset). N is bounded by what the editor
// actually has open, so this is cheap in practice.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto find_loaded_dependents(asset_manager& am,
                                          const hpp::uuid& uid) -> std::vector<hpp::uuid>;

//------------------------------------------------------------------------------
// Same as `find_loaded_dependents` but follows the chain transitively: returns
// every loaded asset that depends on `root` directly *or* indirectly (a mesh
// that uses a material that uses the changed texture, etc.).
//
// Use this when the dependency chain is about to be torn down (e.g. before
// `asset_manager::unload_asset`) — the moment an asset is unloaded its
// `asset_link_t` is cleared, and any other handle that shared the same link
// will report a nil UUID, making post-unload enumeration miss them.
//
// The result excludes `root` itself.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto find_transitive_loaded_dependents(asset_manager& am,
                                                     const hpp::uuid& root)
    -> std::vector<hpp::uuid>;

} // namespace asset_deps
} // namespace unravel
