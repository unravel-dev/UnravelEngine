#include "dynamic_geometry.h"

#include <graphics/vertex_decl.h>

#include <algorithm>
#include <cstring>

namespace unravel
{

namespace
{
/// Where each attribute of a gfx::mesh_vertex sits in a packed vertex.
struct dynamic_vertex_offsets
{
    uint32_t stride = 0;
    uint32_t position = 0;
    uint32_t normal = 0;
    uint32_t tangent = 0;
    uint32_t bitangent = 0;
    uint32_t uv0 = 0;
    uint32_t color = 0;
    uint32_t uv1 = 0;
};

auto get_dynamic_vertex_offsets() -> const dynamic_vertex_offsets&
{
    static const dynamic_vertex_offsets offsets = []
    {
        const auto& layout = gfx::mesh_vertex::get_layout();
        dynamic_vertex_offsets result;
        result.stride = layout.getStride();
        result.position = layout.getOffset(bgfx::Attrib::Position);
        result.normal = layout.getOffset(bgfx::Attrib::Normal);
        result.tangent = layout.getOffset(bgfx::Attrib::Tangent);
        result.bitangent = layout.getOffset(bgfx::Attrib::Bitangent);
        result.uv0 = layout.getOffset(bgfx::Attrib::TexCoord0);
        result.color = layout.getOffset(bgfx::Attrib::Color0);
        result.uv1 = layout.getOffset(bgfx::Attrib::TexCoord1);
        return result;
    }();
    return offsets;
}

/// A unit vector's component as the layout stores it: a byte a shader reads back as byte / 255 * 2 - 1.
auto pack_signed_unit(float value) -> uint8_t
{
    return uint8_t(std::clamp(value, -1.0f, 1.0f) * 127.0f + 128.0f);
}

/// A colour channel between 0 and 1 as the layout stores it.
auto pack_unit(float value) -> uint8_t
{
    return uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void pack_direction(const math::vec3& direction, uint8_t* out)
{
    out[0] = pack_signed_unit(direction.x);
    out[1] = pack_signed_unit(direction.y);
    out[2] = pack_signed_unit(direction.z);
}
} // namespace

void dynamic_geometry::clear()
{
    vertices_.clear();
    indices_.clear();
    vertex_count_ = 0;
    bounds_.reset();
}

auto dynamic_geometry::add_vertex(const dynamic_vertex& vertex) -> uint32_t
{
    const auto& offsets = get_dynamic_vertex_offsets();
    const size_t start = vertices_.size();
    vertices_.resize(start + offsets.stride, uint8_t(0));
    uint8_t* packed = vertices_.data() + start;
    const float position[3] = {vertex.position.x, vertex.position.y, vertex.position.z};
    std::memcpy(packed + offsets.position, position, sizeof(position));
    pack_direction(vertex.normal, packed + offsets.normal);
    pack_direction(vertex.tangent, packed + offsets.tangent);
    pack_direction(vertex.bitangent, packed + offsets.bitangent);
    const float uv[2] = {vertex.uv.x, vertex.uv.y};
    std::memcpy(packed + offsets.uv0, uv, sizeof(uv));
    std::memcpy(packed + offsets.uv1, uv, sizeof(uv));
    packed[offsets.color + 0] = pack_unit(vertex.color.x);
    packed[offsets.color + 1] = pack_unit(vertex.color.y);
    packed[offsets.color + 2] = pack_unit(vertex.color.z);
    packed[offsets.color + 3] = pack_unit(vertex.color.w);
    bounds_.add_point(vertex.position);
    return vertex_count_++;
}

void dynamic_geometry::add_triangle(uint32_t a, uint32_t b, uint32_t c)
{
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

void dynamic_geometry::append(const dynamic_geometry& other, const math::vec3& offset)
{
    if(other.is_empty())
    {
        return;
    }
    const auto& offsets = get_dynamic_vertex_offsets();
    const size_t first_byte = vertices_.size();
    const uint32_t first_vertex = vertex_count_;
    vertices_.insert(vertices_.end(), other.vertices_.begin(), other.vertices_.end());
    indices_.reserve(indices_.size() + other.indices_.size());
    for(const uint32_t index : other.indices_)
    {
        indices_.push_back(first_vertex + index);
    }
    vertex_count_ += other.vertex_count_;
    const float moved_by[3] = {offset.x, offset.y, offset.z};
    uint8_t* vertex = vertices_.data() + first_byte + offsets.position;
    for(uint32_t i = 0; i < other.vertex_count_; ++i, vertex += offsets.stride)
    {
        float position[3];
        std::memcpy(position, vertex, sizeof(position));
        position[0] += moved_by[0];
        position[1] += moved_by[1];
        position[2] += moved_by[2];
        std::memcpy(vertex, position, sizeof(position));
    }
    // Vertices that are no numbers leave the other's bounds as they were made.
    if(other.bounds_.is_populated())
    {
        bounds_.add_point(other.bounds_.min + offset);
        bounds_.add_point(other.bounds_.max + offset);
    }
}

auto dynamic_geometry::is_empty() const -> bool
{
    return indices_.empty();
}

auto dynamic_geometry::get_vertex_count() const -> uint32_t
{
    return vertex_count_;
}

auto dynamic_geometry::get_index_count() const -> uint32_t
{
    return uint32_t(indices_.size());
}

auto dynamic_geometry::get_vertex_data() const -> const uint8_t*
{
    return vertices_.data();
}

auto dynamic_geometry::get_vertex_data_size() const -> uint32_t
{
    return uint32_t(vertices_.size());
}

auto dynamic_geometry::get_indices() const -> const std::vector<uint32_t>&
{
    return indices_;
}

auto dynamic_geometry::get_bounds() const -> const math::bbox&
{
    return bounds_;
}

auto dynamic_geometry::get_position(uint32_t index) const -> math::vec3
{
    const auto& offsets = get_dynamic_vertex_offsets();
    float position[3] = {0.0f, 0.0f, 0.0f};
    std::memcpy(position, vertices_.data() + size_t(index) * offsets.stride + offsets.position, sizeof(position));
    return math::vec3(position[0], position[1], position[2]);
}

auto dynamic_geometry::get_vertex_stride() -> uint32_t
{
    return get_dynamic_vertex_offsets().stride;
}

} // namespace unravel
