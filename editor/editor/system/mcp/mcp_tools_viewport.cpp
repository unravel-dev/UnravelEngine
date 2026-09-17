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
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/pipeline/passes/gi_quiescence_gate_pass.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <seq/seq.h>

#include <chrono>
#include <cmath>
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

/// The pipeline of the Scene panel's editing camera, or - for @p game_camera - of the scene's
/// active rendering camera (the only one that renders while the Game panel is focused).
/// Main thread only: it walks the live registry.
auto resolve_camera_pipeline(rtti::context& ctx, bool game_camera) -> rendering::pipeline*
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
                        "shows and its color legend. Optional scale (default 1): a linear readback "
                        "multiplier for the radiance-valued views (gi_light_voxels, gi_world_probes, "
                        "gi_attr_emissive, gi_direct_lighting, indirect_diffuse), applied before the "
                        "8-bit store so a capture reads linear radiance at any magnitude.",
         .input_schema_json = R"({"type":"object","properties":{"mode":{"description":"Mode name or raw integer id"},"scale":{"type":"number","minimum":0.0000001,"maximum":1000000}},"required":["mode"]})",
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

             double view_scale = 1.0;
             if(args["scale"].get(view_scale) != simdjson::SUCCESS || !(view_scale > 0.0))
             {
                 view_scale = 1.0;
             }
             resolve_scene_panel(ctx).set_visualization_mode(mode_value);
             resolve_scene_panel(ctx).set_visualization_scale(static_cast<float>(view_scale));

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
        {.name = "viewport_measure_temporal",
         .description =
             "Temporal stability of the Scene panel image (temporal_probe_pass): folds `frames` "
             "consecutive rendered frames into per-pixel statistics on the GPU, then one readback. "
             "Reports, in 8-bit display levels: the per-pixel luminance std over the frames (meaningful "
             "for a still camera) and the mean reprojected frame-to-frame change (valid in motion; object "
             "motion, disocclusions and depth edges are excluded), as percentiles, shares and an 8x8 "
             "screen grid (row-major from the top). The image is the pipeline output before the editor "
             "overlays: the lit frame or the active debug view. Optional `lowpass` (bool) runs every statistic on "
             "a 5x5 box mean of the luminance: under camera motion the raw change is dominated by the sub-pixel "
             "resampling of textured detail, which the box removes. Optional `motion` drives the Scene camera "
             "across exactly the measured frames: {\"type\":\"path\",\"from_position\":[..],"
             "\"from_target\":[..],\"to_position\":[..],\"to_target\":[..]} or {\"type\":\"orbit\","
             "\"center\":[..],\"radius\":r,\"height\":h,\"start_degrees\":a,\"degrees\":sweep}.",
         .input_schema_json =
             R"json({"type":"object","properties":{"frames":{"type":"integer","minimum":2,"maximum":4096},"timeout_ms":{"type":"integer","minimum":1000,"maximum":600000},"motion":{"type":"object"},"lowpass":{"type":"boolean"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             int64_t frames = 120;
             if(args["frames"].get(frames))
             {
                 frames = 120;
             }
             frames = std::clamp<int64_t>(frames, 2, 4096);
             bool lowpass = false;
             if(args["lowpass"].get(lowpass))
             {
                 lowpass = false;
             }
             int64_t timeout_ms = 30000 + frames * 100;
             int64_t requested_timeout = 0;
             if(!args["timeout_ms"].get(requested_timeout))
             {
                 timeout_ms = requested_timeout;
             }
             struct motion_spec
             {
                 std::string type;
                 math::vec3 from_position{};
                 math::vec3 from_target{};
                 math::vec3 to_position{};
                 math::vec3 to_target{};
                 math::vec3 center{};
                 float radius = 4.0f;
                 float height = 2.0f;
                 float start_degrees = 0.0f;
                 float degrees = 30.0f;
             };
             motion_spec motion;
             simdjson::dom::object motion_args;
             if(!args["motion"].get(motion_args))
             {
                 read_string(motion_args, "type", motion.type);
                 auto read_float = [&motion_args](const char* key, float& value)
                 {
                     double number = 0.0;
                     if(!motion_args[key].get(number))
                     {
                         value = float(number);
                     }
                 };
                 read_float("radius", motion.radius);
                 read_float("height", motion.height);
                 read_float("start_degrees", motion.start_degrees);
                 read_float("degrees", motion.degrees);
                 if(motion.type == "path")
                 {
                     if(!read_vec3(motion_args, "from_position", motion.from_position) ||
                        !read_vec3(motion_args, "from_target", motion.from_target) ||
                        !read_vec3(motion_args, "to_position", motion.to_position) ||
                        !read_vec3(motion_args, "to_target", motion.to_target))
                     {
                         return {.text = "motion path needs from_position, from_target, to_position, to_target",
                                 .is_error = true};
                     }
                 }
                 else if(motion.type == "orbit")
                 {
                     if(!read_vec3(motion_args, "center", motion.center))
                     {
                         return {.text = "motion orbit needs center", .is_error = true};
                     }
                 }
                 else if(!motion.type.empty())
                 {
                     return {.text = "motion type must be \"path\" or \"orbit\"", .is_error = true};
                 }
             }
             const bool has_motion = !motion.type.empty();
             auto apply_pose = [&ctx, &motion](float u)
             {
                 std::string error;
                 auto camera = resolve_scene_camera(ctx, error);
                 if(!camera)
                 {
                     return;
                 }
                 math::vec3 position{};
                 math::vec3 target{};
                 if(motion.type == "path")
                 {
                     position = motion.from_position + (motion.to_position - motion.from_position) * u;
                     target = motion.from_target + (motion.to_target - motion.from_target) * u;
                 }
                 else
                 {
                     constexpr float degrees_to_radians = 0.017453292f;
                     const float angle = (motion.start_degrees + motion.degrees * u) * degrees_to_radians;
                     position = motion.center +
                                math::vec3(std::cos(angle) * motion.radius, motion.height, std::sin(angle) * motion.radius);
                     target = motion.center;
                 }
                 cancel_camera_focus();
                 auto& tc = camera.get<transform_component>();
                 tc.set_position_global(position);
                 tc.look_at(target);
             };
             auto resolve_pipeline = [&ctx]() -> rendering::pipeline*
             {
                 auto camera_ent = resolve_scene_panel(ctx).get_camera();
                 if(!camera_ent || !camera_ent.all_of<camera_component>())
                 {
                     return nullptr;
                 }
                 return camera_ent.get<camera_component>().get_pipeline_data().get_pipeline().get();
             };
             auto armed = mcp.invoke_on_main(
                 [&]() -> bool
                 {
                     auto* pipeline = resolve_pipeline();
                     if(pipeline == nullptr)
                     {
                         return false;
                     }
                     if(has_motion)
                     {
                         apply_pose(0.0f);
                     }
                     pipeline->request_temporal_probe(uint32_t(frames), lowpass);
                     return true;
                 });
             if(!armed || !*armed)
             {
                 return {.text = "Scene panel camera has no pipeline", .is_error = true};
             }
             auto format_grid = [](const std::vector<float>& grid) -> std::string
             {
                 std::string json = "[";
                 for(size_t i = 0; i < grid.size(); ++i)
                 {
                     json += fmt::format("{}{:.3f}", i == 0 ? "" : ",", grid[i]);
                 }
                 return json + "]";
             };
             const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
             while(std::chrono::steady_clock::now() < deadline)
             {
                 auto polled = mcp.invoke_on_main(
                     [&]() -> std::string
                     {
                         auto* pipeline = resolve_pipeline();
                         if(pipeline == nullptr)
                         {
                             return "!";
                         }
                         const auto& probe = pipeline->get_temporal_probe();
                         if(has_motion && probe.get_frames_done() < probe.get_frames_requested())
                         {
                             apply_pose(float(probe.get_frames_done() + 1u) / float(probe.get_frames_requested()));
                         }
                         if(probe.is_busy() || !probe.get_result().valid)
                         {
                             return {};
                         }
                         const auto& r = probe.get_result();
                         return fmt::format(
                             R"({{"frames":{},"lowpass":{},"width":{},"height":{},"std":{{"p50":{:.3f},"p95":{:.3f},"p99":{:.3f},"share_gt_1_5":{:.5f},"share_gt_4":{:.5f}}},"delta":{{"pixels":{},"mean":{:.3f},"p50":{:.3f},"p95":{:.3f},"p99":{:.3f},"share_gt_1":{:.5f},"share_gt_4":{:.5f}}},"std_grid":{},"delta_grid":{}}})",
                             r.frames, r.lowpass, r.width, r.height, r.std_percentiles[0], r.std_percentiles[1],
                             r.std_percentiles[2], r.std_shares[0], r.std_shares[1], r.delta_pixels, r.delta_mean,
                             r.delta_percentiles[0], r.delta_percentiles[1], r.delta_percentiles[2],
                             r.delta_shares[0], r.delta_shares[1], format_grid(r.std_grid),
                             format_grid(r.delta_grid));
                     });
                 if(polled && !polled->empty())
                 {
                     if(*polled == "!")
                     {
                         return {.text = "Scene panel camera has no pipeline", .is_error = true};
                     }
                     return {.text = *polled, .is_error = false};
                 }
                 std::this_thread::sleep_for(std::chrono::milliseconds(2));
             }
             return {.text = "Timed out waiting for the temporal probe", .is_error = true};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "gi_get_stats",
         .description =
             "The GI waste census for the Scene panel camera: one on-demand readback of the "
             "relight / world-probe statistics slice (never per frame - it is a GPU sync). Per "
             "cascade level: relit faces and how many changed past the quiescence floor "
             "(GI_QUIESCENCE_CONVERGED_MEAN) and past GI_STATS_VISIBLE_CHANGE, world probes by "
             "state (active / asleep / buried) and their traced texels by the same thresholds. "
             "Rows 0-2 describe the frame before the snapshot; the census rows hold the last "
             "frame the gated passes actually ran. The census rows are instrument work the "
             "passes do only while this tool has been called within the last ~240 frames: "
             "the first call arms them and holds the copy a few frames so they accumulate "
             "(a closed gate keeps them at their last armed frame). camera = \"scene\" (default, the Scene "
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
                             "relight_change_sum", "relight_faces", "relight_rise_sum", "relight_faces_moved",
                             "relight_faces_visible", "probes_active", "probes_asleep", "probes_buried",
                             "probe_texels", "probe_texels_moved", "probe_texels_visible",
                             "probes_allocated", "probes_evicted"};
                         // The two fixed-point sums (GI_STATS_RELIGHT_CHANGE / _RISE).
                         static constexpr uint32_t rise_row = 2u;
                         std::string json = fmt::format(R"({{"frame":{},"levels":[)", snap.frame);
                         for(uint32_t level = 0; level < snapshot::level_count; ++level)
                         {
                             json += level == 0 ? "{" : ",{";
                             for(uint32_t q = 0; q < snapshot::quantity_count; ++q)
                             {
                                 const uint32_t raw = snap.at(q, level);
                                 if(q == 0 || q == rise_row)
                                 {
                                     json += fmt::format(R"({}"{}":{:.4f})",
                                                         q == 0 ? "" : ",",
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
                         // The emitter table the tracers aim at (surface_cache_system::rebuild_emitters):
                         // count after the cap, pieces built before it, and the strongest entries.
                         {
                             const auto& surface_cache = ctx.get_cached<surface_cache_system>();
                             const auto& emitters = surface_cache.get_emitters();
                             json += fmt::format(R"(],"emitters":{{"count":{},"total":{},"listed":[)",
                                                 emitters.size(),
                                                 surface_cache.get_emitter_total());
                             constexpr size_t listed_max = 16;
                             for(size_t i = 0; i < emitters.size() && i < listed_max; ++i)
                             {
                                 const auto& e = emitters[i];
                                 const float luminance = 0.2126f * e.radiance.x + 0.7152f * e.radiance.y + 0.0722f * e.radiance.z;
                                 json += fmt::format(R"({}{{"center":[{:.3f},{:.3f},{:.3f}],"radius":{:.3f},"luminance":{:.4f},"extent":[{:.3f},{:.3f},{:.3f}],"power":{:.4f}}})",
                                                     i == 0 ? "" : ",",
                                                     e.center.x,
                                                     e.center.y,
                                                     e.center.z,
                                                     e.radius,
                                                     luminance,
                                                     e.extent.x,
                                                     e.extent.y,
                                                     e.extent.z,
                                                     e.power);
                             }
                             json += "]}";
                             // The temporal's dirty regions as the shaders receive them (the budget cut
                             // applied), with the total before the cut.
                             const auto& regions = surface_cache.get_dirty_regions();
                             json += fmt::format(R"(,"dirty_regions":{{"count":{},"total":{},"bounds":[)",
                                                 regions.size(),
                                                 surface_cache.get_dirty_region_total());
                             for(size_t i = 0; i < regions.size(); ++i)
                             {
                                 const auto& region = regions[i].bounds;
                                 json += fmt::format(R"({}[{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f}])",
                                                     i == 0 ? "" : ",",
                                                     region.min.x,
                                                     region.min.y,
                                                     region.min.z,
                                                     region.max.x,
                                                     region.max.y,
                                                     region.max.z,
                                                     regions[i].emissive_reach);
                             }
                             json += "]}";
                         }
                         json += fmt::format(R"(,"thresholds":{{"moved":{},"visible":{}}}}})",
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
        {.name = "gi_set_experiment_flags",
         .description =
             "Runtime experiment flags every GI tracer reads (u_sdf_grid_params[2].x, sdf_common.sh "
             "u_sdf_experiment_flags; no experiment is compiled in at the moment). Two code paths "
             "compiled into one program alternate "
             "inside ONE editor launch for cost A/Bs. 0 is production. Returns the new and previous flags.",
         .input_schema_json =
             R"({"type":"object","properties":{"flags":{"type":"integer","minimum":0}},"required":["flags"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             double flags = 0.0;
             read_double(args, "flags", flags);
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     auto& surface_cache = ctx.get_cached<surface_cache_system>();
                     const uint32_t previous = surface_cache.get_experiment_flags();
                     surface_cache.set_experiment_flags(uint32_t(math::max(flags, 0.0)));
                     return fmt::format(R"({{"flags":{},"previous":{}}})", surface_cache.get_experiment_flags(), previous);
                 });
             if(!result)
             {
                 return {.text = "experiment flags update failed on the main thread", .is_error = true};
             }
             return {.text = *result, .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_get_exposure",
         .description =
             "The exposure chain's state on the camera's last rendered frame, read from the CPU "
             "side (no GPU sync): pre_exposure (the scene-color scale every lighting pass "
             "multiplies by, UE View.PreExposure) with the previous frame's value and their "
             "ratio, adapted_exposure (auto exposure's own output, delivered by the occlusion-query "
             "readback channel a few frames late), manual_exposure (the tonemapper's scale), and "
             "log2 of each. auto_exposure tells whether the adaptation ran at all; "
             "override_active whether viewport_set_pre_exposure_override is forcing the value. "
             "camera = \"scene\" (default) or \"game\".",
         .input_schema_json = R"({"type":"object","properties":{"camera":{"type":"string","enum":["scene","game"]}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string camera_arg = "scene";
             read_string(args, "camera", camera_arg);
             const bool game_camera = camera_arg == "game";
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     auto* pipeline = resolve_camera_pipeline(ctx, game_camera);
                     if(pipeline == nullptr)
                     {
                         return {};
                     }
                     const auto readout = pipeline->get_exposure_readout();
                     const auto log2_or_zero = [](float value) -> float
                     {
                         return value > 0.0f ? std::log2(value) : 0.0f;
                     };
                     return fmt::format(
                         R"({{"pre_exposure":{:.6g},"previous_pre_exposure":{:.6g},"history_correction":{:.6g},)"
                         R"("adapted_exposure":{:.6g},"manual_exposure":{:.6g},)"
                         R"("log2":{{"pre_exposure":{:.4f},"adapted_exposure":{:.4f},"manual_exposure":{:.4f}}},)"
                         R"("auto_exposure":{},"override_active":{}}})",
                         readout.pre_exposure,
                         readout.previous_pre_exposure,
                         readout.previous_pre_exposure > 0.0f ? readout.pre_exposure / readout.previous_pre_exposure
                                                              : 1.0f,
                         readout.adapted_exposure,
                         readout.manual_exposure,
                         log2_or_zero(readout.pre_exposure),
                         log2_or_zero(readout.adapted_exposure),
                         log2_or_zero(readout.manual_exposure),
                         readout.is_auto_exposure_active,
                         readout.is_override_active);
                 });
             if(!result || result->empty())
             {
                 return {.text = "That camera has no pipeline", .is_error = true};
             }
             return {.text = *result, .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_set_pre_exposure_override",
         .description =
             "Force the scene-color pre-exposure of camera runs (UE r.EyeAdaptation.PreExposureOverride); "
             "0 restores the computed value. An INSTRUMENT: the final image must not change under any "
             "override, because every writer multiplies by the value and every consumer divides it out, "
             "so a visible change means a pass is missing the scale. Values far from the computed one "
             "(0.01, 100) are the useful test. camera = \"scene\" (default) or \"game\".",
         .input_schema_json =
             R"({"type":"object","properties":{"value":{"type":"number","minimum":0,"maximum":65536},"camera":{"type":"string","enum":["scene","game"]}},"required":["value"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             double value = 0.0;
             read_double(args, "value", value);
             std::string camera_arg = "scene";
             read_string(args, "camera", camera_arg);
             const bool game_camera = camera_arg == "game";
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     auto* pipeline = resolve_camera_pipeline(ctx, game_camera);
                     if(pipeline == nullptr)
                     {
                         return {};
                     }
                     const auto previous = pipeline->get_exposure_readout();
                     pipeline->set_pre_exposure_override(static_cast<float>(math::max(value, 0.0)));
                     return fmt::format(R"({{"ok":true,"override":{:.6g},"previous_pre_exposure":{:.6g}}})",
                                        value,
                                        previous.pre_exposure);
                 });
             if(!result || result->empty())
             {
                 return {.text = "That camera has no pipeline", .is_error = true};
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
