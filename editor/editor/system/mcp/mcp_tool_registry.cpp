#include "mcp_tool_registry.h"

#include "mcp_protocol.h"

#include <logging/logging.h>

#include <algorithm>

namespace unravel::mcp
{

namespace
{

/// Walks @p value along @p schema and reports every object key the schema does not declare.
void collect_unknown_arguments(const simdjson::dom::element& schema,
                               const simdjson::dom::element& value,
                               const std::string& path,
                               std::vector<std::string>& out)
{
    simdjson::dom::object schema_object;
    if(schema.get(schema_object))
    {
        return;
    }
    simdjson::dom::object properties;
    simdjson::dom::object value_object;
    if(!schema_object["properties"].get(properties) && !value.get(value_object))
    {
        bool closed = true;
        simdjson::dom::element additional;
        if(!schema_object["additionalProperties"].get(additional))
        {
            bool allowed = true;
            closed = !additional.get(allowed) && !allowed;
        }
        std::string accepted;
        for(auto property : properties)
        {
            accepted += (accepted.empty() ? "" : ", ") + std::string(property.key);
        }
        for(auto field : value_object)
        {
            const std::string field_path = (path.empty() ? "" : path + ".") + std::string(field.key);
            simdjson::dom::element field_schema;
            if(!properties[field.key].get(field_schema))
            {
                collect_unknown_arguments(field_schema, field.value, field_path, out);
            }
            else if(closed)
            {
                auto message = fmt::format("Unknown argument {} was ignored (accepted here: {})", field_path, accepted);
                if(std::find(out.begin(), out.end(), message) == out.end())
                {
                    out.push_back(std::move(message));
                }
            }
        }
        return;
    }
    simdjson::dom::element items;
    simdjson::dom::array value_array;
    if(!schema_object["items"].get(items) && !value.get(value_array))
    {
        for(auto element : value_array)
        {
            collect_unknown_arguments(items, element, path + "[]", out);
        }
    }
}

/// The MCP tool annotations. Everything a tool touches is this editor and its project, never an
/// open world.
auto annotations_json(const mcp_tool& tool) -> std::string
{
    const bool is_read_only = tool.effect == tool_effect::read_only;
    return fmt::format(R"({{"readOnlyHint":{},"destructiveHint":{},"idempotentHint":{},"openWorldHint":false}})",
                       is_read_only ? "true" : "false",
                       tool.effect == tool_effect::destructive ? "true" : "false",
                       is_read_only || tool.idempotent ? "true" : "false");
}

} // namespace

auto find_unknown_arguments(const mcp_tool& tool, const simdjson::dom::element& args) -> std::vector<std::string>
{
    std::vector<std::string> unknown;
    simdjson::dom::parser parser;
    simdjson::dom::element schema;
    if(!parser.parse(tool.input_schema_json).get(schema))
    {
        collect_unknown_arguments(schema, args, {}, unknown);
    }
    return unknown;
}

auto to_string(mcp_profile profile) -> const char*
{
    return profile == mcp_profile::developer ? "developer" : "user";
}

auto parse_mcp_profile(std::string_view name, mcp_profile& out) -> bool
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
    if(index_.count(tool.name) != 0)
    {
        tools_[index_[tool.name]] = std::move(tool);
        return;
    }
    index_[tool.name] = tools_.size();
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
