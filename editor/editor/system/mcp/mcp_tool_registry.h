#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <context/context.hpp>
#include <ser20/external/simdjson/simdjson.h>

namespace unravel::mcp
{

/// Which slice of the tool surface a client sees. Engine-development diagnostics
/// (GI experiments, temporal measurement, CPU scopes) are noise to a client that is
/// authoring a game, and they cost a third of the tool list's description budget.
enum class mcp_profile : uint8_t
{
    user,      ///< Game authoring only.
    developer, ///< Everything, including the engine diagnostics.
};

auto to_string(mcp_profile profile) -> const char*;

/// Parses "user" / "developer". Returns false on anything else.
auto parse_mcp_profile(std::string_view name, mcp_profile& out) -> bool;

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
    /// Hidden from clients on the user profile. For tools that only make sense while
    /// working on the engine itself.
    bool developer_only{false};
};

class mcp_tool_registry
{
public:
    void add(mcp_tool tool);
    /// Looks up by name regardless of profile, so callers can tell "no such tool" from
    /// "that tool is not in this profile" and say which.
    auto find(const std::string& name) const -> const mcp_tool*;
    auto tools() const -> const std::vector<mcp_tool>&;
    /// Only the tools the active profile exposes.
    auto list_tools_json() const -> std::string;

    void set_profile(mcp_profile profile);
    auto get_profile() const -> mcp_profile;
    auto is_available(const mcp_tool& tool) const -> bool;
    auto visible_tool_count() const -> size_t;

private:
    std::vector<mcp_tool> tools_;
    std::unordered_map<std::string, size_t> index_;
    mcp_profile profile_{mcp_profile::developer};
};

void register_scene_tools(mcp_tool_registry& registry);
void register_scene_batch_tools(mcp_tool_registry& registry);
void register_ops_batch_tools(mcp_tool_registry& registry);
void register_asset_tools(mcp_tool_registry& registry);
void register_viewport_tools(mcp_tool_registry& registry);
void register_material_tools(mcp_tool_registry& registry);
void register_project_tools(mcp_tool_registry& registry);
void register_settings_tools(mcp_tool_registry& registry);
void register_deploy_tools(mcp_tool_registry& registry);
void register_input_tools(mcp_tool_registry& registry);
void register_input_action_tools(mcp_tool_registry& registry);
void register_physics_tools(mcp_tool_registry& registry);
void register_ui_tools(mcp_tool_registry& registry);
void register_prefab_tools(mcp_tool_registry& registry);
void register_script_tools(mcp_tool_registry& registry);
void register_editor_tools(mcp_tool_registry& registry);
void register_skill_tools(mcp_tool_registry& registry);

/// Server usage rules injected into clients via the MCP initialize "instructions" field.
auto get_server_instructions() -> const std::string&;

} // namespace unravel::mcp
