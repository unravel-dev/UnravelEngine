#include "mcp_async.h"
#include "mcp_tools_common.h"

#include <editor/hub/hub.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/scene_panel/scene_panel.h>
#include <editor/system/mcp_manager.h>

#include <editor/editing/editing_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <seq/seq.h>

#include <vector>

namespace unravel::mcp
{
namespace
{

auto resolve_scene_panel(rtti::context& ctx) -> scene_panel&
{
    return ctx.get_cached<hub>().get_panels().get_scene_panel();
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
}

} // namespace unravel::mcp
