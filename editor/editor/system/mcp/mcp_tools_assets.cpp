#include "mcp_tools_common.h"

#include "mcp_async.h"

#include <editor/assets/asset_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/prefab.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <chrono>
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

auto ends_with_ci(std::string_view value, std::string_view suffix) -> bool
{
    if(suffix.size() > value.size())
    {
        return false;
    }
    const auto v = value.substr(value.size() - suffix.size());
    return to_lower_ascii(std::string(v)) == to_lower_ascii(std::string(suffix));
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

template<typename T>
auto poll_handle_ready(asset_manager& am, const std::string& key, std::chrono::milliseconds timeout) -> bool
{
    auto handle = am.get_asset<T>(key);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < deadline)
    {
        if(handle && handle.is_ready())
        {
            return true;
        }
        tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
    return handle && handle.is_ready();
}

auto try_wait_asset_ready(rtti::context& ctx, const std::string& key, std::chrono::milliseconds timeout) -> bool
{
    tpp::this_thread::register_this_thread();

    auto& am = ctx.get_cached<asset_manager>();
    const auto ext = fs::path(key).extension().generic_string();
    if(matches_asset_extension<material>(ext))
    {
        return poll_handle_ready<material>(am, key, timeout);
    }
    if(matches_asset_extension<mesh>(ext))
    {
        return poll_handle_ready<mesh>(am, key, timeout);
    }
    if(matches_asset_extension<prefab>(ext))
    {
        return poll_handle_ready<prefab>(am, key, timeout);
    }
    if(matches_asset_extension<scene_prefab>(ext))
    {
        return poll_handle_ready<scene_prefab>(am, key, timeout);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(std::chrono::steady_clock::now() < deadline)
    {
        auto meta = am.get_metadata_for_key(key);
        if(!meta.location.empty() || !meta.meta.type.empty())
        {
            return true;
        }
        const auto absolute = fs::absolute(fs::resolve_protocol(key));
        fs::error_code ec;
        if(fs::exists(absolute, ec) && fs::file_size(absolute, ec) > 0)
        {
            return true;
        }
        tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
    return false;
}

auto read_import_timeout_ms(const simdjson::dom::object& args, int64_t default_ms) -> std::chrono::milliseconds
{
    int64_t timeout_ms = default_ms;
    if(args["wait_ms"].get(timeout_ms))
    {
        timeout_ms = default_ms;
    }
    if(timeout_ms < 0)
    {
        timeout_ms = 0;
    }
    if(timeout_ms > 60000)
    {
        timeout_ms = 60000;
    }
    return std::chrono::milliseconds(timeout_ms);
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
             if(!ctx.has<project_manager>())
             {
                 return {.text = R"({"open":false})", .is_error = false};
             }
             auto& pm = ctx.get_cached<project_manager>();
             if(!pm.has_open_project())
             {
                 return {.text = R"({"open":false})", .is_error = false};
             }

             const auto& info = pm.get_project_info();
             const auto path = fs::resolve_protocol("app:/").generic_string();
             return {.text = fmt::format(R"({{"open":true,"name":{},"path":{},"guid":{}}})",
                                         make_json_string(pm.get_name()),
                                         make_json_string(path),
                                         make_json_string(info.project_guid)),
                     .is_error = false};
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

             int64_t limit = 200;
             if(args["limit"].get(limit))
             {
                 limit = 200;
             }
             if(limit < 1)
             {
                 limit = 1;
             }

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

             int64_t limit = 200;
             if(args["limit"].get(limit))
             {
                 limit = 200;
             }
             if(limit < 1)
             {
                 limit = 1;
             }
             
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
             "Import absolute filesystem paths (outside project) into folder (app:/...). "
             "Waits for copy+ready (wait_ms, default 15000).",
         .input_schema_json =
             R"json({"type":"object","properties":{"paths":{"type":"array","items":{"type":"string"}},"folder":{"type":"string"},"wait_ms":{"type":"integer","minimum":0,"maximum":60000}},"required":["paths","folder"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             const auto wait_ms = read_import_timeout_ms(args, 15000);
             simdjson::dom::array paths_arr;
             if(args["paths"].get(paths_arr))
             {
                 return {.text = "Missing paths array", .is_error = true};
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
             std::vector<std::string> paths;
             for(auto el : paths_arr)
             {
                 std::string_view path_view;
                 if(el.get(path_view) || path_view.empty())
                 {
                     return {.text = "Each paths entry must be a non-empty string", .is_error = true};
                 }
                 paths.emplace_back(path_view);
             }
             if(paths.empty())
             {
                 return {.text = "paths array is empty", .is_error = true};
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
                         ready = try_wait_asset_ready(
                             ctx,
                             key,
                             remaining.count() > 0 ? remaining : std::chrono::milliseconds(0));
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
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
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
         .description = "Reimport many assets by key. Optional wait_ms after the batch.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"}},"required":["key"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 500);
             simdjson::dom::array items_arr;
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }
             std::vector<std::string> keys;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
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
             auto result = mcp.invoke_on_main(
                 [keys]() -> tool_result
                 {
                     std::string results = "[";
                     bool first = true;
                     size_t ok_count = 0;
                     for(const auto& key : keys)
                     {
                         if(!first)
                         {
                             results += ",";
                         }
                         first = false;
                         const auto source = asset_actions::resolve_asset_source_path(key);
                         fs::error_code ec;
                         if(source.empty() || !fs::exists(source, ec))
                         {
                             results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                                    make_json_string(key),
                                                    make_json_string("Asset source not found"));
                             continue;
                         }
                         if(!asset_actions::can_reimport(source))
                         {
                             results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                                    make_json_string(key),
                                                    make_json_string("Asset cannot be reimported"));
                             continue;
                         }
                         asset_actions::reimport_key(key);
                         ++ok_count;
                         results += fmt::format(R"({{"ok":true,"key":{},"reimported":true}})", make_json_string(key));
                     }
                     results += "]";
                     return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                                 results,
                                                 ok_count,
                                                 keys.size()),
                             .is_error = ok_count == 0};
                 });
             if(!result)
             {
                 return {.text = "Timed out reimporting on main thread", .is_error = true};
             }
             if(!result->is_error)
             {
                 sleep_worker(wait_ms);
             }
             return *result;
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_wait_ready_batch",
         .description =
             "Wait until many asset keys are loadable/ready (poll). timeout_ms is a shared "
             "deadline for the whole batch (default 15000, max 60000).",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"}},"required":["key"]}},"timeout_ms":{"type":"integer","minimum":0,"maximum":60000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             simdjson::dom::array items_arr;
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
             }
             int64_t timeout_ms = 15000;
             if(args["timeout_ms"].get(timeout_ms))
             {
                 timeout_ms = 15000;
             }
             if(timeout_ms < 0)
             {
                 timeout_ms = 0;
             }
             if(timeout_ms > 60000)
             {
                 timeout_ms = 60000;
             }
             std::vector<std::string> keys;
             keys.reserve(32);
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
                 }
                 std::string key;
                 if(!read_string(obj, "key", key) || key.empty())
                 {
                     return {.text = "Item missing key", .is_error = true};
                 }
                 keys.push_back(std::move(key));
             }
             const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
             std::vector<char> ready_flags(keys.size(), 0);
             size_t ok_count = 0;
             // Shared-deadline poll: do not spend the full timeout on each key sequentially.
             while(ok_count < keys.size() && std::chrono::steady_clock::now() < deadline)
             {
                 ok_count = 0;
                 for(size_t i = 0; i < keys.size(); ++i)
                 {
                     if(ready_flags[i])
                     {
                         ++ok_count;
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
                     if(try_wait_asset_ready(ctx, keys[i], slice))
                     {
                         ready_flags[i] = 1;
                         ++ok_count;
                     }
                 }
                 if(ok_count < keys.size() && std::chrono::steady_clock::now() < deadline)
                 {
                     tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
                 }
             }
             std::string results = "[";
             bool first = true;
             ok_count = 0;
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 if(ready_flags[i])
                 {
                     ++ok_count;
                 }
                 results += fmt::format(R"({{"key":{},"ready":{}}})",
                                        make_json_string(keys[i]),
                                        ready_flags[i] ? "true" : "false");
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
             if(args["items"].get(items_arr))
             {
                 return {.text = "Missing items array", .is_error = true};
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
                 if(el.get(obj))
                 {
                     return {.text = "Each item must be an object", .is_error = true};
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

