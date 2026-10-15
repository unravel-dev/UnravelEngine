#include "mcp_component_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/editing/entity_inspect.h>
#include <editor/system/mcp_manager.h>
#include <threadpp/thread.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

constexpr int64_t k_wait_default_ms = 10000;
constexpr int64_t k_wait_max_ms = 120000;
constexpr auto k_poll_interval = std::chrono::milliseconds(16);
/// Matching entities a result names; the count is always complete.
constexpr size_t k_reported_entities = 10;
constexpr std::array<const char*, 4> k_condition_keys{"log", "entity", "property", "play_phase"};

/**
 * @brief One check of a wait's condition, on the main thread: true once it holds. Describes
 *        what it saw in @p observed (a JSON value); an @p error ends the wait.
 */
using condition_check = std::function<bool(rtti::context& ctx, std::string& observed, std::string& error)>;

auto make_log_condition(const simdjson::dom::object& spec, condition_check& out, std::string& error) -> bool
{
    std::string contains;
    read_string(spec, "contains", contains);
    if(contains.empty())
    {
        error = "log needs contains";
        return false;
    }
    level::level_enum min_level = level::info;
    std::string level_name;
    if(read_string(spec, "min_level", level_name) && !parse_min_level(level_name, min_level))
    {
        error = "Invalid log.min_level";
        return false;
    }
    // Entries after this id count; without after_id, the ones logged once the wait has begun.
    auto newest_seen = std::make_shared<std::optional<int64_t>>();
    int64_t after_id = 0;
    if(!spec["after_id"].get(after_id))
    {
        *newest_seen = after_id;
    }
    out = [contains, min_level, newest_seen](rtti::context& ctx, std::string& observed, std::string&) -> bool
    {
        const auto logs = editor_actions::get_recent_logs(ctx, level::trace, std::numeric_limits<size_t>::max(), 0);
        if(!newest_seen->has_value())
        {
            *newest_seen = logs.empty() ? int64_t{-1} : static_cast<int64_t>(logs.back().id);
            return false;
        }
        for(const auto& entry : logs)
        {
            if(static_cast<int64_t>(entry.id) > **newest_seen && entry.level >= min_level &&
               contains_ci(entry.text, contains))
            {
                observed = fmt::format(R"({{"id":{},"level":{},"text":{}}})",
                                       entry.id,
                                       make_json_string(level_to_string(entry.level)),
                                       make_json_string(entry.text));
                return true;
            }
        }
        return false;
    };
    return true;
}

auto make_entity_condition(const simdjson::dom::object& spec, condition_check& out, std::string& error) -> bool
{
    const auto filter = read_entity_filter(spec);
    if(filter.is_empty())
    {
        error = "entity needs name_contains, name_exact, component and/or type_name";
        return false;
    }
    bool exists = true;
    read_bool(spec, "exists", exists);
    out = [filter, exists](rtti::context& ctx, std::string& observed, std::string& check_error) -> bool
    {
        scene* scn = nullptr;
        if(!require_active_scene(ctx, scn, check_error))
        {
            return false;
        }
        const auto matches = find_matching_entities(*scn, filter, std::numeric_limits<size_t>::max());
        std::vector<std::string> ids;
        for(size_t i = 0; i < std::min(matches.size(), k_reported_entities); ++i)
        {
            ids.push_back(entity_id_string(matches[i]));
        }
        observed = fmt::format(R"({{"count":{},"entity_ids":{}}})", matches.size(), strings_to_json_array(ids));
        return exists == !matches.empty();
    };
    return true;
}

/// The element at a dotted path ("light.intensity", "position.1") inside a parsed property bag.
auto find_at_path(simdjson::dom::element value, const std::string& path, simdjson::dom::element& out) -> bool
{
    size_t start = 0;
    while(start < path.size())
    {
        const auto dot = path.find('.', start);
        const auto segment = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        simdjson::dom::object object;
        simdjson::dom::array array;
        if(!value.get(object))
        {
            if(object[segment].get(value))
            {
                return false;
            }
        }
        else if(!value.get(array))
        {
            char* end = nullptr;
            const auto index = std::strtoull(segment.c_str(), &end, 10);
            if(end == segment.c_str() || *end != '\0' || array.at(index).get(value))
            {
                return false;
            }
        }
        else
        {
            return false;
        }
        start = dot == std::string::npos ? path.size() : dot + 1;
    }
    out = value;
    return true;
}

/// Whether @p actual relates to @p expected as @p op says; numbers compare by value, anything else by its JSON.
auto compare_values(const simdjson::dom::element& actual, const std::string& op, const simdjson::dom::element& expected)
    -> bool
{
    double lhs = 0.0;
    double rhs = 0.0;
    const bool is_numeric = !actual.get(lhs) && !expected.get(rhs);
    if(op == "==" || op == "!=")
    {
        const bool is_equal = is_numeric ? lhs == rhs : simdjson::minify(actual) == simdjson::minify(expected);
        return (op == "==") == is_equal;
    }
    if(!is_numeric)
    {
        return false;
    }
    return op == "<" ? lhs < rhs : op == "<=" ? lhs <= rhs : op == ">" ? lhs > rhs : lhs >= rhs;
}

struct property_watch
{
    std::string entity_id;
    std::string component;
    script_selector script;
    std::string path;
    std::string op;
    std::string expected_json;
    /// For "changed": the first value read.
    std::optional<std::string> first_json;
};

/// The property bag a watch reads: Transform is the entity's pose, anything else its component.
auto read_watched_json(rtti::context& ctx, entt::handle entity, const property_watch& watch, std::string& error)
    -> std::string
{
    if(watch.component == "Transform")
    {
        return entity_to_pose_json(entity);
    }
    const std::vector<std::string> names{watch.path.substr(0, watch.path.find('.'))};
    return component_properties_to_json(ctx, entity, watch.component, watch.script, &names, error);
}

auto check_property(rtti::context& ctx, property_watch& watch, std::string& observed, std::string& error) -> bool
{
    scene* scn = nullptr;
    if(!require_active_scene(ctx, scn, error))
    {
        return false;
    }
    auto entity = resolve_entity(*scn, watch.entity_id, error);
    if(!entity)
    {
        return false;
    }
    const auto bag = read_watched_json(ctx, entity, watch, error);
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    simdjson::dom::element value;
    if(bag.empty() || parser.parse(bag).get(root) || !find_at_path(root, watch.path, value))
    {
        error = error.empty() ? "No property " + watch.path + " on " + watch.component : error;
        return false;
    }
    observed = simdjson::minify(value);
    if(watch.op == "changed")
    {
        if(!watch.first_json)
        {
            watch.first_json = observed;
        }
        return observed != *watch.first_json;
    }
    simdjson::dom::parser expected_parser;
    simdjson::dom::element expected;
    (void)expected_parser.parse(watch.expected_json).get(expected);
    return compare_values(value, watch.op, expected);
}

auto make_property_condition(const simdjson::dom::object& spec, condition_check& out, std::string& error) -> bool
{
    auto watch = std::make_shared<property_watch>();
    read_string(spec, "entity_id", watch->entity_id);
    read_string(spec, "component", watch->component);
    read_string(spec, "name", watch->path);
    read_string(spec, "op", watch->op);
    if(watch->entity_id.empty() || watch->component.empty() || watch->path.empty() || watch->op.empty())
    {
        error = "property needs entity_id, component, name and op";
        return false;
    }
    if(!read_script_selector(spec, watch->script, error))
    {
        return false;
    }
    simdjson::dom::element expected;
    if(watch->op != "changed")
    {
        if(spec["value"].get(expected))
        {
            error = "property needs value for op " + watch->op;
            return false;
        }
        watch->expected_json = simdjson::minify(expected);
    }
    out = [watch](rtti::context& ctx, std::string& observed, std::string& check_error) -> bool
    {
        return check_property(ctx, *watch, observed, check_error);
    };
    return true;
}

auto make_play_condition(const std::string& phase, condition_check& out) -> bool
{
    out = [phase](rtti::context& ctx, std::string& observed, std::string&) -> bool
    {
        const auto current = play_phase_from_ctx(ctx);
        observed = make_json_string(current);
        return current == phase;
    };
    return true;
}

/// The one condition a call names, as a check.
auto make_condition(const simdjson::dom::object& args, condition_check& out, std::string& error) -> bool
{
    const auto given = std::count_if(k_condition_keys.begin(),
                                     k_condition_keys.end(),
                                     [&args](const char* key)
                                     {
                                         return args[key].error() == simdjson::SUCCESS;
                                     });
    if(given != 1)
    {
        error = "Provide exactly one of log, entity, property, play_phase";
        return false;
    }
    simdjson::dom::object spec;
    std::string phase;
    if(!args["log"].get(spec))
    {
        return make_log_condition(spec, out, error);
    }
    if(!args["entity"].get(spec))
    {
        return make_entity_condition(spec, out, error);
    }
    if(!args["property"].get(spec))
    {
        return make_property_condition(spec, out, error);
    }
    read_string(args, "play_phase", phase);
    return make_play_condition(phase, out);
}

} // namespace

void register_wait_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "editor_wait_for",
         .description =
             "Wait until one condition holds, instead of polling: a log entry containing text (logged after the "
             "call, or after log.after_id), entities matching a filter existing or gone, a property reaching a "
             "value (component Transform is the pose: position.1 is world y; any component or Script field "
             "works), or a play_phase. Returns met, elapsed_ms and the last value seen; a timeout is met false, "
             "not an error. Mostly for play mode: start play, then wait for what the game should do.",
         .input_schema_json =
             R"json({"type":"object","properties":{"log":{"type":"object","properties":{"contains":{"type":"string"},"min_level":{"type":"string","enum":["trace","debug","info","warn","warning","error","critical"]},"after_id":{"type":"integer","minimum":0,"description":"Count entries after this id (next_after_id of logs_get_recent)"}},"required":["contains"]},"entity":{"type":"object","properties":{"name_contains":{"type":"string"},"name_exact":{"type":"string"},"component":{"type":"string"},"type_name":{"type":"string"},"exists":{"type":"boolean","description":"Wait for a match (true, default) or for none"}}},"property":{"type":"object","properties":{"entity_id":{"type":"string"},"component":{"type":"string","description":"Transform, a name from scene_list_components, or Script"},"type_name":{"type":"string","description":"Script type, for component Script"},"script_index":{"type":"integer","minimum":0},"name":{"type":"string","description":"Dotted path: light.intensity, position.1, speed"},"op":{"type":"string","enum":["==","!=","<","<=",">",">=","changed"]},"value":{"anyOf":[{"type":"number"},{"type":"boolean"},{"type":"string"},{"type":"array","items":{"type":"number"}}],"description":"What to compare with; not for changed"}},"required":["entity_id","component","name","op"]},"play_phase":{"type":"string","enum":["inactive","splash","active","running"]},"timeout_ms":{"type":"integer","minimum":0,"maximum":120000,"description":"Default 10000"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             condition_check check;
             std::string error;
             if(!make_condition(args, check, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto timeout = read_timeout_ms(args, k_wait_default_ms, k_wait_max_ms, "timeout_ms");
             const auto started = std::chrono::steady_clock::now();
             auto& mcp = ctx.get_cached<mcp_manager>();
             struct check_outcome
             {
                 bool is_met{};
                 std::string observed;
                 std::string error;
             };
             while(true)
             {
                 auto outcome = mcp.invoke_on_main(
                     [&ctx, &check]() -> check_outcome
                     {
                         check_outcome result;
                         result.is_met = check(ctx, result.observed, result.error);
                         return result;
                     });
                 if(!outcome || !outcome->error.empty())
                 {
                     return {.text = outcome ? outcome->error : "Timed out on the main thread", .is_error = true};
                 }
                 const auto elapsed = std::chrono::steady_clock::now() - started;
                 if(outcome->is_met || elapsed >= timeout)
                 {
                     return {.text = fmt::format(R"({{"met":{},"elapsed_ms":{},"observed":{}}})",
                                                 bool_to_json(outcome->is_met),
                                                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                                                 outcome->observed.empty() ? "null" : outcome->observed),
                             .is_error = false};
                 }
                 tpp::this_thread::sleep_for(k_poll_interval);
             }
         },
         .mutates_scene = false,
         .requires_main_thread = false,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
