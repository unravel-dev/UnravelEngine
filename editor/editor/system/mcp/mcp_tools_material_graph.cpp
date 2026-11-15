#include "mcp_asset_lookup.h"
#include "mcp_tools_common.h"

#include <asset_compiler/asset_compiler.h>
#include <editor/assets/asset_actions.h>
#include <engine/assets/asset_manager.h>
#include <engine/meta/rendering/material_graph.hpp>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/rendering/material_graph/material_graph_library.h>

#include <filesystem/filesystem.h>
#include <graphics/graphics.h>

#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{
/// The key of the graph's object in a graph document ({"material_graph": {...}}).
constexpr const char* GRAPH_DOCUMENT_KEY = "material_graph";
/// The name the GPU check compiles the code under: its fs_ prefix gives shaderc the stage.
constexpr const char* GPU_CHECK_SHADER_NAME = "fs_mcp_material_graph.eshader";
constexpr const char* GPU_CHECK_VERTEX_SHADER_NAME = "vs_mcp_material_graph.eshader";
constexpr const char* GPU_CHECK_FOLDER = "unravel-mcp-material-graph";

auto vec4_to_json(const math::vec4& value) -> std::string
{
    return fmt::format("[{},{},{},{}]", value.x, value.y, value.z, value.w);
}

auto get_severity_name(material_graph_severity severity) -> const char*
{
    switch(severity)
    {
        case material_graph_severity::error:
            return "error";
        case material_graph_severity::warning:
            return "warning";
        case material_graph_severity::info:
        default:
            return "info";
    }
}

auto get_setting_kind_name(material_setting_kind kind) -> const char*
{
    switch(kind)
    {
        case material_setting_kind::choice:
            return "choice";
        case material_setting_kind::number:
            return "number";
        case material_setting_kind::text:
        default:
            return "text";
    }
}

/// A node type in a line: what the library lists.
auto describe_node_brief(const material_node_def& def) -> std::string
{
    return fmt::format(R"({{"type":{},"label":{},"category":{},"description":{}}})",
                       make_json_string(def.type),
                       make_json_string(def.label),
                       make_json_string(get_material_node_category_name(def.category)),
                       make_json_string(def.description));
}

auto describe_pin(const material_pin_def& pin, bool is_input) -> std::string
{
    std::string json = fmt::format(R"({{"name":{},"type":{})",
                                   make_json_string(pin.name),
                                   make_json_string(get_material_value_type_name(pin.type)));
    if(!pin.label.empty() && pin.label != pin.name)
    {
        json += fmt::format(R"(,"label":{})", make_json_string(pin.label));
    }
    if(is_input)
    {
        json += fmt::format(R"(,"default":{})", vec4_to_json(pin.default_value));
        if(!pin.default_code.empty())
        {
            json += fmt::format(R"(,"unlinked_reads":{})", make_json_string(pin.default_code));
        }
        if(pin.is_value_only)
        {
            json += R"(,"value_only":true)";
        }
        if(pin.is_color)
        {
            json += R"(,"color":true)";
        }
        if(pin.is_hidden)
        {
            json += R"(,"hidden":true)";
        }
    }
    if(!pin.tooltip.empty())
    {
        json += fmt::format(R"(,"tooltip":{})", make_json_string(pin.tooltip));
    }
    return json + "}";
}

auto describe_setting(const material_setting_def& setting) -> std::string
{
    std::string json = fmt::format(R"({{"name":{},"kind":{},"default":{})",
                                   make_json_string(setting.name),
                                   make_json_string(get_setting_kind_name(setting.kind)),
                                   make_json_string(setting.default_value));
    if(!setting.choices.empty())
    {
        json += fmt::format(R"(,"choices":{})", strings_to_json_array(setting.choices));
    }
    if(!setting.tooltip.empty())
    {
        json += fmt::format(R"(,"tooltip":{})", make_json_string(setting.tooltip));
    }
    return json + "}";
}

/// A node type in full: its pins and settings, what a graph names them by.
auto describe_node_full(const material_node_def& def) -> std::string
{
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> settings;
    for(const auto& pin : def.inputs)
    {
        inputs.push_back(describe_pin(pin, true));
    }
    for(const auto& pin : def.outputs)
    {
        outputs.push_back(describe_pin(pin, false));
    }
    for(const auto& setting : def.settings)
    {
        settings.push_back(describe_setting(setting));
    }
    std::string brief = describe_node_brief(def);
    brief.pop_back();
    return fmt::format(R"({},"inputs":[{}],"outputs":[{}],"settings":[{}]{}}})",
                       brief,
                       fmt::join(inputs, ","),
                       fmt::join(outputs, ","),
                       fmt::join(settings, ","),
                       def.is_pixel_only ? R"(,"pixel_stage_only":true)" : "");
}

/// What compiling a graph found: its problems, the parameters and textures a material instance sets by name, and the
/// generated code when asked.
auto describe_compile(const material_graph_compile_result& result, bool include_code) -> std::string
{
    std::vector<std::string> diagnostics;
    size_t errors = 0;
    size_t warnings = 0;
    for(const auto& diagnostic : result.diagnostics)
    {
        errors += diagnostic.severity == material_graph_severity::error ? 1 : 0;
        warnings += diagnostic.severity == material_graph_severity::warning ? 1 : 0;
        diagnostics.push_back(fmt::format(R"({{"severity":{},"node":{},"pin":{},"message":{}}})",
                                          make_json_string(get_severity_name(diagnostic.severity)),
                                          make_json_string(diagnostic.node_id),
                                          make_json_string(diagnostic.pin),
                                          make_json_string(diagnostic.message)));
    }
    std::vector<std::string> parameters;
    // A graph whose vertex shader computes values (an offset, interpolators) never shares a draw: that shader reads the
    // material's own values.
    const bool can_share_draw = !result.computes_vertex_values();
    for(const auto& parameter : result.parameters)
    {
        parameters.push_back(fmt::format(R"({{"name":{},"node":{},"type":{},"default":{},"register":{},"shares_draw":{}}})",
                                         make_json_string(parameter.name),
                                         make_json_string(parameter.node_id),
                                         make_json_string(get_material_value_type_name(parameter.type)),
                                         vec4_to_json(parameter.default_value),
                                         parameter.register_index,
                                         bool_to_json(can_share_draw && parameter.register_index < material::MAX_INSTANCE_CONSTANTS)));
    }
    std::vector<std::string> textures;
    for(const auto& texture : result.textures)
    {
        textures.push_back(fmt::format(R"({{"node":{},"texture":{},"kind":{}}})",
                                       make_json_string(texture.name),
                                       texture.texture ? make_json_string(texture.texture.id()) : std::string("null"),
                                       make_json_string(get_material_value_type_name(texture.kind))));
    }
    std::string json = fmt::format(R"({{"errors":{},"warnings":{},"diagnostics":[{}],"parameters":[{}],"textures":[{}],)"
                                   R"("uses_opacity_mask":{},"opacity_mask_animated":{},)"
                                   R"("uses_world_position_offset":{},"world_position_offset_animated":{},"max_world_position_offset":{},)"
                                   R"("reads_object_data":{},"vertex_interpolators":{})",
                                   errors,
                                   warnings,
                                   fmt::join(diagnostics, ","),
                                   fmt::join(parameters, ","),
                                   fmt::join(textures, ","),
                                   bool_to_json(result.uses_opacity_mask),
                                   bool_to_json(result.is_opacity_mask_animated),
                                   bool_to_json(result.uses_world_position_offset),
                                   bool_to_json(result.is_world_position_offset_animated),
                                   result.max_world_position_offset,
                                   bool_to_json(result.reads_object_data),
                                   result.interpolator_count);
    if(include_code)
    {
        json += fmt::format(R"(,"code":{})", make_json_string(result.shader));
        if(!result.vertex_shader.empty())
        {
            json += fmt::format(R"(,"vertex_code":{})", make_json_string(result.vertex_shader));
        }
    }
    return json + "}";
}

/// The graph's own object, as a graph document holds it under "material_graph".
auto make_graph_json(const material_graph& graph) -> std::string
{
    const std::string document = save_to_string(graph);
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    simdjson::dom::element inner;
    if(parser.parse(document).get(root) || root[GRAPH_DOCUMENT_KEY].get(inner))
    {
        return "null";
    }
    return std::string(simdjson::minify(inner));
}

/// Reads the "graph" argument: a graph's object or its whole document, as JSON or as a string of it.
auto read_graph_argument(const simdjson::dom::object& args, material_graph& graph, std::string& error) -> bool
{
    simdjson::dom::element value;
    if(args["graph"].get(value))
    {
        error = "graph is required: {\"nodes\": [...], \"comments\": [...]}";
        return false;
    }
    std::string text;
    std::string_view string_value;
    if(value.get(string_value) == simdjson::SUCCESS)
    {
        text = std::string(string_value);
    }
    else
    {
        text = std::string(simdjson::minify(value));
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    if(parser.parse(text).get(root) || !root.is_object())
    {
        error = "graph must be a JSON object";
        return false;
    }
    simdjson::dom::element inner;
    if(root[GRAPH_DOCUMENT_KEY].get(inner) != simdjson::SUCCESS)
    {
        text = fmt::format(R"({{"{}":{}}})", GRAPH_DOCUMENT_KEY, text);
    }
    if(!load_from_string(text, graph))
    {
        error = "graph does not read as a material graph: see material_graph_describe_nodes for the format";
        return false;
    }
    ensure_material_graph_output(graph);
    return true;
}

/// The graph material named by the "key" argument; empty with @p error when it is not one.
auto find_graph_material(rtti::context& ctx, const simdjson::dom::object& args, std::string& error) -> asset_handle<material>
{
    std::string key;
    if(!read_string(args, "key", key) || key.empty())
    {
        error = "key is required: the graph material's asset key (app:/data/....mat)";
        return {};
    }
    auto asset = find_loaded_asset<material>(ctx, key, error);
    if(!asset)
    {
        return {};
    }
    if(!asset.get()->is<graph_material>())
    {
        error = key + " is not a material graph (a PBR material: use assets_set_properties_batch)";
        return {};
    }
    return asset;
}

/// Compiles the generated code, and the vertex code if there is any, with shaderc for the renderer the editor draws
/// with.
auto describe_gpu_compile(const material_graph_compile_result& result) -> std::string
{
    const auto folder = fs::temp_directory_path() / GPU_CHECK_FOLDER;
    fs::error_code ec;
    fs::create_directories(folder, ec);
    const auto renderer = gfx::get_renderer_based_on_filename_extension(gfx::get_current_renderer_filename_extension());
    std::vector<compiled_shader_variant> variants;
    std::string error;
    bool is_compiled = asset_compiler::compile_shader_text(folder / GPU_CHECK_SHADER_NAME, result.shader, renderer, folder, variants, &error);
    std::vector<compiled_shader_variant> vertex_variants;
    if(is_compiled && !result.vertex_shader.empty())
    {
        is_compiled = asset_compiler::compile_shader_text(folder / GPU_CHECK_VERTEX_SHADER_NAME,
                                                          result.vertex_shader,
                                                          renderer,
                                                          folder,
                                                          vertex_variants,
                                                          &error);
    }
    return fmt::format(R"({{"renderer":{},"ok":{},"variants":{},"vertex_variants":{},"error":{}}})",
                       make_json_string(gfx::get_current_renderer_filename_extension()),
                       bool_to_json(is_compiled),
                       variants.size(),
                       vertex_variants.size(),
                       is_compiled ? std::string("null") : make_json_string(error));
}

auto compile_graph(const material_graph& graph, const std::string& source_name) -> material_graph_compile_result
{
    material_graph_compile_options options;
    options.source_name = source_name;
    return compile_material_graph(graph, options);
}
} // namespace

void register_material_graph_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "material_graph_describe_nodes",
         .description =
             "The node types a material graph is made of. Without arguments: every type in a line (type, label, "
             "category, description). With types (or a category): each one in full - its inputs (name, type, default, "
             "value_only for values set on the node, unlinked_reads for inputs with a natural source such as the "
             "mesh UV), outputs and settings. A graph names nodes by type and pins by these names; the material-graphs "
             "skill (skills_get) shows the format and the workflow.",
         .input_schema_json =
             R"json({"type":"object","properties":{"types":{"type":"array","items":{"type":"string"}},"category":{"type":"string"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             (void)ctx;
             std::vector<std::string> types;
             std::string error;
             if(!read_string_array(args, "types", types, error, false))
             {
                 return {.text = error, .is_error = true};
             }
             std::string category;
             read_string(args, "category", category);
             std::vector<std::string> nodes;
             std::vector<std::string> unknown;
             for(const auto& type : types)
             {
                 const auto* def = find_material_node_def(type);
                 if(def == nullptr)
                 {
                     unknown.push_back(type);
                     continue;
                 }
                 nodes.push_back(describe_node_full(*def));
             }
             if(types.empty())
             {
                 for(const auto& def : get_material_node_library())
                 {
                     const bool is_in_category = category.empty() || contains_ci(get_material_node_category_name(def.category), category);
                     if(is_in_category)
                     {
                         nodes.push_back(category.empty() ? describe_node_brief(def) : describe_node_full(def));
                     }
                 }
             }
             return {.text = fmt::format(R"({{"nodes":[{}],"count":{},"unknown":{}}})",
                                         fmt::join(nodes, ","),
                                         nodes.size(),
                                         strings_to_json_array(unknown)),
                     .is_error = nodes.empty()};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "material_graph_get",
         .description =
             "A graph material's graph (key: app:/data/....mat) as JSON, ready to edit and pass to material_graph_set, "
             "with what its compile found: errors and warnings by node, the parameters and texture nodes material "
             "instances set by name, whether its Opacity Mask clips. include_code adds the generated shader.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string"},"include_code":{"type":"boolean"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             const auto asset = find_graph_material(ctx, args, error);
             if(!asset)
             {
                 return {.text = error, .is_error = true};
             }
             bool include_code = false;
             read_bool(args, "include_code", include_code);
             const auto& graph = asset.get()->safe_cast<graph_material>()->get_graph();
             return {.text = fmt::format(R"({{"key":{},"graph":{},"compile":{}}})",
                                         make_json_string(asset.id()),
                                         make_graph_json(graph),
                                         describe_compile(compile_graph(graph, asset.id()), include_code))};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "material_graph_set",
         .description =
             "Replaces the graph of a graph material (key: app:/data/....mat) and saves it, as the Material Graph "
             "window does: the scene shows the new graph once the material recompiles (a moment); the "
             "window, if it shows the material, follows. create:true makes the .mat when there is none. graph is the "
             "object material_graph_get returns (nodes, comments; positions may be left out - the window lays the "
             "graph out). The result says what the compile found; a graph with errors is saved too (its errors draw "
             "defaults), so read errors and fix them. Not undoable through edit_undo: keep the material_graph_get "
             "result to go back.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string"},"graph":{"type":["object","string"]},"create":{"type":"boolean"}},"required":["key","graph"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             material_graph graph;
             if(!read_graph_argument(args, graph, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::string key;
             read_string(args, "key", key);
             bool create = false;
             read_bool(args, "create", create);
             // The file decides: looking a missing key up registers it with the asset manager.
             fs::error_code ec;
             const bool exists = !key.empty() && fs::exists(fs::resolve_protocol(key), ec);
             bool is_created = false;
             if(!exists && create)
             {
                 if(!ends_with_ci(key, ex::get_format<material>()) || !starts_with(key, "app:/"))
                 {
                     return {.text = "A new graph material's key is app:/<folder>/<name>.mat", .is_error = true};
                 }
                 if(!asset_actions::create_material_graph(ctx, fs::resolve_protocol(key), graph))
                 {
                     return {.text = "Could not create " + key, .is_error = true};
                 }
                 is_created = true;
             }
             else
             {
                 const auto asset = find_graph_material(ctx, args, error);
                 if(!asset)
                 {
                     return {.text = exists || create ? error : error + " (create:true makes it)", .is_error = true};
                 }
                 if(!asset_actions::save_material_graph(ctx, asset, graph))
                 {
                     return {.text = "Could not save " + key, .is_error = true};
                 }
             }
             const auto result = compile_graph(graph, key);
             return {.text = fmt::format(R"({{"ok":true,"key":{},"created":{},"compile":{}}})",
                                         make_json_string(key),
                                         bool_to_json(is_created),
                                         describe_compile(result, false))};
         },
         .effect = tool_effect::destructive,
         .writes_project_files = true});

    registry.add(
        {.name = "material_graph_compile",
         .description =
             "Compiles a material graph without saving anything: the graph argument (as material_graph_set takes it), "
             "or the graph of the graph material at key. Returns what the compile found (errors, warnings, parameters, "
             "textures, World Position Offset); include_code adds the generated shader (and vertex_code, the vertex "
             "shader of an offset that moves the vertices); gpu:true also compiles them with shaderc for the "
             "renderer the editor draws with and reports the shader compiler's error lines - a check for code the "
             "graph compiler let through.",
         .input_schema_json =
             R"json({"type":"object","properties":{"key":{"type":"string"},"graph":{"type":["object","string"]},"include_code":{"type":"boolean"},"gpu":{"type":"boolean"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             material_graph graph;
             std::string source_name = "MCP";
             if(!args["graph"].error())
             {
                 if(!read_graph_argument(args, graph, error))
                 {
                     return {.text = error, .is_error = true};
                 }
             }
             else
             {
                 const auto asset = find_graph_material(ctx, args, error);
                 if(!asset)
                 {
                     return {.text = "Pass graph, or the key of a graph material: " + error, .is_error = true};
                 }
                 graph = asset.get()->safe_cast<graph_material>()->get_graph();
                 source_name = asset.id();
             }
             bool include_code = false;
             read_bool(args, "include_code", include_code);
             bool gpu = false;
             read_bool(args, "gpu", gpu);
             const auto result = compile_graph(graph, source_name);
             std::string json = describe_compile(result, include_code);
             if(gpu)
             {
                 json.pop_back();
                 json += fmt::format(R"(,"gpu":{}}})", describe_gpu_compile(result));
             }
             return {.text = json};
         },
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "material_graph_open",
         .description =
             "Opens a graph material (key: app:/data/....mat) in the Material Graph window and shows it, as "
             "double-clicking it in the Content Browser does. Refused while the window holds an untitled graph with "
             "unsaved changes, which the user decides about.",
         .input_schema_json = R"json({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             const auto asset = find_graph_material(ctx, args, error);
             if(!asset || !editor_actions::open_material_graph_without_asking(ctx, asset, error))
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"ok":true,"key":{}}})", make_json_string(asset.id()))};
         },
         .idempotent = true});
}

} // namespace unravel::mcp
