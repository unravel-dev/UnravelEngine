#include "asset_dependency_graph.h"

#include "asset_manager.h"

#include <engine/ecs/prefab.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>

#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <deque>
#include <unordered_map>
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

namespace
{

//------------------------------------------------------------------------------
// Depth-first walk of a parsed prefab buffer, collecting `prefab_component`'s
// `source.uid` wherever one appears.
//
// Matches on the component's serialized name rather than on the shape of the
// value, because several components serialize a `source`/`uid` pair and only
// this one denotes a nested prefab instance.
//------------------------------------------------------------------------------
void collect_prefab_sources(const simdjson::dom::element& value, std::vector<hpp::uuid>& out)
{
    if(value.is_array())
    {
        for(auto element : value.get_array())
        {
            collect_prefab_sources(element, out);
        }
        return;
    }

    if(!value.is_object())
    {
        return;
    }

    for(auto field : value.get_object())
    {
        if(field.key == "prefab_component")
        {
            std::string_view uid_str;
            if(!field.value["source"]["uid"].get_string().get(uid_str))
            {
                const auto uid = hpp::uuid::from_string(uid_str);
                if(uid.has_value() && !uid->is_nil())
                {
                    out.push_back(*uid);
                }
            }
        }

        // Descended into regardless: a prefab_component's own subtree holds nothing
        // further, but instances nest, so the rest of the document still has to be walked.
        collect_prefab_sources(field.value, out);
    }
}

} // namespace

//------------------------------------------------------------------------------
// prefab -> the prefabs instanced inside it. See the header for why this reads
// the buffer rather than deserializing it.
//------------------------------------------------------------------------------
auto get_referenced_uids(const prefab& p) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> result;

    const auto& buffer = p.buffer.data;
    if(buffer.empty())
    {
        return result;
    }

    simdjson::dom::parser parser;
    simdjson::dom::element document;
    const auto error = parser.parse(buffer.data(), buffer.size()).get(document);
    if(error)
    {
        // A buffer that will not parse cannot be loaded either; whatever surfaces that
        // is the right place to report it, not a dependency query.
        return result;
    }

    collect_prefab_sources(document, result);

    // Document order, first occurrence wins. One prefab may be instanced many times.
    std::vector<hpp::uuid> unique;
    unique.reserve(result.size());
    for(const auto& uid : result)
    {
        if(std::find(unique.begin(), unique.end(), uid) == unique.end())
        {
            unique.push_back(uid);
        }
    }
    return unique;
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

    // Prefabs instance other prefabs, so editing one changes how every prefab and scene
    // nesting it looks.
    collect_dependents<prefab>(am, uid, result);
    collect_dependents<scene_prefab>(am, uid, result);

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

//------------------------------------------------------------------------------
// Kahn's algorithm. Chosen over a DFS post-order because what is left over when
// it stalls *is* the answer for cycles - no separate colouring pass, and the
// leftovers naturally include assets that merely depend on a cycle, which are
// just as unbuildable as its members.
//------------------------------------------------------------------------------
auto compute_build_order(const std::vector<hpp::uuid>& roots,
                         const dependency_resolver& resolve_dependencies) -> build_order
{
    build_order result;
    if(!resolve_dependencies)
    {
        return result;
    }

    // Discovery. Kept in a vector as well as a set so the output is a function of
    // input order rather than of hash iteration order.
    std::vector<hpp::uuid> nodes;
    std::unordered_set<hpp::uuid> known;
    std::unordered_map<hpp::uuid, std::vector<hpp::uuid>> dependencies;

    std::deque<hpp::uuid> pending_discovery(roots.begin(), roots.end());
    while(!pending_discovery.empty())
    {
        const auto uid = pending_discovery.front();
        pending_discovery.pop_front();

        if(uid.is_nil() || !known.insert(uid).second)
        {
            continue;
        }
        nodes.push_back(uid);

        // Deduplicated here rather than trusted from the resolver: a repeated
        // dependency would otherwise be counted twice and the node would never reach
        // zero, reporting a cycle that does not exist.
        std::vector<hpp::uuid> deps;
        for(const auto& dep : resolve_dependencies(uid))
        {
            if(dep.is_nil() || std::find(deps.begin(), deps.end(), dep) != deps.end())
            {
                continue;
            }
            deps.push_back(dep);
            pending_discovery.push_back(dep);
        }
        dependencies.emplace(uid, std::move(deps));
    }

    // Unresolved-dependency counts, and the reverse edges used to decrement them.
    std::unordered_map<hpp::uuid, size_t> unresolved;
    std::unordered_map<hpp::uuid, std::vector<hpp::uuid>> dependents;
    for(const auto& uid : nodes)
    {
        const auto& deps = dependencies[uid];
        unresolved[uid] = deps.size();
        for(const auto& dep : deps)
        {
            dependents[dep].push_back(uid);
        }
    }

    std::vector<hpp::uuid> ready;
    for(const auto& uid : nodes)
    {
        if(unresolved[uid] == 0)
        {
            ready.push_back(uid);
        }
    }

    std::unordered_set<hpp::uuid> emitted;
    for(size_t head = 0; head < ready.size(); ++head)
    {
        const auto uid = ready[head];
        result.ordered.push_back(uid);
        emitted.insert(uid);

        const auto it = dependents.find(uid);
        if(it == dependents.end())
        {
            continue;
        }
        for(const auto& dependent : it->second)
        {
            if(--unresolved[dependent] == 0)
            {
                ready.push_back(dependent);
            }
        }
    }

    // Whatever never reached zero is in a cycle or downstream of one.
    for(const auto& uid : nodes)
    {
        if(emitted.count(uid) == 0)
        {
            result.cyclic.push_back(uid);
        }
    }

    return result;
}

namespace
{

//------------------------------------------------------------------------------
// Appends the uid of every indexed asset of `T`, loaded or not.
//------------------------------------------------------------------------------
template<typename T>
void collect_asset_uids(asset_manager& am, std::vector<hpp::uuid>& out)
{
    am.for_each_asset<T>(
        [&](const auto& kvp) -> void
        {
            const auto& uid = kvp.second.uid();
            if(!uid.is_nil())
            {
                out.push_back(uid);
            }
        });
}

//------------------------------------------------------------------------------
// Reads one asset's nested-instance references, forcing a load. Returns false if
// the uid does not name an asset of this type, so the caller can try the other.
//------------------------------------------------------------------------------
template<typename T>
auto try_read_references(asset_manager& am, const hpp::uuid& uid, std::vector<hpp::uuid>& out) -> bool
{
    auto handle = am.get_asset<T>(uid);
    if(!handle.is_valid())
    {
        return false;
    }

    auto asset = handle.get();
    if(!asset || asset->buffer.data.empty())
    {
        return false;
    }

    out = get_referenced_uids(*asset);
    return true;
}

} // namespace

auto collect_prefab_asset_uids(asset_manager& am) -> std::vector<hpp::uuid>
{
    std::vector<hpp::uuid> result;
    collect_asset_uids<prefab>(am, result);
    collect_asset_uids<scene_prefab>(am, result);
    return result;
}

auto make_prefab_dependency_resolver(asset_manager& am) -> dependency_resolver
{
    return [&am](const hpp::uuid& uid) -> std::vector<hpp::uuid>
    {
        std::vector<hpp::uuid> result;
        if(uid.is_nil())
        {
            return result;
        }

        if(try_read_references<prefab>(am, uid, result))
        {
            return result;
        }

        try_read_references<scene_prefab>(am, uid, result);
        return result;
    };
}

auto compute_prefab_build_order(asset_manager& am) -> build_order
{
    return compute_build_order(collect_prefab_asset_uids(am), make_prefab_dependency_resolver(am));
}

} // namespace unravel::asset_deps
