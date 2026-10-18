#include "mcp_component_utils.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include <editor/assets/asset_actions.h>
#include <editor/editing/actions/actions.h>
#include <editor/system/project_manager.h>
#include <editor/system/mcp_manager.h>
#include <engine/assets/asset_manager.h>
#include <asset_compiler/asset_compiler.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/play_mode.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/scripting/script.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <hpp/optional.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace unravel::mcp
{
namespace
{

/// Where generated scripts go when the caller names no folder.
constexpr const char* k_default_script_folder = "app:/data/scripts";

/// Script source files are written and built only in the project; engine scripts are shared.
constexpr const char* k_script_protocol = "app";

/// The source file of an entity's script of `type_name` (full or short C# name), as its script
/// component records it.
auto script_source_of(rtti::context& ctx,
                      const std::string& entity_id,
                      const std::string& type_name,
                      std::string& path,
                      std::string& error) -> bool
{
    auto* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
    auto entity = scn && scn->registry ? find_entity(*scn, entity_id) : entt::handle{};
    const auto* script_comp = entity ? entity.try_get<script_component>() : nullptr;
    const auto position = script_comp ? find_script_position(entity, {type_name, 0}) : std::nullopt;
    if(!position)
    {
        error = "No script of type " + type_name + " on entity " + entity_id;
        return false;
    }
    path = script_comp->get_script_source_location(script_comp->get_script_components()[position->index]);
    if(path.empty())
    {
        error = "No source file known for script type " + type_name;
        return false;
    }
    return true;
}

auto unique_script_path(const fs::path& dir, const std::string& stem) -> fs::path
{
    const auto ext = ex::get_format<script>();
    fs::path candidate = dir / (stem + ext);
    fs::error_code ec;
    int i = 0;
    while(fs::exists(candidate, ec))
    {
        ++i;
        candidate = dir / (fmt::format("{}{}", stem, i) + ext);
    }
    return candidate;
}

/**
 * Whether the asset pipeline has imported the script. A build compiles the imported scripts, so one
 * requested before a new file is imported runs without it - and fails on every use of its types.
 * The asset watcher requests a build itself once it has imported a file, and that one includes it.
 */
auto is_script_imported(rtti::context& ctx, const fs::path& absolute) -> bool
{
    const auto key = fs::convert_to_protocol(absolute).generic_string();
    return ctx.get_cached<asset_manager>().find_asset<script>(key).is_valid();
}

/// The build this tool requested: now, after the asset pipeline imports the new files, or none (the asset
/// watcher still rebuilds the scripts it imports).
auto describe_build(bool recompile, bool requested_now) -> const char*
{
    if(!recompile)
    {
        return "not_requested";
    }
    return requested_now ? "now" : "after_import";
}

/**
 * @brief Script sources written here that no finished build has compiled yet.
 *
 * Builds this tool does not start (recompile:false, or the asset watcher's own rebuild) can end
 * up compiling the edit later. So "no build pending" alone does not mean the edit is built: the
 * wait also needs the last build to have compiled these.
 */
struct unbuilt_sources
{
    std::mutex mutex;
    /// Normalized path, the comparison key -> the path as written, which the existence check needs:
    /// the normalized one is lowercased and names no file on a case-sensitive filesystem.
    std::map<std::string, fs::path> paths;
};

auto get_unbuilt_sources() -> unbuilt_sources&
{
    static unbuilt_sources sources;
    return sources;
}

/// One spelling per file, so a build's list and a written path compare equal.
auto normalize_source_path(const fs::path& path) -> std::string
{
    return to_lower_ascii(path.lexically_normal().generic_string());
}

/// Notes a written source the protocol's library builds: a .cs file under its data folder.
void note_written_source(const fs::path& absolute)
{
    const auto key = fs::convert_to_protocol(absolute).generic_string();
    const auto protocol = fs::extract_protocol(key).string();
    if(absolute.extension() != ex::get_format<script>() || protocol.empty() || !key.starts_with(protocol + ":/data/"))
    {
        return;
    }
    auto& sources = get_unbuilt_sources();
    std::lock_guard<std::mutex> lock(sources.mutex);
    sources.paths.insert_or_assign(normalize_source_path(absolute), absolute);
}

/// Drops the noted sources the last build compiled, or that no longer exist; whether any are left.
auto has_unbuilt_sources(const asset_compiler::script_compilation_result& last) -> bool
{
    std::set<std::string> built;
    for(const auto& file : last.files)
    {
        built.insert(normalize_source_path(file));
    }
    auto& sources = get_unbuilt_sources();
    std::lock_guard<std::mutex> lock(sources.mutex);
    std::erase_if(sources.paths,
                  [&](const auto& source)
                  {
                      fs::error_code ec;
                      return built.contains(source.first) || !fs::exists(source.second, ec);
                  });
    return !sources.paths.empty();
}

/// scripts_invoke's "arguments": numbers, booleans, strings, and number arrays for vector parameters.
auto read_script_arguments(const simdjson::dom::object& args,
                           std::vector<script_system::script_argument>& out,
                           std::string& error) -> bool
{
    simdjson::dom::array list;
    if(args["arguments"].get(list))
    {
        return true;
    }
    for(auto value : list)
    {
        bool flag = false;
        int64_t integer = 0;
        double number = 0.0;
        hpp::string_view text;
        simdjson::dom::array components;
        if(!value.get(flag))
        {
            out.emplace_back(flag);
        }
        else if(!value.get(integer))
        {
            out.emplace_back(integer);
        }
        else if(!value.get(number))
        {
            out.emplace_back(number);
        }
        else if(!value.get(text))
        {
            out.emplace_back(std::string(text));
        }
        else if(!value.get(components))
        {
            std::vector<double> numbers;
            for(auto component : components)
            {
                float component_value = 0.0f;
                if(!parse_number(component, component_value, error))
                {
                    error = "An array argument holds numbers only";
                    return false;
                }
                numbers.push_back(component_value);
            }
            out.emplace_back(std::move(numbers));
        }
        else
        {
            error = "Arguments are numbers, booleans, strings or arrays of numbers";
            return false;
        }
    }
    return true;
}

} // namespace

void register_script_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scripts_list_types",
         .description =
             "List the C# ScriptComponent type full names - project and engine - that "
             "scene_add_components_batch adds as component Script with that type_name.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             if(!ctx.has<script_system>())
             {
                 return {.text = "Script system unavailable", .is_error = true};
             }
             const auto& types = ctx.get_cached<script_system>().get_all_scriptable_components();
             std::vector<std::string> names;
             names.reserve(types.size());
             for(const auto& type : types)
             {
                 names.push_back(type.get_fullname());
             }
             return {.text = strings_to_json_array(names), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scripts_api_search",
         .description =
             "Search the C# scripting API - engine and project types - by words in type and member names "
             "and their documentation (all words must match; 'camera screen ray', 'entity pool'). Returns "
             "one line per result: Type.Member: C# signature -- summary. Use before writing gameplay code "
             "to find what exists; scripts_api_get lists a whole type. Optional limit (default 20).",
         .input_schema_json =
             R"json({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":200}},"required":["query"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             if(!ctx.has<script_system>())
             {
                 return {.text = "Script system unavailable", .is_error = true};
             }
             std::string query;
             if(!read_string(args, "query", query) || query.empty())
             {
                 return {.text = "Missing query", .is_error = true};
             }
             const auto limit = static_cast<int>(read_int64_or(args, "limit", 0));
             return {.text = ctx.get_cached<script_system>().search_api(query, limit), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scripts_api_get",
         .description =
             "Describe one C# scripting API type (engine or project): its declaration, base type and "
             "summary, then every public constructor, property, field, event and method with its C# "
             "signature and summary. type: full name (Unravel.Core.CameraComponent) or short name.",
         .input_schema_json =
             R"json({"type":"object","properties":{"type":{"type":"string"}},"required":["type"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             if(!ctx.has<script_system>())
             {
                 return {.text = "Script system unavailable", .is_error = true};
             }
             std::string type_name;
             if(!read_string(args, "type", type_name) || type_name.empty())
             {
                 return {.text = "Missing type", .is_error = true};
             }
             return {.text = ctx.get_cached<script_system>().describe_api_type(type_name), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scripts_invoke",
         .description =
             "Call a public method of a running script (play mode) and return its result as JSON: a "
             "repeatable way to trigger gameplay, e.g. a spell's TryCast at an exact world point instead "
             "of clicking the screen. Identify the script by entity_id, type_name (full or short name) "
             "and optional script_index. arguments convert to the parameter types: numbers, booleans, "
             "strings, enum names, and number arrays for Vector2/3/4, Color or Quaternion; missing "
             "trailing arguments take their defaults.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"type_name":{"type":"string"},"script_index":{"type":"integer","minimum":0},"method":{"type":"string"},"arguments":{"type":"array","items":{"anyOf":[{"type":"number"},{"type":"boolean"},{"type":"string"},{"type":"array","items":{"type":"number"}}]}}},"required":["entity_id","type_name","method"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             if(!ctx.has<script_system>() || !ctx.has<play_mode>() || !ctx.get_cached<play_mode>().is_simulation_running())
             {
                 return {.text = "Scripts run in play mode only (play_set_active)", .is_error = true};
             }
             scene* scn = nullptr;
             std::string error;
             std::string entity_id;
             std::string method;
             script_selector selector;
             std::vector<script_system::script_argument> arguments;
             if(!require_active_scene(ctx, scn, error) || !read_script_selector(args, selector, error) ||
                !read_script_arguments(args, arguments, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(!read_string(args, "entity_id", entity_id) || !read_string(args, "method", method) ||
                selector.type.empty() || method.empty())
             {
                 return {.text = "Needs entity_id, type_name and method", .is_error = true};
             }
             auto entity = resolve_entity(*scn, entity_id, error);
             if(!entity)
             {
                 return {.text = error, .is_error = true};
             }
             const auto position = find_script_position(entity, selector);
             if(!position)
             {
                 return {.text = "No such script on the entity; it has " + scripts_summary_json(entity),
                         .is_error = true};
             }
             const auto& script = entity.get<script_component>().get_script_components()[position->index];
             auto result =
                 ctx.get_cached<script_system>().invoke_script_method(script.pinned->get_object(), method, arguments);
             const bool failed = result == "null" || starts_with(result, R"({"error")");
             return {.text = std::move(result), .is_error = failed};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "scripts_get_compile_status",
         .description =
             "Whether the project's scripts are building, and the last build's errors and warnings with file, "
             "line and message. wait:true first waits for a queued or running build to finish and, when it "
             "succeeds, for the editor to load it - new script types can be attached once it returns - up to "
             "timeout_ms (default 60000). Call it after scripts_set_sources_batch; play mode will not start while "
             "has_errors is true.",
         .input_schema_json =
             R"json({"type":"object","properties":{"wait":{"type":"boolean"},"timeout_ms":{"type":"integer","minimum":0,"maximum":300000}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             constexpr int64_t k_default_wait_ms = 60000;
             constexpr int64_t k_max_wait_ms = 300000;
             constexpr auto k_poll_interval = std::chrono::milliseconds(100);
             constexpr const char* k_protocol = "app";

             auto& mcp = ctx.get_cached<mcp_manager>();
             bool wait = false;
             read_bool(args, "wait", wait);
             const auto timeout = read_timeout_ms(args, k_default_wait_ms, k_max_wait_ms, "timeout_ms");

             struct build_state
             {
                 bool pending{false};
                 bool has_errors{false};
             };
             auto query = [&]() -> hpp::optional<build_state>
             {
                 return mcp.invoke_on_main(
                     [&ctx]() -> build_state
                     {
                         if(!ctx.has<script_system>())
                         {
                             return {};
                         }
                         const auto& scripts = ctx.get_cached<script_system>();
                         const bool pending = scripts.is_build_pending(k_protocol) ||
                                              has_unbuilt_sources(asset_compiler::get_last_script_compilation(k_protocol));
                         return {.pending = pending, .has_errors = scripts.has_compilation_errors()};
                     });
             };

             auto state = query();
             const auto deadline = std::chrono::steady_clock::now() + timeout;
             // A main thread busy loading the new build misses a query: a reason to keep waiting, not to fail.
             while(wait && (!state || state->pending) && std::chrono::steady_clock::now() < deadline)
             {
                 std::this_thread::sleep_for(k_poll_interval);
                 state = query();
             }
             if(!state)
             {
                 return {.text = "Timed out reaching the editor main thread", .is_error = true};
             }

             auto diagnostics_json = [](const std::vector<asset_compiler::script_compilation_entry>& entries)
             {
                 std::string json = "[";
                 bool first = true;
                 for(const auto& entry : entries)
                 {
                     if(!first)
                     {
                         json += ",";
                     }
                     first = false;
                     json += fmt::format(R"({{"key":{},"file":{},"line":{},"message":{}}})",
                                         make_json_string(fs::convert_to_protocol(entry.file).generic_string()),
                                         make_json_string(entry.file),
                                         entry.line,
                                         make_json_string(entry.msg));
                 }
                 json += "]";
                 return json;
             };

             const auto last = asset_compiler::get_last_script_compilation(k_protocol);
             return {.text = fmt::format(
                         R"({{"building":{},"has_errors":{},"last_build":{{"ran":{},"succeeded":{},"errors":{},"warnings":{}}}{}}})",
                         bool_to_json(state->pending),
                         bool_to_json(state->has_errors),
                         bool_to_json(last.has_run),
                         bool_to_json(last.succeeded),
                         diagnostics_json(last.errors),
                         diagnostics_json(last.warnings),
                         state->pending ? R"(,"note":"A build is still queued or running; pass wait:true to wait for it.")"
                                        : ""),
                     .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scripts_get_source",
         .description =
             "Read C# source for a script: key (app:/ or engine:/ .cs, or a path relative to "
             "app:/data), or entity_id + type_name (full or short C# name).",
         .input_schema_json =
             R"json({"type":"object","properties":{"path":{"type":"string"},"key":{"type":"string"},"entity_id":{"type":"string"},"type_name":{"type":"string"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string path;
             std::string key;
             read_string(args, "path", path);
             read_string(args, "key", key);
             if(path.empty() && !key.empty())
             {
                 path = key;
             }

             std::string type_name;
             read_string(args, "type_name", type_name);
             std::string entity_id;
             read_string(args, "entity_id", entity_id);

             std::string error;
             if(path.empty())
             {
                 if(entity_id.empty() || type_name.empty())
                 {
                     return {.text = "Provide path/key, or entity_id + type_name", .is_error = true};
                 }
                 if(!script_source_of(ctx, entity_id, type_name, path, error))
                 {
                     return {.text = error, .is_error = true};
                 }
             }
             std::string source_key;
             if(!to_readable_file_key(path, ex::get_suported_formats<script>(), source_key, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto absolute = fs::absolute(fs::resolve_protocol(source_key));
             std::string code;
             if(!asset_actions::read_text_file(absolute, code, &error))
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = fmt::format(R"({{"key":{},"bytes":{},"code":{}}})",
                                         make_json_string(source_key),
                                         code.size(),
                                         make_json_string(code)),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "scripts_set_sources_batch",
         .description =
             "Write C# source files in the project (batch, atomic writes). Each item: code, plus key "
             "(app:/data/... or a path relative to app:/data; .cs is added) or entity_id + type_name. "
             "Keep [ScriptSourceFile] on every script component class, as the template has it: the "
             "inspector opens the source through it. Optional recompile (default true, once for batch). "
             "A batch that adds a new file builds once the asset pipeline has imported it (build: "
             "after_import); scripts_get_compile_status wait:true waits for that build.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"code":{"type":"string"},"path":{"type":"string"},"key":{"type":"string"},"entity_id":{"type":"string"},"type_name":{"type":"string"}},"required":["code"]}},"recompile":{"type":"boolean","default":true}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             bool recompile = true;
             read_bool(args, "recompile", recompile);
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             const std::string protocol = k_script_protocol;
             bool all_imported = true;
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 std::string code;
                 if(!read_string(obj, "code", code))
                 {
                     results += R"({"ok":false,"error":"Missing code"})";
                     continue;
                 }
                 std::string path;
                 std::string key;
                 read_string(obj, "path", path);
                 read_string(obj, "key", key);
                 if(path.empty() && !key.empty())
                 {
                     path = key;
                 }
                 std::string type_name;
                 std::string entity_id;
                 read_string(obj, "type_name", type_name);
                 read_string(obj, "entity_id", entity_id);
                 const auto fail = [&](const std::string& message)
                 {
                     results += fmt::format(R"({{"ok":false,"error":{}}})", make_json_string(message));
                 };
                 if(path.empty())
                 {
                     if(entity_id.empty() || type_name.empty())
                     {
                         fail("Provide path/key, or entity_id + type_name");
                         continue;
                     }
                     if(!script_source_of(ctx, entity_id, type_name, path, error))
                     {
                         fail(error);
                         continue;
                     }
                 }
                 std::string source_key;
                 if(!to_project_data_file_key(ctx, path, ex::get_suported_formats<script>(), source_key, error))
                 {
                     fail(error);
                     continue;
                 }
                 const auto absolute = fs::absolute(fs::resolve_protocol(source_key));
                 if(!asset_actions::write_text_file(absolute, code, &error))
                 {
                     fail(error);
                     continue;
                 }
                 note_written_source(absolute);
                 all_imported &= is_script_imported(ctx, absolute);
                 ++ok_count;
                 results += fmt::format(R"({{"ok":true,"key":{},"bytes":{}}})", make_json_string(source_key), code.size());
             }
             results += "]";
             const bool build_now = recompile && ok_count > 0 && !protocol.empty() && all_imported;
             if(build_now)
             {
                 script_system::set_needs_recompile(protocol, true);
             }
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{},"recompile":{},"build":"{}","protocol":{}}})",
                                         results,
                                         ok_count,
                                         requested,
                                         recompile ? "true" : "false",
                                         describe_build(recompile, build_now),
                                         make_json_string(protocol)),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = false,
         .effect = tool_effect::destructive,
         .idempotent = true});

    registry.add(
        {.name = "scripts_create_batch",
         .description =
             "Create C# script component files from the engine's template. Each item: name, optional folder "
             "(app:/data/... or a path relative to app:/data; default app:/data/scripts). Optional recompile "
             "(default true, once). New files build once the asset pipeline has imported them (build: "
             "after_import); scripts_get_compile_status wait:true waits for that build.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"folder":{"type":"string"}},"required":["name"]}},"recompile":{"type":"boolean","default":true}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             if(!require_not_play_mode(ctx, error) || !require_open_project(ctx, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items_arr;
             if(!read_required_array(args, "items", items_arr, error))
             {
                 return {.text = error, .is_error = true};
             }
             bool recompile = true;
             read_bool(args, "recompile", recompile);
             const auto template_path =
                 fs::resolve_protocol("engine:/data/templates/TemplateComponent" + ex::get_format<script>() + ".in");
             std::string results = "[";
             bool first = true;
             size_t ok_count = 0;
             size_t requested = 0;
             const std::string protocol = k_script_protocol;
             bool all_imported = true;
             for(auto el : items_arr)
             {
                 ++requested;
                 simdjson::dom::object obj;
                 if(!read_object(el, obj, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 if(!first)
                 {
                     results += ",";
                 }
                 first = false;
                 std::string name;
                 if(!read_string(obj, "name", name) || name.empty())
                 {
                     results += R"({"ok":false,"error":"Missing name"})";
                     continue;
                 }
                 if(name.size() > 3 && name.substr(name.size() - 3) == ".cs")
                 {
                     name = name.substr(0, name.size() - 3);
                 }
                 if(!asset_actions::is_valid_csharp_identifier(name))
                 {
                     results += R"({"ok":false,"error":"name must be a valid C# identifier"})";
                     continue;
                 }
                 std::string folder = k_default_script_folder;
                 read_string(obj, "folder", folder);
                 std::string folder_key;
                 if(!to_project_data_key(ctx, folder.empty() ? k_default_script_folder : folder, folder_key, error))
                 {
                     results += fmt::format(R"({{"ok":false,"error":{}}})", make_json_string(error));
                     continue;
                 }
                 const auto dir = fs::absolute(fs::resolve_protocol(folder_key));
                 const auto dst = unique_script_path(dir, name);
                 if(!asset_actions::create_script_from_template(template_path, dst, &error))
                 {
                     results += fmt::format(R"({{"ok":false,"error":{}}})", make_json_string(error));
                     continue;
                 }
                 note_written_source(dst);
                 all_imported &= is_script_imported(ctx, dst);
                 ++ok_count;
                 const auto key = fs::convert_to_protocol(dst).generic_string();
                 results += fmt::format(R"({{"ok":true,"path":{},"key":{},"type_name":{}}})",
                                        make_json_string(dst.generic_string()),
                                        make_json_string(key),
                                        make_json_string(dst.stem().string()));
             }
             results += "]";
             const bool build_now = recompile && ok_count > 0 && !protocol.empty() && all_imported;
             if(build_now)
             {
                 script_system::set_needs_recompile(protocol, true);
             }
             return {.text = fmt::format(R"({{"results":{},"count":{},"requested":{},"recompile":{},"build":"{}","protocol":{}}})",
                                         results,
                                         ok_count,
                                         requested,
                                         recompile ? "true" : "false",
                                         describe_build(recompile, build_now),
                                         make_json_string(protocol)),
                     .is_error = ok_count == 0 && requested > 0};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
