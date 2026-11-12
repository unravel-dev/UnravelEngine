#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <hpp/string_view.hpp>
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
auto parse_mcp_profile(hpp::string_view name, mcp_profile& out) -> bool;

struct tool_result
{
    std::string text;
    bool is_error{false};
    /// Input the call ignored (unknown arguments, values with no effect); reported with the result.
    std::vector<std::string> warnings;
    /// Optional PNG (or other) payload returned as an MCP image content block.
    std::string image_base64;
    std::string image_mime{"image/png"};
};

using tool_handler = std::function<tool_result(rtti::context& ctx, const simdjson::dom::object& args)>;

/// What a call can do to the user's work. Sent as the MCP tool annotations a client decides
/// confirmations by.
enum class tool_effect : uint8_t
{
    /// Changes the editor or the project in ways edit_undo, or another call, takes back.
    changes,
    /// Changes nothing the user keeps (readOnlyHint).
    read_only,
    /// Can write over or remove what edit_undo does not bring back: files, settings, unsaved work
    /// (destructiveHint).
    destructive,
};

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
    tool_effect effect{tool_effect::changes};
    /// A second call with the same arguments changes nothing more (idempotentHint). Read-only
    /// tools are idempotent without saying so.
    bool idempotent{false};
    /// Reaches past this computer: publishes or uploads (openWorldHint).
    bool open_world{false};
    /// Writes the project's asset files: refused while a deploy runs, which reads them until it finishes.
    bool writes_project_files{false};
    /// Names the tool had before a rename. A call by one of them still works and its result names
    /// the current tool; tools/list shows only the current name.
    std::vector<std::string> former_names;
    /// Arguments renamed since: {path as it was, current name}. The path names the argument the way the
    /// warnings do, items in arrays as [] ("items[].index"). A call that still uses the old name is
    /// read as if it used the new one, with a warning; the schema shows only the new name.
    std::vector<std::pair<std::string, std::string>> former_arguments;
};

/// A call's arguments checked against the tool's input schema.
struct argument_check
{
    /// What refuses the call: a wrong type, a value outside the enum or the range, a missing required one.
    std::vector<std::string> errors;
    /// Arguments the schema does not declare; the call goes on and ignores them.
    std::vector<std::string> warnings;
    /// The arguments to hand the handler: as sent, with numbers written as integers where they are whole.
    std::string normalized_json;
};

class mcp_tool_registry
{
public:
    void add(mcp_tool tool);
    /// Looks up by name or former name regardless of profile, so callers can tell "no such tool" from
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
void register_create_tools(mcp_tool_registry& registry);
void register_ops_batch_tools(mcp_tool_registry& registry);
void register_asset_tools(mcp_tool_registry& registry);
void register_viewport_tools(mcp_tool_registry& registry);
void register_material_tools(mcp_tool_registry& registry);
void register_asset_property_tools(mcp_tool_registry& registry);
void register_project_tools(mcp_tool_registry& registry);
void register_settings_tools(mcp_tool_registry& registry);
void register_deploy_tools(mcp_tool_registry& registry);
void register_profile_tools(mcp_tool_registry& registry);
void register_input_tools(mcp_tool_registry& registry);
void register_input_action_tools(mcp_tool_registry& registry);
void register_ui_tools(mcp_tool_registry& registry);
void register_ui_inspect_tools(mcp_tool_registry& registry);
void register_prefab_tools(mcp_tool_registry& registry);
void register_prefab_instance_tools(mcp_tool_registry& registry);
void register_spatial_tools(mcp_tool_registry& registry);
void register_wait_tools(mcp_tool_registry& registry);
void register_script_tools(mcp_tool_registry& registry);
void register_editor_tools(mcp_tool_registry& registry);
void register_skill_tools(mcp_tool_registry& registry);
void register_animation_tools(mcp_tool_registry& registry);
void register_physics_tools(mcp_tool_registry& registry);

/**
 * @brief Checks a call's arguments against the tool's input schema: type, enum, const, minimum / maximum,
 *        string length, item counts, required properties and anyOf / oneOf forms. Objects whose schema
 *        lists no "properties" are taken as they are; keys a closed object does not declare become warnings.
 */
auto check_arguments(const mcp_tool& tool, const simdjson::dom::element& args) -> argument_check;

/// Refuses while a deploy runs: it reads the project's files and settings until it finishes. Any thread; off the main
/// thread it sees the running deploys only, not the Deploy window's queued steps. Defined with the deploy tools.
auto require_not_deploying(rtti::context& ctx, std::string& error) -> bool;

/// Server usage rules injected into clients via the MCP initialize "instructions" field.
auto get_server_instructions() -> const std::string&;

/// The skills as MCP resources, the resources/list array: one markdown document per skill at unravel://skills/<name>,
/// the guides skills_get serves.
auto list_skill_resources_json() -> std::string;

/**
 * @brief The resources/read contents array of the skill @p uri names.
 * @return False with @p error set when it names none.
 */
auto read_skill_resource_json(hpp::string_view uri, std::string& contents_json, std::string& error) -> bool;

} // namespace unravel::mcp
