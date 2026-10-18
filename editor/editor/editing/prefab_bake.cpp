#include "prefab_bake.h"

#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/play_mode.h>
#include <logging/logging.h>
#include <serialization/serialization.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace unravel
{
namespace
{
/// Marks a compiled cache as scrubbed of the nesting marker, so the scrub runs once per cache.
constexpr const char* NESTING_MARKER_SCRUBBED_FLAG = "app:/editor/.cache/nesting_marker_scrubbed";
constexpr const char* PLAY_MODE_REFUSAL = "Stop play mode first: prefabs cannot be cooked while the game runs.";

auto count_roots(entt::registry& registry) -> size_t
{
    size_t count = 0;
    registry.view<root_component, transform_component>().each([&](auto, auto&&, auto&&) { ++count; });
    return count;
}

/// Where a baked copy goes: the staging mirror of the compiled layout, never the source and never the
/// editor's compiled cache, which has to stay a pure function of the source. Empty when the asset
/// compiles outside its protocol's compiled folder.
auto get_baked_path(const std::string& id, const fs::path& staging_root) -> fs::path
{
    const auto compiled_path = asset_reader::resolve_compiled_path(id);
    const auto protocol = fs::extract_protocol(fs::path(id)).generic_string();
    const auto compiled_root = fs::resolve_protocol(ex::get_compiled_directory(protocol));
    fs::error_code ec;
    const auto relative = fs::relative(compiled_path, compiled_root, ec);
    return ec || relative.empty() ? fs::path{} : staging_root / protocol / relative;
}

/// Loads a prefab (one root) or a scene into `scratch`; false when it does not load (logged).
auto load_for_bake(asset_manager& am, scene& scratch, const std::string& id, bool is_prefab, entt::handle& root) -> bool
{
    scratch.unload();
    if(is_prefab)
    {
        // load_from_prefab_out rather than instantiate: instantiate regenerates every id_component,
        // which would rewrite the file with new uids on every bake.
        auto handle = am.get_asset<prefab>(id);
        if(!load_from_prefab_out(handle, *scratch.registry, root) || !root)
        {
            APPLOG_ERROR("Bake: could not load prefab {}", id);
            return false;
        }
        return true;
    }
    if(!load_from_prefab(am.get_asset<scene_prefab>(id), scratch))
    {
        APPLOG_ERROR("Bake: could not load scene {}", id);
        return false;
    }
    return true;
}

/// Serializes the loaded asset: compact and marked for a baked copy, the source's own format otherwise.
auto save_for_bake(scene& scratch, entt::handle root, const hpp::uuid& uid, bool is_prefab, bool in_place) -> std::string
{
    std::stringstream buffer;
    std::unique_ptr<serialization::scoped_output_format> compact;
    if(!in_place)
    {
        compact = std::make_unique<serialization::scoped_output_format>(serialization::output_format::compact);
    }
    const bool pushed = push_save_context();
    get_save_context().nesting_resolved = !in_place;
    if(is_prefab)
    {
        auto& save_ctx = get_save_context();
        save_ctx.save_source = root;
        save_ctx.to_prefab = true;
        // The asset being baked is the document these records belong to.
        save_ctx.document_uid = uid;
        save_to_stream(buffer, entt::const_handle{root});
        save_ctx.document_uid = {};
        save_ctx.to_prefab = false;
        save_ctx.save_source = {};
    }
    else
    {
        save_to_stream(buffer, scratch);
    }
    get_save_context().nesting_resolved = false;
    pop_save_context(pushed);
    return buffer.str();
}

/// Reads the serialized asset back with the reader that matches its writer and counts its roots: the
/// two readers are one argument apart and read different formats.
auto count_roots_after_round_trip(const std::string& text, bool is_scene) -> size_t
{
    scene verify("prefab_bake_verify");
    std::stringstream in(text);
    if(is_scene)
    {
        load_from_stream(in, verify);
    }
    else
    {
        load_from_stream(in, *verify.registry);
    }
    return count_roots(*verify.registry);
}

auto read_whole_file(const fs::path& file, std::string& text) -> bool
{
    std::ifstream in(file, std::ios::binary);
    if(!in.good())
    {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/// Flips the marker literal in one compiled file; true when it changed the file.
auto clear_marker_in_file(const fs::path& file) -> bool
{
    // The marker is written by exactly one place, as a JSON boolean, so flipping the literal is enough.
    static const std::string marker_true_min = "\"nesting_resolved\":true";
    static const std::string marker_true_pretty = "\"nesting_resolved\": true";
    static const std::string marker_false_min = "\"nesting_resolved\":false";
    static const std::string marker_false_pretty = "\"nesting_resolved\": false";
    std::string contents;
    if(!read_whole_file(file, contents))
    {
        return false;
    }
    const auto original = contents;
    contents = string_utils::replace(contents, marker_true_min, marker_false_min);
    contents = string_utils::replace(contents, marker_true_pretty, marker_false_pretty);
    if(contents == original)
    {
        return false;
    }
    fs::error_code ec;
    asset_writer::atomic_write_file(file,
                                    [&](const fs::path& temp)
                                    {
                                        std::ofstream out(temp, std::ios::binary);
                                        out << contents;
                                    },
                                    ec);
    return !ec;
}
} // namespace

auto prefab_graph_report::describe_problem() const -> std::string
{
    if(!refusal.empty())
    {
        return refusal;
    }
    if(cyclic_ids.empty())
    {
        return {};
    }
    return fmt::format("{} prefab(s) take part in a nesting cycle or depend on one (first: {}); fix them, or turn "
                       "off Deploy Cooked Assets.",
                       cyclic_ids.size(),
                       cyclic_ids.front());
}

auto validate_prefab_graph(rtti::context& ctx) -> prefab_graph_report
{
    auto& am = ctx.get_cached<asset_manager>();
    prefab_graph_report report;
    // A uid alone is useless in an error about a cycle: name the file to open.
    const auto describe = [&am](const hpp::uuid& uid) -> std::string
    {
        const auto meta = am.get_metadata(uid);
        return meta.location.empty() ? hpp::to_string(uid) : meta.location;
    };
    const auto uids = asset_deps::collect_prefab_asset_uids(am);
    const auto resolve = asset_deps::make_prefab_dependency_resolver(am);
    const auto order = asset_deps::compute_build_order(uids, resolve);
    report.asset_count = order.ordered.size() + order.cyclic.size();
    // Depth of each asset = one more than its deepest dependency; the order puts every dependency first.
    std::unordered_map<hpp::uuid, size_t> depth;
    for(const auto& uid : order.ordered)
    {
        size_t deepest = 0;
        const auto deps = resolve(uid);
        report.nesting_count += deps.empty() ? 0 : 1;
        for(const auto& dep : deps)
        {
            const auto it = depth.find(dep);
            deepest = std::max(deepest, it == depth.end() ? size_t(1) : it->second + 1);
        }
        depth[uid] = deepest;
        report.max_depth = std::max(report.max_depth, deepest);
    }
    std::transform(order.cyclic.begin(), order.cyclic.end(), std::back_inserter(report.cyclic_ids), describe);
    if(report.is_valid())
    {
        APPLOG_INFO("Prefab graph: {} assets, {} containing nested prefabs, deepest nesting {}.",
                    report.asset_count,
                    report.nesting_count,
                    report.max_depth);
        return report;
    }
    APPLOG_ERROR("Prefab graph: {} of {} assets cannot be ordered for a build - they take part in a nesting cycle, "
                 "or depend on one:",
                 report.cyclic_ids.size(),
                 report.asset_count);
    for(const auto& id : report.cyclic_ids)
    {
        APPLOG_ERROR("  {}", id);
    }
    return report;
}

auto bake_prefab_asset(rtti::context& ctx, scene& scratch, const hpp::uuid& uid, const fs::path& staging_root, bool in_place)
    -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Typed by extension, never by asking the asset manager for a typed handle: get_asset<prefab> on a
    // scene's uid hands back a valid handle to the scene file, and the prefab writer would keep only its
    // first root.
    const auto id = am.get_metadata(uid).location;
    const auto extension = fs::path(id).extension().generic_string();
    const bool is_scene = ex::is_format<scene_prefab>(extension);
    const bool is_prefab = ex::is_format<prefab>(extension);
    const auto path = in_place ? fs::resolve_protocol(id) : get_baked_path(id, staging_root);
    if(id.empty() || (!is_scene && !is_prefab) || path.empty())
    {
        return false;
    }
    entt::handle root{};
    if(!load_for_bake(am, scratch, id, is_prefab, root))
    {
        return false;
    }
    // Nothing loaded means something is wrong with the asset or the load path; an empty document must
    // never be written over content that is presumably fine.
    const size_t roots_loaded = count_roots(*scratch.registry);
    if(roots_loaded == 0 || (is_prefab && roots_loaded != 1))
    {
        APPLOG_ERROR("Bake: {} loaded {} root(s); leaving it untouched.", id, roots_loaded);
        return false;
    }
    // load_from_prefab_out refreshed what was nested inside the root; this catches instances that are
    // roots in their own right, the scene case.
    const size_t synced = sync_all_prefab_instances(*scratch.registry);
    const auto text = save_for_bake(scratch, root, uid, is_prefab, in_place);
    const size_t roots_after = count_roots_after_round_trip(text, is_scene);
    if(roots_after != roots_loaded)
    {
        APPLOG_ERROR("Bake: {} would round-trip to {} roots instead of {}; leaving it untouched.", id, roots_after, roots_loaded);
        return false;
    }
    fs::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    asset_writer::atomic_write_file(path,
                                    [&](const fs::path& temp)
                                    {
                                        std::ofstream out(temp);
                                        out << text;
                                    },
                                    ec);
    if(ec)
    {
        APPLOG_ERROR("Bake: could not write {} -> {}", id, path.generic_string());
        return false;
    }
    // Per asset rather than summarised: a write to a path the deploy does not read, or nothing synced,
    // is invisible in a total.
    APPLOG_INFO("Bake: {} ({} root(s), {} instance(s) synced) -> {}", id, roots_loaded, synced, path.generic_string());
    return true;
}

auto bake_prefab_nesting(rtti::context& ctx, const fs::path& staging_root, size_t* baked_count) -> prefab_graph_report
{
    if(baked_count != nullptr)
    {
        *baked_count = 0;
    }
    if(ctx.get_cached<play_mode>().is_active())
    {
        prefab_graph_report refused;
        refused.refusal = PLAY_MODE_REFUSAL;
        return refused;
    }
    auto report = validate_prefab_graph(ctx);
    if(!report.is_valid())
    {
        // Before any write: a half-baked set is harder to reason about than one never baked.
        APPLOG_ERROR("Bake: aborted, the prefab graph contains a cycle. Nothing was written.");
        return report;
    }
    auto& am = ctx.get_cached<asset_manager>();
    const auto order = asset_deps::compute_build_order(asset_deps::collect_prefab_asset_uids(am),
                                                       asset_deps::make_prefab_dependency_resolver(am));
    // One scratch scene reused for every asset; each bake unloads it first.
    scene scratch("prefab_bake");
    const auto written = std::count_if(order.ordered.begin(),
                                       order.ordered.end(),
                                       [&](const hpp::uuid& uid)
                                       {
                                           return bake_prefab_asset(ctx, scratch, uid, staging_root, false);
                                       });
    scratch.unload();
    if(baked_count != nullptr)
    {
        *baked_count = size_t(written);
    }
    APPLOG_INFO("Bake: resolved nesting in {} of {} assets.", written, order.ordered.size());
    return report;
}

auto clear_prefab_nesting_marker_once(rtti::context& ctx) -> size_t
{
    const auto flag = fs::resolve_protocol(NESTING_MARKER_SCRUBBED_FLAG);
    fs::error_code ec;
    if(fs::exists(flag, ec))
    {
        return 0;
    }
    auto& am = ctx.get_cached<asset_manager>();
    size_t cleared = 0;
    for(const auto& uid : asset_deps::collect_prefab_asset_uids(am))
    {
        const auto meta = am.get_metadata(uid);
        if(!meta.location.empty() && clear_marker_in_file(asset_reader::resolve_compiled_path(meta.location)))
        {
            ++cleared;
        }
    }
    fs::create_directories(flag.parent_path(), ec);
    std::ofstream(flag) << "The compiled cache carries no nesting marker outside cooked deploys.\n";
    if(cleared > 0)
    {
        APPLOG_INFO("Cleared the nesting-resolved marker from {} compiled asset(s).", cleared);
    }
    return cleared;
}

} // namespace unravel
