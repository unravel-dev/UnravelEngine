#include "mcp_asset_lookup.h"
#include "mcp_material_utils.h"
#include "mcp_project_paths.h"
#include "mcp_tools_common.h"
#include "mcp_viewport_utils.h"

#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/material_graph/graph_material.h>

#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/editing_manager.h>
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
        if(args["origin"].error() == simdjson::SUCCESS || args["direction"].error() == simdjson::SUCCESS)
        {
            error = "Pass screen, or origin and direction, not both";
            return false;
        }
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
        // The camera counts pixels from where its view sits on the desktop; the tools count from the view's corner.
        const auto& view_corner = cam.get_viewport_pos();
        pixel += math::vec2(float(view_corner.x), float(view_corner.y));
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
                       position_to_json(hit.point),
                       vec3_to_json(hit.normal),
                       hit.distance,
                       hit.submesh_index);
}

/// One projected point: Scene view pixel, view depth and whether it is on screen. A point at or behind the eye
/// has no pixel.
auto projected_point_json(const camera& cam, const math::vec3& point) -> std::string
{
    const float depth = cam.world_to_view_depth(point);
    if(depth <= 0.0f)
    {
        return fmt::format(R"({{"screen":null,"depth":{:.6g},"visible":false}})", depth);
    }
    // The camera counts pixels from where its view sits on the desktop; the tools count from the view's corner.
    const auto& view_corner = cam.get_viewport_pos();
    const auto screen = cam.world_to_viewport(point) - math::vec3(float(view_corner.x), float(view_corner.y), 0.0f);
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

namespace
{
/// One decal of a scene_place_decals_batch call, read and checked before any is made.
struct decal_placement
{
    asset_handle<material> material;
    math::vec3 point{};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    std::string hit_entity;
    entt::handle parent;
    std::string name;
    float rotation{};
    hpp::optional<math::vec2> size;
    hpp::optional<float> depth;
};

/// Reads one item: its material (the Default Decal when it names none), where it goes (a ray or a point and normal), its
/// size and parent. False, with the reason, for an item that cannot be placed.
auto read_decal_placement(rtti::context& ctx, scene& scn, const simdjson::dom::object& item, decal_placement& out, std::string& error)
    -> bool
{
    std::string key;
    if(read_string(item, "material", key) && !key.empty())
    {
        out.material = resolve_domain_material(ctx, key, material_domain::decal, error);
        if(!out.material)
        {
            return false;
        }
    }
    simdjson::dom::element point_value;
    if(!item["point"].get(point_value))
    {
        simdjson::dom::element normal_value;
        if(item["normal"].get(normal_value) || !parse_vec3(point_value, out.point, error) || !parse_vec3(normal_value, out.normal, error))
        {
            error = error.empty() ? std::string("Pass normal with point") : error;
            return false;
        }
        if(math::length(out.normal) < k_min_direction_length)
        {
            error = "The normal must not be zero";
            return false;
        }
    }
    else
    {
        scene_ray ray;
        if(!read_ray(ctx, item, ray, error))
        {
            return false;
        }
        ray.max_distance = k_default_reach;
        const auto hits = raycast_scene_models(scn, ray, {});
        if(hits.empty())
        {
            error = "The ray hits no model surface: aim it at one, or pass point and normal";
            return false;
        }
        out.point = hits.front().point;
        out.normal = hits.front().normal;
        out.hit_entity = entity_id_string(hits.front().entity);
    }
    std::string parent_id;
    if(read_string(item, "parent_id", parent_id) && !parent_id.empty())
    {
        std::vector<entt::handle> found;
        if(!resolve_entities(scn, {parent_id}, found, error) || found.empty())
        {
            return false;
        }
        out.parent = found.front();
    }
    out.name = out.material ? out.material.name() : std::string("Decal");
    read_string(item, "name", out.name);
    double rotation = 0.0;
    read_double(item, "rotation", rotation);
    out.rotation = float(rotation);
    simdjson::dom::element size_value;
    if(!item["size"].get(size_value))
    {
        math::vec2 size{};
        if(!parse_vec2(size_value, size, error))
        {
            return false;
        }
        out.size = size;
    }
    double depth = 0.0;
    if(read_double(item, "depth", depth))
    {
        out.depth = float(depth);
    }
    return true;
}

/// The decal of @p placement, made in @p scn and laid onto its surface.
auto make_placed_decal(rtti::context& ctx, scene& scn, const decal_placement& placement) -> entt::handle
{
    auto decal = defaults::create_decal_entity(ctx, scn, placement.name, placement.material);
    auto& comp = decal.get<decal_component>();
    if(placement.size)
    {
        comp.resize(*placement.size);
    }
    if(placement.depth)
    {
        comp.depth = std::max(*placement.depth, 0.001f);
    }
    defaults::place_decal_on_surface(decal, placement.point, placement.normal, placement.rotation);
    if(placement.parent)
    {
        decal.get<transform_component>().set_parent(placement.parent, true);
    }
    return decal;
}
} // namespace

void register_spatial_tools(mcp_tool_registry& registry)
{
    registry.add(
        {.name = "scene_place_decals_batch",
         .description =
             "Place decals on surfaces, as dragging a Decal material onto them in the Scene view does. Each item: a ray "
             "(origin + direction, or screen: a Scene view pixel) that finds the nearest model surface, or point and "
             "normal; material (a Decal material, default the engine's Default Decal); size (width and height in metres, "
             "default 1 high and the material's texture aspect wide), depth (how deep the box reaches, default half the "
             "smaller side), rotation (degrees about the normal), name, parent_id (an entity the decal moves with). "
             "Every item is checked first: one that cannot be placed places none. Returns each decal's entity_id and "
             "where it lies. One undo step.",
         .input_schema_json =
             R"json({"type":"object","properties":{"items":{"type":"array","minItems":1,"items":{"type":"object","properties":{"material":{"type":"string","description":"Key or uid of a Decal material"},"origin":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"direction":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"screen":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Scene view pixel [x,y], top-left origin"},"point":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"Where on a surface, with normal: instead of a ray"},"normal":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"size":{"type":"array","items":{"type":"number","exclusiveMinimum":0},"minItems":2,"maxItems":2},"depth":{"type":"number","exclusiveMinimum":0},"rotation":{"type":"number"},"name":{"type":"string"},"parent_id":{"type":"string","description":"An entity the decal moves with; its world pose stays"}}}}},"required":["items"]})json",
         .handler =
             [](rtti::context& ctx, const simdjson::dom::object& args) -> tool_result
         {
             std::string error;
             scene* scn = nullptr;
             if(!require_edit_scene(ctx, scn, error))
             {
                 return {.text = error, .is_error = true};
             }
             simdjson::dom::array items;
             if(!read_required_array(args, "items", items, error))
             {
                 return {.text = error, .is_error = true};
             }
             std::vector<decal_placement> placements;
             for(auto element : items)
             {
                 simdjson::dom::object item;
                 decal_placement placement;
                 if(!read_object(element, item, error) || !read_decal_placement(ctx, *scn, item, placement, error))
                 {
                     return {.text = make_batch_item_error(placements.size(), error, "Nothing was placed."), .is_error = true};
                 }
                 placements.push_back(std::move(placement));
             }
             std::vector<entt::handle> decals;
             ctx.get_cached<editing_manager>().do_action<create_entities_action_t>(
                 "MCP Place Decals",
                 [&]()
                 {
                     decals.clear();
                     for(const auto& placement : placements)
                     {
                         decals.push_back(make_placed_decal(ctx, *scn, placement));
                     }
                     return decals;
                 });
             std::string created = "[";
             for(size_t i = 0; i < decals.size(); ++i)
             {
                 const auto& comp = decals[i].get<decal_component>();
                 const auto& placement = placements[i];
                 created += fmt::format(R"({}{{"entity_id":{},"name":{},"point":{},"normal":{},"size":[{},{}],"depth":{},"on_entity_id":{}}})",
                                        i == 0 ? "" : ",",
                                        make_json_string(entity_id_string(decals[i])),
                                        make_json_string(placement.name),
                                        position_to_json(placement.point),
                                        vec3_to_json(math::normalize(placement.normal)),
                                        comp.size.x,
                                        comp.size.y,
                                        comp.depth,
                                        placement.hit_entity.empty() ? std::string("null") : make_json_string(placement.hit_entity));
             }
             created += "]";
             return {.text = fmt::format(R"({{"created":{},"count":{}}})", created, decals.size()), .is_error = false};
         },
         .mutates_scene = true});

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
                                         position_to_json(ray.origin),
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
             "shows it at scale 1; null at or behind the eye), view depth (negative: behind the camera) and visible "
             "(in front and inside the view). Pairs with scene_raycast {screen} for the other direction.",
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
             if(cam.get_viewport_size().width == 0 || cam.get_viewport_size().height == 0)
             {
                 return {.text = "The Scene view has no size yet (show it with panel_focus)", .is_error = true};
             }
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
