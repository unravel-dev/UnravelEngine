#pragma once

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief One vertex of CPU-built geometry before it is packed: a place and a tangent frame in world space, a texture
 * coordinate and a linear colour between 0 and 1.
 */
struct dynamic_vertex
{
    math::vec3 position{0.0f, 0.0f, 0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    /// The direction the first texture coordinate grows in.
    math::vec3 tangent{1.0f, 0.0f, 0.0f};
    /// The direction the second texture coordinate grows in.
    math::vec3 bitangent{0.0f, 0.0f, 1.0f};
    math::vec2 uv{0.0f, 0.0f};
    math::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

/**
 * @brief Triangles built on the CPU in the vertex layout every mesh draws with (gfx::mesh_vertex), in world space:
 * what a line or a trail hands to a draw. Any Surface material draws them with the programs it has.
 *
 * A triangle's front is the side its corners turn clockwise on, as a mesh's is. The second texture coordinates copy
 * the first.
 */
class dynamic_geometry
{
public:
    /// Removes every vertex and index; the memory stays for the next build.
    void clear();

    /// Adds a vertex and returns its index.
    auto add_vertex(const dynamic_vertex& vertex) -> uint32_t;
    /// Adds the triangle of three vertices added before.
    void add_triangle(uint32_t a, uint32_t b, uint32_t c);
    /// Adds the vertices and triangles of @p other after this geometry's own, each vertex moved by @p offset: geometry
    /// kept relative to one point joins geometry placed relative to another.
    void append(const dynamic_geometry& other, const math::vec3& offset);

    /// Whether the geometry has no triangle.
    auto is_empty() const -> bool;
    /// How many vertices were added.
    auto get_vertex_count() const -> uint32_t;
    /// How many indices the triangles added take: three each.
    auto get_index_count() const -> uint32_t;
    /// The packed vertices, get_vertex_stride() bytes each.
    auto get_vertex_data() const -> const uint8_t*;
    /// The size of the packed vertices, in bytes.
    auto get_vertex_data_size() const -> uint32_t;
    /// The triangles' corners, as indices of the vertices.
    auto get_indices() const -> const std::vector<uint32_t>&;
    /// The box around every vertex added; unpopulated while empty.
    auto get_bounds() const -> const math::bbox&;
    /// The position of vertex @p index, read back from the packed data.
    auto get_position(uint32_t index) const -> math::vec3;

    /// The size of one packed vertex, in bytes.
    static auto get_vertex_stride() -> uint32_t;

private:
    std::vector<uint8_t> vertices_;
    std::vector<uint32_t> indices_;
    uint32_t vertex_count_ = 0;
    math::bbox bounds_;
};

} // namespace unravel
