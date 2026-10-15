#include "mcp_tools_common.h"
#include "mcp_viewport_utils.h"

#include <editor/editing/scene_raycast.h>

#include <algorithm>
#include <string>
#include <vector>

namespace unravel::mcp
{
namespace
{

constexpr float k_default_reach = 10000.0f;
constexpr int64_t k_default_max_hits = 1;
constexpr int64_t k_max_hits_limit = 50;
constexpr size_t k_max_projected_points = 256;
/// A direction shorter than this names no direction.
constexpr float k_min_direction_length = 1e-6f;

auto viewport_size_json(const camera& cam) -> std::string
{
    const auto& size = cam.get_viewport_size();
    return fmt::format("[{},{}]", size.width, size.height);
}

/// The ray a call describes: origin + direction, or a pixel of the Scene view (screen).
auto read_ray(rtti::context& ctx, const simdjson::dom::object& args, scene_ray& ray, std::string& error) -> bool
{
    simdjson::dom::element screen_value;
    if(!args["screen"].get(screen_value))
    {
        math::vec2 pixel{};
        if(!parse_vec2(screen_value, pixel, error))
        {
            return false;
        }
        auto camera_entity = resolve_scene_camera(ctx, error);
        if(!camera_entity)
        {
            return false;
        }
        const auto& cam = camera_entity.get<camera_component>().get_camera();
        if(!cam.viewport_to_ray(pixel, ray.origin, ray.direction))
        {
            error = "The Scene view has no size yet (show it with panel_focus)";
            return false;
        }
        ray.direction = math::normalize(ray.direction);
        return true;
    }
    simdjson::dom::element origin_value;
    simdjson::dom::element direction_value;
    if(args["origin"].get(origin_value) || args["direction"].get(direction_value))
    {
        error = "Provide origin and direction, or screen";
        return false;
    }
    if(!parse_vec3(origin_value, ray.origin, error) || !parse_vec3(direction_value, ray.direction, error))
    {
        return false;
    }
    if(math::length(ray.direction) < k_min_direction_length)
    {
        error = "direction must not be zero";
        return false;
    }
    ray.direction = math::normalize(ray.direction);
    return true;
}

auto hit_json(const scene_ray_hit& hit) -> std::string
{
    const auto* tag = hit.entity.try_get<tag_component>();
    return fmt::format(R"({{"entity_id":{},"name":{},"point":{},"normal":{},"distance":{:.6g},"submesh":{}}})",
                       make_json_string(entity_id_string(hit.entity)),
                       make_json_string(tag != nullptr ? tag->name : std::string{}),
                       vec3_to_json(hit.point),
                       vec3_to_json(hit.normal),
                       hit.distance,
                       hit.submesh_index);
}

/// One projected point: Scene view pixel, view depth and whether it is on screen.
auto projected_point_json(const camera& cam, const math::vec3& point) -> std::string
{
    const auto screen = cam.world_to_viewport(point);
    const float depth = cam.world_to_view_depth(point);
    const auto& size = cam.get_viewport_size();
    const bool is_visible = depth > 0.0f && screen.x >= 0.0f && screen.y >= 0.0f &&
                            screen.x < static_cast<float>(size.width) && screen.y < static_cast<float>(size.height);
    return fmt::format(R"({{"screen":[{:.1f},{:.1f}],"depth":{:.6g},"visible":{}}})",
                       screen.x,
                       screen.y,
                       depth,
                       bool_to_json(is_visible));
}

} // namespace

void register_spatial_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_raycast",
         .description =
             "Cast a ray against the models' triangles and return the hits nearest first (one per entity: "
             "entity_id, point, normal facing the ray, distance) - in edit mode too, no physics needed. The ray "
             "is origin + direction, or screen: a Scene view pixel as viewport_capture_scene shows it at scale 1. "
             "Use it to place things on surfaces (cast down) or to find what is under a pixel of a capture.",
         .input_schema_json =
             R"json({"type":"object","properties":{"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"World direction; normalized for you"},"screen":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Scene view pixel [x,y], top-left origin; instead of origin + direction"},"max_distance":{"type":"number","exclusiveMinimum":0,"description":"Default 10000"},"max_hits":{"type":"integer","minimum":1,"maximum":50,"description":"Default 1"},"ignore_entity_ids":{"type":"array","items":{"type":"string"},"description":"Left out, with everything under them"}}})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             scene* scn = nullptr;
             if(!require_active_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             scene_ray ray;
             if(!read_ray(ctx, args, ray, error))
             {
                 return {.text = error, .is_error = true};
             }
             double max_distance = k_default_reach;
             read_double(args, "max_distance", max_distance);
             ray.max_distance = static_cast<float>(std::max(max_distance, 0.0));
             const auto max_hits = std::clamp(read_int64_or(args, "max_hits", k_default_max_hits), int64_t{1}, k_max_hits_limit);
             std::vector<entt::handle> ignored;
             std::vector<std::string> ignored_ids;
             if(!read_string_array(args, "ignore_entity_ids", ignored_ids, error, false) ||
                !resolve_entities(*scn, ignored_ids, ignored, error))
             {
                 return {.text = error, .is_error = true};
             }
             const auto hits = raycast_scene_models(*scn, ray, ignored);
             std::string hits_json;
             const auto count = std::min(hits.size(), static_cast<size_t>(max_hits));
             for(size_t i = 0; i < count; ++i)
             {
                 hits_json += (i > 0 ? "," : "") + hit_json(hits[i]);
             }
             return {.text = fmt::format(R"({{"ray":{{"origin":{},"direction":{}}},"hits":[{}],"count":{},"more":{}}})",
                                         vec3_to_json(ray.origin),
                                         vec3_to_json(ray.direction),
                                         hits_json,
                                         count,
                                         hits.size() - count),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});

    registry.add(
        {.name = "viewport_world_to_screen",
         .description =
             "Where world points appear in the Scene view: pixel [x,y] (top-left origin, as viewport_capture_scene "
             "shows it at scale 1), view depth (negative: behind the camera) and visible (in front and inside the "
             "view). Pairs with scene_raycast {screen} for the other direction.",
         .input_schema_json =
             R"json({"type":"object","properties":{"points":{"type":"array","maxItems":256,"items":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}}},"required":["points"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             simdjson::dom::array points;
             if(!read_required_array(args, "points", points, error))
             {
                 return {.text = error, .is_error = true};
             }
             if(points.size() > k_max_projected_points)
             {
                 return {.text = fmt::format("At most {} points per call", k_max_projected_points), .is_error = true};
             }
             auto camera_entity = resolve_scene_camera(ctx, error);
             if(!camera_entity)
             {
                 return {.text = error, .is_error = true};
             }
             const auto& cam = camera_entity.get<camera_component>().get_camera();
             std::string results;
             for(auto element : points)
             {
                 math::vec3 point{};
                 if(!parse_vec3(element, point, error))
                 {
                     return {.text = error, .is_error = true};
                 }
                 results += (results.empty() ? "" : ",") + projected_point_json(cam, point);
             }
             return {.text = fmt::format(R"({{"viewport_size":{},"points":[{}]}})", viewport_size_json(cam), results),
                     .is_error = false};
         },
         .mutates_scene = false,
         .effect = tool_effect::read_only});
}

} // namespace unravel::mcp
