#include "line_component.h"

#include <engine/rendering/material_graph/graph_material.h>

#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

void line_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    entity.get<line_component>().set_material(default_strip_material());
}

void line_component::build(const math::transform& world,
                           const math::vec3& camera_position,
                           const math::vec3& origin,
                           dynamic_geometry& out,
                           const strip_width_floor& width_floor) const
{
    if(std::min(points.size(), MAX_POINTS) < 2)
    {
        return;
    }
    const math::transform::position_t origin_position(origin);
    if(!keeps_geometry(width_floor))
    {
        build_strip_geometry(world, camera_position, origin_position, out, width_floor);
        return;
    }
    const auto kept_origin = get_kept_origin(world);
    // A strip that faces the camera follows where the camera stands from it.
    const math::vec3 camera_offset = strip.faces_camera() ? camera_position - math::vec3(kept_origin) : math::vec3(0.0f);
    const uint64_t key = get_geometry_key(world, camera_offset);
    if(!has_kept_geometry_ || key != kept_key_)
    {
        kept_geometry_.clear();
        build_strip_geometry(world, camera_position, kept_origin, kept_geometry_, {});
        kept_key_ = key;
        has_kept_geometry_ = true;
    }
    out.append(kept_geometry_, math::vec3(kept_origin - origin_position));
}

auto line_component::keeps_geometry(const strip_width_floor& width_floor) const -> bool
{
    // A width floor is one build's own.
    if(width_floor.is_set())
    {
        return false;
    }
    if(!strip.faces_camera())
    {
        return true;
    }
    // A strip that faces the camera changes whenever the camera moves, but one that casts a shadow is built twice a
    // frame for the same camera: for the shadow maps, then for the view.
    const auto* drawn = get_drawn_graph();
    return casts_shadow && drawn != nullptr && drawn->casts_shadow();
}

auto line_component::get_kept_origin(const math::transform& world) const -> math::transform::position_t
{
    // Relative to a point of its own: the numbers stay small wherever the line is, and an entity that only moves
    // keeps its geometry.
    return world_space ? math::transform::position_t(points.front()) : world.get_position();
}

auto line_component::get_geometry_key(const math::transform& world, const math::vec3& camera_offset) const -> uint64_t
{
    const size_t count = std::min(points.size(), MAX_POINTS);
    // The numbers the geometry follows, in one run of floats: a vector's padding is no part of it.
    thread_local std::vector<float> values;
    values.clear();
    values.reserve(count * 3 + 32);
    // How the entity is turned and scaled: where it is moves the kept geometry as a whole.
    const math::mat4 matrix = world;
    for(int column = 0; column < 3; ++column)
    {
        values.push_back(matrix[column].x);
        values.push_back(matrix[column].y);
        values.push_back(matrix[column].z);
    }
    for(size_t i = 0; i < count; ++i)
    {
        values.push_back(points[i].x);
        values.push_back(points[i].y);
        values.push_back(points[i].z);
    }
    for(const float value : {camera_offset.x,
                             camera_offset.y,
                             camera_offset.z,
                             strip.axis.x,
                             strip.axis.y,
                             strip.axis.z,
                             strip.width,
                             strip.tile_length,
                             float(strip.shape),
                             float(strip.alignment),
                             float(strip.uv_mode),
                             float(strip.subdivisions),
                             float(strip.corner_vertices),
                             float(strip.cap_vertices),
                             float(strip.tube_sides),
                             world_space ? 1.0f : 0.0f,
                             loop ? 1.0f : 0.0f})
    {
        values.push_back(value);
    }
    return XXH3_64bits_withSeed(values.data(), values.size() * sizeof(float), sampler_.get(strip).key);
}

void line_component::build_strip_geometry(const math::transform& world,
                                          const math::vec3& camera_position,
                                          const math::transform::position_t& origin,
                                          dynamic_geometry& out,
                                          const strip_width_floor& width_floor) const
{
    const size_t count = std::min(points.size(), MAX_POINTS);
    // A loop's curves end back on its first point, one step past its last.
    const float steps = float(loop && count > 2 ? count : count - 1);
    // Keeps its memory between builds.
    thread_local std::vector<strip_point> placed;
    placed.clear();
    placed.reserve(count);
    for(size_t i = 0; i < count; ++i)
    {
        strip_point point;
        const math::transform::position_t at = world_space ? math::transform::position_t(points[i])
                                                           : world.transform_coord(math::transform::position_t(points[i]));
        point.position = math::vec3(at - origin);
        point.along = float(i) / steps;
        placed.push_back(point);
    }
    auto options = strip.make_options(sampler_.get(strip), camera_position - math::vec3(origin), world.get_rotation());
    options.is_loop = loop;
    options.width_floor = width_floor;
    build_strip(placed, options, out);
}

auto line_component::get_world_bounds(const math::transform& world) const -> math::bbox
{
    math::bbox bounds;
    const size_t count = std::min(points.size(), MAX_POINTS);
    for(size_t i = 0; i < count; ++i)
    {
        // A point that is no number is no part of the line.
        const auto& point = points[i];
        if(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
        {
            bounds.add_point(world_space ? point : math::vec3(world.transform_coord(math::transform::position_t(point))));
        }
    }
    if(!bounds.is_populated())
    {
        return bounds;
    }
    const math::vec3 margin(strip.get_width_reach(sampler_.get(strip)) + strip.get_curve_reach(bounds));
    return math::bbox(bounds.min - margin, bounds.max + margin);
}

} // namespace unravel
