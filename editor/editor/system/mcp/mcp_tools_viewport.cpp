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
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <seq/seq.h>

#include <chrono>
#include <cmath>
#include <cstdio>
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
        {.name = "viewport_set_render_size",
         .description = "Render the Scene panel camera at exactly width x height pixels (e.g. 1920 x 1080 for cost "
                        "measurements independent of the panel size) and show it scaled into the panel; 0 x 0 "
                        "follows the panel again. Picking and gizmos assume the panel size while it is forced.",
         .input_schema_json = R"({"type":"object","properties":{"width":{"type":"integer","minimum":0,"maximum":8192},"height":{"type":"integer","minimum":0,"maximum":8192}},"required":["width","height"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             int64_t width = 0;
             int64_t height = 0;
             if(args["width"].get(width) || args["height"].get(height) || width < 0 || height < 0 || width > 8192 ||
                height > 8192)
             {
                 return {.text = "width and height must be integers in 0..8192", .is_error = true};
             }
             resolve_scene_panel(ctx).set_forced_render_size(uint32_t(width), uint32_t(height));
             return {.text = fmt::format(R"({{"ok":true,"width":{},"height":{}}})", width, height), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_set_debug_view",
         .description = "Set the Scene panel debug visualization mode. Pass mode as a name string "
                        "(e.g. \"full\", \"base_color\", \"normals\", \"lumen_scene\") or as the "
                        "raw integer id. \"full\" (-1) restores the normal render. Call "
                        "viewport_list_debug_views for every mode with its group, what it actually "
                        "shows and its color legend. Optional scale (default 1): an exposure "
                        "multiplier the indirect_diffuse view applies ahead of its tone map.",
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
             "\"from_target\":[..],\"to_position\":[..],\"to_target\":[..]}, {\"type\":\"orbit\","
             "\"center\":[..],\"radius\":r,\"height\":h,\"start_degrees\":a,\"degrees\":sweep} or a pure turn at "
             "a constant rate {\"type\":\"yaw\",\"position\":[..],\"forward\":[..],\"start_degrees\":a,"
             "\"degrees\":sweep} (the forward rotated about world up). Optional `save` (absolute file path) also "
             "writes the per-pixel planes as raw little-endian float32, rows from the top: the mean luminance, its "
             "std, the mean reprojected change (-1 where unmeasured) and the LAST measured frame's luminance - "
             "frame-locked to the end of the measurement (and of the motion). Optional `motion_frames` runs the "
             "motion over the first motion_frames frames and holds its end pose for the rest. Optional `marks` "
             "(1-based frame indices, at most 16, needs `save`) captures those frames whole into <save>.marks: "
             "uint32 magic 'UPMK', version 1, count, then per mark uint32 frame, width, height, bgfx texture format, "
             "colour byte count, the displayed image as rendered (rows from the top) and the AGE plane as float32 "
             "(frames each pixel's surface had stayed visible within the measurement; 0 = revealed this frame).",
         .input_schema_json =
             R"json({"type":"object","properties":{"frames":{"type":"integer","minimum":2,"maximum":4096},"timeout_ms":{"type":"integer","minimum":1000,"maximum":600000},"motion":{"type":"object"},"motion_frames":{"type":"integer","minimum":1,"maximum":4096},"marks":{"type":"array","items":{"type":"integer","minimum":1,"maximum":4096}},"lowpass":{"type":"boolean"},"save":{"type":"string"}}})json",
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
             std::string save_path;
             read_string(args, "save", save_path);
             int64_t motion_frames = frames;
             if(args["motion_frames"].get(motion_frames))
             {
                 motion_frames = frames;
             }
             motion_frames = std::clamp<int64_t>(motion_frames, 1, frames);
             std::vector<uint32_t> marks;
             simdjson::dom::array marks_args;
             if(!args["marks"].get(marks_args))
             {
                 for(auto element : marks_args)
                 {
                     int64_t mark = 0;
                     if(!element.get(mark) && mark >= 1 && mark <= frames)
                     {
                         marks.push_back(uint32_t(mark));
                     }
                 }
                 if(save_path.empty())
                 {
                     return {.text = "marks need a save path", .is_error = true};
                 }
             }
             struct motion_spec
             {
                 std::string type;
                 math::vec3 from_position{};
                 math::vec3 from_target{};
                 math::vec3 to_position{};
                 math::vec3 to_target{};
                 math::vec3 position{};
                 math::vec3 forward{0.0f, 0.0f, 1.0f};
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
                 else if(motion.type == "yaw")
                 {
                     if(!read_vec3(motion_args, "position", motion.position) ||
                        !read_vec3(motion_args, "forward", motion.forward) || math::length(motion.forward) < 1e-6f)
                     {
                         return {.text = "motion yaw needs position and a non-zero forward", .is_error = true};
                     }
                     motion.forward = math::normalize(motion.forward);
                 }
                 else if(!motion.type.empty())
                 {
                     return {.text = "motion type must be \"path\", \"orbit\" or \"yaw\"", .is_error = true};
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
                 else if(motion.type == "yaw")
                 {
                     constexpr float degrees_to_radians = 0.017453292f;
                     const float angle = (motion.start_degrees + motion.degrees * u) * degrees_to_radians;
                     const float c = std::cos(angle);
                     const float s = std::sin(angle);
                     const math::vec3 direction(c * motion.forward.x + s * motion.forward.z,
                                                motion.forward.y,
                                                -s * motion.forward.x + c * motion.forward.z);
                     position = motion.position;
                     target = motion.position + direction * 10.0f;
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
                     pipeline->request_temporal_probe(uint32_t(frames), lowpass, !save_path.empty(), marks);
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
             std::vector<float> saved_images;
             std::vector<temporal_probe_pass::mark_capture> saved_marks;
             uint32_t saved_width = 0;
             uint32_t saved_height = 0;
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
                             apply_pose(std::min(float(probe.get_frames_done() + 1u) / float(motion_frames), 1.0f));
                         }
                         if(probe.is_busy() || !probe.get_result().valid)
                         {
                             return {};
                         }
                         const auto& r = probe.get_result();
                         if(!save_path.empty())
                         {
                             saved_images = r.images;
                             saved_marks = r.marks;
                             saved_width = r.width;
                             saved_height = r.height;
                         }
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
                     if(!save_path.empty())
                     {
                         const size_t expected = size_t(saved_width) * saved_height * 4u;
                         if(saved_images.size() != expected || expected == 0u)
                         {
                             return {.text = "temporal probe returned no per-pixel planes to save", .is_error = true};
                         }
                         std::FILE* file = std::fopen(save_path.c_str(), "wb");
                         if(file == nullptr)
                         {
                             return {.text = "cannot open the save path for writing", .is_error = true};
                         }
                         const size_t written = std::fwrite(saved_images.data(), sizeof(float), saved_images.size(), file);
                         std::fclose(file);
                         if(written != saved_images.size())
                         {
                             return {.text = "short write to the save path", .is_error = true};
                         }
                     }
                     if(!marks.empty())
                     {
                         std::FILE* file = std::fopen((save_path + ".marks").c_str(), "wb");
                         if(file == nullptr)
                         {
                             return {.text = "cannot open the marks file for writing", .is_error = true};
                         }
                         constexpr uint32_t marks_magic = 0x4B4D5055u;
                         constexpr uint32_t marks_version = 1u;
                         const uint32_t header[3] = {marks_magic, marks_version, uint32_t(saved_marks.size())};
                         bool ok = std::fwrite(header, sizeof(uint32_t), 3, file) == 3;
                         for(const auto& mark : saved_marks)
                         {
                             const uint32_t mark_header[5] = {mark.frame,
                                                              mark.width,
                                                              mark.height,
                                                              uint32_t(mark.format),
                                                              uint32_t(mark.color.size())};
                             ok = ok && std::fwrite(mark_header, sizeof(uint32_t), 5, file) == 5;
                             ok = ok && std::fwrite(mark.color.data(), 1, mark.color.size(), file) == mark.color.size();
                             ok = ok && std::fwrite(mark.age.data(), sizeof(float), mark.age.size(), file) ==
                                            mark.age.size();
                         }
                         std::fclose(file);
                         if(!ok)
                         {
                             return {.text = "short write to the marks file", .is_error = true};
                         }
                         std::string marks_json = ",\"marks\":[";
                         for(size_t i = 0; i < saved_marks.size(); ++i)
                         {
                             marks_json += fmt::format("{}{}", i == 0 ? "" : ",", saved_marks[i].frame);
                         }
                         marks_json += "]";
                         polled->insert(polled->size() - 1u, marks_json);
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
        {.name = "gi_sdf_probe",
         .description =
             "Diagnostic: every GI placement whose world bounds come within `radius` metres (default 1) of `position`, "
             "with the world distance its traced field level and its chain's coarsest resident level report there "
             "(sample_instance_distance, as the composed fields read a placement) and the coarse-first answer the "
             "Lumen cascade composes (UE DistanceToMeshSurfaceStandalone), sorted by that answer.",
         .input_schema_json =
             R"({"type":"object","properties":{"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"radius":{"type":"number","minimum":0}},"required":["position"]})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             math::vec3 position(0.0f);
             if(!read_vec3(args, "position", position))
             {
                 return {.text = "position must be [x, y, z]", .is_error = true};
             }
             double radius = 1.0;
             read_double(args, "radius", radius);
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     struct probe_row
                     {
                         size_t index = 0;
                         float traced = 0.0f;
                         float coarse = 0.0f;
                         float standalone = 0.0f;
                         const global_sdf_instance* placement = nullptr;
                     };
                     const auto& placements = ctx.get_cached<surface_cache_system>().get_clipmap_instances();
                     std::vector<probe_row> rows;
                     for(size_t i = 0; i < placements.size(); ++i)
                     {
                         const auto& placement = placements[i];
                         math::bbox reach = placement.world_bounds;
                         reach.inflate(float(radius));
                         if(placement.sdf == nullptr || !placement.sdf->is_sampleable() || !reach.contains_point(position))
                         {
                             continue;
                         }
                         const math::vec3 local(placement.world_to_local * math::vec4(position, 1.0f));
                         probe_row row;
                         row.index = i;
                         row.placement = &placement;
                         row.traced = sample_instance_distance(*placement.sdf,
                                                               local,
                                                               placement.axis_scale,
                                                               placement.local_to_world_scale,
                                                               true);
                         row.coarse = row.traced;
                         row.standalone = row.traced;
                         if(placement.coarse_sdf != nullptr)
                         {
                             row.coarse = sample_instance_distance(*placement.coarse_sdf,
                                                                   local,
                                                                   placement.axis_scale,
                                                                   placement.local_to_world_scale,
                                                                   true);
                             const float threshold = 0.25f * mesh_sdf::encode_range * placement.coarse_sdf->voxel_size *
                                                     placement.local_to_world_scale;
                             row.standalone = std::abs(row.coarse) > threshold ? row.coarse : row.traced;
                         }
                         rows.push_back(row);
                     }
                     std::sort(rows.begin(),
                               rows.end(),
                               [](const probe_row& a, const probe_row& b)
                               {
                                   return a.standalone < b.standalone;
                               });
                     std::string out = "[";
                     for(const auto& row : rows)
                     {
                         const auto& p = *row.placement;
                         const float coarse_voxel =
                             p.coarse_sdf != nullptr ? p.coarse_sdf->voxel_size * p.local_to_world_scale : 0.0f;
                         out += fmt::format(
                             R"({}{{"instance":{},"traced":{:.4f},"coarse":{:.4f},"standalone":{:.4f},"traced_voxel":{:.4f},"coarse_voxel":{:.4f},"two_sided":{},"min":[{:.2f},{:.2f},{:.2f}],"max":[{:.2f},{:.2f},{:.2f}]}})",
                             out.size() > 1 ? "," : "",
                             row.index,
                             row.traced,
                             row.coarse,
                             row.standalone,
                             p.sdf->voxel_size * p.local_to_world_scale,
                             coarse_voxel,
                             p.sdf->is_two_sided ? "true" : "false",
                             p.world_bounds.min.x,
                             p.world_bounds.min.y,
                             p.world_bounds.min.z,
                             p.world_bounds.max.x,
                             p.world_bounds.max.y,
                             p.world_bounds.max.z);
                     }
                     return out + "]";
                 });
             if(!result)
             {
                 return {.text = "sdf probe failed on the main thread", .is_error = true};
             }
             return {.text = *result, .is_error = false};
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "gi_set_experiment_flags",
         .description =
             "Runtime experiment flags of the Lumen passes (surface_cache_system::get_experiment_flags): each bit "
             "switches one stage for an in-session A/B - the gather's bits are listed in lumen_gather_pass.h, the "
             "reflections' in lumen_reflection_pass.h, the surface cache's in lumen_surface_cache_pass.h, the global "
             "distance field's in lumen_pass_common.h and the deferred pipeline. 0 is production. Returns the new and "
             "previous flags.",
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
        {.name = "viewport_set_overlays",
         .description =
             "Show or hide the Scene panel's editor overlays: the icon gizmos (camera, light and probe billboards and "
             "gizmos), the grid and the selection outline. Omitted keys keep their state; the state is not saved. "
             "viewport_measure_temporal reads the pipeline output and never includes them, viewport_capture_scene "
             "does. Returns the resulting state.",
         .input_schema_json =
             R"({"type":"object","properties":{"gizmos":{"type":"boolean"},"grid":{"type":"boolean"},"selection_outline":{"type":"boolean"}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             bool gizmos = false;
             bool grid = false;
             bool selection_outline = false;
             const bool set_gizmos = args["gizmos"].get(gizmos) == simdjson::SUCCESS;
             const bool set_grid = args["grid"].get(grid) == simdjson::SUCCESS;
             const bool set_selection_outline = args["selection_outline"].get(selection_outline) == simdjson::SUCCESS;
             auto& mcp = ctx.get_cached<mcp_manager>();
             auto result = mcp.invoke_on_main(
                 [&]() -> std::string
                 {
                     auto& em = ctx.get_cached<editing_manager>();
                     if(set_gizmos)
                     {
                         em.show_icon_gizmos = gizmos;
                     }
                     if(set_grid)
                     {
                         em.show_grid = grid;
                     }
                     if(set_selection_outline)
                     {
                         em.gizmos.show_selection_outline = selection_outline;
                     }
                     return fmt::format(R"({{"gizmos":{},"grid":{},"selection_outline":{}}})",
                                        em.show_icon_gizmos,
                                        em.show_grid,
                                        em.gizmos.show_selection_outline);
                 });
             if(!result)
             {
                 return {.text = "overlay update failed on the main thread", .is_error = true};
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
