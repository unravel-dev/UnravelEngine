#include "mcp_tool_registry.h"

#include "mcp_protocol.h"

#include <logging/logging.h>

namespace unravel::mcp
{

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

auto mcp_tool_registry::list_tools_json() const -> std::string
{
    std::string out = "[";
    for(size_t i = 0; i < tools_.size(); ++i)
    {
        const auto& tool = tools_[i];
        if(i > 0)
        {
            out += ",";
        }
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
