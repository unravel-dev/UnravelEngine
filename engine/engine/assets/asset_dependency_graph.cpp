#include "asset_dependency_graph.h"

#include "asset_manager.h"

#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace unravel::asset_deps
{

namespace
{

//------------------------------------------------------------------------------
// Append the handle's UUID to `out` unless it's nil. Centralises the nil
// filter so call sites stay terse.
//------------------------------------------------------------------------------
template<typename T>
void push_if_set(std::vector<hpp::uuid>& out, const asset_handle<T>& handle)
{
    const auto& uid = handle.uid();
    if(!uid.is_nil())
    {
        out.push_back(uid);
    }
}

} // namespace

//------------------------------------------------------------------------------
// material → up to six texture maps.
//
// Only `pbr_material` carries texture slots today; the base `material` class
// is abstract / has no maps. We dynamic-cast rather than make this virtual to
// keep the dep-graph concern out of the material interface.
//------------------------------------------------------------------------------
auto get_referenced_uids(const material& m) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> result;
    if(m.is<pbr_material>())
    {
        const auto& pbr = static_cast<const pbr_material&>(m);

        result.reserve(6);
        push_if_set(result, pbr.get_color_map());
        push_if_set(result, pbr.get_normal_map());
        push_if_set(result, pbr.get_roughness_map());
        push_if_set(result, pbr.get_metalness_map());
        push_if_set(result, pbr.get_ao_map());
        push_if_set(result, pbr.get_emissive_map());
    }
    return result;
}

//------------------------------------------------------------------------------
// mesh → the materials it was imported with.
//
// We read the raw UUIDs (not `get_imported_materials()`), which would force a
// load via `asset_manager::get_asset<material>(uid)` — we just want
// references, not loaded objects.
//------------------------------------------------------------------------------
auto get_referenced_uids(const mesh& m) -> std::vector<hpp::uuid>
{
    const auto& uids = m.get_default_material_uids();
    std::vector<hpp::uuid> result;
    result.reserve(uids.size());
    for(const auto& uid : uids)
    {
        if(!uid.is_nil())
        {
            result.push_back(uid);
        }
    }
    return result;
}

//------------------------------------------------------------------------------
// Walks the loaded set of `T` and appends the UUID of every asset whose
// reference list contains `target`. Skips assets that aren't currently
// resident (`peek()` returns nullptr) — they have no in-memory thumbnail to
// invalidate, and will be re-rendered fresh on the next request.
//
// Lives at file scope (not as a lambda) so we can inline-template it cleanly
// over multiple types without copy-pasting the body.
//------------------------------------------------------------------------------
template<typename T>
void collect_dependents(asset_manager& am,
                        const hpp::uuid& target,
                        std::vector<hpp::uuid>& out)
{
    am.for_each_asset<T>(
        [&](const auto& kvp) -> void
        {
            const auto& handle = kvp.second;
            if(handle.uid().is_nil())
            {
                return;
            }

            auto loaded = handle.peek();
            if(!loaded)
            {
                return;
            }

            const auto deps = get_referenced_uids(*loaded);
            if(std::find(deps.begin(), deps.end(), target) != deps.end())
            {
                out.push_back(handle.uid());
            }
        });
}

auto find_loaded_dependents(asset_manager& am, const hpp::uuid& uid) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> result;

    collect_dependents<material>(am, uid, result);
    collect_dependents<mesh>(am, uid, result);

    return result;
}

auto find_transitive_loaded_dependents(asset_manager& am, const hpp::uuid& root)
    -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> result;
    if(root.is_nil())
    {
        return result;
    }

    std::unordered_set<hpp::uuid> visited;
    std::deque<hpp::uuid> queue;
    visited.insert(root);
    queue.push_back(root);

    while(!queue.empty())
    {
        const auto current = queue.front();
        queue.pop_front();

        for(const auto& dep : find_loaded_dependents(am, current))
        {
            if(visited.insert(dep).second)
            {
                result.push_back(dep);
                queue.push_back(dep);
            }
        }
    }

    return result;
}

} // namespace unravel::asset_deps
