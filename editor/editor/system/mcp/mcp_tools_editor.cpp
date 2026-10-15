#include "mcp_input_utils.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"

#include "mcp_viewport_utils.h"

#include <editor/hub/hub.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/viewport_stats_overlay.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/editing/create_scene_modal.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/entity_inspect.h>
#include <editor/system/mcp_manager.h>
#include <editor/system/project_manager.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/ecs_utils.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <graphics/graphics.h>
#include <imgui/imgui_internal.h>

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
        R"("shadow_submeshes":{},"lights":{},"shadow_lights":{},"particle_emitters":{{"active":{},"simulated":{},)"
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
             "(phase, paused, frames running and simulated), the dialog the editor waits on (modal: title, "
             "message, buttons; answer it with editor_answer_modal) and the active MCP profile.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
             {
                 const auto play_json = play_state_to_json(editor_actions::get_play_state(ctx));
                 const auto prefab_json = ctx.has<editing_manager>()
                                              ? prefab_mode_status_json(ctx.get_cached<editing_manager>())
                                              : std::string(R"({"active":false})");
                 return {.text = fmt::format(R"({{"project":{},"scene":{},"prefab_mode":{},"play":{},"modal":{},"mcp":{}}})",
                                             project_info_json(ctx),
                                             active_scene_status_json(ctx),
                                             prefab_json,
                                             play_json,
                                             modal_status_json(),
                                             mcp_status_json(ctx)),
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
             for(int frame = 0; frame < k_answer_frames; ++frame)
             {
                 mcp.invoke_on_main(
                     []()
                     {
                         return true;
                     });
             }
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
             "Play, pause, resume, stop, reset or emit particle emitters in edit or play mode, or only read their "
             "state (action status, the default). count sets an emit burst, whatever the emission rate; "
             "with_children reaches the emitters below the entity too. Each item reports num_particles, playing, "
             "paused, simulated (false while culling freezes an emitter no camera sees) and culling_mode, as of "
             "this frame: a burst shows up on the next.",
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
        {.name = "window_request_focus",
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
         .idempotent = true});

    registry.add(
        {.name = "panel_focus",
         .description = "Make the Scene or the Game panel the active editor tab, as clicking its tab does.",
         .input_schema_json =
             R"({"type":"object","properties":{"panel":{"type":"string","enum":["scene","game"]}},"required":["panel"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
             {
                 std::string panel;
                 read_string(args, "panel", panel);
                 if(panel != "scene" && panel != "game")
                 {
                     return {.text = "panel must be scene or game", .is_error = true};
                 }
                 std::string error;
                 const bool is_focused = panel == "scene" ? editor_actions::focus_scene_panel(ctx, &error)
                                                          : editor_actions::focus_game_panel(ctx, &error);
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
             "severity (default info), contains by text (case-insensitive). The console keeps the last "
             "~1000 entries.",
         .input_schema_json =
             R"({"type":"object","properties":{"min_level":{"type":"string","enum":["trace","debug","info","warn","warning","error","critical"]},"max_count":{"type":"integer","minimum":1,"maximum":1024},"after_id":{"type":"integer","minimum":0,"description":"Return entries after this id, oldest first"},"contains":{"type":"string","description":"Only entries whose text contains this"}}})",
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
                 // The console's id 0 is a real entry: without a cursor nothing is skipped.
                 auto logs = editor_actions::get_recent_logs(ctx, min_level, std::numeric_limits<size_t>::max(), 0);
                 std::erase_if(logs,
                               [&](const auto& entry)
                               {
                                   return (has_cursor && entry.id <= after_id) ||
                                          (!contains.empty() && !contains_ci(entry.text, contains));
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
        {.name = "profiler_get_passes",
         .description =
             "Per-render-pass CPU/GPU timings of the last frame (bgfx view stats, ms). Optional "
             "prefix filters pass names (e.g. \"Atmospherics\"). One frame is noisy: sample it "
             "several times and average. Pass enable:true once to switch the GPU profiler on.",
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
                 // The GPU view timings only exist while the bgfx profiler is on. Switched through the
                 // profiler panel's own Enable state, so the panel shows what is really on.
                 bool enable = false;
                 if(!args["enable"].get(enable))
                 {
                     ctx.get_cached<hub>().get_panels().gpu_profiler_enabled() = enable;
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
         .mutates_scene = false,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
