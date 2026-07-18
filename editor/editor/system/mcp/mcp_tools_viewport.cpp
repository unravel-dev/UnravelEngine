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

auto read_wait_ms(const simdjson::dom::object& args) -> std::chrono::milliseconds
{
    int64_t wait_ms = 3000;
    if(args["wait_ms"].get(wait_ms))
    {
        wait_ms = 3000;
    }
    if(wait_ms < 100)
    {
        wait_ms = 100;
    }
    if(wait_ms > 15000)
    {
        wait_ms = 15000;
    }
    return std::chrono::milliseconds(wait_ms);
}

auto vec3_to_json(const math::vec3& v) -> std::string
{
    return fmt::format("[{:.6g},{:.6g},{:.6g}]", v.x, v.y, v.z);
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

    out.clear();

    std::string single_id;
    if(read_string(args, "entity_id", single_id) && !single_id.empty())
    {
        auto entity = find_entity(*scn, single_id);
        if(!entity)
        {
            error = "Entity not found: " + single_id;
            return false;
        }
        out.push_back(entity);
    }

    simdjson::dom::array ids;
    if(!args["entity_ids"].get(ids))
    {
        for(auto el : ids)
        {
            std::string_view id_view;
            if(el.get(id_view))
            {
                error = "entity_ids must be an array of strings";
                return false;
            }
            auto entity = find_entity(*scn, std::string(id_view));
            if(!entity)
            {
                error = "Entity not found: " + std::string(id_view);
                return false;
            }
            out.push_back(entity);
        }
    }

    if(out.empty())
    {
        error = "Provide entity_id or entity_ids";
        return false;
    }
    return true;
}

} // namespace

void register_viewport_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "viewport_screenshot_scene",
         .description = "Capture a PNG screenshot of the Scene panel OBUFFER (editor viewport). "
                        "Blit + GPU readback of the offscreen color target (action + wait). "
                        "Returns an image content block plus metadata JSON.",
         .input_schema_json =
             R"({"type":"object","properties":{"wait_ms":{"type":"integer","minimum":100,"maximum":15000,"description":"Max time to wait for PNG after requesting capture (default 3000)."}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             return capture_fbo_screenshot(mcp, ctx, resolve_scene_obuffer, "scene", read_wait_ms(args));
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_screenshot_game",
         .description = "Capture a PNG screenshot of the Game panel OBUFFER (active scene camera). "
                        "Blit + GPU readback of the offscreen color target (action + wait). "
                        "Returns an image content block plus metadata JSON.",
         .input_schema_json =
             R"({"type":"object","properties":{"wait_ms":{"type":"integer","minimum":100,"maximum":15000,"description":"Max time to wait for PNG after requesting capture (default 3000)."}}})",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             auto& mcp = ctx.get_cached<mcp_manager>();
             return capture_fbo_screenshot(mcp, ctx, resolve_game_obuffer, "game", read_wait_ms(args));
         },
         .mutates_scene = false,
         .requires_main_thread = false});

    registry.add(
        {.name = "viewport_get_camera",
         .description =
             "Get the Scene panel editor camera pose (position, rotation_euler degrees, forward/up, "
             "fov, ortho_size). This is the viewport camera, not a scene Camera entity.",
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
             "Set Scene panel camera position and/or rotation_euler (degrees). Cancels any in-flight "
             "focus animation. Optional relative:true applies position as a local-space offset.",
         .input_schema_json =
             R"json({"type":"object","properties":{"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"rotation_euler":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"relative":{"type":"boolean","description":"If true, position is added in camera local space (default false)"}}})json",
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

             return {.text = camera_to_json(camera), .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_look_at",
         .description =
             "Aim the Scene panel camera at a world-space target point. Optional position moves the "
             "camera first; optional up vector (default world up).",
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

             return {.text = fmt::format(R"({{"ok":true,"target":{},"camera":{}}})",
                                         vec3_to_json(target),
                                         camera_to_json(camera)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_focus_entities",
         .description =
             "Focus the Scene panel camera on one or more scene entities using "
             "defaults::focus_camera_on_entities (same as hierarchy F / double-click). "
             "Keeps current rotation unless aim:true (look at bounds center first). "
             "duration default 0.4s; use 0 for instant.",
         .input_schema_json =
             R"json({"type":"object","properties":{"entity_id":{"type":"string"},"entity_ids":{"type":"array","items":{"type":"string"}},"duration":{"type":"number","minimum":0,"maximum":10},"aim":{"type":"boolean","description":"Look at the entities' bounds center before focusing (default false)"}}})json",
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

             bool aim = false;
             read_bool(args, "aim", aim);
             const float duration = read_duration(args, 0.4f);

             cancel_camera_focus();

             defaults::focus_camera_on_entities(camera, hpp::span<const entt::handle>{entities}, duration);
    

             return {.text = fmt::format(
                         R"({{"ok":true,"count":{},"duration":{:.3g},"aim":{},"camera":{}}})",
                         entities.size(),
                         duration,
                         aim ? "true" : "false",
                         camera_to_json(camera)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_focus_bounds",
         .description =
             "Focus the Scene panel camera on a world-space sphere (center+radius) or box "
             "(min+max) via defaults::focus_camera_on_bounds. Optional aim:true looks at center first.",
         .input_schema_json =
             R"json({"type":"object","properties":{"center":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"radius":{"type":"number","minimum":0},"min":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"max":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"duration":{"type":"number","minimum":0,"maximum":10},"aim":{"type":"boolean"}}})json",
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
             bool aim = false;
             read_bool(args, "aim", aim);

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
                 if(aim)
                 {
                     camera.get<transform_component>().look_at(box.get_center());
                 }
                 defaults::focus_camera_on_bounds(camera, box, duration);
             }
             else if(has_center && has_radius)
             {
                 if(radius < 0.001)
                 {
                     radius = 0.001;
                 }
                 math::bsphere sphere{center, static_cast<float>(radius)};
                 if(aim)
                 {
                     camera.get<transform_component>().look_at(center);
                 }
                 defaults::focus_camera_on_bounds(camera, sphere, duration);
             }
             else
             {
                 return {.text = "Provide center+radius, or min+max", .is_error = true};
             }

             return {.text = fmt::format(R"({{"ok":true,"duration":{:.3g},"aim":{},"camera":{}}})",
                                         duration,
                                         aim ? "true" : "false",
                                         camera_to_json(camera)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_orbit_camera",
         .description =
             "Orbit the Scene panel camera around a world pivot by yaw/pitch degrees (Y-up then "
             "camera-right). Defaults pivot to current look target estimate from focus, or "
             "explicit pivot. Keeps distance to pivot.",
         .input_schema_json =
             R"json({"type":"object","properties":{"yaw":{"type":"number","description":"Degrees around world up"},"pitch":{"type":"number","description":"Degrees around camera right"},"pivot":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"distance":{"type":"number","minimum":0.01,"description":"Optional override distance to pivot"}}})json",
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

             return {.text = fmt::format(R"({{"ok":true,"pivot":{},"yaw":{:.3g},"pitch":{:.3g},"camera":{}}})",
                                         vec3_to_json(pivot),
                                         yaw,
                                         pitch,
                                         camera_to_json(camera)),
                     .is_error = false};
         },
         .mutates_scene = false});

    registry.add(
        {.name = "viewport_reset_camera",
         .description =
             "Reset the Scene panel camera to the default editor pose (same as Scene panel "
             "Reset Camera button).",
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
             return {.text = fmt::format(R"({{"ok":true,"camera":{}}})", camera_to_json(camera)),
                     .is_error = false};
         },
         .mutates_scene = false});
}

} // namespace unravel::mcp
