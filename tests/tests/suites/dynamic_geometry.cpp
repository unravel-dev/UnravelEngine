/*
 * Validation suite for CPU-built geometry: the packed vertices lines and trails draw with, and the strip builder.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "dynamic geometry"
 *
 * Pins the packing: a vertex is written byte for byte as bgfx packs it into the mesh layout, so every program that
 * draws a mesh reads it. Pins the winding: every triangle of a band, a rounded band, a tube and their caps
 * faces the side its vertex normals point to. Pins the strip's rules: the first texture coordinate runs across it and
 * the second along it by the UV mode, a band turns toward the camera or along its normal, repeated points count once,
 * a loop closes onto its first point, subdivisions run a smooth curve through the points, corner vertices turn a
 * sharp corner as an arc that fans its outer edge about one inner vertex (at a loop's joint too) and leaves a turn out
 * of the band's plane alone, the cross-section budget holds, the options' curves are read at every cross-section (a
 * loop's last one at their end), a point that starts a run ends the strip before it, a tiled texture follows the
 * distances its points carry and the bitangent the way it grows. Pins the components: a Line places its points by its
 * entity or in the world, relative to the origin it is built for; a Trail takes a point as its entity moves, ends where
 * its time runs out and sweeps a sheet in Edge mode; and each saves and loads what its Inspector shows.
 */

#include "../tests.h"

#include <engine/meta/ecs/components/line_component.hpp>
#include <engine/meta/ecs/components/trail_component.hpp>
#include <engine/rendering/dynamic_geometry/dynamic_geometry.h>
#include <engine/rendering/dynamic_geometry/strip_builder.h>
#include <serialization/associative_archive.h>
#include <graphics/vertex_decl.h>

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel::test;

using namespace unravel;

namespace
{
constexpr float GEOMETRY_TOLERANCE = 1.0e-4f;

/// What bgfx unpacks of attribute @p attribute of vertex @p index.
auto unpack_attribute(const dynamic_geometry& geometry, bgfx::Attrib::Enum attribute, uint32_t index) -> math::vec4
{
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::vertexUnpack(values, attribute, gfx::mesh_vertex::get_layout(), geometry.get_vertex_data(), index);
    return math::vec4(values[0], values[1], values[2], values[3]);
}

auto get_normal(const dynamic_geometry& geometry, uint32_t index) -> math::vec3
{
    return math::vec3(unpack_attribute(geometry, bgfx::Attrib::Normal, index));
}

auto get_bitangent(const dynamic_geometry& geometry, uint32_t index) -> math::vec3
{
    return math::vec3(unpack_attribute(geometry, bgfx::Attrib::Bitangent, index));
}

auto get_uv(const dynamic_geometry& geometry, uint32_t index) -> math::vec2
{
    return math::vec2(unpack_attribute(geometry, bgfx::Attrib::TexCoord0, index));
}

auto get_color(const dynamic_geometry& geometry, uint32_t index) -> math::vec4
{
    return unpack_attribute(geometry, bgfx::Attrib::Color0, index);
}

/// How many triangles of @p geometry face away from the mean of their corners' normals, or have no area.
auto count_misfacing_triangles(const dynamic_geometry& geometry) -> size_t
{
    size_t misfacing = 0;
    const auto& indices = geometry.get_indices();
    for(size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const math::vec3 a = geometry.get_position(indices[i]);
        const math::vec3 b = geometry.get_position(indices[i + 1]);
        const math::vec3 c = geometry.get_position(indices[i + 2]);
        // A front face turns clockwise: its corners' cross product points at the viewer.
        const math::vec3 face = math::cross(b - a, c - a);
        const math::vec3 normal = get_normal(geometry, indices[i]) + get_normal(geometry, indices[i + 1]) + get_normal(geometry, indices[i + 2]);
        if(math::dot(face, face) < 1.0e-14f || math::dot(face, normal) <= 0.0f)
        {
            ++misfacing;
        }
    }
    return misfacing;
}

auto make_points(const std::vector<math::vec3>& positions, float width = 1.0f) -> std::vector<strip_point>
{
    std::vector<strip_point> points;
    for(const auto& position : positions)
    {
        strip_point point;
        point.position = position;
        point.width = width;
        points.push_back(point);
    }
    return points;
}

auto build(const std::vector<strip_point>& points, const strip_options& options) -> dynamic_geometry
{
    dynamic_geometry geometry;
    build_strip(points, options, geometry);
    return geometry;
}

/// Options of a band that faces up, so its vertices are easy to place.
auto make_flat_options() -> strip_options
{
    strip_options options;
    options.facing = strip_facing::normal;
    options.normal = math::vec3(0.0f, 1.0f, 0.0f);
    return options;
}

void test_vertex_packing()
{
    std::printf("test_vertex_packing\n");
    const auto& layout = gfx::mesh_vertex::get_layout();
    check(dynamic_geometry::get_vertex_stride() == layout.getStride(), "a packed vertex is as large as the mesh layout's");
    dynamic_vertex vertex;
    vertex.position = math::vec3(1.5f, -2.25f, 3.0f);
    vertex.normal = math::normalize(math::vec3(0.3f, 0.8f, -0.5f));
    vertex.tangent = math::normalize(math::vec3(1.0f, 0.1f, 0.2f));
    vertex.bitangent = math::normalize(math::vec3(-0.2f, 0.4f, 0.9f));
    vertex.uv = math::vec2(0.25f, 3.5f);
    // Channels a byte holds exactly: the packing rounds where bgfx cuts.
    vertex.color = math::vec4(0.2f, 0.4f, 1.0f, 0.8f);
    dynamic_geometry geometry;
    const uint32_t index = geometry.add_vertex(vertex);
    check(index == 0 && geometry.get_vertex_count() == 1 && geometry.get_vertex_data_size() == layout.getStride(),
          "a vertex added is one packed vertex, and its index the count before it");

    // The same vertex as bgfx packs it.
    std::vector<uint8_t> expected(layout.getStride(), uint8_t(0));
    const float position[4] = {vertex.position.x, vertex.position.y, vertex.position.z, 0.0f};
    const float normal[4] = {vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f};
    const float tangent[4] = {vertex.tangent.x, vertex.tangent.y, vertex.tangent.z, 0.0f};
    const float bitangent[4] = {vertex.bitangent.x, vertex.bitangent.y, vertex.bitangent.z, 0.0f};
    const float uv[4] = {vertex.uv.x, vertex.uv.y, 0.0f, 0.0f};
    const float color[4] = {vertex.color.x, vertex.color.y, vertex.color.z, vertex.color.w};
    bgfx::vertexPack(position, false, bgfx::Attrib::Position, layout, expected.data());
    bgfx::vertexPack(normal, true, bgfx::Attrib::Normal, layout, expected.data());
    bgfx::vertexPack(tangent, true, bgfx::Attrib::Tangent, layout, expected.data());
    bgfx::vertexPack(bitangent, true, bgfx::Attrib::Bitangent, layout, expected.data());
    bgfx::vertexPack(uv, false, bgfx::Attrib::TexCoord0, layout, expected.data());
    bgfx::vertexPack(color, true, bgfx::Attrib::Color0, layout, expected.data());
    bgfx::vertexPack(uv, false, bgfx::Attrib::TexCoord1, layout, expected.data());
    check(std::memcmp(expected.data(), geometry.get_vertex_data(), expected.size()) == 0,
          "a vertex is packed byte for byte as bgfx packs it into the mesh layout");
    check(math::distance(geometry.get_position(0), vertex.position) < GEOMETRY_TOLERANCE &&
              math::distance(geometry.get_bounds().min, vertex.position) < GEOMETRY_TOLERANCE,
          "its position reads back, and the bounds hold it");

    geometry.clear();
    check(geometry.is_empty() && geometry.get_vertex_count() == 0 && !geometry.get_bounds().is_populated(), "clear empties the geometry and its bounds");
}

/// A triangle in the plane z = @p z whose front, and whose vertex normals, look down -z.
auto make_triangle(float z) -> dynamic_geometry
{
    dynamic_geometry geometry;
    dynamic_vertex vertex;
    vertex.normal = math::vec3(0.0f, 0.0f, -1.0f);
    vertex.position = math::vec3(0.0f, 1.0f, z);
    const uint32_t top = geometry.add_vertex(vertex);
    vertex.position = math::vec3(1.0f, 0.0f, z);
    const uint32_t right = geometry.add_vertex(vertex);
    vertex.position = math::vec3(0.0f, 0.0f, z);
    const uint32_t corner = geometry.add_vertex(vertex);
    geometry.add_triangle(top, right, corner);
    return geometry;
}

void test_appending()
{
    std::printf("test_appending\n");
    dynamic_geometry both;
    both.append(make_triangle(0.0f), math::vec3(0.0f));
    both.append(make_triangle(2.0f), math::vec3(10.0f, 0.0f, 0.0f));
    check(both.get_vertex_count() == 6 && both.get_index_count() == 6 && both.get_indices()[3] == 3 && count_misfacing_triangles(both) == 0,
          "appended geometry keeps its triangles, on the vertices after the ones there");
    check(math::distance(both.get_position(3), math::vec3(10.0f, 1.0f, 2.0f)) < GEOMETRY_TOLERANCE &&
              std::abs(both.get_bounds().max.x - 11.0f) < GEOMETRY_TOLERANCE && std::abs(both.get_bounds().min.x) < GEOMETRY_TOLERANCE,
          "its vertices move by the offset it is appended with, and the bounds hold both");
    both.append(dynamic_geometry(), math::vec3(100.0f));
    check(both.get_vertex_count() == 6 && both.get_bounds().max.x < 12.0f, "empty geometry adds nothing");
}

void test_band()
{
    std::printf("test_band\n");
    const auto points = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 6.0f}}, 2.0f);
    auto options = make_flat_options();
    auto geometry = build(points, options);
    check(geometry.get_vertex_count() == 6 && geometry.get_index_count() == 12 && count_misfacing_triangles(geometry) == 0,
          "a flat band has two vertices a point and two triangles a segment, facing its normal");
    check(math::distance(get_normal(geometry, 0), math::vec3(0.0f, 1.0f, 0.0f)) < 0.02f, "a band with a normal faces along it");
    check(std::abs(geometry.get_position(0).x) > 0.99f && std::abs(geometry.get_position(0).x + geometry.get_position(1).x) < GEOMETRY_TOLERANCE,
          "its vertices stand half the width either side of the point");
    check(std::abs(get_uv(geometry, 0).x) < GEOMETRY_TOLERANCE && std::abs(get_uv(geometry, 1).x - 1.0f) < GEOMETRY_TOLERANCE,
          "the first texture coordinate runs across the band");
    check(std::abs(get_uv(geometry, 0).y) < GEOMETRY_TOLERANCE && std::abs(get_uv(geometry, 2).y - 1.0f / 3.0f) < GEOMETRY_TOLERANCE &&
              std::abs(get_uv(geometry, 4).y - 1.0f) < GEOMETRY_TOLERANCE,
          "Stretch runs the second from 0 to 1 by the distance along the band");
    options.uv_mode = strip_uv_mode::tile;
    options.tile_length = 2.0f;
    geometry = build(points, options);
    check(std::abs(get_uv(geometry, 2).y - 1.0f) < GEOMETRY_TOLERANCE && std::abs(get_uv(geometry, 4).y - 3.0f) < GEOMETRY_TOLERANCE,
          "Tile repeats it every tile length");
    options.uv_mode = strip_uv_mode::per_segment;
    geometry = build(points, options);
    check(std::abs(get_uv(geometry, 2).y - 1.0f) < GEOMETRY_TOLERANCE && std::abs(get_uv(geometry, 4).y - 2.0f) < GEOMETRY_TOLERANCE,
          "Per Segment repeats it between each pair of points");

    strip_options facing;
    facing.camera_position = math::vec3(0.0f, 0.0f, -10.0f);
    const auto across = make_points({{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});
    geometry = build(across, facing);
    check(math::distance(get_normal(geometry, 0), math::vec3(0.0f, 0.0f, -1.0f)) < 0.02f && count_misfacing_triangles(geometry) == 0,
          "a band that faces the camera turns its face to it");

    options = make_flat_options();
    options.shape = strip_shape::rounded;
    geometry = build(points, options);
    check(geometry.get_vertex_count() == 9 && geometry.get_index_count() == 24 && count_misfacing_triangles(geometry) == 0,
          "a rounded band has three vertices a point");
    check(get_normal(geometry, 0).x * geometry.get_position(0).x > 0.4f && std::abs(get_normal(geometry, 1).x) < 0.02f &&
              get_normal(geometry, 2).x * geometry.get_position(2).x > 0.4f,
          "whose normals lean out at the edges, so it shades as a cylinder");
    options.cap_vertices = 3;
    const auto capped = build(points, options);
    check(capped.get_vertex_count() > geometry.get_vertex_count() && count_misfacing_triangles(capped) == 0 &&
              capped.get_bounds().min.z < -0.9f && capped.get_bounds().max.z > 6.9f,
          "cap vertices close each end with a half disc that faces as the band does");
}

void test_paths()
{
    std::printf("test_paths\n");
    auto options = make_flat_options();
    check(build(make_points({{1.0f, 0.0f, 0.0f}}), options).is_empty() &&
              build(make_points({{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}), options).is_empty(),
          "fewer than two distinct points make no strip");
    const auto repeated = build(make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}), options);
    check(repeated.get_vertex_count() == 4 && count_misfacing_triangles(repeated) == 0, "a point that repeats the one before it counts once");

    const auto square = make_points({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}});
    const auto open = build(square, options);
    options.is_loop = true;
    const auto loop = build(square, options);
    check(open.get_vertex_count() == 8 && loop.get_vertex_count() == 10 && count_misfacing_triangles(loop) == 0,
          "a loop adds a cross-section back on its first point");
    check(math::distance(loop.get_position(0), loop.get_position(8)) < GEOMETRY_TOLERANCE && std::abs(get_uv(loop, 8).y - 1.0f) < GEOMETRY_TOLERANCE,
          "which stands where the first does, at the end of the texture");
    options.is_loop = false;

    // A sharp corner turns as an arc of cross-sections around its point.
    const auto corner = make_points({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}});
    const auto mitred = build(corner, options);
    options.corner_vertices = 3;
    const auto arced = build(corner, options);
    check(mitred.get_vertex_count() == 6 && arced.get_vertex_count() == 6 + 4 * 2, "corner vertices add cross-sections around a sharp corner");
    float farthest = 0.0f;
    for(uint32_t i = 0; i < arced.get_vertex_count(); ++i)
    {
        farthest = std::max(farthest, math::distance(arced.get_position(i), math::vec3(4.0f, 0.0f, 0.0f)));
    }
    check(farthest > 3.9f && arced.get_bounds().max.x < 4.51f && arced.get_bounds().min.z > -0.51f,
          "which stay half a width from the corner: its outside is an arc, not a point");
    // The band turns toward +Z: its inner edge is the one on that side, and keeps one place through the arc, where
    // the inner edges before and after the corner meet.
    const math::vec3 inside(3.5f, 0.0f, 0.5f);
    size_t at_inside = 0;
    for(uint32_t i = 2; i < 12; ++i)
    {
        at_inside += math::distance(arced.get_position(i), inside) < GEOMETRY_TOLERANCE ? 1 : 0;
    }
    check(at_inside == 5 && count_misfacing_triangles(arced) == 0 && arced.get_index_count() == (2 * 2 + 4) * 3,
          "the arc fans the outer edge about one inner vertex, so the band never folds over itself");
    options.shape = strip_shape::rounded;
    const auto rounded_arc = build(corner, options);
    check(count_misfacing_triangles(rounded_arc) == 0 && rounded_arc.get_index_count() == (2 * 4 + 4) * 3,
          "as a rounded band's does about its middle");
    options.shape = strip_shape::flat;

    // A turn out of the band's plane has no inner and no outer edge: a ramp takes no arc and keeps its width.
    const auto ramp = make_points({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {5.2f, 3.8f, 0.0f}});
    const auto ramped = build(ramp, options);
    check(ramped.get_vertex_count() == 6 && std::abs(ramped.get_bounds().max.z - 0.5f) < GEOMETRY_TOLERANCE &&
              std::abs(ramped.get_bounds().min.z + 0.5f) < GEOMETRY_TOLERANCE,
          "a corner that turns out of the band's plane takes no arc, and its edges stay half a width out");
    // A band that faces the camera turns in its plane as the camera sees it: a turn in depth is no corner either.
    auto facing = options;
    facing.facing = strip_facing::camera;
    facing.camera_position = math::vec3(2.0f, 0.0f, -50.0f);
    const auto into_depth = make_points({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 9.0f}});
    check(build(into_depth, facing).get_vertex_count() == 6, "nor does a turn toward or away from the camera a band faces");

    // A loop's joint is a corner like the others: its arc is split over the strip's two ends.
    options.is_loop = true;
    options.corner_vertices = 2;
    const auto loop_arcs = build(square, options);
    const uint32_t loop_last = loop_arcs.get_vertex_count() - 2;
    check(loop_arcs.get_vertex_count() == (3 * 4 + 3 + 3) * 2 && count_misfacing_triangles(loop_arcs) == 0,
          "a loop rounds the corner at its joint too");
    check(math::distance(loop_arcs.get_position(0), loop_arcs.get_position(loop_last)) < GEOMETRY_TOLERANCE &&
              math::distance(loop_arcs.get_position(1), loop_arcs.get_position(loop_last + 1)) < GEOMETRY_TOLERANCE,
          "and still ends on the cross-section it starts with");
    options.is_loop = false;

    // The arcs give way before the budget does.
    std::vector<math::vec3> zigzag;
    for(int i = 0; i < 20; ++i)
    {
        zigzag.emplace_back(float(i), 0.0f, (i % 2) == 0 ? 0.0f : 3.0f);
    }
    options.corner_vertices = 16;
    options.max_cross_sections = 40;
    check(build(make_points(zigzag, 0.2f), options).get_vertex_count() <= 40 * 2, "the cross-section budget holds the corners back");
    options.max_cross_sections = 4096;
    options.corner_vertices = 0;

    // Subdivisions run a smooth curve through the points.
    options.subdivisions = 3;
    const auto smooth = build(corner, options);
    check(smooth.get_vertex_count() == (2 * 4 + 1) * 2 && count_misfacing_triangles(smooth) == 0, "subdivisions add points between each pair");
    bool passes_corner = false;
    for(uint32_t i = 0; i + 1 < smooth.get_vertex_count(); i += 2)
    {
        const math::vec3 middle = (smooth.get_position(i) + smooth.get_position(i + 1)) * 0.5f;
        passes_corner = passes_corner || math::distance(middle, math::vec3(4.0f, 0.0f, 0.0f)) < GEOMETRY_TOLERANCE;
    }
    check(passes_corner, "and the curve still passes through every point");
    options.max_cross_sections = 5;
    const auto budgeted = build(corner, options);
    check(budgeted.get_vertex_count() <= 5 * 2 && budgeted.get_vertex_count() >= 3 * 2, "the cross-section budget holds the subdivisions back");

    // An open strip is built with every point its budget holds; a loop keeps one cross-section to close with.
    options = make_flat_options();
    options.max_cross_sections = 4;
    const auto five = make_points({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.5f}, {3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.5f}});
    const auto held = build(five, options);
    check(held.get_vertex_count() == 4 * 2 && held.get_bounds().max.x > 2.9f, "an open strip is built with as many points as its budget holds");
    options.is_loop = true;
    check(build(five, options).get_vertex_count() == 4 * 2, "and a loop keeps one cross-section of it to close with");
}

void test_curves_and_runs()
{
    std::printf("test_curves_and_runs\n");
    // A width that rises to the middle of the strip and falls again, and a colour that fades out along it.
    strip_samples curves;
    for(size_t i = 0; i < strip_samples::COUNT; ++i)
    {
        const float along = float(i) / float(strip_samples::COUNT - 1);
        curves.width_scales[i] = 1.0f - std::abs(along * 2.0f - 1.0f);
        curves.colors[i] = math::vec4(1.0f, 1.0f, 1.0f, 1.0f - along);
    }
    auto options = make_flat_options();
    options.curves = &curves;
    options.width = 2.0f;
    options.subdivisions = 3;
    auto ends = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 8.0f}});
    ends[1].along = 1.0f;
    const auto shaped = build(ends, options);
    check(shaped.get_vertex_count() == 5 * 2 && std::abs(shaped.get_position(0).x) < GEOMETRY_TOLERANCE &&
              std::abs(std::abs(shaped.get_position(4).x) - 1.0f) < 0.02f && std::abs(shaped.get_position(8).x) < GEOMETRY_TOLERANCE,
          "the width curve is read at every cross-section, the ones between two points too");
    check(get_color(shaped, 0).w > 0.99f && std::abs(get_color(shaped, 4).w - 0.5f) < 0.02f && get_color(shaped, 8).w < 0.01f,
          "as the colour curve is");
    curves.interpolates_width = false;
    const float near_middle = 0.5f - 0.2f / float(strip_samples::COUNT);
    check(std::abs(curves.get_width_scale(near_middle) - curves.width_scales[32]) < GEOMETRY_TOLERANCE,
          "samples of a curve with a key that holds take the nearer value");
    check(std::abs(curves.get_color(near_middle).w - curves.colors[32].w) > 1.0e-3f, "while the other curve still mixes its own");
    curves.interpolates_width = true;

    // A loop's closing cross-section stands on its first point and reads the curves at their end.
    auto ring = make_points({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}});
    for(size_t i = 0; i < ring.size(); ++i)
    {
        ring[i].along = float(i) / float(ring.size());
    }
    options.is_loop = true;
    const auto closed = build(ring, options);
    check(get_color(closed, 0).w > 0.99f && get_color(closed, closed.get_vertex_count() - 2).w < 0.01f,
          "a loop's last cross-section reads the curves at their end, on its first point");
    options.is_loop = false;
    options.curves = nullptr;
    options.subdivisions = 0;

    // A point that starts a run ends the strip before it.
    auto runs = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 1.0f}});
    const auto joined = build(runs, options);
    runs[2].starts_run = true;
    const auto parted = build(runs, options);
    check(joined.get_index_count() == 3 * 6 && parted.get_index_count() == 2 * 6 && parted.get_vertex_count() == 8 &&
              count_misfacing_triangles(parted) == 0,
          "no strip joins a point that starts a run to the one before it");
    check(std::abs(get_uv(parted, 2).y - 1.0f) < GEOMETRY_TOLERANCE && std::abs(get_uv(parted, 4).y) < GEOMETRY_TOLERANCE,
          "and each run stretches the texture over itself");
    options.is_loop = true;
    check(build(runs, options).get_index_count() == 2 * 6, "a strip of more than one run is no loop");
    options.is_loop = false;

    // A width floor widens what is too thin: by a width, and by a width per metre from the camera.
    auto thin = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}}, 0.01f);
    options.camera_position = math::vec3(0.0f, 0.0f, 0.0f);
    options.width_floor.constant = 0.5f;
    check(std::abs(std::abs(build(thin, options).get_position(0).x) - 0.25f) < GEOMETRY_TOLERANCE, "a width floor widens a thin strip");
    options.width_floor = {0.0f, 0.1f};
    const auto widened = build(thin, options);
    check(std::abs(widened.get_position(0).x) < 0.006f && std::abs(std::abs(widened.get_position(2).x) - 0.5f) < GEOMETRY_TOLERANCE,
          "and one per metre widens it by its distance from the camera");
    options.width_floor = {};

    // A tiled texture follows the distances the points carry, whichever end the strip grows at.
    auto travelled = make_points({{0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}});
    travelled[0].distance = 7.0f;
    travelled[1].distance = 5.0f;
    travelled[2].distance = 4.0f;
    options.uv_mode = strip_uv_mode::tile;
    options.tile_length = 2.0f;
    options.has_point_distances = true;
    const auto anchored = build(travelled, options);
    check(std::abs(get_uv(anchored, 0).y - 3.5f) < GEOMETRY_TOLERANCE && std::abs(get_uv(anchored, 4).y - 2.0f) < GEOMETRY_TOLERANCE,
          "Tile reads the distance a point carries");
    // The strip runs down -Z and its distances fall along it: the tiled coordinate grows toward +Z.
    check(get_bitangent(anchored, 0).z > 0.9f, "the bitangent runs the way the tiled coordinate grows, against a strip whose distances fall");
    options.uv_mode = strip_uv_mode::stretch;
    const auto stretched = build(travelled, options);
    check(get_bitangent(stretched, 0).z < -0.9f, "and along a strip whose coordinate grows with it");
    check(std::abs(get_uv(stretched, 0).y) < GEOMETRY_TOLERANCE && std::abs(get_uv(stretched, 2).y - 2.0f / 3.0f) < GEOMETRY_TOLERANCE &&
              std::abs(get_uv(stretched, 4).y - 1.0f) < GEOMETRY_TOLERANCE,
          "and Stretch still runs from 0 at the first point to 1 at the last");

    // Points that are no numbers are left out.
    auto broken = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 2.0f}});
    broken[1].position.x = std::numeric_limits<float>::quiet_NaN();
    options = make_flat_options();
    const auto skipped = build(broken, options);
    check(skipped.get_vertex_count() == 4 && skipped.get_bounds().is_populated() && std::isfinite(skipped.get_bounds().max.z),
          "a point that is no number is left out of the strip");
    broken[1].position.x = std::numeric_limits<float>::infinity();
    const auto unbounded = build(broken, options);
    check(unbounded.get_vertex_count() == 4 && std::isfinite(unbounded.get_bounds().max.x) && std::isfinite(get_uv(unbounded, 2).y),
          "as is one that is infinitely far");
    auto no_width = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, std::numeric_limits<float>::quiet_NaN());
    const auto flat_line = build(no_width, options);
    dynamic_geometry joined_to;
    joined_to.append(build(broken, options), math::vec3(0.0f));
    joined_to.append(flat_line, math::vec3(0.0f));
    check(std::isfinite(flat_line.get_position(0).x) && std::isfinite(joined_to.get_bounds().max.x) && std::isfinite(joined_to.get_bounds().min.x),
          "a width that is no number draws a strip of no width, and its vertices are numbers");

    // A sheet does not fold where its edge stood still.
    const std::vector<sheet_point> swing = {{math::vec3(2.0f, 0.0f, 0.0f), math::vec3(2.0f, 2.0f, 0.0f)},
                                            {math::vec3(2.0f, 0.0f, 0.0f), math::vec3(2.0f, 2.0f, 0.0f)},
                                            {math::vec3(1.0f, 0.0f, 0.0f), math::vec3(1.0f, 2.0f, 0.0f)},
                                            {math::vec3(0.0f, 0.0f, 0.0f), math::vec3(0.0f, 2.0f, 0.0f)}};
    strip_options sheet_options;
    sheet_options.subdivisions = 4;
    dynamic_geometry sheet;
    build_sheet(swing, sheet_options, sheet);
    check(sheet.get_vertex_count() == (2 * 5 + 1) * 2 && count_misfacing_triangles(sheet) == 0 && sheet.get_bounds().max.x < 2.0f + GEOMETRY_TOLERANCE,
          "a sheet counts an edge that repeats the one before it once, and does not reach past it");
}

void test_tube()
{
    std::printf("test_tube\n");
    strip_options options;
    options.shape = strip_shape::tube;
    options.tube_sides = 6;
    const auto points = make_points({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}, {2.0f, 1.0f, 4.0f}}, 0.5f);
    const auto tube = build(points, options);
    check(tube.get_vertex_count() == 3 * 7 && tube.get_index_count() == 2 * 6 * 6 && count_misfacing_triangles(tube) == 0,
          "a tube has a ring of sides + 1 vertices a point, and its triangles face out");
    bool is_round = true;
    for(uint32_t i = 0; i < 7; ++i)
    {
        is_round = is_round && std::abs(math::distance(tube.get_position(i), math::vec3(0.0f)) - 0.25f) < GEOMETRY_TOLERANCE;
    }
    check(is_round && math::distance(tube.get_position(0), tube.get_position(6)) < GEOMETRY_TOLERANCE,
          "the ring stands half a width from the point, and closes on its first vertex");
    check(std::abs(get_uv(tube, 0).x) < GEOMETRY_TOLERANCE && std::abs(get_uv(tube, 6).x - 1.0f) < GEOMETRY_TOLERANCE,
          "the first texture coordinate runs once around it");
    options.cap_vertices = 1;
    const auto capped = build(points, options);
    check(capped.get_vertex_count() == tube.get_vertex_count() + 2 * 8 && count_misfacing_triangles(capped) == 0,
          "cap vertices close each end with a disc that faces out of the tube");
    options.tube_sides = 100;
    check(build(points, options).get_vertex_count() <= 3 * 33 + 2 * 34, "a tube has at most 32 sides");

    // A loop that leaves its plane twists on its way round: the twist is spread, so its last ring meets its first.
    options.tube_sides = 6;
    options.cap_vertices = 0;
    options.is_loop = true;
    const auto knot = build(make_points({{0.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, {3.0f, -1.0f, 3.0f}, {0.0f, 2.0f, 3.0f}}, 0.5f), options);
    const uint32_t last_ring = knot.get_vertex_count() - 7;
    check(count_misfacing_triangles(knot) == 0 && math::distance(knot.get_position(0), knot.get_position(last_ring)) < 1.0e-3f &&
              math::distance(knot.get_position(2), knot.get_position(last_ring + 2)) < 1.0e-3f,
          "a tube loop's last ring stands on its first, vertex for vertex");
}

/// Saves @p value and loads it into a value made anew.
template<typename T>
auto save_and_load(const T& value) -> T
{
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("value", value));
    }
    T loaded;
    ser20::iarchive_associative_t ar(stream);
    try_load(ar, ser20::make_nvp("value", loaded));
    return loaded;
}

void test_line_component()
{
    std::printf("test_line_component\n");
    line_component line;
    line.strip.alignment = strip_alignment::world_axis;
    line.strip.width = 0.2f;
    math::transform world;
    world.set_position(math::transform::position_t(10.0f, 0.0f, 0.0f));
    const math::vec3 origin(9.0f, 0.0f, 0.0f);
    dynamic_geometry geometry;
    line.build(world, origin, origin, geometry);
    check(geometry.get_vertex_count() == 4 && count_misfacing_triangles(geometry) == 0, "a line of two points is one quad");
    check(std::abs(geometry.get_bounds().min.x - 0.5f) < GEOMETRY_TOLERANCE && std::abs(geometry.get_bounds().max.x - 1.5f) < GEOMETRY_TOLERANCE,
          "its points move with the entity, and its vertices are placed relative to the origin it is built for");
    const auto bounds = line.get_world_bounds(world);
    check(std::abs(bounds.min.x - 9.4f) < GEOMETRY_TOLERANCE && std::abs(bounds.max.x - 10.6f) < GEOMETRY_TOLERANCE && bounds.max.y > 0.09f,
          "its bounds hold the points, grown by half its width");
    line.world_space = true;
    geometry.clear();
    line.build(world, origin, origin, geometry);
    check(std::abs(geometry.get_bounds().min.x + 9.5f) < GEOMETRY_TOLERANCE, "World Space leaves the points where they are");
    // A line that does not face the camera keeps its geometry: a build of the same line copies it, placed for the
    // origin asked, and a change of anything it follows builds it again.
    dynamic_geometry again;
    line.build(world, origin, origin, again);
    check(again.get_vertex_data_size() == geometry.get_vertex_data_size() &&
              std::memcmp(again.get_vertex_data(), geometry.get_vertex_data(), geometry.get_vertex_data_size()) == 0,
          "a second build of the same line gives the same vertices");
    again.clear();
    line.build(world, origin, math::vec3(0.0f), again);
    check(std::abs(again.get_bounds().min.x + 0.5f) < GEOMETRY_TOLERANCE && std::abs(again.get_bounds().max.x - 0.5f) < GEOMETRY_TOLERANCE,
          "for another origin the kept geometry is placed relative to that one");
    line.points[1].x = 2.0f;
    again.clear();
    line.build(world, origin, origin, again);
    check(std::abs(again.get_bounds().max.x + 7.0f) < GEOMETRY_TOLERANCE, "a moved point builds it again");
    line.points[1].x = 0.5f;
    line.strip.width = 1.0f;
    again.clear();
    line.build(world, origin, origin, again);
    check(std::abs(again.get_bounds().max.y - 0.0f) < GEOMETRY_TOLERANCE && std::abs(again.get_bounds().max.z - 0.5f) < GEOMETRY_TOLERANCE,
          "as does a change of its strip");
    line.strip.color_over_length.clear();
    line.strip.color_over_length.add_point(math::color(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    line.strip.color_over_length.add_point(math::color(0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
    again.clear();
    line.build(world, origin, origin, again);
    check(get_color(again, 0).x < 0.01f, "and of a curve of its strip");
    line.strip.width = 0.2f;

    line.points = {math::vec3(0.0f)};
    geometry.clear();
    line.build(world, origin, origin, geometry);
    check(geometry.is_empty(), "one point draws nothing");

    // The kept geometry follows how the entity is turned, and the bounds a point that is a number.
    line_component turned;
    turned.strip.alignment = strip_alignment::local_axis;
    turned.strip.width = 0.2f;
    math::transform placed;
    dynamic_geometry level;
    turned.build(placed, origin, math::vec3(0.0f), level);
    placed.set_rotation(math::quat(math::radians(math::vec3(0.0f, 0.0f, 90.0f))));
    dynamic_geometry upright;
    turned.build(placed, origin, math::vec3(0.0f), upright);
    check(level.get_bounds().max.x > 0.49f && upright.get_bounds().max.y > 0.49f && upright.get_bounds().max.x < 0.2f,
          "a turn of the entity builds the kept geometry again");
    turned.points.emplace_back(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    const auto finite_bounds = turned.get_world_bounds(placed);
    check(finite_bounds.is_populated() && std::isfinite(finite_bounds.max.x) && std::isfinite(finite_bounds.min.y),
          "a point that is no number is no part of a line's bounds");
    turned.strip.corner_vertices = 4;
    check(turned.get_world_bounds(placed).max.y > finite_bounds.max.y + 0.05f, "and Corner Vertices grow them by where an arc's inner vertex can stand");

    // The tables of a strip's curves follow the curves.
    strip_sampler sampler;
    strip_style style;
    const uint64_t plain_key = sampler.get(style).key;
    style.width_over_length.clear();
    style.width_over_length.add_point(1.0f, 0.0f);
    style.width_over_length.add_point(0.0f, 1.0f);
    const auto& tapered = sampler.get(style);
    check(tapered.key != plain_key && std::abs(tapered.widest_scale - 1.0f) < GEOMETRY_TOLERANCE &&
              std::abs(tapered.width_scales[32] - 0.5f) < 0.02f && tapered.interpolates_width && tapered.interpolates_color,
          "a strip's curves are read into tables that change with them");

    line_component saved;
    saved.points = {math::vec3(1.0f, 2.0f, 3.0f), math::vec3(4.0f, 5.0f, 6.0f), math::vec3(7.0f, 8.0f, 9.0f)};
    saved.world_space = true;
    saved.loop = true;
    saved.sort_priority = 4;
    saved.strip.shape = strip_shape::tube;
    saved.strip.width = 0.75f;
    saved.strip.uv_mode = strip_uv_mode::tile;
    saved.strip.tube_sides = 12;
    const auto loaded = save_and_load(saved);
    check(loaded.points.size() == 3 && math::distance(loaded.points[2], saved.points[2]) < GEOMETRY_TOLERANCE && loaded.world_space &&
              loaded.loop && loaded.sort_priority == 4 && loaded.strip.shape == strip_shape::tube && loaded.strip.width == 0.75f &&
              loaded.strip.uv_mode == strip_uv_mode::tile && loaded.strip.tube_sides == 12,
          "a line saves and loads its points, its flags, its sort priority and its strip");
}

void test_trail_component()
{
    std::printf("test_trail_component\n");
    trail_component trail;
    trail.time = 1.0f;
    trail.min_distance = 0.5f;
    trail.strip.alignment = strip_alignment::world_axis;
    math::transform world;
    const math::vec3 origin(0.0f);
    const auto move_to = [&world](float x)
    {
        world.set_position(math::transform::position_t(x, 0.0f, 0.0f));
    };
    trail.update(world, 0.1f);
    check(trail.get_point_count() == 1, "a trail takes its first point where the entity is");
    move_to(0.2f);
    trail.update(world, 0.1f);
    check(trail.get_point_count() == 1, "and no other until the entity has moved its Min Distance");
    move_to(1.0f);
    trail.update(world, 0.1f);
    move_to(2.0f);
    trail.update(world, 0.1f);
    check(trail.get_point_count() == 3, "then one each time it has");
    dynamic_geometry geometry;
    trail.build(world, origin, origin, geometry);
    check(!geometry.is_empty() && count_misfacing_triangles(geometry) == 0 && geometry.get_bounds().min.x < 0.01f &&
              geometry.get_bounds().max.x > 1.99f,
          "the strip runs from the entity back to its oldest point");
    // The first point is 0.2 s old (its age held while the entity stayed on it) and the second 0.1 s: after 0.85 s
    // more the first is past the trail's time, and the trail ends between the two.
    trail.update(world, 0.85f);
    geometry.clear();
    trail.build(world, origin, origin, geometry);
    check(geometry.get_bounds().min.x > 0.05f && geometry.get_bounds().min.x < 0.99f, "the tail ends where the trail's time runs out, between two points");
    trail.emitting = false;
    trail.update(world, 2.0f);
    check(trail.get_point_count() == 0, "a trail that takes no points ends when the ones it has are over");
    trail.emitting = true;
    trail.update(world, 0.1f);
    trail.clear();
    check(trail.get_point_count() == 0, "clear forgets every point");

    // A trail that rests keeps its last point young: when it leaves, the strip starts there.
    trail_component rested;
    rested.time = 1.0f;
    rested.min_distance = 0.5f;
    rested.strip.alignment = strip_alignment::world_axis;
    move_to(0.0f);
    rested.update(world, 0.1f);
    rested.update(world, 10.0f);
    move_to(1.0f);
    rested.update(world, 0.016f);
    geometry.clear();
    rested.build(world, origin, origin, geometry);
    check(rested.get_point_count() == 2 && geometry.get_bounds().min.x < 0.01f && geometry.get_bounds().max.x > 0.99f,
          "a trail that rested draws its first step from where it rested");

    // A trail that stops taking points and starts again leaves the two stretches apart.
    trail_component paused;
    paused.time = 10.0f;
    paused.min_distance = 0.5f;
    paused.strip.alignment = strip_alignment::world_axis;
    for(const float x : {0.0f, 1.0f, 2.0f})
    {
        move_to(x);
        paused.update(world, 0.1f);
    }
    paused.emitting = false;
    move_to(10.0f);
    paused.update(world, 0.1f);
    paused.emitting = true;
    for(const float x : {20.0f, 21.0f, 22.0f})
    {
        move_to(x);
        paused.update(world, 0.1f);
    }
    geometry.clear();
    paused.build(world, origin, origin, geometry);
    bool spans_the_pause = false;
    const auto& indices = geometry.get_indices();
    for(size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const float first = geometry.get_position(indices[i]).x;
        for(size_t corner = 1; corner < 3; ++corner)
        {
            spans_the_pause = spans_the_pause || std::abs(geometry.get_position(indices[i + corner]).x - first) > 5.0f;
        }
    }
    check(paused.get_point_count() == 6 && geometry.get_index_count() == 4 * 6 && !spans_the_pause,
          "no strip joins the points before a pause to the ones after it");

    // A trail a script told to emit again, after the frame's update, joins nothing to the stretch that still fades.
    trail_component resumed;
    resumed.time = 10.0f;
    resumed.min_distance = 0.5f;
    resumed.strip.alignment = strip_alignment::world_axis;
    for(const float x : {0.0f, 1.0f, 2.0f})
    {
        move_to(x);
        resumed.update(world, 0.1f);
    }
    resumed.emitting = false;
    resumed.update(world, 0.1f);
    move_to(30.0f);
    resumed.emitting = true;
    geometry.clear();
    resumed.build(world, origin, origin, geometry);
    check(!geometry.is_empty() && geometry.get_bounds().max.x < 2.5f, "a trail told to emit again starts its run at its next update");

    // An entity that holds still rests on its last point at any Min Distance: a paused game keeps its trail.
    trail_component still;
    still.time = 10.0f;
    still.min_distance = 0.0f;
    still.strip.alignment = strip_alignment::world_axis;
    for(const float x : {0.0f, 1.0f})
    {
        move_to(x);
        still.update(world, 0.1f);
    }
    for(int frame = 0; frame < 2000; ++frame)
    {
        still.update(world, 0.0f);
    }
    check(still.get_point_count() == 2, "a trail with no Min Distance takes no points while its entity holds still");

    // A tiled texture stays on the points it was laid on while the trail grows at its head.
    trail_component track;
    track.time = 10.0f;
    track.min_distance = 0.5f;
    track.strip.alignment = strip_alignment::world_axis;
    track.strip.uv_mode = strip_uv_mode::tile;
    track.strip.tile_length = 1.0f;
    for(const float x : {0.0f, 1.0f, 2.0f})
    {
        move_to(x);
        track.update(world, 0.1f);
    }
    const auto find_v_at = [&geometry](float x)
    {
        for(uint32_t i = 0; i < geometry.get_vertex_count(); ++i)
        {
            if(std::abs(geometry.get_position(i).x - x) < GEOMETRY_TOLERANCE)
            {
                return get_uv(geometry, i).y;
            }
        }
        return -1.0f;
    };
    geometry.clear();
    track.build(world, origin, origin, geometry);
    const float v_before = find_v_at(1.0f);
    move_to(3.0f);
    track.update(world, 0.1f);
    geometry.clear();
    track.build(world, origin, origin, geometry);
    check(std::abs(v_before - 1.0f) < GEOMETRY_TOLERANCE && std::abs(find_v_at(1.0f) - v_before) < GEOMETRY_TOLERANCE,
          "a tiled texture keeps its place on a trail's points as the trail grows");

    // A trail that runs far starts its distances over by whole repeats, so a float keeps them fine.
    trail_component runner;
    runner.time = 0.35f;
    runner.min_distance = 0.5f;
    runner.strip.alignment = strip_alignment::world_axis;
    runner.strip.uv_mode = strip_uv_mode::tile;
    runner.strip.tile_length = 1.0f;
    for(int step = 0; step <= 600; ++step)
    {
        move_to(float(step) * 10.0f);
        runner.update(world, 0.1f);
    }
    geometry.clear();
    runner.build(world, origin, origin, geometry);
    float largest_v = 0.0f;
    for(uint32_t i = 0; i < geometry.get_vertex_count(); ++i)
    {
        largest_v = std::max(largest_v, get_uv(geometry, i).y);
    }
    check(!geometry.is_empty() && largest_v < 4200.0f && std::abs(largest_v - std::round(largest_v)) < 0.01f,
          "a trail that runs far starts its distances over, by whole repeats of the texture");
    move_to(2.0f);

    trail_component blade;
    blade.edge = true;
    blade.edge_base = math::vec3(0.0f, 0.0f, 0.0f);
    blade.edge_tip = math::vec3(0.0f, 2.0f, 0.0f);
    blade.min_distance = 0.1f;
    math::transform swing;
    blade.update(swing, 0.016f);
    swing.set_position(math::transform::position_t(1.0f, 0.0f, 0.0f));
    blade.update(swing, 0.016f);
    swing.set_position(math::transform::position_t(2.0f, 0.0f, 0.0f));
    geometry.clear();
    blade.build(swing, origin, origin, geometry);
    check(!geometry.is_empty() && count_misfacing_triangles(geometry) == 0 && std::abs(geometry.get_bounds().max.y - 2.0f) < GEOMETRY_TOLERANCE &&
              geometry.get_bounds().min.x < 0.01f && geometry.get_bounds().max.x > 1.99f,
          "an edge sweeps a sheet from its base to its tip along where it has been");
    const auto edge_bounds = blade.get_world_bounds(swing);
    check(edge_bounds.max.y > 1.99f && edge_bounds.min.x < 0.01f, "whose bounds hold both of its ends");

    trail_component saved;
    saved.time = 2.5f;
    saved.min_distance = 0.25f;
    saved.emitting = false;
    saved.edge = true;
    saved.edge_tip = math::vec3(0.0f, 3.0f, 0.0f);
    saved.strip.subdivisions = 4;
    const auto loaded = save_and_load(saved);
    check(loaded.time == 2.5f && loaded.min_distance == 0.25f && !loaded.emitting && loaded.edge && loaded.edge_tip.y == 3.0f &&
              loaded.strip.subdivisions == 4 && loaded.get_point_count() == 0,
          "a trail saves and loads its settings, and none of its points");
}

/// Runs @p work @p repeats times and returns the nanoseconds one run took.
template<typename Work>
auto time_one_run(int repeats, Work&& work) -> double
{
    const auto start = std::chrono::steady_clock::now();
    for(int i = 0; i < repeats; ++i)
    {
        work();
    }
    const std::chrono::duration<double, std::nano> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count() / double(repeats);
}

/// What a frame pays for one Line: its bounds and its geometry, for a camera that moved.
void run_dynamic_geometry_benchmarks()
{
    std::printf("\n================================ benchmarks ================================\n");
    constexpr int REPEATS = 20000;
    constexpr size_t POINTS = 16;
    math::transform world;
    world.set_position(math::transform::position_t(3.0f, 1.0f, 2.0f));
    const math::vec3 camera(0.0f, 4.0f, -10.0f);
    dynamic_geometry geometry;

    line_component flat;
    flat.points.clear();
    for(size_t i = 0; i < POINTS; ++i)
    {
        flat.points.emplace_back(-1.0f + 2.0f * float(i) / float(POINTS - 1), (i % 2) == 0 ? -0.4f : 0.4f, 0.0f);
    }
    line_component tube = flat;
    tube.strip.shape = strip_shape::tube;
    tube.strip.subdivisions = 2;
    line_component faded = flat;
    faded.strip.color_over_length.clear();
    faded.strip.color_over_length.add_point(math::color(1.0f, 0.2f, 0.1f, 1.0f), 0.0f);
    faded.strip.color_over_length.add_point(math::color(0.1f, 0.4f, 1.0f, 0.0f), 1.0f);

    const auto report = [](const char* what, double nanoseconds, uint32_t vertices)
    {
        std::printf("%-44s %9.0f ns %6u vertices %7.1f ns each\n", what, nanoseconds, vertices, vertices > 0 ? nanoseconds / vertices : 0.0);
    };
    const auto bench_line = [&](const char* what, const line_component& line)
    {
        const double build = time_one_run(REPEATS,
                                          [&]()
                                          {
                                              geometry.clear();
                                              line.build(world, camera, camera, geometry);
                                          });
        report(what, build, geometry.get_vertex_count());
    };
    strip_sampler sampler;
    report("strip samples, unchanged curves", time_one_run(REPEATS, [&]() { (void)sampler.get(flat.strip); }), 0);
    bench_line("line, flat, camera facing, 16 points", flat);
    bench_line("line, the same with a colour gradient", faded);
    bench_line("line, tube of 8 sides, 2 subdivisions", tube);
    report("line bounds", time_one_run(REPEATS, [&]() { (void)flat.get_world_bounds(world); }), 0);
}

void run_dynamic_geometry_suite(rtti::context& /*ctx*/)
{
    const bool bench_only = tests::wants("bench-only");
    if(bench_only || tests::wants("bench"))
    {
        run_dynamic_geometry_benchmarks();
    }
    if(bench_only)
    {
        return;
    }
    RUN_CASE(test_vertex_packing());
    RUN_CASE(test_appending());
    RUN_CASE(test_band());
    RUN_CASE(test_paths());
    RUN_CASE(test_curves_and_runs());
    RUN_CASE(test_tube());
    RUN_CASE(test_line_component());
    RUN_CASE(test_trail_component());
}

} // namespace

REGISTER_TEST_SUITE("dynamic geometry", run_dynamic_geometry_suite)
