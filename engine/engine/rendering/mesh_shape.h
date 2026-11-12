#pragma once

#include <engine/rendering/mesh.h>

#include <hpp/optional.hpp>
#include <hpp/string_view.hpp>

#include <cstdint>

namespace unravel
{

/// The generator a shape is built by.
enum class mesh_shape_kind : uint8_t
{
    /// No generator: the shape builds nothing.
    none,
    cube,
    rounded_cube,
    sphere,
    plane,
    cylinder,
    capsule,
    cone,
    torus,
    teapot,
    icosahedron,
    dodecahedron,
    icosphere,
    /// A square of rolling hills: sin along x times cos along z.
    wave_terrain,
};

/**
 * @brief A mesh described by a generator and its numbers: what an .emesh file holds.
 *
 * The asset compiler builds the mesh from the description and bakes what an imported model gets (its distance
 * field and cards), so a shape is an asset like any other and nothing is generated while a game runs. Each kind
 * reads the fields its generator takes and ignores the others.
 *
 * A compiled shape is a function of its generator: a change to what a generator builds needs a bump of
 * ex::get_format_version<mesh>() (asset_extensions.h), or projects keep the mesh compiled before the change.
 */
struct mesh_shape
{
    mesh_shape_kind kind{mesh_shape_kind::none};
    /// cube, rounded_cube, plane, wave_terrain: the size along x.
    float width{1.0f};
    /// cube, rounded_cube: the size along y. cylinder, capsule, cone: the height. wave_terrain: the height of a hill.
    float height{1.0f};
    /// cube, rounded_cube, plane, wave_terrain: the size along z.
    float depth{1.0f};
    /// sphere, cylinder, capsule, cone: the radius. torus: the radius of the ring.
    float radius{0.5f};
    /// torus: the radius of the tube.
    float tube_radius{0.25f};
    /// cube, rounded_cube, plane, wave_terrain: the segments along x.
    uint32_t width_segments{1};
    /// cube, rounded_cube: the segments along y.
    uint32_t height_segments{1};
    /// cube, rounded_cube, plane, wave_terrain: the segments along z.
    uint32_t depth_segments{1};
    /// sphere, cylinder, capsule, cone: the segments along the axis. torus: the segments around the ring.
    uint32_t stacks{20};
    /// sphere, cylinder, capsule, cone: the segments around the axis. torus: the segments around the tube.
    uint32_t slices{20};
    /// icosphere: how often each edge of the icosahedron is split.
    uint32_t level{0};
};

/// The name a kind has in an .emesh file ("rounded_cube"); empty for mesh_shape_kind::none.
auto get_shape_kind_name(mesh_shape_kind kind) -> hpp::string_view;

/// The kind called `name` in an .emesh file; none when no generator has that name.
auto find_shape_kind(hpp::string_view name) -> hpp::optional<mesh_shape_kind>;

/**
 * @brief Builds `shape` into `target`, as the mesh::create_* call of its kind does.
 * @param sdf Whether the mesh also gets its distance field now (see primitive_sdf).
 * @return False when the shape names no generator or its numbers build no mesh.
 */
auto create_shape_mesh(mesh& target,
                       const bgfx::VertexLayout& format,
                       const mesh_shape& shape,
                       bool hardware_copy = true,
                       primitive_sdf sdf = primitive_sdf::none) -> bool;

/**
 * @brief The data the asset compiler starts from for `shape`: the vertices, triangles and bounds the generator
 *        builds, as one submesh, in the engine's mesh vertex layout. No buffers are created on the graphics device.
 * @return False when the shape builds no mesh.
 */
auto build_shape_load_data(const mesh_shape& shape, mesh::load_data& data) -> bool;

} // namespace unravel
