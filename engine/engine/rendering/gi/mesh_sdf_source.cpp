#include "mesh_sdf_source.h"

#include <engine/profiler/profiler.h>

#include <graphics/graphics.h>

#include <unordered_map>

namespace unravel
{

auto extract_sdf_source_geometry(const uint8_t* vertex_data,
                                 uint32_t vertex_count,
                                 const gfx::vertex_layout& format,
                                 const uint32_t* indices,
                                 uint32_t triangle_count,
                                 sdf_source_geometry& out) -> bool
{
    APP_SCOPE_PERF("GI/Bake/Extract SDF Geometry");
    out = {};
    if(vertex_data == nullptr || indices == nullptr || vertex_count == 0 || triangle_count == 0)
    {
        return false;
    }
    if(!format.has(gfx::attribute::Position))
    {
        return false;
    }
    out.positions.resize(vertex_count);
    out.bounds.reset();
    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float unpacked[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx::vertex_unpack(unpacked, gfx::attribute::Position, format, vertex_data, i);
        out.positions[i] = math::vec3(unpacked[0], unpacked[1], unpacked[2]);
        out.bounds.add_point(out.positions[i]);
    }
    out.indices.assign(indices, indices + size_t(triangle_count) * 3);
    return out.is_valid();
}

namespace
{

/**
 * @brief Shared compaction for both the base-LOD and generated-LOD extraction paths.
 *
 * @p get_corner returns the source vertex index of a triangle corner, which is the only thing
 * the two paths differ in: the base LOD stores triangles as structs carrying a data group, while
 * a generated LOD stores a flat index array. Both index the SAME vertex buffer -- simplification
 * removes and reindexes triangles, it does not create vertices.
 */
template<typename CornerFn>
auto compact_submesh_geometry(const mesh::load_data& data,
                              size_t face_begin,
                              size_t face_count,
                              CornerFn&& get_corner,
                              sdf_source_geometry& out) -> bool
{
    // Only the vertices this submesh actually references are gathered, with the indices remapped
    // to match.
    //
    // Keeping the whole shared vertex buffer and filtering only the indices would be simpler, and
    // it costs almost nothing in memory -- but every per-vertex pass in the baker (bounds,
    // position welding, vertex adjacency) is linear in the vertex COUNT it is handed. Handing it
    // the entire model once per submesh makes baking quadratic in model complexity: a few
    // thousand submeshes over a few million shared vertices turns a seconds-long bake into a
    // minutes-long one, while each submesh individually is tiny.
    //
    // The remap is keyed by a map rather than by an array indexed with the source vertex id, for
    // the same reason: an array costs one allocate-and-clear of the WHOLE model's vertex count
    // per submesh, which is the same quadratic term measured in memory traffic instead of in
    // triangles.
    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(face_count * 3);
    out.indices.reserve(face_count * 3);
    out.positions.reserve(face_count * 3);
    out.bounds.reset();
    for(size_t face = face_begin; face < face_begin + face_count; ++face)
    {
        for(uint32_t corner = 0; corner < 3; ++corner)
        {
            const uint32_t source_index = get_corner(face, corner);
            if(source_index >= data.vertex_count)
            {
                continue;
            }
            const auto inserted = remap.emplace(source_index, uint32_t(out.positions.size()));
            if(inserted.second)
            {
                float unpacked[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                gfx::vertex_unpack(unpacked,
                                   gfx::attribute::Position,
                                   data.vertex_format,
                                   data.vertex_data.data(),
                                   source_index);
                out.positions.emplace_back(unpacked[0], unpacked[1], unpacked[2]);
                out.bounds.add_point(out.positions.back());
            }
            out.indices.push_back(inserted.first->second);
        }
    }
    return out.is_valid();
}

/// Shared entry validation for both paths.
auto has_usable_vertex_data(const mesh::load_data& data) -> bool
{
    return !data.vertex_data.empty() && data.vertex_count != 0 &&
           data.vertex_format.has(gfx::attribute::Position);
}

} // namespace

auto extract_sdf_source_geometry(const mesh::load_data& data,
                                 const mesh::submesh& submesh,
                                 sdf_source_geometry& out) -> bool
{
    APP_SCOPE_PERF("GI/Bake/Extract Submesh Geometry");
    out = {};
    if(data.triangle_data.empty() || !has_usable_vertex_data(data))
    {
        return false;
    }
    // A submesh owns a CONTIGUOUS range of the triangle array -- the importer appends each
    // submesh's faces in turn, and both the skinning and the LOD passes preserve that. Walking
    // only this range is what keeps the whole per-submesh pass linear in the model: scanning all
    // triangles per submesh instead costs submesh_count x triangle_count, which on a model with
    // thousands of submeshes is the difference between seconds and minutes.
    if(submesh.face_start < 0 || submesh.face_count == 0)
    {
        return false;
    }
    const size_t face_begin = size_t(submesh.face_start);
    if(face_begin + submesh.face_count > data.triangle_data.size())
    {
        return false;
    }
    return compact_submesh_geometry(
        data,
        face_begin,
        submesh.face_count,
        [&](size_t face, uint32_t corner) { return data.triangle_data[face].indices[corner]; },
        out);
}

auto extract_sdf_source_geometry(const mesh::load_data& data,
                                 uint32_t lod_index,
                                 size_t submesh_index,
                                 sdf_source_geometry& out) -> bool
{
    out = {};
    if(submesh_index >= data.submeshes.size())
    {
        return false;
    }
    // LOD 0 is the base topology in triangle_data; data.lods holds the GENERATED levels, so
    // LOD n lives at index n - 1 and the valid range is [0, lods.size()].
    //
    // A request past the last generated level is CLAMPED to the coarsest one available, not
    // dropped to the base. Asking for a higher level means "cheaper", so answering an
    // unavailable one with full detail would be the most expensive possible reading of the
    // request -- and silently the slowest, which is the opposite of what the caller asked for.
    const auto effective_lod = uint32_t(math::min<size_t>(lod_index, data.lods.size()));
    if(effective_lod == 0)
    {
        return extract_sdf_source_geometry(data, data.submeshes[submesh_index], out);
    }
    APP_SCOPE_PERF("GI/Bake/Extract Submesh Geometry");
    const auto& lod = data.lods[effective_lod - 1];
    if(lod.index_data.empty() || submesh_index >= lod.submeshes.size() || !has_usable_vertex_data(data))
    {
        return extract_sdf_source_geometry(data, data.submeshes[submesh_index], out);
    }
    // The LOD pass emits one entry per base submesh in order, including empty ones, so the
    // indexing the GI registration relies on still holds at every level.
    const auto& submesh = lod.submeshes[submesh_index];
    if(submesh.face_start < 0 || submesh.face_count == 0)
    {
        return false;
    }
    const size_t face_begin = size_t(submesh.face_start);
    if((face_begin + submesh.face_count) * 3 > lod.index_data.size())
    {
        return false;
    }
    return compact_submesh_geometry(
        data,
        face_begin,
        submesh.face_count,
        [&](size_t face, uint32_t corner) { return lod.index_data[face * 3 + corner]; },
        out);
}

} // namespace unravel
