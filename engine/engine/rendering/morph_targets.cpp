#include "morph_targets.h"

#include <engine/animation/animation.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace unravel
{
namespace
{

/// One 10-bit signed normalized component of a value in -1..1.
auto pack_snorm10(float value) -> uint32_t
{
    const auto scaled = int32_t(std::lround(std::clamp(value, -1.0f, 1.0f) * 511.0f));
    return uint32_t(scaled) & 0x3ffu;
}

auto unpack_snorm10(uint32_t bits) -> float
{
    // Sign-extend the 10 bits.
    const int32_t value = int32_t(bits << 22) >> 22;
    return std::max(float(value) / 511.0f, -1.0f);
}

auto get_vec3(const std::array<float, 3>& value) -> math::vec3
{
    return {value[0], value[1], value[2]};
}

/// The mesh's packed normal, tangent and bitangent of vertex @p index, unpacked.
struct vertex_basis
{
    math::vec3 normal{0.0f};
    math::vec3 tangent{0.0f};
    math::vec3 bitangent{0.0f};
};

auto read_basis(const bgfx::VertexLayout& layout, const uint8_t* vertices, uint32_t index) -> vertex_basis
{
    vertex_basis basis;
    float value[4] = {};
    if(layout.has(bgfx::Attrib::Normal))
    {
        bgfx::vertexUnpack(value, bgfx::Attrib::Normal, layout, vertices, index);
        basis.normal = {value[0], value[1], value[2]};
    }
    if(layout.has(bgfx::Attrib::Tangent))
    {
        bgfx::vertexUnpack(value, bgfx::Attrib::Tangent, layout, vertices, index);
        basis.tangent = {value[0], value[1], value[2]};
    }
    if(layout.has(bgfx::Attrib::Bitangent))
    {
        bgfx::vertexUnpack(value, bgfx::Attrib::Bitangent, layout, vertices, index);
        basis.bitangent = {value[0], value[1], value[2]};
    }
    return basis;
}

void write_vertex(const math::vec3& position, const vertex_basis& basis, uint8_t* deformed, uint32_t index)
{
    const auto& layout = get_morph_deform_layout();
    const float packed_position[4] = {position.x, position.y, position.z, 0.0f};
    const float normal[4] = {basis.normal.x, basis.normal.y, basis.normal.z, 0.0f};
    const float tangent[4] = {basis.tangent.x, basis.tangent.y, basis.tangent.z, 0.0f};
    const float bitangent[4] = {basis.bitangent.x, basis.bitangent.y, basis.bitangent.z, 0.0f};
    bgfx::vertexPack(packed_position, false, bgfx::Attrib::Position, layout, deformed, index);
    bgfx::vertexPack(normal, true, bgfx::Attrib::Normal, layout, deformed, index);
    bgfx::vertexPack(tangent, true, bgfx::Attrib::Tangent, layout, deformed, index);
    bgfx::vertexPack(bitangent, true, bgfx::Attrib::Bitangent, layout, deformed, index);
}

/// @p basis with its normal moved by @p normal_delta: the normal renormalized, the tangent made orthogonal to it again
/// and the bitangent rebuilt with the handedness it had.
auto morph_basis(const vertex_basis& basis, const math::vec3& normal_delta) -> vertex_basis
{
    const float handedness =
        math::dot(math::cross(basis.normal, basis.tangent), basis.bitangent) < 0.0f ? -1.0f : 1.0f;
    vertex_basis result;
    const math::vec3 moved = basis.normal + normal_delta;
    const float moved_length = math::length(moved);
    result.normal = moved_length > 1e-6f ? moved / moved_length : basis.normal;
    const math::vec3 tangent = basis.tangent - result.normal * math::dot(result.normal, basis.tangent);
    const float tangent_length = math::length(tangent);
    result.tangent = tangent_length > 1e-6f ? tangent / tangent_length : basis.tangent;
    result.bitangent = math::cross(result.normal, result.tangent) * handedness;
    return result;
}

} // namespace

auto morph_target_data::find_target(hpp::string_view name) const -> int
{
    for(size_t i = 0; i < targets.size(); ++i)
    {
        if(name == hpp::string_view(targets[i].name))
        {
            return int(i);
        }
    }
    return -1;
}

auto pack_morph_normal(const math::vec3& delta) -> uint32_t
{
    return pack_snorm10(delta.x * 0.5f) | (pack_snorm10(delta.y * 0.5f) << 10) | (pack_snorm10(delta.z * 0.5f) << 20);
}

auto unpack_morph_normal(uint32_t packed) -> math::vec3
{
    return math::vec3(unpack_snorm10(packed & 0x3ffu), unpack_snorm10((packed >> 10) & 0x3ffu), unpack_snorm10((packed >> 20) & 0x3ffu)) *
           2.0f;
}

void morph_target_builder::add(const std::string& name, float default_weight, const std::vector<vertex_delta>& deltas)
{
    auto it = std::find_if(targets_.begin(),
                           targets_.end(),
                           [&name](const target_entry& entry)
                           {
                               return entry.name == name;
                           });
    if(it == targets_.end())
    {
        targets_.push_back({name, default_weight, {}});
        it = std::prev(targets_.end());
    }
    it->deltas.insert(it->deltas.end(), deltas.begin(), deltas.end());
}

auto morph_target_builder::empty() const -> bool
{
    return targets_.empty();
}

auto morph_target_builder::build(float position_threshold, float normal_threshold) const -> morph_target_data
{
    morph_target_data data;
    // The vertices each target keeps, then the slots of all of them.
    std::vector<std::vector<vertex_delta>> kept(targets_.size());
    std::vector<uint32_t> morphed;
    for(size_t t = 0; t < targets_.size(); ++t)
    {
        for(const auto& delta : targets_[t].deltas)
        {
            const bool moves = math::length(delta.position) >= position_threshold;
            const bool turns = math::length(delta.normal) >= normal_threshold;
            if(moves || turns)
            {
                kept[t].push_back(delta);
                morphed.push_back(delta.vertex);
            }
        }
    }
    std::sort(morphed.begin(), morphed.end());
    morphed.erase(std::unique(morphed.begin(), morphed.end()), morphed.end());
    std::unordered_map<uint32_t, uint32_t> slot_of;
    slot_of.reserve(morphed.size());
    for(uint32_t slot = 0; slot < morphed.size(); ++slot)
    {
        slot_of.emplace(morphed[slot], slot);
    }
    data.morphed_vertices = std::move(morphed);
    data.targets.reserve(targets_.size());
    for(size_t t = 0; t < targets_.size(); ++t)
    {
        auto& target = data.targets.emplace_back();
        target.name = targets_[t].name;
        target.default_weight = targets_[t].default_weight;
        target.first_delta = uint32_t(data.deltas.size());
        auto& deltas = kept[t];
        // Stable, so of a vertex listed twice the delta added first stays first.
        std::stable_sort(deltas.begin(),
                         deltas.end(),
                         [](const vertex_delta& lhs, const vertex_delta& rhs)
                         {
                             return lhs.vertex < rhs.vertex;
                         });
        for(const auto& delta : deltas)
        {
            // A vertex listed twice for one target (two shapes of one mesh sharing its name) keeps its first delta.
            if(!data.deltas.empty() && data.deltas.size() > target.first_delta &&
               data.morphed_vertices[data.deltas.back().slot] == delta.vertex)
            {
                continue;
            }
            morph_delta& entry = data.deltas.emplace_back();
            entry.slot = slot_of.at(delta.vertex);
            entry.position = {delta.position.x, delta.position.y, delta.position.z};
            entry.normal = pack_morph_normal(delta.normal);
            target.max_displacement = std::max(target.max_displacement, math::length(delta.position));
        }
        target.delta_count = uint32_t(data.deltas.size()) - target.first_delta;
    }
    return data;
}

void add_morph_vertex_copies(morph_target_data& data, const std::vector<std::pair<uint32_t, uint32_t>>& copies)
{
    if(data.empty() || copies.empty())
    {
        return;
    }
    // Rebuild through the builder: unpack every target's deltas, give each copy its original's, sort again.
    std::unordered_map<uint32_t, std::vector<uint32_t>> copies_of;
    for(const auto& [copy, original] : copies)
    {
        copies_of[original].push_back(copy);
    }
    morph_target_builder builder;
    for(const auto& target : data.targets)
    {
        std::vector<morph_target_builder::vertex_delta> deltas;
        deltas.reserve(target.delta_count);
        for(uint32_t i = target.first_delta; i < target.first_delta + target.delta_count; ++i)
        {
            const auto& delta = data.deltas[i];
            const uint32_t vertex = data.morphed_vertices[delta.slot];
            const morph_target_builder::vertex_delta unpacked{vertex, get_vec3(delta.position), unpack_morph_normal(delta.normal)};
            deltas.push_back(unpacked);
            const auto found = copies_of.find(vertex);
            if(found != copies_of.end())
            {
                for(uint32_t copy : found->second)
                {
                    auto copied = unpacked;
                    copied.vertex = copy;
                    deltas.push_back(copied);
                }
            }
        }
        builder.add(target.name, target.default_weight, deltas);
    }
    // Every delta already passed the thresholds; keep them all.
    data = builder.build(0.0f, 0.0f);
}

auto get_morph_vertex_displacements(const morph_target_data& data, uint32_t vertex_count) -> std::vector<float>
{
    std::vector<float> displacements(vertex_count, 0.0f);
    for(const auto& delta : data.deltas)
    {
        const uint32_t vertex = data.morphed_vertices[delta.slot];
        if(vertex < vertex_count)
        {
            displacements[vertex] = std::max(displacements[vertex], math::length(get_vec3(delta.position)));
        }
    }
    return displacements;
}

void accumulate_morph_deltas(const morph_target_data& data, hpp::span<const float> weights, std::vector<float>& deltas)
{
    deltas.assign(data.morphed_vertices.size() * 6, 0.0f);
    for(size_t t = 0; t < data.targets.size() && t < weights.size(); ++t)
    {
        const float weight = weights[t];
        // A weight that is not a finite number moves nothing, as one of 0 does: scaled deltas would be NaN.
        if(!std::isfinite(weight) || std::abs(weight) <= k_morph_weight_threshold)
        {
            continue;
        }
        const auto& target = data.targets[t];
        for(uint32_t i = target.first_delta; i < target.first_delta + target.delta_count; ++i)
        {
            const auto& delta = data.deltas[i];
            float* slot = deltas.data() + size_t(delta.slot) * 6;
            const math::vec3 normal = unpack_morph_normal(delta.normal);
            slot[0] += delta.position[0] * weight;
            slot[1] += delta.position[1] * weight;
            slot[2] += delta.position[2] * weight;
            slot[3] += normal.x * weight;
            slot[4] += normal.y * weight;
            slot[5] += normal.z * weight;
        }
    }
}

void apply_morph_position_deltas(const morph_target_data& data,
                                 const std::vector<float>& deltas,
                                 std::vector<math::vec3>& positions)
{
    for(size_t slot = 0; slot < data.morphed_vertices.size() && slot * 6 + 2 < deltas.size(); ++slot)
    {
        const uint32_t vertex = data.morphed_vertices[slot];
        if(vertex < positions.size())
        {
            const float* delta = deltas.data() + slot * 6;
            positions[vertex] += math::vec3(delta[0], delta[1], delta[2]);
        }
    }
}

auto get_morph_bounds_padding(const morph_target_data& data, hpp::span<const float> weights) -> float
{
    float padding = 0.0f;
    for(size_t t = 0; t < data.targets.size() && t < weights.size(); ++t)
    {
        const float weight = weights[t];
        if(std::isfinite(weight) && std::abs(weight) > k_morph_weight_threshold)
        {
            padding += std::abs(weight) * data.targets[t].max_displacement;
        }
    }
    return padding;
}

auto make_morph_curve_targets(const morph_target_data& data) -> std::vector<morph_curve_target>
{
    std::vector<morph_curve_target> curve_targets;
    curve_targets.reserve(data.targets.size());
    for(size_t t = 0; t < data.targets.size(); ++t)
    {
        curve_targets.push_back({get_animation_curve_id(data.targets[t].name), uint32_t(t)});
    }
    std::sort(curve_targets.begin(),
              curve_targets.end(),
              [](const morph_curve_target& lhs, const morph_curve_target& rhs)
              {
                  return lhs.curve_id != rhs.curve_id ? lhs.curve_id < rhs.curve_id : lhs.target < rhs.target;
              });
    return curve_targets;
}

auto get_morph_deform_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []
    {
        bgfx::VertexLayout result;
        result.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
            .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
            .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
            .end();
        return result;
    }();
    return layout;
}

void write_morphed_vertices(const morph_target_data& data,
                            const bgfx::VertexLayout& mesh_layout,
                            const uint8_t* mesh_vertices,
                            const std::vector<float>& deltas,
                            uint8_t* deformed)
{
    for(uint32_t slot = 0; slot < data.morphed_vertices.size(); ++slot)
    {
        const uint32_t vertex = data.morphed_vertices[slot];
        const float* delta = deltas.data() + size_t(slot) * 6;
        float position[4] = {};
        bgfx::vertexUnpack(position, bgfx::Attrib::Position, mesh_layout, mesh_vertices, vertex);
        const math::vec3 moved(position[0] + delta[0], position[1] + delta[1], position[2] + delta[2]);
        const auto basis = morph_basis(read_basis(mesh_layout, mesh_vertices, vertex), math::vec3(delta[3], delta[4], delta[5]));
        write_vertex(moved, basis, deformed, vertex);
    }
}

void write_rest_vertices(const bgfx::VertexLayout& mesh_layout,
                         const uint8_t* mesh_vertices,
                         uint32_t vertex_count,
                         uint8_t* deformed)
{
    bgfx::vertexConvert(get_morph_deform_layout(), deformed, mesh_layout, mesh_vertices, vertex_count);
}

} // namespace unravel
