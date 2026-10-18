#pragma once

#include <context/context.hpp>
#include <filesystem/filesystem.h>
#include <hpp/uuid.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace unravel
{

class scene;

/**
 * @brief Outcome of checking the prefab nesting graph.
 */
struct prefab_graph_report
{
    /// Prefabs and scenes examined.
    size_t asset_count{};
    /// Assets that reference at least one other prefab.
    size_t nesting_count{};
    /// Deepest chain in the dependency order, in assets.
    size_t max_depth{};
    /// Human-readable ids of assets that take part in, or depend on, a nesting cycle.
    std::vector<std::string> cyclic_ids;
    /// Why the bake did not run at all (play mode); empty when it ran or only validated.
    std::string refusal;

    auto is_valid() const -> bool
    {
        return cyclic_ids.empty() && refusal.empty();
    }

    /// One sentence on what keeps the set from baking; empty when it is valid.
    auto describe_problem() const -> std::string;
};

/**
 * @brief Checks that every prefab and scene can be ordered for a build.
 *
 * A prefab that instances another has to be baked after it, so the set has to be topologically
 * orderable. A cycle - A instancing B instancing A - has no valid order and would recurse forever if
 * expanded, so it is reported here rather than left to fail at load. Read-only.
 */
auto validate_prefab_graph(rtti::context& ctx) -> prefab_graph_report;

/**
 * @brief Resolves nested prefab instances in every prefab and scene into cooked copies under
 * `staging_root` (one folder per protocol, laid out like the compiled cache).
 *
 * A prefab that instances another stores a snapshot of it, taken when that prefab was last saved. Each
 * asset is loaded, its instances resolved against their own assets, and written with a marker that
 * tells the loader the work is done. Assets are processed dependency-first. The sources and the compiled
 * cache are not touched. A cycle aborts the run before anything is written.
 *
 * @return The validation report; its refusal is set when play mode keeps the bake from running.
 */
auto bake_prefab_nesting(rtti::context& ctx, const fs::path& staging_root, size_t* baked_count = nullptr)
    -> prefab_graph_report;

/**
 * @brief Bakes one prefab or scene: into `staging_root`, or with `in_place` its own source file in the
 * current format and without the marker (a migration). `scratch` is reused between calls.
 * @return False when the asset was left untouched (logged).
 */
auto bake_prefab_asset(rtti::context& ctx, scene& scratch, const hpp::uuid& uid, const fs::path& staging_root, bool in_place)
    -> bool;

/**
 * @brief Clears the "nesting already resolved" claim from every compiled asset, once per compiled
 * cache: caches written before cooking moved to a staging folder may still carry it, and an uncooked
 * build must not ship that claim.
 *
 * Rewrites the one boolean in place instead of a load-and-re-save, so it cannot lose data.
 * @return How many compiled assets were changed.
 */
auto clear_prefab_nesting_marker_once(rtti::context& ctx) -> size_t;

} // namespace unravel
