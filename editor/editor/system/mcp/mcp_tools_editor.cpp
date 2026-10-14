#include "mcp_input_utils.h"
#include "mcp_tools_common.h"

#include <editor/editing/editor_actions.h>
#include <editor/editing/entity_inspect.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/ecs/ecs_utils.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <logging/logging.h>

namespace unravel::mcp
{
namespace
{

/// Most frames one play_skip_frame call steps: ten seconds of game time.
constexpr int64_t k_max_skip_frames = 600;
/// Largest burst one particles_control_batch emit queues.
constexpr int64_t k_max_emit_count = 100000;

auto level_to_string(level::level_enum level) -> const char*
{
    switch(level)
    {
        case level::trace:
            return "trace";
        case level::debug:
            return "debug";
        case level::info:
            return "info";
        case level::warn:
            return "warn";
        case level::err:
            return "error";
        case level::critical:
            return "critical";
        default:
            return "off";
    }
}

auto parse_min_level(const std::string& name, level::level_enum& out) -> bool
{
    if(name.empty() || name == "trace")
    {
        out = level::trace;
        return true;
    }
    if(name == "debug")
    {
        out = level::debug;
        return true;
    }
    if(name == "info")
    {
        out = level::info;
        return true;
    }
    if(name == "warn" || name == "warning")
    {
        out = level::warn;
        return true;
    }
    if(name == "error" || name == "err")
    {
        out = level::err;
        return true;
    }
    if(name == "critical")
    {
        out = level::critical;
        return true;
    }
    return false;
}

/// The tool surface this editor serves, so a client can tell which profile it has
/// without probing for a tool and reading the refusal.
auto mcp_status_json(rtti::context& ctx) -> std::string
{
    if(!ctx.has<mcp_manager>())
    {
        return "null";
    }
    const auto& manager = ctx.get_cached<mcp_manager>();
    return fmt::format(R"({{"profile":{},"tools_exposed":{},"tools_total":{}}})",
                       make_json_string(manager.get_profile_name()),
                       manager.get_visible_tool_count(),
                       manager.get_tool_count());
}

/// An emitter's playback and simulation state, as JSON fields without the braces.
auto emitter_state_fields(const particle_emitter_component& emitter) -> std::string
{
    const bool always_simulate =
        emitter.get_culling_mode() == particle_emitter_component::culling_mode::always_simulate;
    return fmt::format(R"("num_particles":{},"playing":{},"paused":{},"simulated":{},"culling_mode":"{}")",
                       emitter.get_num_particles(),
                       bool_to_json(emitter.is_playing()),
                       bool_to_json(emitter.is_paused()),
                       bool_to_json(emitter.is_simulated()),
                       always_simulate ? "always_simulate" : "renderer_based");
}

/// A playback action the inspector's buttons offer, and its counterpart for the emitters below.
struct playback_action
{
    const char* name;
    void (particle_emitter_component::*apply)();
    void (particle_emitter_component::*apply_to_children)();
};

const std::array<playback_action, 5> k_playback_actions{{
    {"play", &particle_emitter_component::play, &particle_emitter_component::play_child_emitters},
    {"pause", &particle_emitter_component::pause, &particle_emitter_component::pause_child_emitters},
    {"resume", &particle_emitter_component::resume, &particle_emitter_component::resume_child_emitters},
    {"stop", &particle_emitter_component::stop, &particle_emitter_component::stop_child_emitters},
    {"stop_and_reset",
     &particle_emitter_component::stop_and_reset,
     &particle_emitter_component::stop_and_reset_child_emitters},
}};

/// Runs one action: status changes nothing, emit queues a burst of @p count for the next update, and
/// the playback actions run as the inspector's buttons do, with_children reaching the emitters below.
auto apply_emitter_action(particle_emitter_component& emitter,
                          const std::string& action,
                          uint32_t count,
                          bool with_children,
                          std::string& error) -> bool
{
    if(action == "status")
    {
        return true;
    }
    if(action == "emit")
    {
        emitter.emit(count);
        return true;
    }
    for(const auto& playback : k_playback_actions)
    {
        if(action == playback.name)
        {
            (emitter.*playback.apply)();
            if(with_children)
            {
                (emitter.*playback.apply_to_children)();
            }
            return true;
        }
    }
    error = "action must be status, emit, play, pause, resume, stop or stop_and_reset";
    return false;
}

/// One particles_control_batch item: runs its action and reports the emitter's state.
auto control_emitter_item(scene& scn, const simdjson::dom::element& value, size_t& failed) -> std::string
{
    const auto fail = [&failed](const std::string& entity_id, const std::string& error) -> std::string
    {
        ++failed;
        return fmt::format(R"({{"ok":false,"entity_id":{},"error":{}}})",
                           make_json_string(entity_id),
                           make_json_string(error));
    };
    std::string error;
    simdjson::dom::object item;
    std::string entity_id;
    if(!read_object(value, item, error) || !read_string(item, "entity_id", entity_id))
    {
        return fail(entity_id, error.empty() ? "Each item needs entity_id" : error);
    }
    auto entity = resolve_entity(scn, entity_id, error);
    if(!entity)
    {
        return fail(entity_id, error);
    }
    auto* emitter = entity.try_get<particle_emitter_component>();
    if(emitter == nullptr)
    {
        return fail(entity_id, "Entity has no Particle Emitter");
    }
    std::string action = "status";
    read_string(item, "action", action);
    bool with_children = false;
    read_bool(item, "with_children", with_children);
    const auto count = std::clamp<int64_t>(read_int64_or(item, "count", 1), 1, k_max_emit_count);
    if(!apply_emitter_action(*emitter, action, static_cast<uint32_t>(count), with_children, error))
    {
        return fail(entity_id, error);
    }
    std::string json = fmt::format(R"({{"ok":true,"entity":{},"action":{},{})",
                                   entity_to_lean_json(entity, false),
                                   make_json_string(action),
                                   emitter_state_fields(*emitter));
    if(with_children)
    {
        std::string children;
        for_each_component_in_children<particle_emitter_component>(
            entity,
            [&children](particle_emitter_component& child)
            {
                children += (children.empty() ? "" : ",") +
                            fmt::format(R"({{"entity":{},{}}})",
                                        entity_to_lean_json(child.get_owner(), false),
                                        emitter_state_fields(child));
            });
        json += R"(,"children":[)" + children + "]";
    }
    return json + "}";
}

auto play_state_to_json(const play_state_info& info) -> std::string
{
    return fmt::format(
        R"({{"phase":{},"is_active":{},"is_paused":{},"is_splash":{},"is_simulation_running":{},"frames_running":{},"frames_simulated":{}}})",
        make_json_string(info.phase),
        bool_to_json(info.is_active),
        bool_to_json(info.is_paused),
        bool_to_json(info.is_splash),
        bool_to_json(info.is_simulation_running),
        info.frames_running,
        info.frames_simulated);
}

} // namespace

void register_editor_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "editor_get_status",
         .description =
             "Bootstrap status: open project, active scene, prefab mode, play state and the active MCP "
             "profile in "
             "one call. Prefer this over separate project_get_info + scene_get_info + play_get_state.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 const auto play_json = play_state_to_json(editor_actions::get_play_state(ctx));
                 const auto prefab_json = ctx.has<editing_manager>()
                                              ? prefab_mode_status_json(ctx.get_cached<editing_manager>())
                                              : std::string(R"({"active":false})");
                 return {.text = fmt::format(R"({{"project":{},"scene":{},"prefab_mode":{},"play":{},"mcp":{}}})",
                                             project_info_json(ctx),
                                             active_scene_status_json(ctx),
                                             prefab_json,
                                             play_json,
                                             mcp_status_json(ctx)),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_get_state",
         .description = "Get editor play mode state: phase, active, paused, splash, frames_running (every frame "
                        "since play began running, paused ones too) and frames_simulated (frames the game "
                        "advanced: unpaused frames and play_skip_frame steps).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 return {.text = play_state_to_json(editor_actions::get_play_state(ctx)), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_set_active",
         .description =
             "Enter or exit play mode. Blocked when scripts have compile errors. Optional allow_splash.",
         .input_schema_json =
             R"({"type":"object","properties":{"active":{"type":"boolean"},"allow_splash":{"type":"boolean"}},"required":["active"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 bool active = false;
                 if(!read_bool(args, "active", active))
                 {
                     return {.text = "Missing active", .is_error = true};
                 }
                 bool allow_splash = true;
                 read_bool(args, "allow_splash", allow_splash);
                 std::string error;
                 if(!editor_actions::set_play_active(ctx, active, allow_splash, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"active":{}}})", bool_to_json(active)),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_set_paused",
         .description = "Pause or resume simulation while play mode is active.",
         .input_schema_json =
             R"({"type":"object","properties":{"paused":{"type":"boolean"}},"required":["paused"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 bool paused = false;
                 if(!read_bool(args, "paused", paused))
                 {
                     return {.text = "Missing paused", .is_error = true};
                 }
                 std::string error;
                 if(!editor_actions::set_play_paused(ctx, paused, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"paused":{}}})", bool_to_json(paused)),
                         .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "play_skip_frame",
         .description = "Advance frames while play mode is paused: scripts (Update, LateUpdate), physics, "
                        "animation and particles run once per frame over 1/60 s of game time (scaled by the "
                        "time scale). count (default 1, up to 600) steps that many frames; the call returns "
                        "once they have run, so a capture right after shows the last one.",
         .input_schema_json =
             R"json({"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":600,"description":"Default 1"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 auto& mcp = ctx.get_cached<mcp_manager>();
                 const auto count = std::clamp<int64_t>(read_int64_or(args, "count", 1), 1, k_max_skip_frames);
                 auto queued = mcp.invoke_on_main(
                     [&ctx, count]() -> tool_result
                     {
                         std::string error;
                         for(int64_t step = 0; step < count; ++step)
                         {
                             if(!editor_actions::skip_play_frame(ctx, &error))
                             {
                                 return {.text = error, .is_error = true};
                             }
                         }
                         return {.text = "ok", .is_error = false};
                     });
                 if(!queued)
                 {
                     return {.text = "Timed out reaching the editor main thread", .is_error = true};
                 }
                 if(queued->is_error)
                 {
                     return *queued;
                 }
                 // The steps run one per frame; one more frame lets the last one finish.
                 if(!wait_input_frames(ctx, static_cast<uint64_t>(count) + 1))
                 {
                     return {.text = "The editor did not advance the requested frames", .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"frames":{}}})", count), .is_error = false};
             },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "particles_control_batch",
         .description =
             "Play, pause, resume, stop, stop_and_reset or emit particle emitters, in edit or play mode, "
             "as the inspector's playback buttons do, and read their state. Each item: entity_id, action "
             "(default status), count for emit (a burst, whatever the emission rate), with_children to "
             "reach the emitters below the entity as well. Every item reports num_particles, playing, "
             "paused, simulated (false while renderer-based culling freezes an emitter no camera sees) "
             "and culling_mode, as of this frame: an emitted burst shows up on the next one. "
             "with_children also lists the child emitters' state.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","items":{"type":"object","properties":{"entity_id":{"type":"string"},"action":{"type":"string","enum":["status","play","pause","resume","stop","stop_and_reset","emit"]},"count":{"type":"integer","minimum":1,"maximum":100000},"with_children":{"type":"boolean"}},"required":["entity_id"]}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 scene* scn = nullptr;
                 std::string error;
                 simdjson::dom::array items;
                 if(!require_active_scene(ctx, scn, error) || !read_required_array(args, "items", items, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string results;
                 size_t count = 0;
                 size_t failed = 0;
                 for(auto item : items)
                 {
                     results += (count++ > 0 ? "," : "") + control_emitter_item(*scn, item, failed);
                 }
                 if(count == 0)
                 {
                     return {.text = "items array is empty", .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"results":[{}],"count":{},"failed":{}}})", results, count, failed),
                         .is_error = failed == count};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_get",
         .description = "Get the active entity selection (active_entity_id + entity_ids).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 const auto sel = editor_actions::get_selection(ctx);
                 return {.text = selection_to_json(sel.active_entity_id, sel.entity_ids), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_set_batch",
         .description =
             "Set editor entity selection by UUID (or integral) ids. "
             "mode=normal (default) replaces selection; mode=add appends (ctrl-select).",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}},"mode":{"type":"string","enum":["normal","add"]}},"required":["entity_ids"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::vector<std::string> ids;
                 std::string error;
                 if(!read_string_array(args, "entity_ids", ids, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 bool add = false;
                 std::string mode;
                 if(read_string(args, "mode", mode) && mode == "add")
                 {
                     add = true;
                 }
                 if(!editor_actions::set_selection(ctx, ids, add, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 const auto sel = editor_actions::get_selection(ctx);
                 return {.text = selection_to_json(sel.active_entity_id, sel.entity_ids), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_set_asset",
         .description = "Select an asset by key (e.g. app:/data/Model.fbx), as a click in the Content Browser does, so "
                        "the Inspector shows its details and import settings. Textures, materials, meshes, prefabs "
                        "and scenes.",
         .input_schema_json = R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string key;
                 if(!read_string(args, "key", key) || key.empty())
                 {
                     return {.text = "Missing key", .is_error = true};
                 }
                 std::string error;
                 if(!editor_actions::select_asset(ctx, key, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"selected":{}}})", make_json_string(key)), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "editor_open_project_settings",
         .description = "Open the Project Settings window, optionally at a category (e.g. \"Global Illumination\").",
         .input_schema_json = R"({"type":"object","properties":{"category":{"type":"string"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string category;
                 read_string(args, "category", category);
                 std::string error;
                 if(!editor_actions::open_project_settings(ctx, category, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"opened":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "selection_clear",
         .description = "Clear the editor selection.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 editor_actions::clear_selection(ctx);
                 return {.text = R"({"cleared":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "window_request_focus",
         .description =
             "Focus/raise the editor OS window so filesystem watchers can process asset changes.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::request_main_window_focus(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"ok":true,"requested":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "panel_focus_scene",
         .description = "Focus the Scene panel (editor viewport tab) so it becomes the active ImGui window.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::focus_scene_panel(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"panel":"scene","focused":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "panel_focus_game",
         .description = "Focus the Game panel (play view tab) so it becomes the active ImGui window.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::focus_game_panel(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"panel":"game","focused":true})", .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "logs_get_recent",
         .description =
             "Get recent console log entries. Optional min_level (trace|debug|info|warn|error|critical), "
             "max_count (default 50), after_id (exclusive cursor).",
         .input_schema_json =
             R"({"type":"object","properties":{"min_level":{"type":"string"},"max_count":{"type":"integer","minimum":1,"maximum":1024},"after_id":{"type":"integer","minimum":0}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 level::level_enum min_level = level::info;
                 std::string level_name;
                 if(read_string(args, "min_level", level_name) && !parse_min_level(level_name, min_level))
                 {
                     return {.text = "Invalid min_level", .is_error = true};
                 }
                 double max_count_d = 50.0;
                 size_t max_count = 50;
                 if(read_double(args, "max_count", max_count_d))
                 {
                     max_count = static_cast<size_t>(std::clamp(max_count_d, 1.0, 1024.0));
                 }
                 double after_d = 0.0;
                 uint64_t after_id = 0;
                 if(read_double(args, "after_id", after_d) && after_d >= 0.0)
                 {
                     after_id = static_cast<uint64_t>(after_d);
                 }
                 const auto logs = editor_actions::get_recent_logs(ctx, min_level, max_count, after_id);
                 std::string json = "[";
                 for(size_t i = 0; i < logs.size(); ++i)
                 {
                     if(i > 0)
                     {
                         json += ",";
                     }
                     const auto& e = logs[i];
                     json += fmt::format(R"({{"id":{},"level":{},"text":{},"filename":{},"funcname":{},"line":{}}})",
                                         e.id,
                                         make_json_string(level_to_string(e.level)),
                                         make_json_string(e.text),
                                         make_json_string(e.filename),
                                         make_json_string(e.funcname),
                                         e.line);
                 }
                 json += "]";
                 return {.text = std::move(json), .is_error = false};
             },
         .mutates_scene = false});

    registry.add(
        {.name = "edit_undo",
         .description = "Undo the last undoable editor action (same as Ctrl+Z).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 if(!em.can_undo())
                 {
                     return {.text = R"({"ok":false,"undone":false,"reason":"nothing to undo"})", .is_error = false};
                 }
                 auto action = em.undo();
                 const auto name = action ? action->name : std::string{};
                 return {.text = fmt::format(R"({{"ok":true,"undone":true,"action":{}}})", make_json_string(name)),
                         .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "edit_redo",
         .description = "Redo the last undone editor action (same as Ctrl+Y).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 auto& em = ctx.get_cached<editing_manager>();
                 if(!em.can_redo())
                 {
                     return {.text = R"({"ok":false,"redone":false,"reason":"nothing to redo"})", .is_error = false};
                 }
                 auto action = em.redo();
                 const auto name = action ? action->name : std::string{};
                 return {.text = fmt::format(R"({{"ok":true,"redone":true,"action":{}}})", make_json_string(name)),
                         .is_error = false};
             },
         .mutates_scene = true});

    registry.add(
        {.name = "profiler_get_passes",
         .description =
             "Per-render-pass CPU/GPU timings of the last frame (bgfx view stats, ms). Optional "
             "prefix filters pass names (e.g. \"Atmospherics\"). One frame is noisy: sample it "
             "several times and average. Pass enable:true once to switch the GPU profiler on.",
         .input_schema_json = R"({"type":"object","properties":{"prefix":{"type":"string"},"enable":{"type":"boolean"}}})",
         .handler =
             [](rtti::context&, const simdjson::dom::object& args) -> tool_result
             {
                 std::string prefix;
                 std::string_view prefix_view;
                 if(!args["prefix"].get(prefix_view))
                 {
                     prefix = std::string(prefix_view);
                 }
                 // The GPU view timings only exist while the bgfx profiler is on (same switch
                 // as the profiler panel's Enable checkbox).
                 bool enable = false;
                 if(!args["enable"].get(enable))
                 {
                     bgfx::setDebug(enable ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
                 }
                 const auto* stats = bgfx::getStats();
                 if(!stats)
                 {
                     return {.text = "No stats available", .is_error = true};
                 }
                 const double cpu_to_ms = 1000.0 / static_cast<double>(stats->cpuTimerFreq);
                 const double gpu_to_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
                 std::string json = "[";
                 bool first = true;
                 for(uint16_t pos = 0; pos < stats->numViews; ++pos)
                 {
                     const auto& view = stats->viewStats[pos];
                     const std::string name(view.name);
                     if(!prefix.empty() && name.rfind(prefix, 0) != 0)
                     {
                         continue;
                     }
                     const double cpu_ms = static_cast<double>(view.cpuTimeEnd - view.cpuTimeBegin) * cpu_to_ms;
                     const double gpu_ms = static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * gpu_to_ms;
                     json += fmt::format(R"({}{{"view":{},"name":{},"cpu_ms":{:.4f},"gpu_ms":{:.4f}}})",
                                         first ? "" : ",",
                                         int(view.view),
                                         make_json_string(name),
                                         cpu_ms,
                                         gpu_ms);
                     first = false;
                 }
                 json += "]";
                 return {.text = json, .is_error = false};
             },
         .mutates_scene = false});
}

} // namespace unravel::mcp
