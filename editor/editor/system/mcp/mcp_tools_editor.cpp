#include "mcp_input_utils.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include "mcp_viewport_utils.h"

#include <editor/hub/hub.h>
#include <editor/hub/panels/profiler_panel/gpu_frame_stats_widgets.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/viewport_stats_overlay.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/editing/create_scene_modal.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/entity_inspect.h>
#include <editor/editing/prefab_bake.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/ecs_utils.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <graphics/device_caps.h>
#include <graphics/graphics.h>
#include <imgui/imgui_internal.h>
#include <simulation/simulation.h>

#include <algorithm>
#include <limits>
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

constexpr double k_milliseconds_per_second = 1000.0;
/// Frames an answered dialog gets to close and run its answer before the result is read.
constexpr int k_answer_frames = 2;
/// Fastest game time play_set_time_scale sets, as a multiple of real time.
constexpr double k_max_time_scale = 10.0;
/// Highest frame cap editor_set_frame_pacing sets; 0 is uncapped.
constexpr int64_t k_max_frame_cap = 1000;
/// The project's own assets, what editor_run_command recompiles.
constexpr const char* k_project_assets_group = "app:/";

/// The newest message box still waiting for an answer: the one on top.
auto find_waiting_box() -> std::shared_ptr<ImBox::MsgBox>
{
    const auto& boxes = ImBox::MsgBoxManager::GetInstance().GetActiveBoxes();
    const auto it = std::find_if(boxes.rbegin(),
                                 boxes.rend(),
                                 [](const auto& box)
                                 {
                                     return box && box->IsWaiting();
                                 });
    return it != boxes.rend() ? *it : nullptr;
}

auto answerable_modal_json(const std::string& title, const std::string& message, const std::vector<std::string>& labels)
    -> std::string
{
    return fmt::format(R"({{"title":{},"message":{},"buttons":{},"answerable":true}})",
                       make_json_string(title),
                       make_json_string(message),
                       strings_to_json_array(labels));
}

/**
 * @brief The dialog the editor waits on, or null. A message box (the save prompts) and the Create Scene
 *        dialog are answerable with editor_answer_modal; another modal window is named so a stalled call
 *        can be explained.
 */
auto modal_status_json() -> std::string
{
    if(auto box = find_waiting_box())
    {
        std::vector<std::string> labels;
        for(const auto& button : box->GetButtons())
        {
            labels.push_back(button.second);
        }
        return answerable_modal_json(box->GetTitle(), box->GetMessage(), labels);
    }
    if(create_scene_modal::is_pending())
    {
        return answerable_modal_json(create_scene_modal::get_title(),
                                     create_scene_modal::get_message(),
                                     create_scene_modal::get_answer_labels());
    }
    const auto* popup = ImGui::GetCurrentContext() != nullptr ? ImGui::GetTopMostAndVisiblePopupModal() : nullptr;
    if(popup == nullptr)
    {
        return "null";
    }
    const std::string name(popup->Name);
    return fmt::format(R"({{"title":{},"answerable":false}})", make_json_string(name.substr(0, name.find("##"))));
}

/// Answers the Create Scene dialog with its button or preset card labelled @p label, as a click would.
auto request_create_scene_answer(const std::string& label, std::string& title, std::string& error) -> bool
{
    title = create_scene_modal::get_title();
    if(create_scene_modal::answer_if_pending(label))
    {
        return true;
    }
    error = fmt::format("\"{}\" has no button {} (buttons: {})",
                        title,
                        label,
                        strings_to_json_array(create_scene_modal::get_answer_labels()));
    return false;
}

/// Asks the waiting dialog to take the answer of the button labelled @p label, as a click would.
auto request_modal_answer(const std::string& label, std::string& title, std::string& error) -> bool
{
    auto box = find_waiting_box();
    if(!box && create_scene_modal::is_pending())
    {
        return request_create_scene_answer(label, title, error);
    }
    if(!box)
    {
        error = "No dialog waits for an answer";
        return false;
    }
    title = box->GetTitle();
    std::vector<std::string> labels;
    for(const auto& [result, button_label] : box->GetButtons())
    {
        if(to_lower_ascii(button_label) == to_lower_ascii(label))
        {
            return box->RequestAnswer(result);
        }
        labels.push_back(button_label);
    }
    error = fmt::format("\"{}\" has no button {} (buttons: {})", title, label, strings_to_json_array(labels));
    return false;
}

/// What one camera's pipeline drew last frame.
auto pipeline_stats_json(const rendering::pipeline_stats& stats) -> std::string
{
    return fmt::format(
        R"({{"models":{},"submeshes":{},"skinned_models":{},"skinned_submeshes":{},"shadow_models":{},)"
        R"("shadow_submeshes":{},"lights":{},"shadow_lights":{},"shadow_maps":{{"rendered":{},"reused":{},"restored":{}}},)"
        R"("gi":{{"cards":{},"captures":{},"material_changes":{},"direct_dirty_pages":{},"pages_used":{},)"
        R"("direct_invalidated":{}}},)"
        R"("particle_emitters":{{"active":{},"simulated":{},)"
        R"("drawn":{},"skipped":{}}},"particles":{{"active":{},"simulated":{},"drawn":{},"skipped":{}}},)"
        R"("particle_batches":{}}})",
        stats.drawn_models,
        stats.drawn_static_submeshes,
        stats.drawn_skinned_models,
        stats.drawn_skinned_submeshes,
        stats.drawn_models_for_shadows + stats.drawn_skinned_models_for_shadows,
        stats.drawn_submeshes_for_shadows + stats.drawn_skinned_submeshes_for_shadows,
        stats.drawn_lights,
        stats.drawn_lights_casting_shadows,
        stats.shadow_maps_rendered,
        stats.shadow_maps_reused,
        stats.shadow_maps_restored,
        stats.gi_cards,
        stats.gi_captures,
        stats.gi_material_changes,
        stats.gi_direct_dirty_pages,
        stats.gi_pages_used,
        stats.gi_direct_invalidated,
        stats.active_particle_emitters,
        stats.simulated_particle_emitters,
        stats.drawn_particle_emitters,
        stats.skipped_particle_emitters,
        stats.active_particles,
        stats.simulated_particles,
        stats.drawn_particles,
        stats.skipped_particles,
        stats.drawn_particles_batches);
}

/// The frame as the viewport statistics overlay shows it: time, draw counts without the editor UI,
/// GPU memory, and what the Scene view's and the game's cameras drew.
auto frame_stats_json(rtti::context& ctx) -> std::string
{
    const auto* stats = bgfx::getStats();
    const double cpu_ms = static_cast<double>(stats->cpuTimeEnd - stats->cpuTimeBegin) * k_milliseconds_per_second /
                          static_cast<double>(stats->cpuTimerFreq);
    const double gpu_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * k_milliseconds_per_second /
                          static_cast<double>(stats->gpuTimerFreq);
    const float fps = ImGui::GetIO().Framerate;
    const auto counts = viewport_stats_overlay::get_scene_draw_counts();
    std::string error;
    const auto scene_camera = resolve_scene_camera(ctx, error);
    const auto scene_view_json =
        scene_camera ? pipeline_stats_json(
                           scene_camera.get<camera_component>().get_pipeline_data().get_pipeline()->get_stats())
                     : std::string("null");
    auto* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
    return fmt::format(
        R"({{"frame":{{"fps":{:.1f},"frame_ms":{:.2f},"cpu_submit_ms":{:.2f},"gpu_ms":{:.2f},"gpu_latency_frames":{}}},)"
        R"("draw":{{"triangles":{},"draw_calls":{},"compute_calls":{},"blit_calls":{},"render_passes":{}}},)"
        R"("memory":{{"gpu_used_bytes":{},"gpu_max_bytes":{},"texture_bytes":{},"render_target_bytes":{}}},)"
        R"("entities":{},"play_phase":{},"scene_view":{},"game":{}}})",
        fps,
        fps > 0.0f ? static_cast<float>(k_milliseconds_per_second) / fps : 0.0f,
        cpu_ms,
        gpu_ms,
        stats->maxGpuLatency,
        counts.triangles,
        counts.draw_calls,
        stats->numCompute,
        stats->numBlit,
        gfx::render_pass::get_last_frame_max_pass_id(),
        stats->gpuMemoryUsed,
        stats->gpuMemoryMax,
        stats->textureMemoryUsed,
        stats->rtMemoryUsed,
        scn != nullptr && scn->registry ? count_live_entities(*scn->registry) : 0,
        make_json_string(play_phase_from_ctx(ctx)),
        scene_view_json,
        pipeline_stats_json(game_panel::get_rendered_pipeline_stats(ctx)));
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

/// The device the editor renders on and the rendering features it does not run, with why and what renders
/// instead (render_feature_support). "simulated" is set when --gfx-disable masks the device.
auto graphics_status_json() -> std::string
{
    const auto& caps = gfx::get_device_caps();
    const auto& support = get_render_feature_support();
    std::string unavailable;
    for(const auto feature : support.get_unavailable_features())
    {
        const auto& status = support.get_status(feature);
        const auto& needs = get_render_feature_requirements(feature);
        unavailable += fmt::format(R"({}{{"feature":{},"reason":{},"fallback":{}}})",
                                   unavailable.empty() ? "" : ",",
                                   make_json_string(needs.name),
                                   make_json_string(status.reason),
                                   make_json_string(support.get_fallback(feature)));
    }
    return fmt::format(R"({{"backend":{},"vendor":{},"device_id":{},"compute_bindings":{},"simulated":{},"unavailable":[{}]}})",
                       make_json_string(bgfx::getRendererName(caps.renderer)),
                       make_json_string(gfx::get_vendor_name(caps.vendor_id)),
                       caps.device_id,
                       caps.max_compute_bindings,
                       bool_to_json(!gfx::get_device_caps_mask().is_empty()),
                       unavailable);
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

/// Every action particles_control_batch takes: status, emit and the playback actions, from the table.
auto particle_action_names() -> std::vector<std::string>
{
    std::vector<std::string> names{"status"};
    for(const auto& playback : k_playback_actions)
    {
        names.emplace_back(playback.name);
    }
    names.emplace_back("emit");
    return names;
}

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

/// The time scale and frame pacing the header shows, and the scripts' build mode.
auto pacing_status_json(rtti::context& ctx) -> std::string
{
    const auto& sim = ctx.get_cached<simulation>();
    return fmt::format(R"({{"time_scale":{},"vsync":{},"max_fps":{},"script_debug_mode":{}}})",
                       sim.get_time_scale(),
                       bool_to_json(ctx.get_cached<renderer>().get_vsync()),
                       sim.get_max_fps(),
                       bool_to_json(script_system::get_script_debug_mode()));
}

/// One command of editor_run_command: an item of the editor's Build or Developer menu.
struct editor_command
{
    const char* name;
    /// Refused while playing, as the menu item is: it rewrites project files the running game was loaded from.
    bool is_edit_only;
    /// Runs the command; returns the result's own fields ("probes":3), or nothing.
    std::string (*run)(rtti::context& ctx);
};

auto run_validate_prefab_graph(rtti::context& ctx) -> std::string
{
    const auto report = validate_prefab_graph(ctx);
    return fmt::format(R"("valid":{},"asset_count":{},"nesting_count":{},"max_depth":{},"cyclic_ids":{},"problem":{})",
                       bool_to_json(report.is_valid()),
                       report.asset_count,
                       report.nesting_count,
                       report.max_depth,
                       strings_to_json_array(report.cyclic_ids),
                       make_json_string(report.describe_problem()));
}

auto run_migrate_prefabs(rtti::context& ctx) -> std::string
{
    size_t total = 0;
    const auto written = editor_actions::migrate_prefabs(ctx, &total);
    return fmt::format(R"("written":{},"total":{})", written, total);
}

const std::array<editor_command, 10> k_editor_commands{{
    {"build_reflection_captures",
     false,
     [](rtti::context& ctx) -> std::string
     {
         return fmt::format(R"("probes":{})", editor_actions::rebuild_reflection_probes(ctx, true));
     }},
    {"validate_prefab_graph", false, run_validate_prefab_graph},
    {"migrate_prefabs", true, run_migrate_prefabs},
    {"migrate_color_spaces",
     true,
     [](rtti::context&) -> std::string
     {
         return fmt::format(R"("textures":{})", editor_actions::migrate_texture_color_spaces(k_project_assets_group));
     }},
    {"recompile_shaders",
     false,
     [](rtti::context&) -> std::string
     {
         editor_actions::recompile_shaders(k_project_assets_group);
         return {};
     }},
    {"recompile_meshes",
     false,
     [](rtti::context&) -> std::string
     {
         editor_actions::recompile_meshes(k_project_assets_group);
         return {};
     }},
    {"recompile_ui",
     false,
     [](rtti::context&) -> std::string
     {
         editor_actions::recompile_ui(k_project_assets_group);
         return {};
     }},
    {"recompile_scripts",
     false,
     [](rtti::context&) -> std::string
     {
         editor_actions::recompile_scripts(k_project_assets_group);
         return {};
     }},
    {"recompile_all",
     false,
     [](rtti::context&) -> std::string
     {
         editor_actions::recompile_all(k_project_assets_group);
         return {};
     }},
    {"regenerate_agent_files",
     false,
     [](rtti::context& ctx) -> std::string
     {
         return fmt::format(R"("written":{})", bool_to_json(ctx.get_cached<project_manager>().regenerate_agent_files()));
     }},
}};

auto editor_command_names() -> std::vector<std::string>
{
    std::vector<std::string> names;
    for(const auto& command : k_editor_commands)
    {
        names.emplace_back(command.name);
    }
    return names;
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

/// Most history steps one undo / redo call takes.
constexpr int64_t k_max_history_steps = 100;

/**
 * @brief Steps the undo history `count` times backward (undo) or forward (redo). An action whose
 *        entities are gone is passed over by the stack ("missing references") and reported as
 *        skipped, not as done.
 */
auto step_history(editing_manager& em, bool backward, int64_t count) -> tool_result
{
    std::vector<std::string> done;
    std::vector<std::string> skipped;
    for(int64_t i = 0; i < count && (backward ? em.can_undo() : em.can_redo()); ++i)
    {
        const auto action = backward ? em.undo() : em.redo();
        if(action && action->is_valid())
        {
            done.push_back(action->name);
        }
        else
        {
            skipped.push_back(action ? action->name : std::string{});
        }
    }
    if(done.empty() && skipped.empty())
    {
        return {.text = backward ? R"json({"ok":false,"reason":"nothing to undo (the history is cleared when scripts recompile, a scene loads or play ends)"})json"
                                 : R"({"ok":false,"reason":"nothing to redo"})",
                .is_error = false};
    }
    return {.text = fmt::format(R"({{"ok":{},"{}":{},"skipped_missing_references":{}}})",
                                bool_to_json(!done.empty()),
                                backward ? "undone" : "redone",
                                strings_to_json_array(done),
                                strings_to_json_array(skipped)),
            .is_error = false};
}

} // namespace

void register_editor_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "editor_get_status",
         .description =
             "Where the editor is, in one call: the open project, the active scene, prefab mode, play state "
             "(phase, paused, frames running and simulated), pacing (time scale, vsync, max fps, whether scripts "
             "build in debug mode), the dialog the editor waits on (modal: title, "
             "message, buttons; answer it with editor_answer_modal), the active MCP profile, and the graphics "
             "device (backend, vendor, the rendering features it does not run with the reason and the fallback).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 const auto play_json = play_state_to_json(editor_actions::get_play_state(ctx));
                 const auto prefab_json = ctx.has<editing_manager>()
                                              ? prefab_mode_status_json(ctx.get_cached<editing_manager>())
                                              : std::string(R"({"active":false})");
                 return {.text = fmt::format(R"({{"project":{},"scene":{},"prefab_mode":{},"play":{},"pacing":{},"modal":{},"mcp":{},"graphics":{}}})",
                                             project_info_json(ctx),
                                             active_scene_status_json(ctx),
                                             prefab_json,
                                             play_json,
                                             pacing_status_json(ctx),
                                             modal_status_json(),
                                             mcp_status_json(ctx),
                                             graphics_status_json()),
                         .is_error = false};
             },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "editor_answer_modal",
         .description =
             "Answer the dialog the editor waits on (editor_get_status modal) by pressing one of its buttons, "
             "by label (\"Save\", \"Don't Save\", \"Cancel\", ...), as a click would; the Create Scene dialog "
             "also takes a preset name (\"Standard\", ...). Returns the next dialog when the answer opened one.",
         .input_schema_json =
             R"json({"type":"object","properties":{"button":{"type":"string","description":"Button label, case-insensitive"}},"required":["button"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string label;
             read_string(args, "button", label);
             auto& mcp = ctx.get_cached<mcp_manager>();
             struct request_outcome
             {
                 std::string title;
                 std::string error;
             };
             auto requested = mcp.invoke_on_main(
                 [&label]() -> request_outcome
                 {
                     request_outcome outcome;
                     request_modal_answer(label, outcome.title, outcome.error);
                     return outcome;
                 });
             if(!requested || !requested->error.empty())
             {
                 return {.text = requested ? requested->error : "Timed out on the main thread", .is_error = true};
             }
             wait_input_frames(ctx, k_answer_frames);
             auto next = mcp.invoke_on_main(
                 []()
                 {
                     return modal_status_json();
                 });
             return {.text = fmt::format(R"({{"ok":true,"answered":{},"dialog":{},"modal":{}}})",
                                         make_json_string(label),
                                         make_json_string(requested->title),
                                         next ? *next : std::string("null")),
                     .is_error = false};
         },
         .mutates_scene = true,
         .requires_main_thread = false,
         .effect = tool_effect::destructive});

    registry.add(
        {.name = "play_set_active",
         .description =
             "Enter or exit play mode. Entering is blocked while scripts have compile errors. allow_splash "
             "(default true) shows the project's splash screen first when it has one; false starts the "
             "game at once.",
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
         .mutates_scene = false,
         .idempotent = true});

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
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "play_set_time_scale",
         .description = "Set how fast game time runs, as the header's time scale does: 1 is real time, 0.5 half "
                        "speed, 2 twice as fast, 0 stops game time while frames go on. Works in and out of play; "
                        "play stopping puts it back to 1, and scripts may change it (Time.timeScale). "
                        "editor_get_status pacing shows it.",
         .input_schema_json =
             R"({"type":"object","properties":{"time_scale":{"type":"number","minimum":0,"maximum":10}},"required":["time_scale"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 double time_scale = 1.0;
                 if(!read_double(args, "time_scale", time_scale) || time_scale < 0.0 || time_scale > k_max_time_scale)
                 {
                     return {.text = "time_scale must be a number from 0 to 10", .is_error = true};
                 }
                 auto& sim = ctx.get_cached<simulation>();
                 const float previous = sim.get_time_scale();
                 sim.set_time_scale(static_cast<float>(time_scale));
                 return {.text = fmt::format(R"({{"ok":true,"time_scale":{},"previous":{}}})",
                                             sim.get_time_scale(),
                                             previous),
                         .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "editor_set_frame_pacing",
         .description = "Set vsync and the frame cap for this editor session, as the header's frame pacing menu "
                        "does: vsync waits for the display before each frame, max_fps caps the frame rate (0: "
                        "uncapped). Pass either or both. GPU timings read high with vsync on; visual checks are "
                        "steadier with it.",
         .input_schema_json =
             R"({"type":"object","properties":{"vsync":{"type":"boolean"},"max_fps":{"type":"integer","minimum":0,"maximum":1000,"description":"0: uncapped"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 bool vsync = false;
                 const bool has_vsync = read_bool(args, "vsync", vsync);
                 int64_t max_fps = 0;
                 const bool has_max_fps = args["max_fps"].get(max_fps) == simdjson::SUCCESS;
                 if(!has_vsync && !has_max_fps)
                 {
                     return {.text = "Pass vsync, max_fps or both", .is_error = true};
                 }
                 if(has_max_fps && (max_fps < 0 || max_fps > k_max_frame_cap))
                 {
                     return {.text = "max_fps must be from 0 (uncapped) to 1000", .is_error = true};
                 }
                 if(has_vsync)
                 {
                     ctx.get_cached<renderer>().set_vsync(vsync);
                 }
                 if(has_max_fps)
                 {
                     ctx.get_cached<simulation>().set_max_fps(static_cast<uint32_t>(max_fps));
                 }
                 return {.text = fmt::format(R"({{"ok":true,"pacing":{}}})", pacing_status_json(ctx)),
                         .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "editor_run_command",
         .description =
             "Run a command of the editor's Build and Developer menus. build_reflection_captures rebakes every "
             "reflection probe in the loaded scenes (after editing the environment or static geometry). "
             "validate_prefab_graph checks that the prefabs and scenes can be ordered for a build and names those in "
             "a nesting cycle; it writes nothing. migrate_prefabs re-saves every prefab and scene in the current "
             "format. migrate_color_spaces tags the textures materials use with their color space and normal maps "
             "as normal maps, then recompiles the changed ones. recompile_shaders, recompile_meshes, recompile_ui, "
             "recompile_scripts and recompile_all compile the project's assets of that kind again in the background "
             "(assets_wait_ready_batch, scripts_get_compile_status tell when they are done). "
             "regenerate_agent_files rewrites the project's .agents/UNRAVEL.md and adds a missing AGENTS.md. The "
             "migrations are refused while playing.",
         .input_schema_json = R"json({"type":"object","properties":{"command":{"type":"string","enum":)json" +
                              strings_to_json_array(editor_command_names()) +
                              R"json(}},"required":["command"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string name;
                 read_string(args, "command", name);
                 const auto command = std::find_if(k_editor_commands.begin(),
                                                   k_editor_commands.end(),
                                                   [&name](const editor_command& entry)
                                                   {
                                                       return name == entry.name;
                                                   });
                 if(command == k_editor_commands.end())
                 {
                     return {.text = "Unknown command: " + name, .is_error = true};
                 }
                 std::string error;
                 if(command->is_edit_only && !require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 if(!ctx.get_cached<project_manager>().has_open_project())
                 {
                     return {.text = "No project is open (project_open)", .is_error = true};
                 }
                 const auto fields = command->run(ctx);
                 return {.text = fmt::format(R"({{"ok":true,"command":{}{}{}}})",
                                             make_json_string(name),
                                             fields.empty() ? "" : ",",
                                             fields),
                         .is_error = false};
             },
         .mutates_scene = false,
         .effect = tool_effect::destructive,
         .writes_project_files = true});

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
                 // Counted by the frames the simulation ran, so steps dropped by an unpause or a stop meanwhile show.
                 struct queued_steps
                 {
                     std::string error;
                     uint64_t simulated_before{};
                 };
                 auto queued = mcp.invoke_on_main(
                     [&ctx, count]() -> queued_steps
                     {
                         queued_steps steps{.simulated_before = editor_actions::get_play_state(ctx).frames_simulated};
                         for(int64_t step = 0; step < count && steps.error.empty(); ++step)
                         {
                             editor_actions::skip_play_frame(ctx, &steps.error);
                         }
                         return steps;
                     });
                 if(!queued)
                 {
                     return {.text = "Timed out reaching the editor main thread", .is_error = true};
                 }
                 if(!queued->error.empty())
                 {
                     return {.text = queued->error, .is_error = true};
                 }
                 // The steps run one per frame; one more frame lets the last one finish.
                 const bool advanced = wait_input_frames(ctx, static_cast<uint64_t>(count) + 1);
                 const auto state = mcp.invoke_on_main(
                     [&ctx]()
                     {
                         return editor_actions::get_play_state(ctx);
                     });
                 const auto ran = state && state->frames_simulated >= queued->simulated_before
                                      ? state->frames_simulated - queued->simulated_before
                                      : 0;
                 if(!advanced || ran < static_cast<uint64_t>(count))
                 {
                     return {.text = fmt::format("{} of {} frames ran (play is {}): play was resumed or stopped "
                                                 "meanwhile, or the editor stopped advancing frames",
                                                 ran,
                                                 count,
                                                 state ? state->phase : std::string("unknown")),
                             .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"ok":true,"frames":{}}})", count), .is_error = false};
             },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "particles_control_batch",
         .description =
             "Play, pause, resume, stop, reset or emit particle emitters in edit or play mode, or only read their "
             "state (action status, the default). count sets an emit burst, whatever the emission rate; "
             "with_children reaches the emitters below the entity too. Each item reports num_particles, playing, "
             "paused, simulated (false while culling freezes an emitter no camera sees) and culling_mode, as of "
             "this frame: a burst shows up on the next.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"entity_id":{"type":"string"},"action":{"type":"string","enum":)json" +
                 strings_to_json_array(particle_action_names()) +
                 R"json(},"count":{"type":"integer","minimum":1,"maximum":100000},"with_children":{"type":"boolean"}},"required":["entity_id"]}}},"required":["items"]})json",
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "selection_set",
         .description =
             "Set what the editor has selected. entity_ids replaces the entity selection (mode add adds "
             "to it, as ctrl-click does; [] clears it). asset (a key, or a path relative to app:/data) "
             "selects an asset as a Content Browser click does, so the Inspector shows its details and "
             "import settings. Returns the entity selection.",
         .input_schema_json =
             R"({"type":"object","properties":{"entity_ids":{"type":"array","items":{"type":"string"}},"mode":{"type":"string","enum":["replace","add"],"default":"replace"},"asset":{"type":"string"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string error;
                 std::string asset;
                 const bool has_asset = read_string(args, "asset", asset);
                 const bool has_entities = args["entity_ids"].error() == simdjson::SUCCESS;
                 if(has_asset == has_entities)
                 {
                     return {.text = "Pass entity_ids or asset", .is_error = true};
                 }
                 if(has_asset)
                 {
                     const auto key = to_asset_key(asset);
                     if(!editor_actions::select_asset(ctx, key, &error))
                     {
                         return {.text = error, .is_error = true};
                     }
                     return {.text = fmt::format(R"({{"asset":{}}})", make_json_string(key)), .is_error = false};
                 }
                 std::vector<std::string> ids;
                 if(!read_string_array(args, "entity_ids", ids, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 std::string mode;
                 const bool is_add = read_string(args, "mode", mode) && mode == "add";
                 if(ids.empty() && !is_add)
                 {
                     editor_actions::clear_selection(ctx);
                 }
                 else if(!editor_actions::set_selection(ctx, ids, is_add, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 const auto sel = editor_actions::get_selection(ctx);
                 return {.text = selection_to_json(sel.active_entity_id, sel.entity_ids), .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "assets_reveal",
         .description =
             "Show a file or folder in the Content Browser, as an asset field's locate button does: the browser "
             "opens the folder that holds it, scrolls to it and rings it, and the Content panel comes to the "
             "front. key: a key, a uid, or a path relative to app:/data. The selection is left as it is "
             "(selection_set {\"asset\":...} selects).",
         .input_schema_json =
             R"({"type":"object","properties":{"key":{"type":"string","description":"Key, uid, or path relative to app:/data"}},"required":["key"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string input;
                 read_string(args, "key", input);
                 std::string key = to_asset_key(input);
                 if(const auto uid = hpp::uuid::from_string(input))
                 {
                     key = ctx.get_cached<asset_manager>().get_metadata(*uid).location;
                     if(key.empty())
                     {
                         return {.text = "No asset has uid " + input, .is_error = true};
                     }
                 }
                 std::string error;
                 if(!editor_actions::reveal_in_content_browser(ctx, key, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"revealed":{}}})", make_json_string(key)), .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

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
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "editor_open_deploy",
         .description = "Open the Deploy window, as the header's Deploy button does, for a user who should see it. "
                        "Deploying itself needs no window: deploy_get_state, deploy_set_settings and deploy_start.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 std::string error;
                 if(!editor_actions::open_deploy_window(ctx, &error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = R"({"opened":true})", .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "editor_focus_os_window",
         .description =
             "Raise and focus the editor's OS window, for a user who should see it. Asset changes are picked up "
             "without it.",
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
         .mutates_scene = false,
         .idempotent = true,
         .former_names = {"window_request_focus"}});

    registry.add(
        {.name = "panel_focus",
         .description = "Make the Scene or the Game panel the active editor tab, as clicking its tab does, or open "
                        "the MCP Server window (mcp), as the header's MCP indicator does.",
         .input_schema_json =
             R"({"type":"object","properties":{"panel":{"type":"string","enum":["scene","game","mcp"]}},"required":["panel"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string panel;
                 read_string(args, "panel", panel);
                 if(panel != "scene" && panel != "game" && panel != "mcp")
                 {
                     return {.text = "panel must be scene, game or mcp", .is_error = true};
                 }
                 std::string error;
                 const bool is_focused = panel == "scene"  ? editor_actions::focus_scene_panel(ctx, &error)
                                         : panel == "game" ? editor_actions::focus_game_panel(ctx, &error)
                                                           : editor_actions::show_mcp_window(ctx, &error);
                 if(!is_focused)
                 {
                     return {.text = error, .is_error = true};
                 }
                 return {.text = fmt::format(R"({{"panel":{},"focused":true}})", make_json_string(panel)),
                         .is_error = false};
             },
         .mutates_scene = false,
         .idempotent = true});

    registry.add(
        {.name = "logs_get_recent",
         .description =
             "Read the editor console log. Without after_id: the newest max_count entries (default 50). "
             "With after_id (the next_after_id of an earlier call): the entries logged since, oldest "
             "first, max_count at a time - has_more says another page waits. min_level filters by "
             "severity (default info), contains by text (case-insensitive). It reads the last 4096 lines "
             "logged, whatever the console cleared since (on play, on recompile). A game running on an Android device logs its own lines as [Device]; the device "
             "system's lines of its process ([Device System]) are left out unless include_device_system is true, "
             "as the console hides them by default.",
         .input_schema_json =
             R"({"type":"object","properties":{"min_level":{"type":"string","enum":["trace","debug","info","warn","warning","error","critical"]},"max_count":{"type":"integer","minimum":1,"maximum":1024},"after_id":{"type":"integer","minimum":0,"description":"Return entries after this id, oldest first"},"contains":{"type":"string","description":"Only entries whose text contains this"},"include_device_system":{"type":"boolean","description":"Also return the device system's lines of a game on an Android device; default false"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 level::level_enum min_level = level::info;
                 std::string level_name;
                 if(read_string(args, "min_level", level_name) && !parse_min_level(level_name, min_level))
                 {
                     return {.text = "Invalid min_level", .is_error = true};
                 }
                 const auto max_count = static_cast<size_t>(std::clamp<int64_t>(read_int64_or(args, "max_count", 50), 1, 1024));
                 const bool has_cursor = args["after_id"].error() == simdjson::SUCCESS;
                 const auto after_id = static_cast<uint64_t>(std::max<int64_t>(read_int64_or(args, "after_id", 0), 0));
                 std::string contains;
                 read_string(args, "contains", contains);
                 bool include_device_system = false;
                 read_bool(args, "include_device_system", include_device_system);
                 // The console's id 0 is a real entry: without a cursor nothing is skipped.
                 auto logs = editor_actions::get_recent_logs(ctx, min_level, std::numeric_limits<size_t>::max(), 0);
                 std::erase_if(logs,
                               [&](const auto& entry)
                               {
                                   return (has_cursor && entry.id <= after_id) ||
                                          (!contains.empty() && !contains_ci(entry.text, contains)) ||
                                          (entry.is_device_system && !include_device_system);
                               });
                 const bool has_more = has_cursor && logs.size() > max_count;
                 // A cursor pages forward from the oldest; a plain call wants the newest.
                 if(logs.size() > max_count && has_cursor)
                 {
                     logs.erase(logs.begin() + static_cast<std::ptrdiff_t>(max_count), logs.end());
                 }
                 else if(logs.size() > max_count)
                 {
                     logs.erase(logs.begin(), logs.end() - static_cast<std::ptrdiff_t>(max_count));
                 }
                 std::string json = "[";
                 for(size_t i = 0; i < logs.size(); ++i)
                 {
                     const auto& e = logs[i];
                     json += fmt::format(R"({}{{"id":{},"level":{},"text":{},"filename":{},"funcname":{},"line":{}}})",
                                         i > 0 ? "," : "",
                                         e.id,
                                         make_json_string(level_to_string(e.level)),
                                         make_json_string(e.text),
                                         make_json_string(e.filename),
                                         make_json_string(e.funcname),
                                         e.line);
                 }
                 json += "]";
                 const auto next_after_id = logs.empty() ? after_id : logs.back().id;
                 return {.text = fmt::format(R"({{"entries":{},"count":{},"next_after_id":{},"has_more":{}}})",
                                             json,
                                             logs.size(),
                                             next_after_id,
                                             bool_to_json(has_more))};
             },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "edit_undo",
         .description = "Undo editor actions (Ctrl+Z), count steps (default 1). Every MCP call that "
                        "changes the scene is one step. Returns the names undone.",
         .input_schema_json =
             R"json({"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":100,"default":1}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 const auto count = std::clamp<int64_t>(read_int64_or(args, "count", 1), 1, k_max_history_steps);
                 return step_history(ctx.get_cached<editing_manager>(), true, count);
             },
         .mutates_scene = true});

    registry.add(
        {.name = "edit_redo",
         .description = "Redo undone editor actions (Ctrl+Y), count steps (default 1). Returns the names "
                        "redone.",
         .input_schema_json =
             R"json({"type":"object","properties":{"count":{"type":"integer","minimum":1,"maximum":100,"default":1}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string error;
                 if(!require_not_play_mode(ctx, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 const auto count = std::clamp<int64_t>(read_int64_or(args, "count", 1), 1, k_max_history_steps);
                 return step_history(ctx.get_cached<editing_manager>(), false, count);
             },
         .mutates_scene = true});

    registry.add(
        {.name = "stats_get",
         .description =
             "Frame statistics, as the viewport statistics overlay shows them: fps, CPU and GPU ms, triangles and "
             "draw calls without the editor UI, GPU memory, live entities, and per view (scene_view, game) the "
             "models, lights, shadow casters and particles drawn. Last frame's values; with vsync on, times "
             "include waiting for the display.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             return {.text = frame_stats_json(ctx), .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "profiler_get_gpu_passes",
         .description =
             "Per-render-pass CPU/GPU timings of the last frame (bgfx view stats, ms). Optional "
             "prefix filters pass names (e.g. \"Atmospherics\"). One frame is noisy: sample it "
             "several times and average. enable:true switches the GPU view timing on (the profiler window's View / "
             "Encoder Timing toggle), false off; it stays as set.",
         .input_schema_json = R"({"type":"object","properties":{"prefix":{"type":"string"},"enable":{"type":"boolean"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string prefix;
                 hpp::string_view prefix_view;
                 if(!args["prefix"].get(prefix_view))
                 {
                     prefix = std::string(prefix_view);
                 }
                 // The GPU view timings only exist while the bgfx profiler is on. Switched as the profiler
                 // panel's own toggle switches it, so the panel shows what is really on.
                 bool enable = false;
                 if(!args["enable"].get(enable))
                 {
                     set_gpu_view_timing(ctx.get_cached<hub>().get_panels().gpu_profiler_enabled(), enable);
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
         .mutates_scene = false,
         .idempotent = true,
         .former_names = {"profiler_get_passes"}});
}

} // namespace unravel::mcp
