#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

namespace unravel::mcp
{

struct tool_result
{
    std::string text;
    bool is_error{false};
    /// Optional PNG (or other) payload returned as an MCP image content block.
    std::string image_base64;
    std::string image_mime{"image/png"};
};

using tool_handler = std::function<tool_result(rtti::context& ctx, const simdjson::dom::object& args)>;

struct mcp_tool
{
    std::string name;
    std::string description;
    std::string input_schema_json;
    tool_handler handler;
    bool mutates_scene{false};
    /// When true (default), the handler runs entirely on the editor main thread.
    /// Set false for action+wait tools (screenshot, compile-and-poll) that must
    /// hop to main for the action, then wait on the HTTP worker while frames pump.
    bool requires_main_thread{true};
};

class mcp_tool_registry
{
public:
    void add(mcp_tool tool);
    auto find(const std::string& name) const -> const mcp_tool*;
    auto tools() const -> const std::vector<mcp_tool>&;
    auto list_tools_json() const -> std::string;

private:
    std::vector<mcp_tool> tools_;
    std::unordered_map<std::string, size_t> index_;
};

void register_scene_tools(mcp_tool_registry& registry);
void register_scene_batch_tools(mcp_tool_registry& registry);
void register_ops_batch_tools(mcp_tool_registry& registry);
void register_asset_tools(mcp_tool_registry& registry);
void register_viewport_tools(mcp_tool_registry& registry);
void register_material_tools(mcp_tool_registry& registry);
void register_project_tools(mcp_tool_registry& registry);
void register_script_tools(mcp_tool_registry& registry);
void register_editor_tools(mcp_tool_registry& registry);

} // namespace unravel::mcp
