#include "mcp_tool_registry.h"

#include "mcp_protocol.h"

#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace unravel::mcp
{

namespace
{

/// More problems than this in one call are summed up as a count.
constexpr size_t k_max_reported_problems = 20;

/// The JSON types a schema node allows; empty when it says none.
auto get_schema_types(const simdjson::dom::object& schema) -> std::vector<std::string>
{
    std::vector<std::string> types;
    simdjson::dom::element type;
    if(schema["type"].get(type))
    {
        return types;
    }
    hpp::string_view single;
    if(!type.get(single))
    {
        types.emplace_back(single);
        return types;
    }
    simdjson::dom::array list;
    if(!type.get(list))
    {
        for(auto entry : list)
        {
            if(!entry.get(single))
            {
                types.emplace_back(single);
            }
        }
    }
    return types;
}

auto is_whole_number(double value) -> bool
{
    constexpr double k_int64_limit = 9223372036854775808.0;
    return std::isfinite(value) && std::floor(value) == value && value >= -k_int64_limit && value < k_int64_limit;
}

auto read_number(const simdjson::dom::element& value, double& out) -> bool
{
    return !value.get(out);
}

auto matches_type(const simdjson::dom::element& value, hpp::string_view type) -> bool
{
    const auto kind = value.type();
    if(type == "object")
    {
        return kind == simdjson::dom::element_type::OBJECT;
    }
    if(type == "array")
    {
        return kind == simdjson::dom::element_type::ARRAY;
    }
    if(type == "string")
    {
        return kind == simdjson::dom::element_type::STRING;
    }
    if(type == "boolean")
    {
        return kind == simdjson::dom::element_type::BOOL;
    }
    if(type == "null")
    {
        return kind == simdjson::dom::element_type::NULL_VALUE;
    }
    if(type == "number")
    {
        return kind == simdjson::dom::element_type::INT64 || kind == simdjson::dom::element_type::UINT64 ||
               kind == simdjson::dom::element_type::DOUBLE;
    }
    if(type == "integer")
    {
        double number = 0.0;
        return kind == simdjson::dom::element_type::INT64 || kind == simdjson::dom::element_type::UINT64 ||
               (kind == simdjson::dom::element_type::DOUBLE && read_number(value, number) && is_whole_number(number));
    }
    return true;
}

/// "a string", "the number 3.5", ... for messages.
auto describe_value(const simdjson::dom::element& value) -> std::string
{
    constexpr size_t k_max_quoted_length = 40;
    switch(value.type())
    {
        case simdjson::dom::element_type::OBJECT:
            return "an object";
        case simdjson::dom::element_type::ARRAY:
            return "an array";
        case simdjson::dom::element_type::STRING:
        {
            auto text = std::string(simdjson::minify(value));
            if(text.size() > k_max_quoted_length)
            {
                text = text.substr(0, k_max_quoted_length) + "...\"";
            }
            return "the string " + text;
        }
        case simdjson::dom::element_type::BOOL:
            return "a boolean";
        case simdjson::dom::element_type::NULL_VALUE:
            return "null";
        default:
            return "the number " + std::string(simdjson::minify(value));
    }
}

auto join_names(const std::vector<std::string>& names, hpp::string_view separator) -> std::string
{
    std::string joined;
    for(const auto& name : names)
    {
        joined += (joined.empty() ? "" : std::string(separator)) + name;
    }
    return joined;
}

auto values_equal(const simdjson::dom::element& lhs, const simdjson::dom::element& rhs) -> bool
{
    double lhs_number = 0.0;
    double rhs_number = 0.0;
    if(read_number(lhs, lhs_number) && read_number(rhs, rhs_number))
    {
        return lhs_number == rhs_number;
    }
    return lhs.type() == rhs.type() && std::string(simdjson::minify(lhs)) == std::string(simdjson::minify(rhs));
}

/**
 * @brief Walks a call's arguments along the tool's schema, collecting what is wrong and writing the
 *        arguments out again. Numbers are written in their shortest form, so a whole number sent as 5.0
 *        reaches the handler as 5.
 */
class argument_checker
{
public:
    explicit argument_checker(argument_check& out,
                              const std::vector<std::pair<std::string, std::string>>* former_arguments = nullptr)
        : out_(out)
        , former_arguments_(former_arguments)
    {
    }

    void check(const simdjson::dom::element& schema, const simdjson::dom::element& value, const std::string& path, std::string& json)
    {
        simdjson::dom::object schema_object;
        if(schema.get(schema_object))
        {
            write_as_is(value, json);
            return;
        }
        if(check_alternatives(schema_object, value, path, json))
        {
            return;
        }
        const auto types = get_schema_types(schema_object);
        if(!types.empty() && std::none_of(types.begin(),
                                          types.end(),
                                          [&](const std::string& type)
                                          {
                                              return matches_type(value, type);
                                          }))
        {
            add_error(path, "expected " + join_names(types, " or ") + ", got " + describe_value(value));
            write_as_is(value, json);
            return;
        }
        check_allowed_values(schema_object, value, path);
        switch(value.type())
        {
            case simdjson::dom::element_type::OBJECT:
                check_object(schema_object, value, path, json);
                return;
            case simdjson::dom::element_type::ARRAY:
                check_array(schema_object, value, path, json);
                return;
            case simdjson::dom::element_type::STRING:
                check_string(schema_object, value, path);
                write_as_is(value, json);
                return;
            case simdjson::dom::element_type::INT64:
            case simdjson::dom::element_type::UINT64:
            case simdjson::dom::element_type::DOUBLE:
                check_number(schema_object, types, value, path);
                write_number(value, json);
                return;
            default:
                write_as_is(value, json);
                return;
        }
    }

private:
    void add_error(const std::string& path, const std::string& message)
    {
        ++problem_count_;
        if(out_.errors.size() < k_max_reported_problems)
        {
            out_.errors.push_back((path.empty() ? std::string("arguments") : path) + ": " + message);
        }
    }

    static void write_as_is(const simdjson::dom::element& value, std::string& json)
    {
        json += std::string(simdjson::minify(value));
    }

    static void write_number(const simdjson::dom::element& value, std::string& json)
    {
        double number = 0.0;
        if(value.type() == simdjson::dom::element_type::DOUBLE && read_number(value, number))
        {
            json += fmt::format("{}", number);
            return;
        }
        write_as_is(value, json);
    }

    /// anyOf / oneOf: the first form the value matches is taken. Returns whether the node had any.
    auto check_alternatives(const simdjson::dom::object& schema,
                            const simdjson::dom::element& value,
                            const std::string& path,
                            std::string& json) -> bool
    {
        simdjson::dom::array forms;
        if(schema["anyOf"].get(forms) && schema["oneOf"].get(forms))
        {
            return false;
        }
        for(auto form : forms)
        {
            argument_check trial;
            argument_checker checker(trial, former_arguments_);
            std::string form_json;
            checker.check(form, value, path, form_json);
            if(trial.errors.empty())
            {
                json += form_json;
                out_.warnings.insert(out_.warnings.end(), trial.warnings.begin(), trial.warnings.end());
                return true;
            }
        }
        add_error(path, describe_value(value) + " matches none of the accepted forms");
        write_as_is(value, json);
        return true;
    }

    void check_allowed_values(const simdjson::dom::object& schema, const simdjson::dom::element& value, const std::string& path)
    {
        simdjson::dom::array allowed;
        if(!schema["enum"].get(allowed))
        {
            std::vector<std::string> listed;
            bool is_listed = false;
            for(auto entry : allowed)
            {
                is_listed = is_listed || values_equal(entry, value);
                listed.emplace_back(simdjson::minify(entry));
            }
            if(!is_listed)
            {
                add_error(path, describe_value(value) + " is not one of " + join_names(listed, ", "));
            }
        }
        simdjson::dom::element constant;
        if(!schema["const"].get(constant) && !values_equal(constant, value))
        {
            add_error(path, "must be " + std::string(simdjson::minify(constant)));
        }
    }

    void check_number(const simdjson::dom::object& schema,
                      const std::vector<std::string>& types,
                      const simdjson::dom::element& value,
                      const std::string& path)
    {
        double number = 0.0;
        read_number(value, number);
        const bool wants_integer = std::find(types.begin(), types.end(), "integer") != types.end() &&
                                   std::find(types.begin(), types.end(), "number") == types.end();
        if(wants_integer && !is_whole_number(number))
        {
            add_error(path, "expected a whole number, got " + describe_value(value));
            return;
        }
        double bound = 0.0;
        if(!schema["minimum"].get(bound) && number < bound)
        {
            add_error(path, fmt::format("{} is below the minimum {}", number, bound));
        }
        if(!schema["maximum"].get(bound) && number > bound)
        {
            add_error(path, fmt::format("{} is above the maximum {}", number, bound));
        }
        if(!schema["exclusiveMinimum"].get(bound) && number <= bound)
        {
            add_error(path, fmt::format("{} must be greater than {}", number, bound));
        }
        if(!schema["exclusiveMaximum"].get(bound) && number >= bound)
        {
            add_error(path, fmt::format("{} must be less than {}", number, bound));
        }
    }

    void check_string(const simdjson::dom::object& schema, const simdjson::dom::element& value, const std::string& path)
    {
        hpp::string_view text;
        // The type was checked before this runs.
        (void)value.get(text);
        uint64_t limit = 0;
        if(!schema["minLength"].get(limit) && text.size() < limit)
        {
            add_error(path, fmt::format("needs at least {} characters", limit));
        }
        if(!schema["maxLength"].get(limit) && text.size() > limit)
        {
            add_error(path, fmt::format("takes at most {} characters", limit));
        }
    }

    void check_array(const simdjson::dom::object& schema,
                     const simdjson::dom::element& value,
                     const std::string& path,
                     std::string& json)
    {
        simdjson::dom::array items;
        (void)value.get(items);
        uint64_t limit = 0;
        if(!schema["minItems"].get(limit) && items.size() < limit)
        {
            add_error(path, fmt::format("needs at least {} item{}", limit, limit == 1 ? "" : "s"));
        }
        if(!schema["maxItems"].get(limit) && items.size() > limit)
        {
            add_error(path, fmt::format("takes at most {} items, got {}", limit, items.size()));
        }
        simdjson::dom::element item_schema;
        const bool has_item_schema = !schema["items"].get(item_schema);
        json += "[";
        size_t index = 0;
        for(auto item : items)
        {
            json += index == 0 ? "" : ",";
            const auto item_path = fmt::format("{}[{}]", path, index);
            if(has_item_schema)
            {
                check(item_schema, item, item_path, json);
            }
            else
            {
                write_as_is(item, json);
            }
            ++index;
        }
        json += "]";
    }

    void check_object(const simdjson::dom::object& schema,
                      const simdjson::dom::element& value,
                      const std::string& path,
                      std::string& json)
    {
        simdjson::dom::object fields;
        (void)value.get(fields);
        simdjson::dom::object properties;
        if(schema["properties"].get(properties))
        {
            write_as_is(value, json);
            return;
        }
        check_required(schema, fields, path);
        simdjson::dom::element additional;
        const bool has_additional = !schema["additionalProperties"].get(additional);
        bool allows_additional = false;
        const bool is_open = has_additional && !additional.get(allows_additional) && allows_additional;
        json += "{";
        bool is_first = true;
        for(auto field : fields)
        {
            json += is_first ? "" : ",";
            is_first = false;
            const std::string key = current_argument_name(path, std::string(field.key));
            json += make_json_string(key) + ":";
            const std::string field_path = (path.empty() ? "" : path + ".") + key;
            simdjson::dom::element field_schema;
            if(!properties[key].get(field_schema))
            {
                check(field_schema, field.value, field_path, json);
                continue;
            }
            if(has_additional && additional.type() == simdjson::dom::element_type::OBJECT)
            {
                check(additional, field.value, field_path, json);
                continue;
            }
            write_as_is(field.value, json);
            if(!is_open)
            {
                warn_unknown(properties, field_path);
            }
        }
        json += "}";
    }

    void check_required(const simdjson::dom::object& schema, const simdjson::dom::object& fields, const std::string& path)
    {
        simdjson::dom::array required;
        if(schema["required"].get(required))
        {
            return;
        }
        for(auto entry : required)
        {
            hpp::string_view name;
            if(entry.get(name))
            {
                continue;
            }
            const bool is_present = std::any_of(fields.begin(),
                                                fields.end(),
                                                [&](const simdjson::dom::key_value_pair& field)
                                                {
                                                    return current_argument_name(path, std::string(field.key), false) == name;
                                                });
            if(!is_present)
            {
                add_error((path.empty() ? "" : path + ".") + std::string(name), "missing (required)");
            }
        }
    }

    /// The name @p key has now: a former argument name at @p path is read as its current one, with a warning (once
    /// when @p warns).
    auto current_argument_name(const std::string& path, const std::string& key, bool warns = true) -> std::string
    {
        if(former_arguments_ == nullptr)
        {
            return key;
        }
        // Paths as former_arguments write them: array items as [], not [0].
        std::string pattern;
        for(size_t i = 0; i < path.size(); ++i)
        {
            if(path[i] == '[')
            {
                pattern += "[]";
                i = path.find(']', i);
                continue;
            }
            pattern += path[i];
        }
        const auto full = (pattern.empty() ? "" : pattern + ".") + key;
        for(const auto& [former, current] : *former_arguments_)
        {
            if(former == full)
            {
                if(warns)
                {
                    add_warning(fmt::format("Argument {} is now called {}; the old name keeps working", full, current));
                }
                return current;
            }
        }
        return key;
    }

    void add_warning(std::string message)
    {
        if(std::find(out_.warnings.begin(), out_.warnings.end(), message) == out_.warnings.end())
        {
            out_.warnings.push_back(std::move(message));
        }
    }

    void warn_unknown(const simdjson::dom::object& properties, const std::string& field_path)
    {
        std::vector<std::string> accepted;
        for(auto property : properties)
        {
            accepted.emplace_back(property.key);
        }
        auto message = accepted.empty()
                           ? fmt::format("Unknown argument {} was ignored (this takes no arguments)", field_path)
                           : fmt::format("Unknown argument {} was ignored (accepted here: {})",
                                         field_path,
                                         join_names(accepted, ", "));
        if(std::find(out_.warnings.begin(), out_.warnings.end(), message) == out_.warnings.end())
        {
            out_.warnings.push_back(std::move(message));
        }
    }

public:
    /// Problems found, including those past the reported ones.
    size_t problem_count_{};

private:
    argument_check& out_;
    const std::vector<std::pair<std::string, std::string>>* former_arguments_{};
};

/// The MCP tool annotations.
auto annotations_json(const mcp_tool& tool) -> std::string
{
    const bool is_read_only = tool.effect == tool_effect::read_only;
    return fmt::format(R"({{"readOnlyHint":{},"destructiveHint":{},"idempotentHint":{},"openWorldHint":{}}})",
                       is_read_only ? "true" : "false",
                       tool.effect == tool_effect::destructive ? "true" : "false",
                       is_read_only || tool.idempotent ? "true" : "false",
                       tool.open_world ? "true" : "false");
}

/// Clients that read tool schemas as the Claude API does refuse a whole tool list in which one input schema combines
/// alternatives at its top (anyOf, oneOf, allOf, not): such a schema is logged as an error. A handler checks which
/// arguments go together instead.
void report_top_level_combinators(const mcp_tool& tool)
{
    simdjson::dom::parser parser;
    simdjson::dom::object schema;
    if(parser.parse(tool.input_schema_json).get(schema) != simdjson::SUCCESS)
    {
        APPLOG_ERROR("MCP tool {}: its input schema is not a JSON object", tool.name);
        return;
    }
    for(const char* key : {"anyOf", "oneOf", "allOf", "not"})
    {
        simdjson::dom::element combinator;
        if(schema[key].get(combinator) == simdjson::SUCCESS)
        {
            APPLOG_ERROR("MCP tool {}: its input schema has {} at the top, which some clients refuse", tool.name, key);
        }
    }
}

} // namespace

auto check_arguments(const mcp_tool& tool, const simdjson::dom::element& args) -> argument_check
{
    argument_check result;
    simdjson::dom::parser parser;
    simdjson::dom::element schema;
    const std::string schema_json = tool.input_schema_json.empty() ? R"({"type":"object","properties":{}})"
                                                                   : tool.input_schema_json;
    if(parser.parse(schema_json).get(schema))
    {
        result.normalized_json = std::string(simdjson::minify(args));
        return result;
    }
    argument_checker checker(result, &tool.former_arguments);
    checker.check(schema, args, {}, result.normalized_json);
    if(checker.problem_count_ > result.errors.size())
    {
        result.errors.push_back(fmt::format("... and {} more", checker.problem_count_ - result.errors.size()));
    }
    return result;
}

auto to_string(mcp_profile profile) -> const char*
{
    return profile == mcp_profile::developer ? "developer" : "user";
}

auto parse_mcp_profile(hpp::string_view name, mcp_profile& out) -> bool
{
    if(name == "user")
    {
        out = mcp_profile::user;
        return true;
    }
    if(name == "developer")
    {
        out = mcp_profile::developer;
        return true;
    }
    return false;
}

void mcp_tool_registry::add(mcp_tool tool)
{
    report_top_level_combinators(tool);
    const auto existing = index_.find(tool.name);
    const size_t position = existing != index_.end() ? existing->second : tools_.size();
    for(const auto& former_name : tool.former_names)
    {
        index_[former_name] = position;
    }
    index_[tool.name] = position;
    if(position < tools_.size())
    {
        tools_[position] = std::move(tool);
        return;
    }
    tools_.push_back(std::move(tool));
}

auto mcp_tool_registry::find(const std::string& name) const -> const mcp_tool*
{
    auto it = index_.find(name);
    if(it == index_.end())
    {
        return nullptr;
    }
    return &tools_[it->second];
}

auto mcp_tool_registry::tools() const -> const std::vector<mcp_tool>&
{
    return tools_;
}

void mcp_tool_registry::set_profile(mcp_profile profile)
{
    profile_ = profile;
}

auto mcp_tool_registry::get_profile() const -> mcp_profile
{
    return profile_;
}

auto mcp_tool_registry::is_available(const mcp_tool& tool) const -> bool
{
    return !tool.developer_only || profile_ == mcp_profile::developer;
}

auto mcp_tool_registry::visible_tool_count() const -> size_t
{
    return static_cast<size_t>(std::count_if(tools_.begin(),
                                             tools_.end(),
                                             [this](const mcp_tool& tool)
                                             {
                                                 return is_available(tool);
                                             }));
}

auto mcp_tool_registry::list_tools_json() const -> std::string
{
    std::string out = "[";
    bool first = true;
    for(const auto& tool : tools_)
    {
        if(!is_available(tool))
        {
            continue;
        }
        if(!first)
        {
            out += ",";
        }
        first = false;
        out += fmt::format(R"({{"name":{},"description":{},"inputSchema":{},"annotations":{}}})",
                           make_json_string(tool.name),
                           make_json_string(tool.description),
                           tool.input_schema_json.empty() ? R"({"type":"object","properties":{}})"
                                                         : tool.input_schema_json,
                           annotations_json(tool));
    }
    out += "]";
    return out;
}

} // namespace unravel::mcp
