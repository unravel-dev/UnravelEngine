#include "mcp_async.h"
#include "mcp_tools_common.h"

#include <editor/hub/hub.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/scene_panel/scene_panel.h>
#include <editor/hub/panels/visualization_modes.h>
#include <editor/system/mcp_manager.h>

#include <editor/editing/editing_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/pipeline/passes/gi_quiescence_gate_pass.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <seq/seq.h>

#include <chrono>
#include <map>
#include <thread>
#include <vector>

namespace unravel::mcp
{
namespace
{

auto resolve_scene_panel(rtti::context& ctx) -> scene_panel&
{
    return ctx.get_cached<hub>().get_panels().get_scene_panel();
}

auto debug_view_name_for_value(int value) -> const char*
{
    const auto* entry = find_visualization_mode(value);
    return entry != nullptr ? entry->name : "unknown";
}

/// Flat id list for error messages. Grouped output with descriptions and legends comes from
/// viewport_list_debug_views instead - this stays short enough to read in a failure.
auto list_debug_view_names() -> std::string
{
    std::string names;
    for(const auto& entry : get_visualization_modes())
    {
        if(!names.empty())
        {
            names += ", ";
        }
        names += entry.name;
    }
    return names;
}

auto debug_view_mode_json(const visualization_mode_entry& entry, bool include_legend) -> std::string
{
    std::string json = fmt::format(R"({{"id":{},"name":{},"label":{},"description":{})",
                                   static_cast<int>(entry.mode),
                                   make_json_string(entry.name),
                                   make_json_string(entry.label),
                                   make_json_string(entry.description));
    if(include_legend && !entry.legend.empty())
    {
        json += R"(,"legend":[)";
        bool first = true;
        for(const auto& swatch : entry.legend)
        {
            if(!first)
            {
                json += ",";
            }
            first = false;
            // Colors are the literal values the debug shader writes, so an agent reading a
            // capture can match a pixel against them without opening the shader.
            json += fmt::format(R"({{"color":[{:.3g},{:.3g},{:.3g}],"meaning":{}}})",
                                swatch.color[0],
                                swatch.color[1],
                                swatch.color[2],
                                make_json_string(swatch.meaning));
        }
        json += "]";
    }
    json += "}";
    return json;
}

auto resolve_scene_camera(rtti::context& ctx, std::string& error) -> entt::handle
{
    auto camera = resolve_scene_panel(ctx).get_camera();
    if(!camera || !camera.all_of<transform_component, camera_component>())
    {
        error = "Scene panel camera not available";
        return {};
    }
    return camera;
}

auto resolve_scene_obuffer(rtti::context& ctx) -> gfx::frame_buffer::ptr
{
    auto& hub_sys = ctx.get_cached<hub>();
    auto camera = hub_sys.get_panels().get_scene_panel().get_camera();
    if(!camera)
    {
        return {};
    }

    auto* camera_comp = camera.try_get<camera_component>();
    if(!camera_comp)
    {
        return {};
    }

    return camera_comp->get_render_view().fbo_safe_get("OBUFFER");
}

auto resolve_game_obuffer(rtti::context& ctx) -> gfx::frame_buffer::ptr
{
    // Prefer the active edit/play scene camera that owns an OBUFFER (same source as game_panel).
    auto& em = ctx.get_cached<editing_manager>();
    auto* scn = em.get_active_scene(ctx);
    if(!scn || !scn->registry)
    {
        auto& ec = ctx.get_cached<ecs>();
        scn = &ec.get_scene();
    }
    if(!scn || !scn->registry)
    {
        return {};
    }

    gfx::frame_buffer::ptr found;
    scn->registry->view<camera_component>().each(
        [&](auto, auto&& camera_comp)
        {
            if(found)
            {
                return;
            }
            auto obuffer = camera_comp.get_render_view().fbo_safe_get("OBUFFER");
            if(obuffer && obuffer->is_valid())
            {
                found = obuffer;
            }
        });
    return found;
}

auto camera_to_json(entt::handle camera) -> std::string
{
    auto& tc = camera.get<transform_component>();
    auto& cc = camera.get<camera_component>();
    return fmt::format(
        R"({{"position":{},"rotation_euler":{},"forward":{},"up":{},"fov":{:.6g},"ortho_size":{:.6g}}})",
        vec3_to_json(tc.get_position_global()),
        vec3_to_json(tc.get_rotation_euler_global()),
        vec3_to_json(tc.get_z_axis_global()),
        vec3_to_json(tc.get_y_axis_global()),
        cc.get_fov(),
        cc.get_ortho_size());
}

auto cancel_camera_focus() -> void
{
    seq::scope::stop_all("camera_focus");
}

auto read_duration(const simdjson::dom::object& args, float default_duration = 0.4f) -> float
{
    double duration = default_duration;
    (void)args["duration"].get(duration);
    if(duration < 0.0)
    {
        duration = 0.0;
    }
    if(duration > 10.0)
    {
        duration = 10.0;
    }
    return static_cast<float>(duration);
}

auto resolve_focus_entities(rtti::context& ctx,
                            const simdjson::dom::object& args,
                            std::vector<entt::handle>& out,
                            std::string& error) -> bool
{
    scene* scn = nullptr;
    if(!require_edit_scene(ctx, scn, error))
    {
        return false;
    }

    return resolve_entity_id_or_ids(*scn, args, out, error);
}

} // namespace

void register_viewport_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "viewport_capture_scene",
         .description =
             "Capture Scene panel viewport as PNG (image content). Optional wait_ms (default 500), "
             "scale (0-1, default 1; bimg linear resize). Prefer scale 0.5 to save tokens.",
         .input_schema_json =
             R"({"type":"object","properties":{"wait_ms":{"type":"integer","minimum":100,"maximum":15000},"scale":{"type":"number","minimum":0.05,"maximum":1}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             capture_options options{};
             options.wait_timeout = read_wait_ms(args, 500);
             double scale = 1.0;
             if(!args["scale"].get(scale))
             {
                 options.scale = scale;
             }
             return capture_fbo_screenshot(mcp, ctx, resolve_scene_obuffer, "scene", options);
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_capture_game",
         .description =
             "Capture Game panel / active camera as PNG (image content). Optional wait_ms (default 500), "
             "scale (0-1, default 1; bimg linear resize).",
         .input_schema_json =
             R"({"type":"object","properties":{"wait_ms":{"type":"integer","minimum":100,"maximum":15000},"scale":{"type":"number","minimum":0.05,"maximum":1}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             capture_options options{};
             options.wait_timeout = read_wait_ms(args, 500);
             double scale = 1.0;
             if(!args["scale"].get(scale))
             {
                 options.scale = scale;
             }
             return capture_fbo_screenshot(mcp, ctx, resolve_game_obuffer, "game", options);
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_get_camera",
         .description =
             "Get Scene panel editor camera pose (position, rotation_euler, forward/up, fov, ortho_size).",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = camera_to_json(camera), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_set_camera",
         .description =
             "Set Scene panel camera position and/or rotation_euler (degrees). "
             "Optional relative:true applies position in camera local space.",
         .input_schema_json =
             R"json({"type":"object","properties":{"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"relative":{"type":"boolean"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }

             math::vec3 position{};
             math::vec3 rotation{};
             const bool has_position = read_vec3(args, "position", position);
             const bool has_rotation = read_vec3(args, "rotation_euler", rotation);
             if(!has_position && !has_rotation)
             {
                 return {.text = "Provide position and/or rotation_euler", .is_error = true};
             }

             bool relative = false;
             read_bool(args, "relative", relative);

             cancel_camera_focus();
             auto& tc = camera.get<transform_component>();

             if(has_position)
             {
                 if(relative)
                 {
                     const auto world_delta = tc.get_x_axis_global() * position.x +
                                              tc.get_y_axis_global() * position.y +
                                              tc.get_z_axis_global() * position.z;
                     tc.set_position_global(tc.get_position_global() + world_delta);
                 }
                 else
                 {
                     tc.set_position_global(position);
                 }
             }
             if(has_rotation)
             {
                 tc.set_rotation_euler_global(rotation);
             }

             return {.text = R"({"ok":true})", .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_look_at",
         .description =
             "Aim Scene panel camera at a world-space target. Optional position and up (default world up).",
         .input_schema_json =
             R"json({"type":"object","properties":{"target":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"up":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}},"required":["target"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }

             math::vec3 target{};
             if(!read_vec3(args, "target", target))
             {
                 return {.text = "Missing target [x,y,z]", .is_error = true};
             }

             cancel_camera_focus();
             auto& tc = camera.get<transform_component>();

             math::vec3 position{};
             if(read_vec3(args, "position", position))
             {
                 tc.set_position_global(position);
             }

             math::vec3 up{};
             if(read_vec3(args, "up", up))
             {
                 tc.look_at(target, up);
             }
             else
             {
                 tc.look_at(target);
             }

             return {.text = fmt::format(R"({{"ok":true,"target":{}}})", vec3_to_json(target)), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_focus_entities_batch",
         .description =
             "Focus Scene panel camera on entities (dolly along current forward). "
             "duration default 0.4; use 0 for instant.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"entity_ids":{"type":"array","items":{"type":"string"}},"duration":{"type":"number","minimum":0,"maximum":10}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }

             std::vector<entt::handle> entities;
             if(!resolve_focus_entities(ctx, args, entities, error))
             {
                 return {.text = error, .is_error = true};
             }

             const float duration = read_duration(args, 0.4f);

             cancel_camera_focus();
             defaults::focus_camera_on_entities(camera, hpp::span<const entt::handle>{entities}, duration);

             return {.text = fmt::format(R"({{"ok":true,"count":{},"duration":{:.3g}}})", entities.size(), duration),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_focus_bounds",
         .description =
             "Focus Scene panel camera on a world sphere (center+radius) or box (min+max). "
             "duration default 0.4; use 0 for instant.",
         .input_schema_json =
             R"json({"type":"object","properties":{"center":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"radius":{"type":"number","minimum":0},"min":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"max":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"duration":{"type":"number","minimum":0,"maximum":10}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }

             const float duration = read_duration(args, 0.4f);

             math::vec3 center{};
             math::vec3 min_v{};
             math::vec3 max_v{};
             const bool has_center = read_vec3(args, "center", center);
             const bool has_min = read_vec3(args, "min", min_v);
             const bool has_max = read_vec3(args, "max", max_v);

             double radius = 0.0;
             const bool has_radius = !args["radius"].get(radius);

             cancel_camera_focus();

             if(has_min && has_max)
             {
                 math::bbox box;
                 box.add_point(min_v);
                 box.add_point(max_v);
                 defaults::focus_camera_on_bounds(camera, box, duration);
             }
             else if(has_center && has_radius)
             {
                 if(radius < 0.001)
                 {
                     radius = 0.001;
                 }
                 math::bsphere sphere{center, static_cast<float>(radius)};
                 defaults::focus_camera_on_bounds(camera, sphere, duration);
             }
             else
             {
                 return {.text = "Provide center+radius, or min+max", .is_error = true};
             }

             return {.text = fmt::format(R"({{"ok":true,"duration":{:.3g}}})", duration), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_orbit_camera",
         .description =
             "Orbit Scene panel camera around a world pivot by yaw/pitch degrees. "
             "Optional pivot and distance.",
         .input_schema_json =
             R"json({"type":"object","properties":{"yaw":{"type":"number"},"pitch":{"type":"number"},"pivot":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"distance":{"type":"number","minimum":0.01}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }

             double yaw = 0.0;
             double pitch = 0.0;
             (void)args["yaw"].get(yaw);
             (void)args["pitch"].get(pitch);
             if(yaw == 0.0 && pitch == 0.0)
             {
                 return {.text = "Provide yaw and/or pitch in degrees", .is_error = true};
             }

             cancel_camera_focus();
             auto& tc = camera.get<transform_component>();
             const auto position = tc.get_position_global();

             math::vec3 pivot = position + tc.get_z_axis_global() * 5.0f;
             read_vec3(args, "pivot", pivot);

             double distance = 0.0;
             if(args["distance"].get(distance) || distance <= 0.0)
             {
                 distance = static_cast<double>(math::length(position - pivot));
                 if(distance < 0.01)
                 {
                     distance = 5.0;
                 }
             }

             if(yaw != 0.0)
             {
                 tc.rotate_around_global(pivot, math::vec3{0.0f, 1.0f, 0.0f}, static_cast<float>(yaw));
             }
             if(pitch != 0.0)
             {
                 tc.rotate_around_global(pivot, tc.get_x_axis_global(), static_cast<float>(pitch));
             }

             // Enforce distance after orbit (rotate_around keeps it; distance override may differ).
             auto offset = tc.get_position_global() - pivot;
             if(math::length(offset) > 1e-6f)
             {
                 offset = math::normalize(offset) * static_cast<float>(distance);
                 tc.set_position_global(pivot + offset);
             }
             tc.look_at(pivot);

             return {.text = fmt::format(R"({{"ok":true,"pivot":{},"yaw":{:.3g},"pitch":{:.3g}}})",
                                         vec3_to_json(pivot),
                                         yaw,
                                         pitch),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_reset_camera",
         .description = "Reset Scene panel camera to the default editor pose.",
         .input_schema_json = empty_object_schema(),
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object&) -> tool_result
         {
             cancel_camera_focus();
             auto& panel = resolve_scene_panel(ctx);
             panel.reset_camera(ctx);

             std::string error;
             auto camera = resolve_scene_camera(ctx, error);
             if(!camera)
             {
                 return {.text = error, .is_error = true};
             }
             return {.text = R"({"ok":true})", .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_set_debug_view",
         .description = "Set the Scene panel debug visualization mode. Pass mode as a name string "
                        "(e.g. \"full\", \"base_color\", \"normals\", \"gi_light_voxels\") or as the "
                        "raw integer id (-1..31). \"full\" (-1) restores the normal render. Call "
                        "viewport_list_debug_views for every mode with its group, what it actually "
                        "shows and its color legend.",
         .input_schema_json = R"({"type":"object","properties":{"mode":{"description":"Mode name or raw integer id"}},"required":["mode"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             int mode_value = 0;
             bool resolved = false;

             std::string_view mode_name;
             int64_t mode_int = 0;
             if(args["mode"].get(mode_name) == simdjson::SUCCESS)
             {
                 const auto* entry = find_visualization_mode(mode_name);
                 if(entry == nullptr)
                 {
                     return {.text = fmt::format("Unknown debug view \"{}\". Valid modes: {}",
                                                 mode_name,
                                                 list_debug_view_names()),
                             .is_error = true};
                 }
                 mode_value = static_cast<int>(entry->mode);
                 resolved = true;
             }
             else if(args["mode"].get(mode_int) == simdjson::SUCCESS)
             {
                 const auto* entry = find_visualization_mode(static_cast<int>(mode_int));
                 if(entry == nullptr)
                 {
                     return {.text = fmt::format("Debug view id {} out of range. Valid modes: {}",
                                                 mode_int,
                                                 list_debug_view_names()),
                             .is_error = true};
                 }
                 mode_value = static_cast<int>(entry->mode);
                 resolved = true;
             }

             if(!resolved)
             {
                 return {.text = "Missing or invalid \"mode\" (string name or integer id expected)",
                         .is_error = true};
             }

             resolve_scene_panel(ctx).set_visualization_mode(mode_value);

             const auto* entry = find_visualization_mode(mode_value);
             const auto* group = entry != nullptr ? find_visualization_group(entry->group) : nullptr;

             return {.text = fmt::format(R"({{"ok":true,"mode":{},"name":"{}","label":{},"group":"{}"}})",
                                         mode_value,
                                         debug_view_name_for_value(mode_value),
                                         make_json_string(entry != nullptr ? entry->label : "unknown"),
                                         group != nullptr ? group->name : "none"),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "gi_get_stats",
         .description =
             "The GI waste census for the Scene panel camera: one on-demand readback of the "
             "relight / world-probe statistics slice (never per frame - it is a GPU sync). Per "
             "cascade level: relit faces and how many changed past the quiescence floor "
             "(GI_QUIESCENCE_CONVERGED_MEAN) and past GI_STATS_VISIBLE_CHANGE, world probes by "
             "state (active / asleep / buried) and their traced texels by the same thresholds. "
             "Rows 0-1 describe the frame before the snapshot; the census rows hold the last "
             "frame the gated passes actually ran. camera = \"scene\" (default, the Scene "
             "panel's editing camera) or \"game\" (the scene's rendering camera - the only one "
             "that renders while the Game panel is focused, e.g. in play mode).",
         .input_schema_json = R"({"type":"object","properties":{"timeout_ms":{"type":"integer","minimum":100,"maximum":10000},"camera":{"type":"string","enum":["scene","game"]}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             int64_t timeout_ms = 3000;
             if(args["timeout_ms"].get(timeout_ms))
             {
                 timeout_ms = 3000;
             }
             std::string camera_arg = "scene";
             read_string(args, "camera", camera_arg);
             const bool game_camera = camera_arg == "game";
             auto resolve_pipeline = [&ctx, game_camera]() -> rendering::pipeline*
             {
                 if(game_camera)
                 {
                     auto& em = ctx.get_cached<editing_manager>();
                     auto* scn = em.get_active_scene(ctx);
                     if(scn == nullptr)
                     {
                         return nullptr;
                     }
                     rendering::pipeline* found = nullptr;
                     scn->registry->view<camera_component, active_component>().each(
                         [&](auto, auto& cc, auto&)
                         {
                             if(found == nullptr && cc.get_pipeline_data().get_pipeline())
                             {
                                 found = cc.get_pipeline_data().get_pipeline().get();
                             }
                         });
                     return found;
                 }
                 auto camera_ent = resolve_scene_panel(ctx).get_camera();
                 if(!camera_ent || !camera_ent.all_of<camera_component>())
                 {
                     return nullptr;
                 }
                 return camera_ent.get<camera_component>().get_pipeline_data().get_pipeline().get();
             };
             auto requested = mcp.invoke_on_main(
                 [&]() -> uint32_t
                 {
                     auto* pipeline = resolve_pipeline();
                     if(pipeline == nullptr)
                     {
                         return uint32_t(-1);
                     }
                     pipeline->request_gi_stats_snapshot();
                     return gfx::get_render_frame();
                 });
             if(!requested || *requested == uint32_t(-1))
             {
                 return {.text = "Scene panel camera has no pipeline", .is_error = true};
             }
             const uint32_t request_frame = *requested;
             const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
             while(std::chrono::steady_clock::now() < deadline)
             {
                 std::this_thread::sleep_for(std::chrono::milliseconds(40));
                 auto fetched = mcp.invoke_on_main(
                     [&]() -> std::string
                     {
                         auto* pipeline = resolve_pipeline();
                         if(pipeline == nullptr)
                         {
                             return {};
                         }
                         const auto& snap = pipeline->get_gi_stats_snapshot();
                         if(!snap.valid || snap.frame < request_frame)
                         {
                             return {};
                         }
                         using snapshot = gi_quiescence_gate_pass::stats_snapshot;
                         static constexpr const char* names[snapshot::quantity_count] = {
                             "relight_change_sum", "relight_faces", "relight_faces_moved", "relight_faces_visible",
                             "probes_active", "probes_asleep", "probes_buried", "probe_texels", "probe_texels_moved",
                             "probe_texels_visible"};
                         std::string json = fmt::format(R"({{"frame":{},"levels":[)", snap.frame);
                         for(uint32_t level = 0; level < snapshot::level_count; ++level)
                         {
                             json += level == 0 ? "{" : ",{";
                             for(uint32_t q = 0; q < snapshot::quantity_count; ++q)
                             {
                                 const uint32_t raw = snap.at(q, level);
                                 if(q == 0)
                                 {
                                     json += fmt::format(R"("{}":{:.4f})",
                                                         names[q],
                                                         double(raw) / double(gi::GI_QUIESCENCE_STATS_SCALE));
                                 }
                                 else
                                 {
                                     json += fmt::format(R"(,"{}":{})", names[q], raw);
                                 }
                             }
                             json += "}";
                         }
                         json += fmt::format(R"(],"thresholds":{{"moved":{},"visible":{}}}}})",
                                             double(gi::GI_QUIESCENCE_CONVERGED_MEAN),
                                             double(gi::GI_STATS_VISIBLE_CHANGE));
                         return json;
                     });
                 if(fetched && !fetched->empty())
                 {
                     return {.text = *fetched, .is_error = false};
                 }
             }
             return {.text = "Timed out waiting for the GI stats readback (is the profiler / GI running?)",
                     .is_error = true};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "profiler_get_cpu_scopes",
         .description =
             "CPU profiler scopes (APP_SCOPE_PERF) of the newest captured frames, wall ms per "
             "scope name, averaged over `frames` (default 1) newest frames. Optional prefix "
             "filters names (e.g. \"GI/SurfaceCache\"). Starts the profiler recording if it is "
             "off; the first call after that returns no frames, call again.",
         .input_schema_json =
             R"({"type":"object","properties":{"prefix":{"type":"string"},"frames":{"type":"integer","minimum":1,"maximum":256}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string prefix;
             read_string(args, "prefix", prefix);
             int64_t frames = 1;
             if(args["frames"].get(frames))
             {
                 frames = 1;
             }
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     auto* profiler = get_app_profiler();
                     if(profiler == nullptr)
                     {
                         return R"({"error":"no profiler"})";
                     }
                     if(profiler->get_recording_state() != recording_state::recording)
                     {
                         profiler->set_recording_state(recording_state::recording);
                     }
                     const uint32_t available = profiler->get_frame_count();
                     const uint32_t take = std::min<uint32_t>(available, uint32_t(frames));
                     struct scope_stats
                     {
                         double sum_ms = 0.0;
                         double max_ms = 0.0;
                         uint32_t calls = 0;
                         std::string thread;
                     };
                     std::map<std::string, scope_stats> scopes;
                     for(uint32_t i = 0; i < take; ++i)
                     {
                         const auto* snap = profiler->get_frame_snapshot(available - 1 - i);
                         if(snap == nullptr)
                         {
                             continue;
                         }
                         for(const auto& thread : snap->threads)
                         {
                             for(const auto& ev : thread.events)
                             {
                                 const std::string name(ev.name());
                                 if(!prefix.empty() && name.rfind(prefix, 0) != 0)
                                 {
                                     continue;
                                 }
                                 const double ms = double(ev.end_ns - ev.start_ns) / 1'000'000.0;
                                 auto& s = scopes[name];
                                 s.sum_ms += ms;
                                 s.max_ms = std::max(s.max_ms, ms);
                                 ++s.calls;
                                 s.thread = thread.name;
                             }
                         }
                     }
                     std::string json = fmt::format(R"({{"frames":{},"scopes":[)", take);
                     bool first = true;
                     for(const auto& [name, s] : scopes)
                     {
                         json += fmt::format(R"({}{{"name":{},"ms_per_frame":{:.4f},"max_ms":{:.4f},"calls_per_frame":{:.2f},"thread":{}}})",
                                             first ? "" : ",",
                                             make_json_string(name),
                                             take > 0 ? s.sum_ms / double(take) : 0.0,
                                             s.max_ms,
                                             take > 0 ? double(s.calls) / double(take) : 0.0,
                                             make_json_string(s.thread));
                         first = false;
                     }
                     json += "]}";
                     return json;
                 });
             if(!result)
             {
                 return {.text = "profiler query failed on the main thread", .is_error = true};
             }
             return {.text = *result, .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_list_debug_views",
         .description = "List every debug visualization mode for viewport_set_debug_view, grouped, "
                        "with what each one actually shows and the meaning of its categorical "
                        "colors. Optional \"group\" restricts the listing to one group (surface, "
                        "occlusion, lighting, motion, distance_field, global_illumination); optional "
                        "\"include_legend\" (default true) drops the color tables for a short list. "
                        "Also returns the currently applied mode.",
         .input_schema_json =
             R"({"type":"object","properties":{"group":{"type":"string","description":"Restrict the listing to one group id"},"include_legend":{"type":"boolean","description":"Include per-mode color legends; default true"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             bool include_legend = true;
             read_bool(args, "include_legend", include_legend);

             std::string group_filter;
             read_string(args, "group", group_filter);
             const visualization_group_entry* only_group = nullptr;
             if(!group_filter.empty())
             {
                 only_group = find_visualization_group(group_filter);
                 if(only_group == nullptr)
                 {
                     std::string ids;
                     for(const auto& group : get_visualization_groups())
                     {
                         if(!ids.empty())
                         {
                             ids += ", ";
                         }
                         ids += group.name;
                     }
                     return {.text = fmt::format(R"(Unknown group "{}". Valid groups: {})", group_filter, ids),
                             .is_error = true};
                 }
             }

             const int active = resolve_scene_panel(ctx).get_visualization_mode();
             const auto* off = find_visualization_mode(static_cast<int>(visualization_mode::full));

             std::string json =
                 fmt::format(R"({{"active":{{"id":{},"name":"{}"}},"off":{},"groups":[)",
                             active,
                             debug_view_name_for_value(active),
                             off != nullptr ? debug_view_mode_json(*off, include_legend) : "null");

             bool first_group = true;
             for(const auto& group : get_visualization_groups())
             {
                 if(only_group != nullptr && only_group->group != group.group)
                 {
                     continue;
                 }
                 if(!first_group)
                 {
                     json += ",";
                 }
                 first_group = false;

                 json += fmt::format(R"({{"id":{},"label":{},"description":{},"modes":[)",
                                     make_json_string(group.name),
                                     make_json_string(group.label),
                                     make_json_string(group.description));
                 bool first_mode = true;
                 for(const auto& entry : get_visualization_modes(group.group))
                 {
                     if(!first_mode)
                     {
                         json += ",";
                     }
                     first_mode = false;
                     json += debug_view_mode_json(entry, include_legend);
                 }
                 json += "]}";
             }
             json += "]}";

             return {.text = json, .is_error = false};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
