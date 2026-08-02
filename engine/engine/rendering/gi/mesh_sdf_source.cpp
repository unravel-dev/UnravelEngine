#include "mesh_sdf_source.h"

#include <engine/profiler/profiler.h>

#include <graphics/graphics.h>

#include <array>
#include <numeric>
#include <unordered_map>

namespace unravel
{
namespace
{

/**
 * @brief True when a triangle carries no surface and must not reach the bake.
 *
 * Two kinds of junk, both of which the RENDERER quietly discards while the bake would not:
 *
 *   - Non-finite positions. A NaN never compares true, so a degeneracy test alone lets it through,
 *     and one NaN corner poisons the bounds of the whole field.
 *   - Slivers. A triangle whose vertices are collinear has zero area, draws nothing, and cannot
 *     occlude or bounce light -- but its CORNERS still expand the bounds, and the bounds are what
 *     size the field. One sliver spanning a model turns a compact submesh into a field thousands of
 *     units across, whose voxel is correspondingly enormous; since an unsigned shell is floored at
 *     one voxel, that field then renders as a solid block the size of the sliver. An invisible
 *     triangle producing the single most visible artefact in the scene.
 *
 * The sliver test is the triangle's height over its longest edge, expressed as a ratio, so it is
 * scale independent: an absolute area threshold would either miss slivers on a large mesh or reject
 * genuine small triangles on a fine one.
 */
auto carries_no_surface(const math::vec3& a, const math::vec3& b, const math::vec3& c) -> bool
{
    // Slivers are a matter of degree, so the threshold is a judgement, and it is bounded on BOTH
    // sides. The ratio is the reciprocal of the aspect ratio, so this rejects anything thinner than
    // 10000:1 -- comfortably past any tessellation an artist authors, while leaving the 10:1 to 100:1
    // trim, mullions and floor strips that make up much of a building's occlusion.
    //
    // Measured by test_surface_test_keeps_ordinary_tessellation: a 40:1 panel measures 0.025, so a
    // threshold anywhere near 0.1 discards it outright and the geometry produces no field at all.
    // That failure is near-invisible -- a missing occluder leaks light somewhere across the project
    // rather than drawing a block in front of the camera -- which is why the value has a test.
    //
    // This CANNOT be used to suppress an unrepresentable field. A submesh whose parts are scattered
    // is not thin, so no threshold separates it from real geometry; that is what the sparsity check
    // in bake_mesh_sdf is for.
    constexpr float min_height_ratio = 0.0001f;
    if(!math::all(math::isfinite(a)) || !math::all(math::isfinite(b)) || !math::all(math::isfinite(c)))
    {
        return true;
    }
    const math::vec3 ab = b - a;
    const math::vec3 ac = c - a;
    const float longest_edge =
        math::max(math::length(ab), math::max(math::length(ac), math::length(c - b)));
    if(longest_edge <= 0.0f)
    {
        return true;
    }
    // |ab x ac| is twice the area, so dividing by the longest edge gives the height over it.
    return math::length(math::cross(ab, ac)) <= longest_edge * longest_edge * min_height_ratio;
}

} // namespace

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
    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        float unpacked[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx::vertex_unpack(unpacked, gfx::attribute::Position, format, vertex_data, i);
        out.positions[i] = math::vec3(unpacked[0], unpacked[1], unpacked[2]);
    }
    // Same surface test as the submesh path, and for the same reason -- but the bounds are
    // accumulated from the surviving TRIANGLES rather than from every position, because this buffer
    // may carry vertices no triangle references and those must not size the field either.
    out.indices.clear();
    out.indices.reserve(size_t(triangle_count) * 3);
    out.bounds.reset();
    for(uint32_t t = 0; t < triangle_count; ++t)
    {
        const uint32_t i0 = indices[t * 3 + 0];
        const uint32_t i1 = indices[t * 3 + 1];
        const uint32_t i2 = indices[t * 3 + 2];
        if(i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count ||
           carries_no_surface(out.positions[i0], out.positions[i1], out.positions[i2]))
        {
            ++out.discarded_triangles;
            continue;
        }
        out.indices.insert(out.indices.end(), {i0, i1, i2});
        out.bounds.add_point(out.positions[i0]);
        out.bounds.add_point(out.positions[i1]);
        out.bounds.add_point(out.positions[i2]);
    }
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
        // Resolved as a whole triangle before anything is emitted, because whether a corner may
        // contribute to the bounds depends on the other two: a corner of a sliver must not widen
        // the field even though the corner itself is a perfectly ordinary position.
        //
        // This unpacks a shared vertex once per incident face rather than once overall. The bake is
        // dominated by voxel work by a wide margin (test_bake_cost_is_dominated_by_voxels_not_triangles),
        // and test_submesh_bake_pass_cost_is_linear guards the pass staying O(model).
        std::array<uint32_t, 3> source{0u, 0u, 0u};
        std::array<math::vec3, 3> corners{};
        bool addressable = true;
        for(uint32_t corner = 0; corner < 3 && addressable; ++corner)
        {
            const uint32_t source_index = get_corner(face, corner);
            if(source_index >= data.vertex_count)
            {
                addressable = false;
                break;
            }
            source[corner] = source_index;
            float unpacked[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            gfx::vertex_unpack(unpacked,
                               gfx::attribute::Position,
                               data.vertex_format,
                               data.vertex_data.data(),
                               source_index);
            corners[corner] = math::vec3(unpacked[0], unpacked[1], unpacked[2]);
        }
        if(!addressable || carries_no_surface(corners[0], corners[1], corners[2]))
        {
            ++out.discarded_triangles;
            continue;
        }
        for(uint32_t corner = 0; corner < 3; ++corner)
        {
            const auto inserted = remap.emplace(source[corner], uint32_t(out.positions.size()));
            if(inserted.second)
            {
                out.positions.push_back(corners[corner]);
                out.bounds.add_point(corners[corner]);
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
