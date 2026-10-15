#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/assets/asset_actions.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/ui/style_sheet.h>
#include <engine/ui/ui_tree.h>
#include <filesystem/filesystem.h>

#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

enum class ui_source_kind
{
    document,
    style_sheet,
};

auto source_extension(ui_source_kind kind) -> std::string
{
    return kind == ui_source_kind::document ? ex::get_format<ui_tree>() : ex::get_format<style_sheet>();
}

/// Extensions of every UI source file: documents and style sheets.
auto ui_source_extensions() -> std::vector<std::string>
{
    return {ex::get_format<ui_tree>(), ex::get_format<style_sheet>()};
}

} // namespace

void register_ui_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "ui_create_batch",
         .description =
             "Create UI source files from the engine's templates: kind document makes an .rhtml page (linking the "
             "engine's base style sheet and a starter one), kind style_sheet an .rcss file. Each item: kind plus "
             "path (the extension is added) or folder + name. Edit them with ui_set_sources_batch; show a "
             "document with a UI Document component whose asset is the .rhtml key.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"kind":{"type":"string","enum":["document","style_sheet"]},"path":{"type":"string"},"folder":{"type":"string"},"name":{"type":"string"}},"required":["kind"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto element : items)
             {
                 ++requested;
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 auto fail = [&](const std::string& key, const std::string& message)
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(message));
                 };

                 simdjson::dom::object item;
                 if(!read_object(element, item, error))
                 {
                     fail({}, error);
                     continue;
                 }
                 std::string kind_name;
                 read_string(item, "kind", kind_name);
                 if(kind_name != "document" && kind_name != "style_sheet")
                 {
                     fail({}, "kind must be document or style_sheet");
                     continue;
                 }
                 const auto kind = kind_name == "document" ? ui_source_kind::document : ui_source_kind::style_sheet;

                 std::string path;
                 std::string folder;
                 std::string name;
                 read_string(item, "path", path);
                 read_string(item, "folder", folder);
                 read_string(item, "name", name);
                 std::string requested = path;
                 if(requested.empty() && !folder.empty() && !name.empty())
                 {
                     requested = folder + "/" + name;
                 }
                 if(requested.empty())
                 {
                     fail({}, "Provide path, or folder + name");
                     continue;
                 }
                 std::string key;
                 if(!to_project_data_file_key(ctx, requested, {source_extension(kind)}, key, error))
                 {
                     fail(requested, error);
                     continue;
                 }
                 fs::error_code ec;
                 const auto absolute = fs::absolute(fs::resolve_protocol(key));
                 if(fs::exists(absolute, ec))
                 {
                     fail(key, "Already exists; edit it with ui_set_sources_batch");
                     continue;
                 }

                 const bool created = kind == ui_source_kind::document
                                          ? asset_actions::create_ui_document(absolute, &error)
                                          : asset_actions::create_style_sheet(absolute, &error);
                 if(!created)
                 {
                     fail(key, error);
                     continue;
                 }
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"key":{},"kind":{}}})",
                                        make_json_string(key),
                                        make_json_string(kind_name));
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         requested),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "ui_get_sources_batch",
         .description = "Read UI source files (.rhtml documents, .rcss style sheets) by key - project "
                        "files or the engine's own under engine:/data/ui, such as the base rml.rcss.",
         .input_schema_json =
             R"json({"type":"object","properties":{"keys":{"type":"array","items":{"type":"string"}}},"required":["keys"]})json",
         .handler =
             [](rtti::context&, const simdjson::dom::object& args) -> tool_result
         {
             std::vector<std::string> keys;
             std::string error;
             if(!read_string_array(args, "keys", keys, error) || keys.empty())
             {
                 return {.text = error.empty() ? "Pass keys" : error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             for(const auto& key : keys)
             {
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 std::string source;
                 std::string read_error;
                 std::string source_key;
                 if(to_readable_file_key(key, ui_source_extensions(), source_key, read_error))
                 {
                     asset_actions::read_text_file(fs::absolute(fs::resolve_protocol(source_key)), source, &read_error);
                 }
                 if(!read_error.empty())
                 {
                     results += fmt::format(R"({{"key":{},"ok":false,"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(read_error));
                     continue;
                 }
                 results += fmt::format(R"({{"key":{},"ok":true,"source":{}}})",
                                        make_json_string(key),
                                        make_json_string(source));
             }
             results += "]";
             return {.text = results, .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "ui_set_sources_batch",
         .description =
             "Write whole UI source files (.rhtml documents, .rcss style sheets) in the project, atomically. They "
             "reimport and documents on screen reload in edit mode; during play a document keeps what it loaded, "
             "so stop play first. RCSS differs from CSS: see the ui skill.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"key":{"type":"string"},"source":{"type":"string"}},"required":["key","source"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             for(auto element : items)
             {
                 ++requested;
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 simdjson::dom::object item;
                 std::string key;
                 std::string source;
                 if(!read_object(element, item, error) || !read_string(item, "key", key) ||
                    !read_string(item, "source", source))
                 {
                     results += R"({"ok":false,"error":"Each item needs key and source"})";
                     continue;
                 }
                 std::string write_error;
                 std::string target;
                 if(!to_project_data_file_key(ctx, key, ui_source_extensions(), target, write_error) ||
                    !asset_actions::write_text_file(fs::absolute(fs::resolve_protocol(target)), source, &write_error))
                 {
                     results += fmt::format(R"({{"ok":false,"key":{},"error":{}}})",
                                            make_json_string(key),
                                            make_json_string(write_error));
                     continue;
                 }
                 key = target;
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"key":{},"bytes":{}}})", make_json_string(key), source.size());
             }
             results += "]";
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{}}})",
                                         results,
                                         ok_count,
                                         requested),
                     .is_error = ok_count == 0};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive,
         .idempotent = true});
}

} // namespace unravel::mcp
