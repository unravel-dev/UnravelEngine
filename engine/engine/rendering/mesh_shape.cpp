#include "mesh_shape.h"

#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace unravel
{

// File-local helpers live in their own namespace: the non-Debug unity build merges every source's
// unravel::(anonymous).
namespace shape_build
{
namespace
{

struct shape_kind_name
{
    mesh_shape_kind kind;
    hpp::string_view name;
};

constexpr std::array<shape_kind_name, 13> SHAPE_KIND_NAMES = {{
    {mesh_shape_kind::cube, "cube"},
    {mesh_shape_kind::rounded_cube, "rounded_cube"},
    {mesh_shape_kind::sphere, "sphere"},
    {mesh_shape_kind::plane, "plane"},
    {mesh_shape_kind::cylinder, "cylinder"},
    {mesh_shape_kind::capsule, "capsule"},
    {mesh_shape_kind::cone, "cone"},
    {mesh_shape_kind::torus, "torus"},
    {mesh_shape_kind::teapot, "teapot"},
    {mesh_shape_kind::icosahedron, "icosahedron"},
    {mesh_shape_kind::dodecahedron, "dodecahedron"},
    {mesh_shape_kind::icosphere, "icosphere"},
    {mesh_shape_kind::wave_terrain, "wave_terrain"},
}};

/// The waves a wave terrain has along each side.
constexpr float WAVES_PER_SIDE = 2.0f;
constexpr float WAVE_PI = 3.14159265f;
constexpr uint32_t INDICES_PER_FACE = 3;

/// The heights of a wave terrain's vertex grid, row by row along z: sin along x times cos along z, in 0..1.
auto make_wave_heights(uint32_t segments_x, uint32_t segments_z) -> std::vector<float>
{
    const size_t row_size = size_t(segments_x) + 1;
    std::vector<float> heights(row_size * (size_t(segments_z) + 1));
    for(uint32_t z = 0; z <= segments_z; ++z)
    {
        const float side_z = float(z) / float(segments_z) * 2.0f - 1.0f;
        for(uint32_t x = 0; x <= segments_x; ++x)
        {
            const float side_x = float(x) / float(segments_x) * 2.0f - 1.0f;
            heights[size_t(z) * row_size + x] =
                std::sin(side_x * WAVE_PI * WAVES_PER_SIDE) * std::cos(side_z * WAVE_PI * WAVES_PER_SIDE) * 0.5f + 0.5f;
        }
    }
    return heights;
}

auto create_wave_terrain(mesh& target,
                         const bgfx::VertexLayout& format,
                         const mesh_shape& shape,
                         bool hardware_copy,
                         primitive_sdf sdf) -> bool
{
    if(shape.width_segments == 0 || shape.depth_segments == 0)
    {
        return false;
    }
    const auto heights = make_wave_heights(shape.width_segments, shape.depth_segments);
    return target.create_heightfield(format,
                                     heights,
                                     shape.width_segments,
                                     shape.depth_segments,
                                     shape.width * 0.5f,
                                     shape.depth * 0.5f,
                                     shape.height,
                                     hardware_copy,
                                     sdf);
}

/// The triangles of a prepared mesh, each in the data group of the submesh that draws it.
auto read_triangles(mesh& source) -> mesh::triangle_array_t
{
    mesh::triangle_array_t triangles(source.get_face_count());
    const uint32_t* indices = source.get_system_ib();
    for(const auto* submesh : source.get_submeshes())
    {
        for(uint32_t i = 0; i < submesh->face_count; ++i)
        {
            const size_t face = size_t(submesh->face_start) + i;
            const uint32_t* corners = indices + face * INDICES_PER_FACE;
            triangles[face].data_group_id = submesh->data_group_id;
            triangles[face].indices = {corners[0], corners[1], corners[2]};
        }
    }
    return triangles;
}

auto read_submeshes(const mesh& source) -> std::vector<mesh::submesh>
{
    std::vector<mesh::submesh> submeshes;
    for(const auto* submesh : source.get_submeshes())
    {
        submeshes.push_back(*submesh);
    }
    return submeshes;
}

} // namespace
} // namespace shape_build

auto get_shape_kind_name(mesh_shape_kind kind) -> hpp::string_view
{
    const auto& names = shape_build::SHAPE_KIND_NAMES;
    const auto found = std::find_if(names.begin(),
                                    names.end(),
                                    [kind](const shape_build::shape_kind_name& entry)
                                    {
                                        return entry.kind == kind;
                                    });
    return found != names.end() ? found->name : hpp::string_view{};
}

auto find_shape_kind(hpp::string_view name) -> hpp::optional<mesh_shape_kind>
{
    const auto& names = shape_build::SHAPE_KIND_NAMES;
    const auto found = std::find_if(names.begin(),
                                    names.end(),
                                    [name](const shape_build::shape_kind_name& entry)
                                    {
                                        return entry.name == name;
                                    });
    if(found == names.end())
    {
        return {};
    }
    return found->kind;
}

auto create_shape_mesh(mesh& target,
                       const bgfx::VertexLayout& format,
                       const mesh_shape& shape,
                       bool hardware_copy,
                       primitive_sdf sdf) -> bool
{
    switch(shape.kind)
    {
        case mesh_shape_kind::cube:
            return target.create_cube(format,
                                      shape.width,
                                      shape.height,
                                      shape.depth,
                                      shape.width_segments,
                                      shape.height_segments,
                                      shape.depth_segments,
                                      hardware_copy,
                                      sdf);
        case mesh_shape_kind::rounded_cube:
            return target.create_rounded_cube(format,
                                              shape.width,
                                              shape.height,
                                              shape.depth,
                                              shape.width_segments,
                                              shape.height_segments,
                                              shape.depth_segments,
                                              hardware_copy,
                                              sdf);
        case mesh_shape_kind::sphere:
            return target.create_sphere(format, shape.radius, shape.stacks, shape.slices, hardware_copy, sdf);
        case mesh_shape_kind::plane:
            return target.create_plane(format,
                                       shape.width,
                                       shape.depth,
                                       shape.width_segments,
                                       shape.depth_segments,
                                       hardware_copy,
                                       sdf);
        case mesh_shape_kind::cylinder:
            return target.create_cylinder(format,
                                          shape.radius,
                                          shape.height,
                                          shape.stacks,
                                          shape.slices,
                                          hardware_copy,
                                          sdf);
        case mesh_shape_kind::capsule:
            return target.create_capsule(format,
                                         shape.radius,
                                         shape.height,
                                         shape.stacks,
                                         shape.slices,
                                         hardware_copy,
                                         sdf);
        case mesh_shape_kind::cone:
            return target.create_cone(format,
                                      shape.radius,
                                      shape.height,
                                      shape.stacks,
                                      shape.slices,
                                      hardware_copy,
                                      sdf);
        case mesh_shape_kind::torus:
            return target.create_torus(format,
                                       shape.radius,
                                       shape.tube_radius,
                                       shape.stacks,
                                       shape.slices,
                                       hardware_copy,
                                       sdf);
        case mesh_shape_kind::teapot:
            return target.create_teapot(format, hardware_copy, sdf);
        case mesh_shape_kind::icosahedron:
            return target.create_icosahedron(format, hardware_copy, sdf);
        case mesh_shape_kind::dodecahedron:
            return target.create_dodecahedron(format, hardware_copy, sdf);
        case mesh_shape_kind::icosphere:
            return target.create_icosphere(format, int(shape.level), hardware_copy, sdf);
        case mesh_shape_kind::wave_terrain:
            return shape_build::create_wave_terrain(target, format, shape, hardware_copy, sdf);
        case mesh_shape_kind::none:
            return false;
    }
    return false;
}

auto build_shape_load_data(const mesh_shape& shape, mesh::load_data& data) -> bool
{
    mesh source;
    if(!create_shape_mesh(source, gfx::mesh_vertex::get_layout(), shape, false))
    {
        return false;
    }
    const auto& format = source.get_vertex_format();
    const uint8_t* vertices = source.get_system_vb();
    data = mesh::load_data{};
    data.vertex_format = format;
    data.vertex_count = source.get_vertex_count();
    data.vertex_data.assign(vertices, vertices + size_t(data.vertex_count) * format.getStride());
    data.triangle_data = shape_build::read_triangles(source);
    data.triangle_count = uint32_t(data.triangle_data.size());
    data.submeshes = shape_build::read_submeshes(source);
    data.material_count = uint32_t(source.get_data_groups_count());
    data.bbox = source.get_bounds();
    if(const sdf_source_geometry* gi_geometry = source.get_gi_source_geometry())
    {
        data.gi_source_geometry = *gi_geometry;
    }
    return true;
}

} // namespace unravel
