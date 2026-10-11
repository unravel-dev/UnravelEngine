#include "mcp_tool_registry.h"

#include "mcp_protocol.h"

#include <logging/logging.h>

#include <algorithm>

namespace unravel::mcp
{

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
        out += fmt::format(R"({{"name":{},"description":{},"inputSchema":{}}})",
                           make_json_string(tool.name),
                           make_json_string(tool.description),
                           tool.input_schema_json.empty() ? R"({"type":"object","properties":{}})"
                                                         : tool.input_schema_json);
    }
    out += "]";
    return out;
}

} // namespace unravel::mcp
