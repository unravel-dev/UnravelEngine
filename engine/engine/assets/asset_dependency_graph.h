#pragma once
#include <engine/engine_export.h>

#include <uuid/uuid.h>

#include <functional>
#include <vector>

namespace unravel
{
class asset_manager;
class material;
class mesh;
struct prefab;

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
// prefab -> the prefabs instanced inside it.
//
// A prefab is stored as an opaque serialized buffer, so unlike the other types
// there is no loaded object to interrogate. Rather than deserialize it into a
// scratch registry - which would need an asset_manager, run component
// constructors and resolve every unrelated asset reference on the way - this
// reads the references straight out of the buffer, which is JSON: every
// `prefab_component` in it carries a `source` whose `uid` is the asset it
// instances.
//
// Covers scene_prefab too, which is the same format under a different name.
//
// Returns direct references only, in document order, deduplicated. Nil uids and
// anything unparseable are skipped, so a damaged buffer yields fewer references
// rather than throwing.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto get_referenced_uids(const prefab& p) -> std::vector<hpp::uuid>;

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

//------------------------------------------------------------------------------
// Build ordering.
//
// Baking a prefab that instances another requires the inner one to be baked
// first, so the set has to be processed in dependency order - and a cycle has to
// be reported rather than followed.
//------------------------------------------------------------------------------

/// Answers "what does this asset reference", for one uid.
using dependency_resolver = std::function<std::vector<hpp::uuid>(const hpp::uuid&)>;

struct build_order
{
    /// Every dependency precedes the assets that reference it. Includes assets
    /// discovered through `roots` that were not themselves listed.
    std::vector<hpp::uuid> ordered;

    /// Assets that could not be ordered. This is "took part in a cycle **or**
    /// depends on something that did" - a dependent of a cycle is equally
    /// unbuildable, and for an error message that is the set worth naming.
    /// Disjoint from `ordered`.
    std::vector<hpp::uuid> cyclic;
};

//------------------------------------------------------------------------------
// Topologically orders `roots` and everything reachable from them.
//
// Takes the dependency lookup as a callback rather than an asset_manager: the
// ordering is pure graph work, and keeping it that way lets it be tested against
// synthetic graphs instead of a live asset database. The caller supplies both the
// asset set and how to read its references, because "which assets exist" is a
// question for whatever drives the bake, not for the graph.
//
// Deterministic: the output order depends only on the order of `roots` and of
// each dependency list. Nil uids are skipped, duplicate dependencies are counted
// once, and a self-reference is a cycle.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto compute_build_order(const std::vector<hpp::uuid>& roots,
                                       const dependency_resolver& resolve_dependencies) -> build_order;

//------------------------------------------------------------------------------
// Every indexed prefab and scene_prefab, whether or not it is currently loaded.
//
// Unlike `find_loaded_dependents`, which deliberately inspects only resident
// assets, a build has to see all of them - so this enumerates the index rather
// than the resident set.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto collect_prefab_asset_uids(asset_manager& am) -> std::vector<hpp::uuid>;

//------------------------------------------------------------------------------
// A resolver that reads a prefab's nested-instance references through `am`.
//
// **Forces a load** of each asset it is asked about, because the references live
// in the serialized buffer. That is the opposite of the `peek()` policy used for
// thumbnail invalidation and is intended: a build must not skip an asset merely
// because nothing has opened it yet.
//
// The uid may name either a prefab or a scene_prefab; both are tried.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto make_prefab_dependency_resolver(asset_manager& am) -> dependency_resolver;

//------------------------------------------------------------------------------
// The order in which every prefab and scene would have to be baked, plus whatever
// takes part in - or depends on - a nesting cycle.
//
// Convenience over the three functions above. Cycles are reported, never followed.
//------------------------------------------------------------------------------
ENGINE_EXPORT auto compute_prefab_build_order(asset_manager& am) -> build_order;

} // namespace asset_deps
} // namespace unravel
