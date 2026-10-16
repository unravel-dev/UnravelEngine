#include "mcp_tools_common.h"
#include "mcp_asset_lookup.h"
#include "mcp_asset_wait.h"
#include "mcp_project_paths.h"

#include "mcp_async.h"
#include "mcp_meta_properties.h"

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
#include <engine/rendering/font.h>
#include <engine/scripting/script.h>
#include <engine/ui/style_sheet.h>
#include <engine/ui/ui_tree.h>
#include <graphics/shader.h>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/physics/physics_material.h>
#include <engine/animation/animation_controller.h>
#include <engine/audio/audio_clip.h>
#include <engine/animation/animation.h>
#include <filesystem/filesystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace unravel::mcp
{
namespace
{

/// One kind of asset a search can filter by, with every file format the engine reads for it.
struct asset_type_entry
{
    const char* name;
    const std::vector<std::string>& (*formats)();
};

const std::array<asset_type_entry, 14> k_asset_types{{
    {"texture", &ex::get_suported_formats<gfx::texture>},
    {"mesh", &ex::get_suported_formats<mesh>},
    {"material", &ex::get_suported_formats<material>},
    {"prefab", &ex::get_suported_formats<prefab>},
    {"scene", &ex::get_suported_formats<scene_prefab>},
    {"audio_clip", &ex::get_suported_formats<audio_clip>},
    {"animation_clip", &ex::get_suported_formats<animation_clip>},
    {"animation_controller", &ex::get_suported_formats<animation_controller>},
    {"physics_material", &ex::get_suported_formats<physics_material>},
    {"script", &ex::get_suported_formats<script>},
    {"ui_document", &ex::get_suported_formats<ui_tree>},
    {"style_sheet", &ex::get_suported_formats<style_sheet>},
    {"font", &ex::get_suported_formats<font>},
    {"shader", &ex::get_suported_formats<gfx::shader>},
}};

/// A type's formats in lower case, each once (".fbx" and ".FBX" are one format).
auto lower_case_formats(const std::vector<std::string>& formats) -> std::vector<std::string>
{
    std::vector<std::string> lower;
    for(const auto& format : formats)
    {
        auto extension = to_lower_ascii(format);
        if(std::find(lower.begin(), lower.end(), extension) == lower.end())
        {
            lower.push_back(std::move(extension));
        }
    }
    return lower;
}

/// The extensions a search's type filter names: a type name ("texture", "mesh", ...) stands for
/// every format of that type, anything else for that one extension ("png", ".pfb").
auto asset_filter_extensions(const std::string& filter) -> std::vector<std::string>
{
    auto name = to_lower_ascii(filter);
    name.erase(0, name.find_first_not_of("*."));
    for(const auto& type : k_asset_types)
    {
        if(name == type.name)
        {
            return lower_case_formats(type.formats());
        }
    }
    return {"." + name};
}

auto has_extension_in(const std::string& key, const std::vector<std::string>& extensions) -> bool
{
    const auto extension = to_lower_ascii(fs::path(key).extension().generic_string());
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

/// The type name of the asset at @p key ("texture", "mesh", ...), or its extension when no listed type reads it.
auto asset_type_name_of(const std::string& key) -> std::string
{
    for(const auto& type : k_asset_types)
    {
        if(has_extension_in(key, lower_case_formats(type.formats())))
        {
            return type.name;
        }
    }
    return to_lower_ascii(fs::path(key).extension().generic_string());
}

/// The asset groups a search's protocol argument names.
auto search_groups(const std::string& protocol, std::vector<std::string>& groups, std::string& error) -> bool
{
    if(protocol.empty() || protocol == "all")
    {
        groups = {"app:/", "engine:/", "editor:/"};
        return true;
    }
    if(protocol != "app" && protocol != "engine" && protocol != "editor")
    {
        error = "protocol must be app, engine, editor or all";
        return false;
    }
    groups = {protocol + ":/"};
    return true;
}

/// The key of an existing file or folder of the project's data folder that @p reference names (a key, or a
/// path relative to app:/data). The data folder itself is not an entry.
auto to_existing_data_entry(rtti::context& ctx, const std::string& reference, std::string& key, std::string& error)
    -> bool
{
    if(!to_project_data_key(ctx, reference, key, error))
    {
        return false;
    }
    fs::error_code ec;
    if(key == k_project_data_root)
    {
        error = "The data folder itself cannot be deleted, moved or copied";
        return false;
    }
    if(!fs::exists(fs::resolve_protocol(key), ec))
    {
        error = "Not found: " + key;
        return false;
    }
    return true;
}

/// Where a move of @p from_key to @p to ends: inside the folder @p to names (an existing folder, or one
/// written with a trailing /) under from's own name; a file's destination without an extension takes from's.
auto to_move_destination(rtti::context& ctx,
                         const std::string& from_key,
                         const std::string& to,
                         std::string& key,
                         std::string& error) -> bool
{
    if(!to_project_data_key(ctx, to, key, error))
    {
        return false;
    }
    const fs::path from_path(from_key);
    fs::error_code ec;
    const bool is_into_folder = to.back() == '/' || to.back() == '\\' || fs::is_directory(fs::resolve_protocol(key), ec);
    if(is_into_folder)
    {
        key = (fs::path(key) / from_path.filename()).generic_string();
    }
    else if(fs::path(key).extension().empty() && !fs::is_directory(fs::resolve_protocol(from_key), ec))
    {
        key += from_path.extension().generic_string();
    }
    return true;
}

/// Waits until the database knows each moved asset - by the id it had before the move - at its new key, or
/// until @p timeout, and says which it knows. It only reads the database: loading the new key before the
/// watcher reported the move would register it as an asset of its own.
auto wait_for_moves(rtti::context& ctx,
                    const std::vector<std::pair<hpp::uuid, std::string>>& moves,
                    std::chrono::milliseconds timeout) -> std::vector<bool>
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<bool> is_known(moves.size(), false);
    while(true)
    {
        for(size_t i = 0; i < moves.size(); ++i)
        {
            is_known[i] = moves[i].first.is_nil() || am.get_metadata(moves[i].first).location == moves[i].second;
        }
        if(std::all_of(is_known.begin(), is_known.end(), std::identity{}) || std::chrono::steady_clock::now() >= deadline)
        {
            return is_known;
        }
        tpp::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
}

/// Longest an import waits for its files to be copied; wait_ms covers only the loads after it.
constexpr auto k_import_copy_timeout = std::chrono::seconds(60);

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

/// One file an import copied in.
struct imported_file
{
    std::string source;
    std::string key;
    /// False for files no asset type reads (a glTF's .bin, a .txt): copied, never loaded.
    bool is_asset{false};
};

/// Every file an import copied in: each item's own key, or each file under an imported directory.
auto list_imported_files(const import_files_result& result) -> std::vector<imported_file>
{
    std::vector<imported_file> files;
    for(const auto& item : result.items)
    {
        for(auto& key : collect_imported_asset_keys(item))
        {
            const bool is_asset = ex::is_known_format(fs::path(key).extension().generic_string());
            files.push_back({.source = item.source_path, .key = std::move(key), .is_asset = is_asset});
        }
    }
    return files;
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

/// One assets_set_import_settings item. The settings travel as text: the main-thread job parses
/// its own copy, so it never reads the request's document after the request has given up on it.
struct import_settings_request
{
    std::string key;
    std::string settings_json;
};

struct import_settings_outcome
{
    std::vector<std::string> applied;
    std::vector<std::string> unknown;
    std::vector<std::string> errors;
};

/// Applies one item to the asset database's import settings and writes them to the .meta file, the
/// way the inspector's Apply does. Edits land on a copy first: a rejected entry leaves the asset as
/// it was. Main thread only, like the inspector.
auto apply_import_settings(rtti::context& ctx, const import_settings_request& request) -> import_settings_outcome
{
    import_settings_outcome outcome;
    auto& am = ctx.get_cached<asset_manager>();
    auto meta = am.get_metadata_for_key(request.key);
    if(!meta.meta.importer)
    {
        outcome.errors.emplace_back(meta.meta.type.empty() ? "Asset not found" : "This asset type has no import settings");
        return outcome;
    }
    simdjson::dom::parser parser;
    simdjson::dom::object settings;
    if(parser.parse(request.settings_json).get(settings) != simdjson::SUCCESS)
    {
        outcome.errors.emplace_back("settings must be an object");
        return outcome;
    }
    auto importer = meta.meta.importer->as_derived();
    entt::meta_any candidate = importer;
    component_apply_result result;
    apply_meta_properties(ctx, candidate, settings, result);
    outcome.unknown = result.unknown;
    for(const auto& message : result.errors)
    {
        // Unknown names are reported on their own, with the tool that lists the valid ones.
        if(message.rfind("unknown properties:", 0) != 0)
        {
            outcome.errors.push_back(message);
        }
    }
    if(!result.ok)
    {
        return outcome;
    }
    if(!importer.assign(candidate))
    {
        outcome.errors.emplace_back("the import settings rejected the edited copy");
        return outcome;
    }
    if(!editor_actions::save_import_settings(request.key, meta.meta))
    {
        outcome.errors.emplace_back("could not write the .meta file");
        return outcome;
    }
    outcome.applied = result.applied;
    return outcome;
}

/// The loaded texture's reflected info (format, size, mips), the inspector's texture view; empty
/// when @p key is not a texture or is not loaded.
auto loaded_texture_json(rtti::context& ctx, asset_manager& am, const std::string& key) -> std::string
{
    if(!matches_asset_extension<gfx::texture>(fs::path(key).extension().generic_string()))
    {
        return {};
    }
    auto handle = am.find_asset<gfx::texture>(key);
    if(!handle || !handle.is_ready())
    {
        return {};
    }
    auto texture = handle.get();
    if(!texture)
    {
        return {};
    }
    auto texture_object = entt::forward_as_meta(*texture);
    return meta_properties_to_json(ctx, texture_object);
}

/// One prefabs_create_from_entities_batch item.
struct prefab_save_item
{
    std::string entity_id;
    std::string path;
    std::string folder;
    std::string name;
    bool attach{false};
};

/// The entity an item saves and the prefab key it saves to; an empty handle and the reason in
/// @p error when either cannot be had.
auto resolve_prefab_save(rtti::context& ctx,
                         scene& scn,
                         const prefab_save_item& item,
                         std::string& key,
                         std::string& error) -> entt::handle
{
    auto entity = find_entity(scn, item.entity_id);
    if(!entity)
    {
        error = "Entity not found";
        return {};
    }
    std::string requested = item.path;
    if(requested.empty() && !item.folder.empty())
    {
        const auto* tag = entity.try_get<tag_component>();
        const auto name = !item.name.empty() ? item.name : tag ? tag->name : std::string("Prefab");
        if(!is_plain_file_name(name))
        {
            error = "'" + name + "' cannot be a file name; pass name or path";
            return {};
        }
        requested = item.folder + "/" + name;
    }
    if(requested.empty())
    {
        error = "Provide path, or folder and optional name";
        return {};
    }
    if(!to_project_data_file_key(ctx, requested, ex::get_suported_formats<prefab>(), key, error))
    {
        return {};
    }
    return entity;
}

} // namespace

void register_asset_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "assets_find_batch",
         .description =
             "Find assets by type (a name from assets_list_types, covering all its formats, or one extension like "
             "\"png\"), key prefix (app:/data/Props/, or relative to app:/data) and/or name_contains, in "
             "protocol (default all). Returns [{key, uid, type}] up to limit (default 200); truncated says more "
             "matched.",
         .input_schema_json =
             R"json({"type":"object","properties":{"protocol":{"type":"string","enum":["app","engine","editor","all"],"default":"all"},"type":{"type":"string"},"prefix":{"type":"string"},"name_contains":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":5000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string protocol;
             read_string(args, "protocol", protocol);
             std::vector<std::string> groups;
             std::string error;
             if(!search_groups(protocol, groups, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string type_filter;
             read_string(args, "type", type_filter);
             const auto extensions = type_filter.empty() ? std::vector<std::string>{} : asset_filter_extensions(type_filter);
             std::string prefix;
             read_string(args, "prefix", prefix);
             prefix = prefix.empty() ? prefix : to_asset_key(prefix);
             std::string name_contains;
             read_string(args, "name_contains", name_contains);
             const auto limit = static_cast<size_t>(std::clamp<int64_t>(read_int64_or(args, "limit", 200), 1, 5000));
             auto& am = ctx.get_cached<asset_manager>();
             // Filters run on the key alone; the metadata lookup (a scan) is spent only on what is returned.
             std::vector<std::string> keys;
             bool truncated = false;
             for(const auto& group : groups)
             {
                 for(const auto& key : am.get_all_assets(group))
                 {
                     if((!prefix.empty() && !starts_with(key, prefix)) ||
                        (!name_contains.empty() && !contains_ci(fs::path(key).filename().generic_string(), name_contains)) ||
                        (!extensions.empty() && !has_extension_in(key, extensions)))
                     {
                         continue;
                     }
                     if(keys.size() == limit)
                     {
                         truncated = true;
                         break;
                     }
                     keys.push_back(key);
                 }
                 if(truncated)
                 {
                     break;
                 }
             }
             std::string json = "[";
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 const auto meta = am.get_metadata_for_key(keys[i]);
                 json += fmt::format(R"({}{{"key":{},"uid":{},"type":{}}})",
                                     i == 0 ? "" : ",",
                                     make_json_string(keys[i]),
                                     make_json_string(hpp::to_string(meta.meta.uid)),
                                     make_json_string(asset_type_name_of(keys[i])));
             }
             json += "]";
             return {.text = fmt::format(R"({{"assets":{},"count":{},"truncated":{},"limit":{}}})",
                                         json,
                                         keys.size(),
                                         bool_to_json(truncated),
                                         limit)};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_list_types",
         .description = "List the asset type names assets_find_batch filters by, each with the file "
                        "extensions the engine reads for it.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context&, const simdjson::dom::object&) -> tool_result
         {
             std::string json = "[";
             for(const auto& type : k_asset_types)
             {
                 json += fmt::format(R"({}{{"type":{},"extensions":{}}})",
                                     json.size() > 1 ? "," : "",
                                     make_json_string(type.name),
                                     strings_to_json_array(lower_case_formats(type.formats())));
             }
             return {.text = json + "]"};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_list_embedded_primitives",
         .description = "List the engine's built-in meshes by name, for scene_create_batch kind primitive "
                        "(cube is 1 m, centred on the origin).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             constexpr const char* k_embedded_prefix = "engine:/embedded/";
             std::vector<std::string> names;
             for(const auto& handle : ctx.get_cached<asset_manager>().get_assets<mesh>(k_embedded_prefix))
             {
                 // The list starts with the empty handle ("None") pickers offer.
                 if(handle && starts_with(handle.id(), k_embedded_prefix))
                 {
                     names.push_back(handle.id().substr(std::strlen(k_embedded_prefix)));
                 }
             }
             return {.text = strings_to_json_array(names)};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_create_folder",
         .description =
             "Create a folder in the project's data folder (content-browser parity). path is an "
             "app:/data/... key or a path relative to app:/data (e.g. Props/Crates); or folder + name.",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             std::string path;
             std::string folder;
             std::string name;
             read_string(args, "path", path);
             read_string(args, "folder", folder);
             read_string(args, "name", name);

             if(path.empty() && !folder.empty() && !name.empty())
             {
                 if(!is_plain_file_name(name))
                 {
                     return {.text = "name must be a single folder name: " + name, .is_error = true};
                 }
                 path = folder + "/" + name;
             }
             if(path.empty())
             {
                 return {.text = "Provide path, or folder + name", .is_error = true};
             }

             auto create_result = mcp.invoke_on_main(
                 [&]() -> tool_result
                 {
                     std::string key;
                     std::string error;
                     if(!to_project_data_key(ctx, path, key, error))
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
             return *create_result;
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .idempotent = true});

    registry.add(
        {.name = "assets_delete_batch",
         .description =
             "Delete assets or folders from the project's data folder, recoverably: each asset moves with "
             "its .meta into one batch of the project's trash (editor/trash), and assets_restore_deleted "
             "puts the batch back with the same ids. Until then entities and assets that used them hold "
             "empty references. assets: keys or paths relative to app:/data; a folder takes everything "
             "in it.",
         .input_schema_json =
             R"json({"type":"object","properties":{"assets":{"type":"array","items":{"type":"string"},"minItems":1}},"required":["assets"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             std::vector<std::string> references;
             if(!require_not_play_mode(ctx, error) || !read_string_array(args, "assets", references, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<fs::path> paths;
             std::vector<std::string> keys;
             for(const auto& reference : references)
             {
                 std::string key;
                 if(!to_existing_data_entry(ctx, reference, key, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 keys.push_back(key);
                 paths.push_back(fs::absolute(fs::resolve_protocol(key)));
             }
             std::string batch;
             const bool is_deleted = asset_actions::move_to_trash(paths, batch, &error);
             return {.text = fmt::format(R"({{"ok":{},"trash":{},"deleted":{}{}}})",
                                         bool_to_json(is_deleted),
                                         make_json_string(batch),
                                         strings_to_json_array(keys),
                                         is_deleted ? "" : R"(,"error":)" + make_json_string(error)),
                     .is_error = !is_deleted};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "assets_restore_deleted",
         .description =
             "Put a batch that assets_delete_batch moved to the project's trash back where it was, each "
             "asset with its .meta, so it keeps its id and references to it work again. Without trash: "
             "lists the trash's batches, newest first.",
         .input_schema_json =
             R"json({"type":"object","properties":{"trash":{"type":"string","description":"A batch name assets_delete_batch returned"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string batch;
             if(!read_string(args, "trash", batch) || batch.empty())
             {
                 return {.text = fmt::format(R"({{"batches":{}}})", strings_to_json_array(asset_actions::list_trash_batches())),
                         .is_error = false};
             }
             std::vector<fs::path> restored;
             const bool is_restored = asset_actions::restore_from_trash(batch, restored, &error);
             std::vector<std::string> keys;
             for(const auto& path : restored)
             {
                 keys.push_back(fs::convert_to_protocol(path).generic_string());
             }
             return {.text = fmt::format(R"({{"ok":{},"restored":{}{}}})",
                                         bool_to_json(is_restored),
                                         strings_to_json_array(keys),
                                         is_restored ? "" : R"(,"error":)" + make_json_string(error)),
                     .is_error = !is_restored};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "assets_move_batch",
         .description =
             "Rename or move assets and folders inside the project's data folder; each keeps its id, so "
             "references follow. Each item: from and to (keys or paths relative to app:/data); a to that is a "
             "folder, or ends with /, receives from under its own name, and a file's to may leave out the "
             "extension. A C# script whose class has the file's name is renamed along. Waits up to wait_ms "
             "(default 5000) for each moved asset to load under its new key.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"from":{"type":"string"},"to":{"type":"string"}},"required":["from","to"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 5000);
             simdjson::dom::array items;
             std::string error;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<std::pair<std::string, std::string>> moves;
             for(auto element : items)
             {
                 simdjson::dom::object item;
                 auto& move = moves.emplace_back();
                 if(!read_object(element, item, error) || !read_string(item, "from", move.first) ||
                    !read_string(item, "to", move.second))
                 {
                     return {.text = "Each item needs from and to", .is_error = true};
                 }
             }
             struct outcome_t
             {
                 std::string from;
                 std::string to;
                 hpp::uuid uid;
                 std::string error;
             };
             auto outcomes = mcp.invoke_on_main(
                 [&ctx, moves]() -> std::vector<outcome_t>
                 {
                     std::vector<outcome_t> results;
                     std::string error;
                     if(!require_not_play_mode(ctx, error))
                     {
                         return std::vector<outcome_t>(moves.size(), outcome_t{.error = error});
                     }
                     for(const auto& [from, to] : moves)
                     {
                         auto& outcome = results.emplace_back();
                         if(!to_existing_data_entry(ctx, from, outcome.from, outcome.error) ||
                            !to_move_destination(ctx, outcome.from, to, outcome.to, outcome.error))
                         {
                             continue;
                         }
                         outcome.uid = ctx.get_cached<asset_manager>().get_metadata_for_key(outcome.from).meta.uid;
                         asset_actions::rename_entry(fs::absolute(fs::resolve_protocol(outcome.from)),
                                                     fs::absolute(fs::resolve_protocol(outcome.to)),
                                                     &outcome.error);
                     }
                     return results;
                 });
             if(!outcomes)
             {
                 return {.text = "Timed out moving assets on the main thread", .is_error = true};
             }
             std::vector<std::pair<hpp::uuid, std::string>> moved;
             for(const auto& outcome : *outcomes)
             {
                 moved.emplace_back(outcome.error.empty() ? outcome.uid : hpp::uuid{}, outcome.to);
             }
             const auto deadline = std::chrono::steady_clock::now() + wait_ms;
             const auto is_known = wait_for_moves(ctx, moved, wait_ms);
             // Known under its new key, a moved file is then waited for until it loads: a move can make it compile
             // again (one that was still compiling when it moved).
             std::vector<std::string> keys;
             std::vector<bool> is_loaded_waited;
             for(size_t i = 0; i < moved.size(); ++i)
             {
                 keys.push_back(moved[i].second);
                 is_loaded_waited.push_back(is_known[i] && !moved[i].first.is_nil());
             }
             const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
             const auto report = wait_for_assets(ctx, keys, is_loaded_waited, std::max(remaining, std::chrono::milliseconds(1)));
             std::string results = "[";
             size_t ok_count = 0;
             for(size_t i = 0; i < outcomes->size(); ++i)
             {
                 const auto& outcome = (*outcomes)[i];
                 ok_count += outcome.error.empty() ? 1 : 0;
                 const auto state = !outcome.error.empty() ? std::string{}
                                    : !is_known[i]         ? std::string(R"(,"state":"moving")")
                                    : is_loaded_waited[i]  ? report.state_fields[i]
                                                           : std::string(R"(,"state":"ready")");
                 results += fmt::format(R"({}{{"ok":{},"from":{},"to":{}{}{}}})",
                                        i > 0 ? "," : "",
                                        bool_to_json(outcome.error.empty()),
                                        make_json_string(outcome.from.empty() ? moves[i].first : outcome.from),
                                        make_json_string(outcome.to.empty() ? moves[i].second : outcome.to),
                                        state,
                                        outcome.error.empty() ? "" : R"(,"error":)" + make_json_string(outcome.error));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, outcomes->size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_duplicate_batch",
         .description =
             "Copy assets or folders next to themselves under the first free \"name (n)\" name; each copy gets an "
             "id of its own. assets: keys or paths relative to app:/data. Waits up to wait_ms (default 15000; 0: "
             "no wait) for the copies to load.",
         .input_schema_json =
             R"json({"type":"object","properties":{"assets":{"type":"array","items":{"type":"string"},"minItems":1},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["assets"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_wait_ms(args, 15000);
             std::vector<std::string> references;
             std::string error;
             if(!read_string_array(args, "assets", references, error))
             {
                 return {.text = error, .is_error = true};
             }
             struct outcome_t
             {
                 std::string source;
                 std::string copy;
                 std::string error;
             };
             auto outcomes = mcp.invoke_on_main(
                 [&ctx, references]() -> std::vector<outcome_t>
                 {
                     std::vector<outcome_t> results;
                     for(const auto& reference : references)
                     {
                         auto& outcome = results.emplace_back();
                         if(!to_existing_data_entry(ctx, reference, outcome.source, outcome.error))
                         {
                             continue;
                         }
                         const auto copy = asset_actions::duplicate_entry(fs::absolute(fs::resolve_protocol(outcome.source)),
                                                                          &outcome.error);
                         outcome.copy = copy.empty() ? std::string{} : fs::convert_to_protocol(copy).generic_string();
                     }
                     return results;
                 });
             if(!outcomes)
             {
                 return {.text = "Timed out duplicating assets on the main thread", .is_error = true};
             }
             std::vector<std::string> keys;
             std::vector<bool> is_copied_file;
             for(const auto& outcome : *outcomes)
             {
                 fs::error_code ec;
                 keys.push_back(outcome.copy);
                 is_copied_file.push_back(outcome.error.empty() && fs::is_regular_file(fs::resolve_protocol(outcome.copy), ec));
             }
             const auto report = wait_for_assets(ctx, keys, is_copied_file, wait_ms);
             std::string results = "[";
             size_t ok_count = 0;
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 const auto& outcome = (*outcomes)[i];
                 ok_count += outcome.error.empty() ? 1 : 0;
                 results += fmt::format(R"({}{{"ok":{},"source":{},"copy":{}{}{}}})",
                                        i > 0 ? "," : "",
                                        bool_to_json(outcome.error.empty()),
                                        make_json_string(outcome.source.empty() ? references[i] : outcome.source),
                                        make_json_string(outcome.copy),
                                        report.state_fields[i],
                                        outcome.error.empty() ? "" : R"(,"error":)" + make_json_string(outcome.error));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})", results, ok_count, keys.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "assets_import_files",
         .description =
             "Import absolute paths of files or directories outside the project into folder (an app:/data/... key "
             "or a path relative to app:/data). Directories copy recursively: pass one directory per multi-file "
             "model (.gltf + .bin + textures). Waits up to wait_ms (default 15000; 0: no wait) for the assets to "
             "load, each with a state (ready, loading, failed); a file copied over an existing asset is ready "
             "once its reload has replaced the old one. Files no asset type reads (a .bin, a .txt) are copied and "
             "listed with asset:false.",
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
             std::string requested_folder;
             std::string folder;
             if(!read_string(args, "folder", requested_folder) || !to_project_data_key(ctx, requested_folder, folder, error))
             {
                 return {.text = error.empty() ? "Missing folder" : error, .is_error = true};
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
             const auto overwritten =
                 take_asset_snapshot(ctx,
                                     collect_overwritten_keys(editor_actions::resolve_import_items(paths, target_path)));
             auto import_result = editor_actions::import_files(ctx, paths, target_path, true);
             const bool copied = editor_actions::wait_import_jobs(import_result, k_import_copy_timeout);
             const auto files = list_imported_files(import_result);
             std::vector<std::string> keys;
             std::vector<bool> is_asset;
             for(const auto& file : files)
             {
                 keys.push_back(file.key);
                 is_asset.push_back(file.is_asset);
             }
             const auto asset_count = static_cast<size_t>(std::count(is_asset.begin(), is_asset.end(), true));
             const auto report = wait_for_assets(ctx, keys, is_asset, copied ? wait_ms : std::chrono::milliseconds(0), overwritten);
             std::string results = "[";
             for(size_t i = 0; i < files.size(); ++i)
             {
                 results += fmt::format(R"({}{{"source":{},"key":{},"asset":{}{}}})",
                                        i > 0 ? "," : "",
                                        make_json_string(files[i].source),
                                        make_json_string(files[i].key),
                                        bool_to_json(files[i].is_asset),
                                        report.state_fields[i]);
             }
             results += "]";
             const bool ok = copied && !files.empty() && (!report.waited || report.ready_count == asset_count);
             return {.text = fmt::format(
                         R"({{"folder":{},"copied":{},"results":{},"assets":{},"ready":{},"other_files":{},"waited":{}}})",
                         make_json_string(folder),
                         bool_to_json(copied),
                         results,
                         asset_count,
                         report.ready_count,
                         files.size() - asset_count,
                         bool_to_json(report.waited)),
                     .is_error = !ok};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "assets_get_mesh_info",
         .description =
             "A mesh asset's local bounds (min, max, center, extents) by key: its pivot and units, to place it on "
             "a floor and pick its scale before creating it.",
         .input_schema_json = R"json({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string key;
             if(!read_string(args, "key", key) || key.empty())
             {
                 return {.text = "Missing key", .is_error = true};
             }
             std::string error;
             auto handle = find_existing_asset<mesh>(ctx, to_asset_key(key), error);
             if(!handle)
             {
                 return {.text = error, .is_error = true};
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_get_batch",
         .description =
             "Look assets up by key or uid: each reports its uid, location (key) and type - to turn a uid from a "
             "property into a key, or to check that a key exists.",
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

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
                 std::string requested;
                 std::string key;
                 if(!read_string(obj, "key", requested) || !to_project_data_key(ctx, requested, key, error))
                 {
                     return {.text = error.empty() ? "Item missing key" : error, .is_error = true};
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
             std::vector<bool> is_reimported;
             for(const auto& key_error : *errors)
             {
                 is_reimported.push_back(key_error.empty());
             }
             const auto ok_count = static_cast<size_t>(std::count(is_reimported.begin(), is_reimported.end(), true));
             const auto report = wait_for_assets(ctx, keys, is_reimported, wait_ms, previous);
             std::string results = "[";
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 results += i > 0 ? "," : "";
                 if(!is_reimported[i])
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                            make_json_string(keys[i]),
                                            make_json_string((*errors)[i]));
                     continue;
                 }
                 results += fmt::format(R"({{"ok":true,"key":{},"reimported":true{}}})",
                                        make_json_string(keys[i]),
                                        report.state_fields[i]);
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"ready":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         report.ready_count,
                                         keys.size()),
                     .is_error = ok_count == 0 || (report.waited && report.ready_count != ok_count)};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .idempotent = true});

    registry.add(
        {.name = "assets_get_import_settings",
         .description =
             "Read assets' import settings by key - what the .meta beside each asset stores (textures: type, "
             "colorspace, generate_mipmaps, quality.max_size, quality.compression; meshes, animations and audio "
             "have their own). 'project_default' values follow the project's settings (settings_get group "
             "assets). A loaded texture also reports its compiled format, size and mips; 'schemas' describes each "
             "importer type once.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"type":"string"}}},"required":["keys"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             std::vector<std::string> keys;
             if(!read_string_array(args, "keys", keys, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(keys.empty())
             {
                 return {.text = "keys array is empty", .is_error = true};
             }
             auto& am = ctx.get_cached<asset_manager>();
             std::map<std::string, std::string> schemas;
             std::string results = "[";
             size_t ok_count = 0;
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 const auto& key = keys[i];
                 results += i > 0 ? "," : "";
                 const auto meta = am.get_metadata_for_key(key);
                 if(!meta.meta.importer)
                 {
                     const bool exists = !meta.location.empty() || !meta.meta.type.empty();
                     results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(exists ? "This asset type has no import settings"
                                                                    : "Asset not found"));
                     continue;
                 }
                 ++ok_count;
                 auto importer = meta.meta.importer->as_derived();
                 const auto importer_name = entt::get_name(importer.type());
                 if(schemas.count(importer_name) == 0)
                 {
                     schemas.emplace(importer_name, "{" + meta_object_schema(importer.type()) + "}");
                 }
                 const auto texture_json = loaded_texture_json(ctx, am, key);
                 results += fmt::format(R"({{"ok":true,"key":{},"importer":{},"settings":{}{}}})",
                                        make_json_string(key),
                                        make_json_string(importer_name),
                                        meta_properties_to_json(ctx, importer),
                                        texture_json.empty() ? "" : ",\"texture\":" + texture_json);
             }
             results += "]";
             std::string schemas_json = "{";
             for(const auto& [name, schema] : schemas)
             {
                 schemas_json += (schemas_json.size() > 1 ? "," : "") + make_json_string(name) + ":" + schema;
             }
             schemas_json += "}";
             return {.text = fmt::format(R"({{"results":{},"schemas":{},"count":{},"requested":{}}})",
                                         results,
                                         schemas_json,
                                         ok_count,
                                         keys.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "assets_set_import_settings",
         .description =
             "Change import settings and wait until each asset is imported again with them (wait_ms, one "
             "deadline, default 15000; 0 returns at once). Each item: key and settings, a partial object in the "
             "shape assets_get_import_settings returns ({\"quality\":{\"compression\":\"none\"}}, "
             "{\"colorspace\":\"srgb\"}). Writes the .meta file; all-or-nothing per item.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"settings":{"type":"object"}},"required":["key","settings"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":60000}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_import_timeout_ms(args, 15000);
             simdjson::dom::array items_arr;
             std::string error;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<import_settings_request> requests;
             std::vector<std::string> keys;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 import_settings_request request;
                 simdjson::dom::object settings;
                 std::string requested;
                 if(!read_string(obj, "key", requested) || !to_project_data_key(ctx, requested, request.key, error))
                 {
                     return {.text = error.empty() ? "Item missing key" : error, .is_error = true};
                 }
                 if(obj["settings"].get_object().get(settings) != simdjson::SUCCESS)
                 {
                     return {.text = "Item missing a settings object: " + request.key, .is_error = true};
                 }
                 request.settings_json = simdjson::minify(settings);
                 keys.push_back(request.key);
                 requests.push_back(std::move(request));
             }
             if(requests.empty())
             {
                 return {.text = "items array is empty", .is_error = true};
             }
             const auto previous = take_asset_snapshot(ctx, keys);
             auto outcomes = mcp.invoke_on_main(
                 [&ctx, requests]() -> std::vector<import_settings_outcome>
                 {
                     std::vector<import_settings_outcome> results;
                     results.reserve(requests.size());
                     for(const auto& request : requests)
                     {
                         results.push_back(apply_import_settings(ctx, request));
                     }
                     return results;
                 });
             if(!outcomes)
             {
                 return {.text = "Timed out applying import settings on the main thread", .is_error = true};
             }
             std::vector<bool> is_applied;
             for(const auto& outcome : *outcomes)
             {
                 is_applied.push_back(outcome.errors.empty() && outcome.unknown.empty());
             }
             const auto ok_count = static_cast<size_t>(std::count(is_applied.begin(), is_applied.end(), true));
             const auto report = wait_for_assets(ctx, keys, is_applied, wait_ms, previous);
             std::string results = "[";
             for(size_t i = 0; i < keys.size(); ++i)
             {
                 results += i > 0 ? "," : "";
                 const auto& outcome = (*outcomes)[i];
                 if(!is_applied[i])
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"errors":{},"unknown":{}}})",
                                            make_json_string(keys[i]),
                                            strings_to_json_array(outcome.errors),
                                            strings_to_json_array(outcome.unknown));
                     continue;
                 }
                 results += fmt::format(R"({{"ok":true,"key":{},"applied":{}{}}})",
                                        make_json_string(keys[i]),
                                        strings_to_json_array(outcome.applied),
                                        report.state_fields[i]);
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"ready":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         report.ready_count,
                                         keys.size()),
                     .is_error = ok_count != keys.size() || (report.waited && report.ready_count != ok_count)};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "assets_recompile_textures",
         .description =
             "Import every project texture whose key starts with prefix (default app:/data) again - after "
             "changing the settings assets.texture, the defaults of textures left on 'project_default'. wait_ms "
             "(default 0) waits for each to reload; waiting loads every matching texture first.",
         .input_schema_json =
             R"json({"type":"object","properties":{"prefix":{"type":"string"},"wait_ms":{"type":"integer","minimum":0,"maximum":60000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             const auto wait_ms = read_import_timeout_ms(args, 0);
             std::string requested_prefix = k_project_data_root;
             read_string(args, "prefix", requested_prefix);
             std::string prefix;
             std::string error;
             if(!to_project_data_key(ctx, requested_prefix, prefix, error))
             {
                 return {.text = error, .is_error = true};
             }
             auto keys = mcp.invoke_on_main(
                 [&ctx, prefix]() -> std::vector<std::string>
                 {
                     std::vector<std::string> texture_keys;
                     for(const auto& handle : ctx.get_cached<asset_manager>().get_assets<gfx::texture>(prefix))
                     {
                         // The list starts with the empty handle ("None") pickers offer.
                         if(handle)
                         {
                             texture_keys.push_back(handle.id());
                         }
                     }
                     return texture_keys;
                 });
             if(!keys)
             {
                 return {.text = "Timed out listing textures on the main thread", .is_error = true};
             }
             const bool should_wait = wait_ms.count() > 0;
             const auto previous = should_wait ? take_asset_snapshot(ctx, *keys) : asset_snapshot{};
             const auto started = mcp.invoke_on_main(
                 [prefix]() -> bool
                 {
                     editor_actions::recompile_textures(prefix);
                     return true;
                 });
             if(!started)
             {
                 return {.text = "Timed out starting the recompile on the main thread", .is_error = true};
             }
             const auto states = should_wait ? wait_assets_ready(ctx, *keys, wait_ms, previous) : std::vector<asset_readiness>{};
             std::vector<std::string> pending;
             for(size_t i = 0; i < states.size(); ++i)
             {
                 if(states[i] != asset_readiness::ready)
                 {
                     pending.push_back((*keys)[i]);
                 }
             }
             const size_t ready_count = states.size() - pending.size();
             return {.text = fmt::format(R"({{"prefix":{},"textures":{},"waited":{},"ready":{},"not_ready":{}}})",
                                         make_json_string(prefix),
                                         keys->size(),
                                         bool_to_json(should_wait),
                                         ready_count,
                                         strings_to_json_array(pending)),
                     .is_error = should_wait && !pending.empty()};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .idempotent = true});

    registry.add(
        {.name = "assets_wait_ready_batch",
         .description =
             "Wait until assets are loaded and usable, starting loads nothing has asked for. Each key reports "
             "ready, loading (still compiling or loading at the deadline), failed (see logs_get_recent) or "
             "missing (no such file). timeout_ms is one deadline for the whole batch (default 15000); 0 reports "
             "each key's state now.",
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
                 keys.push_back(to_asset_key(key));
             }
             const auto states = wait_assets_ready(ctx, keys, std::chrono::milliseconds(timeout_ms));
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
         .requires_main_thread = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "prefabs_create_from_entities_batch",
         .description =
             "Save entity hierarchies as prefab assets in the project's data folder. Each item: entity_id; path "
             "(.pfb is added) or folder (+ optional name, default the entity's name); attach:true makes the "
             "entity an instance of the new prefab. A prefab already at the path is overwritten. Waits up to "
             "wait_ms (default 15000; 0: no wait) for the prefabs to load, each with a state.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"},"attach":{"type":"boolean"}},"required":["entity_id"]}},"wait_ms":{"type":"integer","minimum":0,"maximum":15000}},"required":["items"]})json",
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
             struct outcome_t
             {
                 std::string key;
                 std::string uid;
                 std::string error;
             };
             std::vector<prefab_save_item> items;
             for(auto el : items_arr)
             {
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 prefab_save_item item{};
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
             // A prefab saved over an existing one reloads into the same handle, and until it has, edits to
             // its instances are measured against the old content. So what each key holds is kept first,
             // and the wait lasts until the key holds something else.
             auto targets = mcp.invoke_on_main(
                 [&ctx, items]() -> std::vector<std::string>
                 {
                     std::vector<std::string> keys;
                     scene* scn = nullptr;
                     std::string error;
                     const bool has_scene = require_edit_scene(ctx, scn, error);
                     for(const auto& item : items)
                     {
                         auto& key = keys.emplace_back();
                         if(!has_scene || !resolve_prefab_save(ctx, *scn, item, key, error))
                         {
                             key.clear();
                         }
                     }
                     return keys;
                 });
             if(!targets)
             {
                 return {.text = "Timed out creating prefabs on main thread", .is_error = true};
             }
             const auto previous = take_asset_snapshot(ctx, *targets);
             auto outcomes = mcp.invoke_on_main(
                 [&ctx, items]() -> std::vector<outcome_t>
                 {
                     scene* scn = nullptr;
                     std::string error;
                     if(!require_edit_scene(ctx, scn, error))
                     {
                         return std::vector<outcome_t>(items.size(), outcome_t{.error = error});
                     }
                     std::vector<outcome_t> created;
                     for(const auto& item : items)
                     {
                         auto& outcome = created.emplace_back();
                         auto entity = resolve_prefab_save(ctx, *scn, item, outcome.key, outcome.error);
                         if(!entity)
                         {
                             continue;
                         }
                         fs::error_code ec;
                         const auto absolute = fs::absolute(fs::resolve_protocol(outcome.key));
                         fs::create_directories(absolute.parent_path(), ec);
                         auto prefab_handle = editor_actions::save_entity_as_prefab(ctx, entity, absolute, item.attach);
                         if(!prefab_handle)
                         {
                             outcome.error = "Failed to save prefab " + outcome.key;
                             continue;
                         }
                         outcome.uid = hpp::to_string(prefab_handle.uid());
                     }
                     return created;
                 });
             if(!outcomes)
             {
                 return {.text = "Timed out creating prefabs on main thread", .is_error = true};
             }
             std::vector<std::string> keys;
             std::vector<bool> is_created;
             for(const auto& outcome : *outcomes)
             {
                 keys.push_back(outcome.key);
                 is_created.push_back(outcome.error.empty());
             }
             const auto report = wait_for_assets(ctx, keys, is_created, wait_ms, previous);
             std::string results = "[";
             for(size_t i = 0; i < items.size(); ++i)
             {
                 const auto& outcome = (*outcomes)[i];
                 results += i > 0 ? "," : "";
                 if(!is_created[i])
                 {
                     results += fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})",
                                            make_json_string(items[i].entity_id),
                                            make_json_string(outcome.error));
                     continue;
                 }
                 results += fmt::format(R"({{"ok":true,"key":{},"uid":{},"entity_id":{},"attached":{}{}}})",
                                        make_json_string(outcome.key),
                                        make_json_string(outcome.uid),
                                        make_json_string(items[i].entity_id),
                                        bool_to_json(items[i].attach),
                                        report.state_fields[i]);
             }
             results += "]";
             const auto ok_count = static_cast<size_t>(std::count(is_created.begin(), is_created.end(), true));
             return {.text = fmt::format(R"({{"results":{},"count":{},"ready":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         report.ready_count,
                                         items.size()),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = true,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});
}

} // namespace unravel::mcp

