#include "mcp_tools_common.h"

#include "mcp_async.h"

#include <engine/meta/ecs/entity.hpp>
#include <editor/assets/asset_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/prefab.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <graphics/texture.h>
#include <engine/ui/ui_tree.h>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/physics/physics_material.h>
#include <engine/audio/audio_clip.h>
#include <engine/animation/animation.h>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <vector>

namespace unravel::mcp
{
namespace
{

auto asset_entry_json(const std::string& uid, const std::string& location, const std::string& type) -> std::string
{
    return fmt::format(R"({{"uid":{},"location":{},"type":{}}})",
                       make_json_string(uid),
                       make_json_string(location),
                       make_json_string(type));
}

auto path_matches_type(const std::string& location, const std::string& type_filter) -> bool
{
    if(type_filter.empty())
    {
        return true;
    }
    const auto lower_loc = to_lower_ascii(location);
    const auto lower_type = to_lower_ascii(type_filter);
    if(ends_with_ci(lower_loc, lower_type))
    {
        return true;
    }
    if(!lower_type.empty() && lower_type.front() == '.')
    {
        return ends_with_ci(lower_loc, lower_type.substr(1));
    }
    return false;
}

auto normalize_folder_key(std::string key) -> std::string
{
    if(key.empty())
    {
        return key;
    }
    fs::error_code ec;
    const fs::path as_path(key);
    if(as_path.is_absolute())
    {
        key = fs::convert_to_protocol(as_path).generic_string();
    }
    while(!key.empty() && (key.back() == '/' || key.back() == '\\'))
    {
        key.pop_back();
    }
    return key;
}

template<typename T>
auto matches_asset_extension(const std::string& ext) -> bool
{
    if(ext.empty())
    {
        return false;
    }
    if(ex::is_format<T>(ext))
    {
        return true;
    }
    const auto lower = to_lower_ascii(ext);
    return lower != ext && ex::is_format<T>(lower);
}

enum class asset_readiness
{
    ready,
    loading,
    failed,
    missing,
};

auto readiness_to_string(asset_readiness state) -> const char*
{
    switch(state)
    {
        case asset_readiness::ready:
            return "ready";
        case asset_readiness::failed:
            return "failed";
        case asset_readiness::missing:
            return "missing";
        case asset_readiness::loading:
            break;
    }
    return "loading";
}

auto source_exists(const std::string& key) -> bool
{
    fs::error_code ec;
    return fs::exists(fs::absolute(fs::resolve_protocol(key)), ec);
}

/**
 * The assets that keys held before an import or reimport overwrote them, by key.
 *
 * A reimport reloads into the same handle, so until the reload is published the handle still
 * returns the old asset and reads as ready. A wait compares against the asset kept here
 * (the identity version() reports); holding it, rather than its address, keeps the reloaded
 * asset from reusing the address and reading as unchanged.
 */
using asset_snapshot = std::map<std::string, std::shared_ptr<void>>;

/// Polls with try_get(), the handle's own readiness test: unlike is_ready() it submits a deferred
/// load, which nothing else would start while we wait. The handle is fetched again each round
/// because right after an import the key may not be registered yet. While the handle returns
/// @p previous, the asset the key held before an overwrite, the reload is still on its way.
///
/// The handle is not touched before the compiler has written the asset: a load started earlier finds no compiled file
/// and leaves the asset empty. An empty asset is not the answer either while time remains: the compiled-asset watcher
/// reloads it once the compiler writes it, so only an asset still empty at the deadline failed.
template<typename T>
auto poll_handle_state(asset_manager& am,
                       const std::string& key,
                       std::chrono::milliseconds timeout,
                       const std::shared_ptr<void>& previous) -> asset_readiness
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto compiled_path = asset_reader::resolve_compiled_path<T>(key);
    bool is_empty = false;
    while(true)
    {
        fs::error_code ec;
        if(fs::exists(compiled_path, ec))
        {
            if(auto handle = am.get_asset<T>(key))
            {
                auto asset = handle.try_get();
                is_empty = asset_handle<T>::is_empty(asset);
                if(asset && asset != previous && !is_empty)
                {
                    return asset_readiness::ready;
                }
            }
        }
        if(std::chrono::steady_clock::now() >= deadline)
        {
            if(is_empty)
            {
                return asset_readiness::failed;
            }
            return source_exists(key) ? asset_readiness::loading : asset_readiness::missing;
        }
        tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
}

template<typename... Types>
struct asset_type_list
{
};

/// The asset types with a loadable handle, in the order extensions are matched.
using loadable_asset_types = asset_type_list<material,
                                             mesh,
                                             prefab,
                                             scene_prefab,
                                             gfx::texture,
                                             audio_clip,
                                             animation_clip,
                                             physics_material,
                                             ui_tree>;

/// Calls @p visit with the asset type that owns extension @p ext; false when no type does.
template<typename Visitor, typename T, typename... Rest>
auto visit_asset_type(const std::string& ext, asset_type_list<T, Rest...>, Visitor&& visit) -> bool
{
    if(matches_asset_extension<T>(ext))
    {
        visit.template operator()<T>();
        return true;
    }
    if constexpr(sizeof...(Rest) > 0)
    {
        return visit_asset_type(ext, asset_type_list<Rest...>{}, std::forward<Visitor>(visit));
    }
    return false;
}

/// Loads and holds what each existing key of @p keys holds now. It is loaded, not just peeked: an
/// asset nothing uses is not resident, and loading the stale compiled file later would read as new.
auto take_asset_snapshot(rtti::context& ctx, const std::vector<std::string>& keys) -> asset_snapshot
{
    tpp::this_thread::register_this_thread();

    auto& am = ctx.get_cached<asset_manager>();
    asset_snapshot snapshot;
    for(const auto& key : keys)
    {
        const auto ext = fs::path(key).extension().generic_string();
        visit_asset_type(ext,
                         loadable_asset_types{},
                         [&]<typename T>()
                         {
                             auto handle = am.find_asset<T>(key);
                             if(!handle)
                             {
                                 return;
                             }
                             if(auto asset = handle.get(); asset && !asset_handle<T>::is_empty(asset))
                             {
                                 snapshot.emplace(key, std::move(asset));
                             }
                         });
    }
    return snapshot;
}

auto find_snapshot_asset(const asset_snapshot& snapshot, const std::string& key) -> std::shared_ptr<void>
{
    auto it = snapshot.find(key);
    return it != snapshot.end() ? it->second : nullptr;
}

/// Waits until @p key is loaded; with a @p previous asset (see asset_snapshot), until it is
/// loaded as something else.
auto wait_asset_state(rtti::context& ctx,
                      const std::string& key,
                      std::chrono::milliseconds timeout,
                      const std::shared_ptr<void>& previous = nullptr) -> asset_readiness
{
    tpp::this_thread::register_this_thread();

    auto& am = ctx.get_cached<asset_manager>();
    const auto ext = fs::path(key).extension().generic_string();
    asset_readiness state{};
    if(visit_asset_type(ext,
                        loadable_asset_types{},
                        [&]<typename T>()
                        {
                            state = poll_handle_state<T>(am, key, timeout, previous);
                        }))
    {
        return state;
    }

    // Types with no loadable handle here: registered metadata, or the file itself, is the signal.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(true)
    {
        auto meta = am.get_metadata_for_key(key);
        if(!meta.location.empty() || !meta.meta.type.empty())
        {
            return asset_readiness::ready;
        }
        if(std::chrono::steady_clock::now() >= deadline)
        {
            return source_exists(key) ? asset_readiness::loading : asset_readiness::missing;
        }
        tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
}

auto read_import_timeout_ms(const simdjson::dom::object& args, int64_t default_ms) -> std::chrono::milliseconds
{
    return read_timeout_ms(args, default_ms, 60000);
}

auto collect_imported_asset_keys(const import_files_item& item) -> std::vector<std::string>
{
    std::vector<std::string> keys;
    if(item.dest_key.empty())
    {
        return keys;
    }
    if(!item.is_directory)
    {
        keys.push_back(item.dest_key);
        return keys;
    }
    fs::error_code ec;
    const fs::path root(item.dest_path);
    const auto meta_ext = ex::get_meta_format();
    for(fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if(!it->is_regular_file(ec))
        {
            continue;
        }
        const auto path = it->path();
        if(path.extension().generic_string() == meta_ext)
        {
            continue;
        }
        const auto key = fs::convert_to_protocol(path).generic_string();
        if(!key.empty())
        {
            keys.push_back(key);
        }
    }
    if(keys.empty())
    {
        keys.push_back(item.dest_key);
    }
    return keys;
}

/// The keys of the existing files an import of @p items overwrites: a directory copies its whole
/// tree onto its destination, so each of its files lands at the same relative path there.
auto collect_overwritten_keys(const std::vector<import_files_item>& items) -> std::vector<std::string>
{
    std::vector<std::string> keys;
    const auto meta_ext = ex::get_meta_format();
    auto add_existing = [&](const fs::path& dest)
    {
        // An error code of its own: the directory walk below stops on any error in ec.
        fs::error_code status_ec;
        if(dest.extension().generic_string() == meta_ext || !fs::is_regular_file(dest, status_ec))
        {
            return;
        }
        auto key = fs::convert_to_protocol(dest).generic_string();
        if(!key.empty())
        {
            keys.push_back(std::move(key));
        }
    };
    for(const auto& item : items)
    {
        if(!item.is_directory)
        {
            add_existing(item.dest_path);
            continue;
        }
        const fs::path source_root(item.source_path);
        const fs::path dest_root(item.dest_path);
        fs::error_code ec;
        for(fs::recursive_directory_iterator it(source_root, ec), end; it != end && !ec; it.increment(ec))
        {
            if(it->is_regular_file(ec))
            {
                add_existing(dest_root / it->path().lexically_relative(source_root));
            }
        }
    }
    return keys;
}

} // namespace

void register_asset_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "project_get_info",
         .description = "Get the currently open project name/path, or empty if none.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             return {.text = project_info_json(ctx), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_list_batch",
         .description =
             "List assets for a protocol (app|engine|editor). Optional type filter and limit "
             "(default 200). Prefer assets_find_batch for filtered search.",
         .input_schema_json =
             R"({"type":"object","properties":{"protocol":{"type":"string","enum":["app","engine","editor"]},"type":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":5000}},"required":["protocol"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string protocol;
             if(!read_string(args, "protocol", protocol))
             {
                 return {.text = "Missing protocol", .is_error = true};
             }
             const auto group = protocol_to_group(protocol);
             if(group.empty())
             {
                 return {.text = "Invalid protocol (use app|engine|editor)", .is_error = true};
             }

             std::string type_filter;
             read_string(args, "type", type_filter);
             if(!type_filter.empty())
             {
                 type_filter = normalize_asset_type_filter(type_filter);
             }

             const int64_t limit = read_clamped_int64(args, "limit", 200, 1);

             auto& am = ctx.get_cached<asset_manager>();
             auto locations = am.get_all_assets(group);

             std::string json = "[";
             bool first = true;
             size_t count = 0;
             bool truncated = false;
             for(const auto& location : locations)
             {
                 auto meta = am.get_metadata_for_key(location);
                 if(!type_filter.empty())
                 {
                     const auto meta_type = normalize_asset_type_filter(meta.meta.type);
                     if(meta_type != type_filter && !path_matches_type(location, type_filter))
                     {
                         continue;
                     }
                 }
                 if(static_cast<int64_t>(count) >= limit)
                 {
                     truncated = true;
                     break;
                 }
                 if(!first)
                 {
                     json += ",";
                 }
                 first = false;
                 json += asset_entry_json(hpp::to_string(meta.meta.uid), location, meta.meta.type);
                 ++count;
             }
             json += "]";
             return {.text = fmt::format(R"({{"assets":{},"count":{},"limit":{},"truncated":{}}})",
                                         json,
                                         count,
                                         limit,
                                         truncated ? "true" : "false"),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_find_batch",
         .description =
             "Search assets by type, prefix, and/or name_contains. Optional protocol and limit "
             "(default 200).",
         .input_schema_json = R"json({"type":"object","properties":{"protocol":{"type":"string","enum":["app","engine","editor","all"],"default":"all"},"type":{"type":"string","description":"Asset extension/type filter e.g. mat, .pfb, emesh"},"prefix":{"type":"string","description":"Location prefix e.g. app:/data/Materials/"},"name_contains":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":5000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string protocol = "all";
             read_string(args, "protocol", protocol);
             if(protocol.empty())
             {
                 protocol = "all";
             }

             std::vector<std::string> groups;
             if(protocol == "all")
             {
                 groups = {"app:/", "engine:/", "editor:/"};
             }
             else
             {
                 const auto group = protocol_to_group(protocol);
                 if(group.empty())
                 {
                     return {.text = "Invalid protocol (use app|engine|editor|all)", .is_error = true};
                 }
                 groups.push_back(group);
             }

             std::string type_filter;
             read_string(args, "type", type_filter);
             if(!type_filter.empty())
             {
                 type_filter = normalize_asset_type_filter(type_filter);
             }

             std::string prefix;
             read_string(args, "prefix", prefix);
             std::string name_contains;
             read_string(args, "name_contains", name_contains);

             const int64_t limit = read_clamped_int64(args, "limit", 200, 1);
             
             auto& am = ctx.get_cached<asset_manager>();
             std::string json = "[";
             bool first = true;
             size_t count = 0;
             size_t scanned = 0;
             for(const auto& group : groups)
             {
                 for(const auto& location : am.get_all_assets(group))
                 {
                     ++scanned;
                     if(!prefix.empty() && !starts_with(location, prefix))
                     {
                         continue;
                     }
                     if(!name_contains.empty() && !contains_ci(location, name_contains))
                     {
                         continue;
                     }
                     auto meta = am.get_metadata_for_key(location);
                     if(!type_filter.empty())
                     {
                         const auto meta_type = normalize_asset_type_filter(meta.meta.type);
                         if(meta_type != type_filter && !path_matches_type(location, type_filter))
                         {
                             continue;
                         }
                     }
                     if(!first)
                     {
                         json += ",";
                     }
                     first = false;
                     json += asset_entry_json(hpp::to_string(meta.meta.uid), location, meta.meta.type);
                     ++count;
                     if(static_cast<int64_t>(count) >= limit)
                     {
                         json += "]";
                         return {.text = fmt::format(
                                     R"({{"assets":{},"count":{},"scanned":{},"truncated":true,"limit":{}}})",
                                     json,
                                     count,
                                     scanned,
                                     limit),
                                 .is_error = false};
                     }
                 }
             }
             json += "]";
             return {.text = fmt::format(R"({{"assets":{},"count":{},"scanned":{},"truncated":false,"limit":{}}})",
                                         json,
                                         count,
                                         scanned,
                                         limit),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_list_types",
         .description = "List known registered asset type names.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             std::string json = "[";
             bool first = true;
             for(const auto& group : ex::get_all_formats())
             {
                 for(const auto& ext : group)
                 {
                     if(!first)
                     {
                         json += ",";
                     }
                     first = false;
                     json += make_json_string(ext);
                 }
             }
             json += "]";
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_list_embedded_primitives",
         .description =
             "List embedded mesh primitive names for scene_create_primitives_batch. Cube is 1x1x1 at origin.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             static const char* names[] = {"Cube",
                                          "Cube Rounded",
                                          "Sphere",
                                          "Plane",
                                          "Cylinder",
                                          "Capsule 2m",
                                          "Capsule 1m",
                                          "Cone",
                                          "Torus",
                                          "Teapot",
                                          "Icosahedron",
                                          "Dodecahedron",
                                          "Icosphere0",
                                          "Icosphere1",
                                          "Icosphere2",
                                          "Icosphere3",
                                          "Terrain Test"};
             std::string json = "[";
             for(size_t i = 0; i < std::size(names); ++i)
             {
                 if(i > 0)
                 {
                     json += ",";
                 }
                 json += make_json_string(names[i]);
             }
             json += "]";
             return {.text = json, .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_create_folder",
         .description =
             "Create a folder under a protocol path (content-browser parity). Provide path key "
             "(e.g. app:/data/Props) or folder+name. Optional wait_ms after create.",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 200);

             std::string path;
             std::string folder;
             std::string name;
             read_string(args, "path", path);
             read_string(args, "folder", folder);
             read_string(args, "name", name);

             std::string key;
             if(!path.empty())
             {
                 key = normalize_folder_key(path);
             }
             else if(!folder.empty() && !name.empty())
             {
                 key = normalize_folder_key(folder);
                 key.push_back('/');
                 key += name;
                 key = normalize_folder_key(key);
             }
             else
             {
                 return {.text = "Provide path, or folder + name", .is_error = true};
             }

             auto create_result = mcp.invoke_on_main(
                 [&ctx, key]() -> tool_result
                 {
                     std::string error;
                     if(!require_open_project(ctx, error) && starts_with(key, "app:/"))
                     {
                         return {.text = error, .is_error = true};
                     }
                     fs::error_code ec;
                     const auto absolute = fs::absolute(fs::resolve_protocol(key));
                     if(fs::exists(absolute, ec))
                     {
                         if(fs::is_directory(absolute, ec))
                         {
                             return {.text = fmt::format(R"({{"key":{},"created":false,"exists":true}})",
                                                         make_json_string(key)),
                                     .is_error = false};
                         }
                         return {.text = "Path exists and is not a folder: " + key, .is_error = true};
                     }
                     fs::create_directories(absolute, ec);
                     if(ec)
                     {
                         return {.text = "Failed to create folder: " + ec.message(), .is_error = true};
                     }
                     return {.text = fmt::format(R"({{"key":{},"created":true,"exists":false}})",
                                                 make_json_string(key)),
                             .is_error = false};
                 });

             if(!create_result)
             {
                 return {.text = "Timed out creating folder on main thread", .is_error = true};
             }
             if(!create_result->is_error)
             {
                 sleep_worker(wait_ms);
             }
             return *create_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_import_files",
         .description =
             "Import absolute filesystem paths (outside project) into folder (app:/...). The single "
             "import path for all assets: paths may be files or directories (directories copy "
             "recursively - pass one directory per multi-file model, e.g. .gltf + .bin + textures). "
             "Waits for copy+ready (wait_ms, default 15000); a file copied over an existing asset is ready "
             "once the reloaded asset has replaced the old one.",
         .input_schema_json =
             R"json({"type":"object","properties":{"paths":{"type":"array","items":{"type":"string"}},"folder":{"type":"string"},"wait_ms":{"type":"integer","minimum":0,"maximum":60000}},"required":["paths","folder"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             const auto wait_ms = read_import_timeout_ms(args, 15000);
             std::string error;
             std::vector<std::string> paths;
             if(!read_string_array(args, "paths", paths, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(paths.empty() || std::any_of(paths.begin(),
                                             paths.end(),
                                             [](const std::string& path)
                                             {
                                                 return path.empty();
                                             }))
             {
                 return {.text = "Each paths entry must be a non-empty string", .is_error = true};
             }
             std::string folder;
             if(!read_string(args, "folder", folder) || folder.empty())
             {
                 return {.text = "Missing folder", .is_error = true};
             }
             folder = normalize_folder_key(folder);
             if(!starts_with(folder, "app:/"))
             {
                 return {.text = "folder must be under app:/ (project data)", .is_error = true};
             }
             std::string project_error;
             if(!require_open_project(ctx, project_error))
             {
                 return {.text = project_error, .is_error = true};
             }
             fs::error_code ec;
             const auto project_root = fs::absolute(fs::resolve_protocol("app:/"));
             for(const auto& path : paths)
             {
                 const auto absolute_source = fs::absolute(fs::path(path));
                 if(fs::is_any_parent_path(project_root, absolute_source) ||
                    fs::equivalent(project_root, absolute_source, ec))
                 {
                     return {.text =
                                 "paths must be outside the project folder; download/stage "
                                 "elsewhere then import (rejected: " +
                                 absolute_source.generic_string() + ")",
                             .is_error = true};
                 }
             }
             const auto target_path = fs::absolute(fs::resolve_protocol(folder));
             if(fs::exists(target_path, ec) && !fs::is_directory(target_path, ec))
             {
                 return {.text = "folder resolves to a non-directory path: " + folder, .is_error = true};
             }
             {
                 std::string focus_error;
                 editor_actions::request_main_window_focus(ctx, &focus_error);
             }
             const auto overwritten =
                 take_asset_snapshot(ctx,
                                     collect_overwritten_keys(editor_actions::resolve_import_items(paths, target_path)));
             auto import_result = editor_actions::import_files(ctx, paths, target_path, true);
             const bool copied = editor_actions::wait_import_jobs(import_result, wait_ms);
             const auto ready_deadline = std::chrono::steady_clock::now() + wait_ms;
             std::string results = "[";
             bool first = true;
             size_t ready_count = 0;
             size_t imported_count = 0;
             for(auto& item : import_result.items)
             {
                 const bool copy_ok = copied;
                 const auto keys = collect_imported_asset_keys(item);
                 if(keys.empty())
                 {
                     if(!first)
                     {
                         results += ",";
                     }
                     first = false;
                     results += fmt::format(
                         R"({{"ok":{},"source":{},"dest":{},"key":{},"is_directory":{},"ready":false,"error":"No destination key"}})",
                         copy_ok ? "true" : "false",
                         make_json_string(item.source_path),
                         make_json_string(item.dest_path),
                         make_json_string(item.dest_key),
                         item.is_directory ? "true" : "false");
                     continue;
                 }
                 imported_count += keys.size();
                 for(const auto& key : keys)
                 {
                     bool ready = false;
                     if(copy_ok)
                     {
                         const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             ready_deadline - std::chrono::steady_clock::now());
                         ready = wait_asset_state(ctx,
                                                  key,
                                                  remaining.count() > 0 ? remaining : std::chrono::milliseconds(0),
                                                  find_snapshot_asset(overwritten, key)) == asset_readiness::ready;
                     }
                     if(ready)
                     {
                         ++ready_count;
                     }
                     if(!first)
                     {
                         results += ",";
                     }
                     first = false;
                     results += fmt::format(
                         R"({{"ok":{},"source":{},"dest":{},"key":{},"is_directory":{},"ready":{}}})",
                         copy_ok ? "true" : "false",
                         make_json_string(item.source_path),
                         make_json_string(item.dest_path),
                         make_json_string(key),
                         item.is_directory ? "true" : "false",
                         ready ? "true" : "false");
                 }
             }
             results += "]";
             const bool ok = copied && ready_count == imported_count && imported_count > 0;
             return {.text = fmt::format(
                         R"({{"folder":{},"results":{},"imported":{},"ready":{},"copied":{}}})",
                         make_json_string(folder),
                         results,
                         imported_count,
                         ready_count,
                         copied ? "true" : "false"),
                     .is_error = !ok};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_get_mesh_info",
         .description = "Get mesh asset local AABB (min/max/center/extents) by asset key.",
         .input_schema_json = R"json({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string key;
             if(!read_string(args, "key", key) || key.empty())
             {
                 return {.text = "Missing key", .is_error = true};
             }
             auto& am = ctx.get_cached<asset_manager>();
             auto handle = am.get_asset<mesh>(key);
             if(!handle)
             {
                 return {.text = "Mesh asset not found: " + key, .is_error = true};
             }
             auto mesh_ptr = handle.get(true);
             if(!mesh_ptr)
             {
                 return {.text = "Mesh failed to load: " + key, .is_error = true};
             }
             const auto& bounds = mesh_ptr->get_bounds();
             return {.text = fmt::format(R"({{"key":{},"uid":{},"bounds":{}}})",
                                         make_json_string(handle.id()),
                                         make_json_string(hpp::to_string(handle.uid())),
                                         bbox_to_json(bounds)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_get_batch",
         .description = "Get many assets by key and/or uid. Each item: key or uid.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"uid":{"type":"string"}}}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& am = ctx.get_cached<asset_manager>();
             simdjson::dom::array items_arr;
             std::string error;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string key;
                 std::string uid_str;
                 read_string(obj, "key", key);
                 read_string(obj, "uid", uid_str);
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 asset_database::meta meta{};
                 if(!key.empty())
                 {
                     meta = am.get_metadata_for_key(key);
                 }
                 else if(!uid_str.empty())
                 {
                     auto uuid = hpp::uuid::from_string(uid_str);
                     if(!uuid)
                     {
                         results += fmt::format(R"({{"ok":false,"uid":{},"error":"Invalid uid"}})",
                                                make_json_string(uid_str));
                         continue;
                     }
                     meta = am.get_metadata(*uuid);
                 }
                 else
                 {
                     results += R"({"ok":false,"error":"Provide key or uid"})";
                     continue;
                 }
                 if(meta.location.empty() && meta.meta.type.empty())
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"uid":{},"error":"Asset not found"}})",
                                            make_json_string(key),
                                            make_json_string(uid_str));
                     continue;
                 }
                 ++ok_count;
                 results += fmt::format(
                     R"({{"ok":true,"uid":{},"location":{},"type":{}}})",
                     make_json_string(hpp::to_string(meta.meta.uid)),
                     make_json_string(meta.location.empty() ? key : meta.location),
                     make_json_string(meta.meta.type));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, requested),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_reimport_batch",
         .description =
             "Reimport many assets by key and wait until each has reloaded (wait_ms, a shared deadline, "
             "default 15000; 0 returns at once). Each reimported key reports a state: ready, loading (still "
             "compiling or loading at the deadline) or failed (see logs_get_recent).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"}},"required":["key"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 15000);
             simdjson::dom::array items_arr;
             std::string error;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<std::string> keys;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string key;
                 if(!read_string(obj, "key", key) || key.empty())
                 {
                     return {.text = "Item missing key", .is_error = true};
                 }
                 keys.push_back(std::move(key));
             }
             if(keys.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             const auto previous = take_asset_snapshot(ctx, keys);
             // Per key: empty when the reimport was requested, otherwise why it was not.
             auto errors = mcp.invoke_on_main(
                 [keys]() -> std::vector<std::string>
                 {
                     std::vector<std::string> key_errors;
                     key_errors.reserve(keys.size());
                     for(const auto& key : keys)
                     {
                         const auto source = asset_actions::resolve_asset_source_path(key);
                         fs::error_code ec;
                         if(source.empty() || !fs::exists(source, ec))
                         {
                             key_errors.emplace_back("Asset source not found");
                             continue;
                         }
                         if(!asset_actions::can_reimport(source))
                         {
                             key_errors.emplace_back("Asset cannot be reimported");
                             continue;
                         }
                         asset_actions::reimport_key(key);
                         key_errors.emplace_back();
                     }
                     return key_errors;
                 });
             if(!errors)
             {
                 return {.text = "Timed out reimporting on main thread", .is_error = true};
             }
             const bool should_wait = wait_ms.count() > 0;
             const auto deadline = std::chrono::steady_clock::now() + wait_ms;
             std::string results = "[";
             size_t ok_count = 0;
             size_t ready_count = 0;
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 if(i > 0)
                 {
                     results += ",";
                 }
                 const auto& key_error = (*errors)[i];
                 if(!key_error.empty())
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                            make_json_string(keys[i]),
                                            make_json_string(key_error));
                     continue;
                 }
                 ++ok_count;
                 if(!should_wait)
                 {
                     results += fmt::format(R"({{"ok":true,"key":{},"reimported":true}})", make_json_string(keys[i]));
                     continue;
                 }
                 const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                     deadline - std::chrono::steady_clock::now());
                 const auto state = wait_asset_state(ctx,
                                                     keys[i],
                                                     (std::max)(remaining, std::chrono::milliseconds(0)),
                                                     find_snapshot_asset(previous, keys[i]));
                 ready_count += state == asset_readiness::ready ? 1 : 0;
                 results += fmt::format(R"({{"ok":true,"key":{},"reimported":true,"state":{}}})",
                                        make_json_string(keys[i]),
                                        make_json_string(readiness_to_string(state)));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"ready":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         ready_count,
                                         keys.size()),
                     .is_error = ok_count == 0 || (should_wait && ready_count != ok_count)};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_wait_ready_batch",
         .description =
             "Wait until assets are loaded and usable. Each key reports a state: ready, loading (still "
             "compiling or loading at the deadline), failed (the load finished without an asset - see "
             "logs_get_recent), or missing (no such file). Loads are started if nothing has asked for "
             "them yet. timeout_ms is a shared deadline for the whole batch (default 15000, max 60000).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"}},"required":["key"]}},"timeout_ms":{"type":"integer","minimum":0,"maximum":60000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             simdjson::dom::array items_arr;
             std::string error;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto timeout_ms = read_timeout_ms(args, 15000, 60000, "timeout_ms").count();
             std::vector<std::string> keys;
             keys.reserve(32);
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string key;
                 if(!read_string(obj, "key", key) || key.empty())
                 {
                     return {.text = "Item missing key", .is_error = true};
                 }
                 keys.push_back(std::move(key));
             }
             const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
             std::vector<asset_readiness> states(keys.size(), asset_readiness::loading);
             auto settled = [](asset_readiness state)
             {
                 // Waiting longer cannot change a failed load or a file that is not there.
                 return state != asset_readiness::loading;
             };
             // Shared-deadline poll: do not spend the full timeout on each key sequentially.
             while(std::chrono::steady_clock::now() < deadline &&
                   !std::all_of(states.begin(), states.end(), settled))
             {
                 for(size_t i = 0; i < keys.size(); ++i)
                 {
                     if(settled(states[i]))
                     {
                         continue;
                     }
                     const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                         deadline - std::chrono::steady_clock::now());
                     if(remaining.count() <= 0)
                     {
                         break;
                     }
                     // Short slice so other keys keep getting checked under the shared budget.
                     const auto slice = (std::min)(remaining, std::chrono::milliseconds(100));
                     states[i] = wait_asset_state(ctx, keys[i], slice);
                 }
             }
             std::string results = "[";
             size_t ok_count = 0;
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 if(i > 0)
                 {
                     results += ",";
                 }
                 const bool ready = states[i] == asset_readiness::ready;
                 ok_count += ready ? 1 : 0;
                 results += fmt::format(R"({{"key":{},"ready":{},"state":{}}})",
                                        make_json_string(keys[i]),
                                        bool_to_json(ready),
                                        make_json_string(readiness_to_string(states[i])));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{},"timeout_ms":{}}})",
                                         results,
                                         ok_count,
                                         keys.size(),
                                         timeout_ms),
                     .is_error = ok_count != keys.size()};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "prefabs_create_from_entities_batch",
         .description =
             "Save entity hierarchies as .pfb prefab assets (batch). Each item: entity_id, path or "
             "folder (+ optional name), optional attach. Optional wait_ms after the batch.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"},"attach":{"type":"boolean"}},"required":["entity_id"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 500);
             simdjson::dom::array items_arr;
             std::string error;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             struct item_t
             {
                 std::string entity_id;
                 std::string path;
                 std::string folder;
                 std::string name;
                 bool attach{false};
             };
             std::vector<item_t> items;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 item_t item{};
                 if(!read_string(obj, "entity_id", item.entity_id) || item.entity_id.empty())
                 {
                     return {.text = "Item missing entity_id", .is_error = true};
                 }
                 read_string(obj, "path", item.path);
                 read_string(obj, "folder", item.folder);
                 read_string(obj, "name", item.name);
                 read_bool(obj, "attach", item.attach);
                 items.push_back(std::move(item));
             }
             if(items.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             auto result = mcp.invoke_on_main(
                 [&ctx, items]() -> tool_result
                 {
                     scene* scn = nullptr;
                     std::string error;
                     if(!require_edit_scene(ctx, scn, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     if(!require_open_project(ctx, error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     auto& am = ctx.get_cached<asset_manager>();
                     std::string results = "[";
                     bool first = true;
                     size_t ok_count = 0;
                     for(const auto& item : items)
                     {
                         if(!first)
                         {
                             results += ",";
                         }
                         first = false;
                         auto entity = find_entity(*scn, item.entity_id);
                         if(!entity)
                         {
                             results += fmt::format(R"({{"ok":false,"entity_id":{},"error":"Entity not found"}})",
                                                    make_json_string(item.entity_id));
                             continue;
                         }
                         std::string key;
                         if(!item.path.empty())
                         {
                             key = item.path;
                         }
                         else if(!item.folder.empty())
                         {
                             key = normalize_folder_key(item.folder);
                             key.push_back('/');
                             if(!item.name.empty())
                             {
                                 key += item.name;
                             }
                             else if(auto* tag = entity.try_get<tag_component>())
                             {
                                 key += tag->name;
                             }
                             else
                             {
                                 key += "Prefab";
                             }
                         }
                         else
                         {
                             results += fmt::format(
                                 R"json({{"ok":false,"entity_id":{},"error":"Provide path, or folder and optional name"}})json",
                                 make_json_string(item.entity_id));
                             continue;
                         }
                         fs::error_code ec;
                         const fs::path as_path(key);
                         if(as_path.is_absolute())
                         {
                             key = fs::convert_to_protocol(as_path).generic_string();
                         }
                         const auto prefab_ext = ex::get_format<prefab>();
                         if(!ends_with_ci(key, prefab_ext))
                         {
                             key += prefab_ext;
                         }
                         const auto absolute = fs::absolute(fs::resolve_protocol(key));
                         fs::create_directories(absolute.parent_path(), ec);
                         if(!asset_writer::atomic_save_to_file(absolute.string(), entt::const_handle(entity)))
                         {
                             results += fmt::format(R"({{"ok":false,"entity_id":{},"key":{},"error":"Failed to save prefab"}})",
                                                    make_json_string(item.entity_id),
                                                    make_json_string(key));
                             continue;
                         }
                         auto prefab_handle = am.get_asset<prefab>(key);
                         if(item.attach && prefab_handle)
                         {
                             entity.get_or_emplace<prefab_component>().source = prefab_handle;
                         }
                         ++ok_count;
                         results += fmt::format(
                             R"({{"ok":true,"key":{},"uid":{},"entity_id":{},"attached":{}}})",
                             make_json_string(key),
                             make_json_string(hpp::to_string(prefab_handle.uid())),
                             make_json_string(item.entity_id),
                             item.attach ? "true" : "false");
                     }
                     results += "]";
                     return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                                 results,
                                                 ok_count,
                                                 items.size()),
                             .is_error = ok_count == 0};
                 },
                 std::chrono::milliseconds(15000));
             if(!result)
             {
                 return {.text = "Timed out creating prefabs on main thread", .is_error = true};
             }
             if(!result->is_error)
             {
                 sleep_worker(wait_ms);
             }
             return *result;
         },
         .mutates_scene = true,
         .requires_main_thread = false});
}

} // namespace unravel::mcp

