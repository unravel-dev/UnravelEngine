/*
 * Validation suite for baking a skin into mesh load data (mesh::apply_skin_to_load_data).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "mesh skinning"
 *
 * Pins:
 *   - a submesh whose bones fit one draw keeps its faces, its vertices and one palette of its bones, ascending;
 *   - a submesh reaching more bones than one draw takes is split into pieces whose palettes fit, every corner of a
 *     piece still names its own bone through the piece's palette, the pieces share the source's data group, flags
 *     and stable id, a seam vertex is copied once per piece, and the armature nodes and the submeshes after it follow;
 *   - arrays indexed by submesh stay in step with a split, and LODs built before it are dropped;
 *   - a triangle corner past the vertices is refused and leaves the data as it was;
 *   - a bone weight on a vertex past the vertices is skipped.
 *
 * Headless safe: only load data is built, nothing reaches the GPU.
 */

#include "../tests.h"

#include <engine/rendering/mesh.h>

#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>

#include <math/math.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

constexpr float ROW_HEIGHT = 0.1f;
constexpr float HALF_WIDTH = 0.1f;
constexpr float PROP_OFFSET = 5.0f;
constexpr uint32_t STRIP_STABLE_ID = 0x1234u;
constexpr uint32_t PROP_STABLE_ID = 0x5678u;
constexpr uint32_t PROP_DATA_GROUP = 1;
constexpr uint32_t PROP_VERTEX_COUNT = 3;

void set_position(mesh::load_data& data, uint32_t vertex, const math::vec3& position)
{
    const float values[4] = {position.x, position.y, position.z, 1.0f};
    bgfx::vertexPack(values, false, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), vertex);
}

auto make_node(const std::string& name, std::vector<uint32_t> submeshes) -> std::unique_ptr<mesh::armature_node>
{
    auto node = std::make_unique<mesh::armature_node>();
    node->name = name;
    node->submeshes = std::move(submeshes);
    return node;
}

/**
 * @brief A strip of @p bone_count rows of two vertices, each row weighted fully to its own bone, as skinned submesh 0
 * on node "Strip"; then a static triangle as submesh 1 on node "Prop".
 */
auto make_strip(uint32_t bone_count) -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    const uint32_t strip_vertex_count = bone_count * 2;
    data.vertex_count = strip_vertex_count + PROP_VERTEX_COUNT;
    data.vertex_data.resize(size_t(data.vertex_count) * data.vertex_format.getStride());
    for(uint32_t row = 0; row < bone_count; ++row)
    {
        const float height = float(row) * ROW_HEIGHT;
        set_position(data, row * 2, math::vec3(-HALF_WIDTH, height, 0.0f));
        set_position(data, row * 2 + 1, math::vec3(HALF_WIDTH, height, 0.0f));
        skin_bind_data::bone_influence bone;
        bone.bone_id = "Joint" + std::to_string(row);
        bone.influences = {{row * 2, 1.0f}, {row * 2 + 1, 1.0f}};
        data.skin_data.add_bone(bone);
    }
    for(uint32_t row = 0; row + 1 < bone_count; ++row)
    {
        const uint32_t first = row * 2;
        data.triangle_data.push_back(mesh::triangle{0, {first, first + 1, first + 2}});
        data.triangle_data.push_back(mesh::triangle{0, {first + 1, first + 3, first + 2}});
    }
    mesh::submesh strip;
    strip.vertex_start = 0;
    strip.vertex_count = strip_vertex_count;
    strip.face_start = 0;
    strip.face_count = uint32_t(data.triangle_data.size());
    strip.skinned = true;
    strip.stable_id = STRIP_STABLE_ID;
    for(uint32_t corner = 0; corner < PROP_VERTEX_COUNT; ++corner)
    {
        set_position(data, strip_vertex_count + corner, math::vec3(PROP_OFFSET + float(corner), 0.0f, 0.0f));
    }
    data.triangle_data.push_back(
        mesh::triangle{PROP_DATA_GROUP, {strip_vertex_count, strip_vertex_count + 1, strip_vertex_count + 2}});
    mesh::submesh prop;
    prop.data_group_id = PROP_DATA_GROUP;
    prop.vertex_start = int32_t(strip_vertex_count);
    prop.vertex_count = PROP_VERTEX_COUNT;
    prop.face_start = int32_t(strip.face_count);
    prop.face_count = 1;
    prop.stable_id = PROP_STABLE_ID;
    data.submeshes = {strip, prop};
    data.triangle_count = uint32_t(data.triangle_data.size());
    data.material_count = PROP_DATA_GROUP + 1;
    data.root_node = make_node("Root", {});
    data.root_node->children.push_back(make_node("Strip", {0}));
    data.root_node->children.push_back(make_node("Prop", {1}));
    return data;
}

/// The row a strip vertex was made for, from its height.
auto get_vertex_row(const mesh::load_data& data, uint32_t vertex) -> uint32_t
{
    float position[4] = {};
    bgfx::vertexUnpack(position, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), vertex);
    return uint32_t(std::lround(position[1] / ROW_HEIGHT));
}

/// The bone a vertex's first blend index names through @p palette; -1 when the index is past it.
auto get_vertex_bone(const mesh::load_data& data, uint32_t vertex, const std::vector<uint32_t>& palette) -> int64_t
{
    float indices[4] = {};
    bgfx::vertexUnpack(indices, bgfx::Attrib::Indices, data.vertex_format, data.vertex_data.data(), vertex);
    const auto entry = size_t(indices[0]);
    return entry < palette.size() ? int64_t(palette[entry]) : -1;
}

/// Whether every corner of every skinned submesh names its own row's bone through the submesh's palette.
auto are_corners_skinned_to_their_rows(const mesh::load_data& data) -> bool
{
    for(size_t submesh_index = 0; submesh_index < data.submeshes.size(); ++submesh_index)
    {
        const auto& submesh = data.submeshes[submesh_index];
        if(!submesh.skinned)
        {
            continue;
        }
        const auto& palette = data.bone_palette_bones[submesh_index];
        for(uint32_t face = uint32_t(submesh.face_start); face < uint32_t(submesh.face_start) + submesh.face_count; ++face)
        {
            for(uint32_t vertex : data.triangle_data[face].indices)
            {
                if(get_vertex_bone(data, vertex, palette) != int64_t(get_vertex_row(data, vertex)))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

auto are_ascending_from_zero(const std::vector<uint32_t>& bones, uint32_t count) -> bool
{
    if(bones.size() != count)
    {
        return false;
    }
    for(uint32_t i = 0; i < count; ++i)
    {
        if(bones[i] != i)
        {
            return false;
        }
    }
    return true;
}

void test_fitting_skin_keeps_its_submesh()
{
    std::printf("test_fitting_skin_keeps_its_submesh\n");
    constexpr uint32_t bone_count = 100;
    auto data = make_strip(bone_count);
    const auto faces_before = data.triangle_data;
    const uint32_t vertex_count_before = data.vertex_count;
    check(mesh::apply_skin_to_load_data(data), "a fitting skin bakes");
    check(data.skin_is_prepared, "and is marked prepared");
    check(data.submeshes.size() == 2 && data.bone_palette_bones.size() == 2, "no submesh is split");
    if(data.bone_palette_bones.size() != 2)
    {
        return;
    }
    check(are_ascending_from_zero(data.bone_palette_bones[0], bone_count), "the palette holds every bone, ascending");
    check(data.bone_palette_bones[1].empty(), "the static submesh has an empty palette");
    check(data.vertex_count == vertex_count_before, "no vertex is copied");
    bool are_faces_unchanged = data.triangle_data.size() == faces_before.size();
    for(size_t i = 0; are_faces_unchanged && i < faces_before.size(); ++i)
    {
        are_faces_unchanged = data.triangle_data[i].indices == faces_before[i].indices;
    }
    check(are_faces_unchanged, "the faces keep their order and corners");
    check(are_corners_skinned_to_their_rows(data), "every corner names its bone");
    check(data.root_node->children[0]->submeshes == std::vector<uint32_t>{0} &&
              data.root_node->children[1]->submeshes == std::vector<uint32_t>{1},
          "the armature nodes keep their submeshes");
}

void test_large_skin_splits_into_fitting_pieces()
{
    std::printf("test_large_skin_splits_into_fitting_pieces\n");
    constexpr uint32_t bone_count = 300;
    // Rows 0-127 fill the first palette, the rows from the seam at 127 the next, and so on.
    constexpr uint32_t expected_piece_count = 3;
    auto data = make_strip(bone_count);
    const uint32_t vertex_count_before = data.vertex_count;
    const uint32_t face_count_before = data.triangle_count;
    check(mesh::apply_skin_to_load_data(data), "a large skin bakes");
    check(data.submeshes.size() == expected_piece_count + 1, "the strip is drawn in three pieces");
    if(data.submeshes.size() != expected_piece_count + 1 || data.bone_palette_bones.size() != data.submeshes.size())
    {
        check(false, "one palette per submesh");
        return;
    }
    const uint32_t palette_size = gfx::get_max_blend_transforms();
    uint32_t piece_face_count = 0;
    for(uint32_t piece = 0; piece < expected_piece_count; ++piece)
    {
        const auto& submesh = data.submeshes[piece];
        check(data.bone_palette_bones[piece].size() <= palette_size, "piece " + std::to_string(piece) + " fits one draw");
        check(submesh.skinned && submesh.data_group_id == 0 && submesh.stable_id == STRIP_STABLE_ID,
              "piece " + std::to_string(piece) + " shares the strip's group, flag and stable id");
        check(submesh.face_start == int32_t(piece_face_count), "piece " + std::to_string(piece) + " follows the last");
        piece_face_count += submesh.face_count;
    }
    check(piece_face_count + 1 == face_count_before, "the pieces hold every strip face");
    const auto& prop = data.submeshes[expected_piece_count];
    check(prop.stable_id == PROP_STABLE_ID && prop.data_group_id == PROP_DATA_GROUP && !prop.skinned,
          "the static submesh follows the pieces");
    check(data.bone_palette_bones[expected_piece_count].empty(), "and keeps an empty palette");
    check(are_corners_skinned_to_their_rows(data), "every corner names its bone through its piece's palette");
    // Each of the two seams copies its row's two vertices once, into the later piece.
    check(data.vertex_count == vertex_count_before + 2 * (expected_piece_count - 1), "a seam vertex is copied once");
    check(data.root_node->children[0]->submeshes == std::vector<uint32_t>{0, 1, 2}, "the strip node holds every piece");
    check(data.root_node->children[1]->submeshes == std::vector<uint32_t>{expected_piece_count},
          "the prop node follows its submesh");
}

void test_split_keeps_per_submesh_arrays_in_step()
{
    std::printf("test_split_keeps_per_submesh_arrays_in_step\n");
    auto data = make_strip(300);
    data.submesh_cards.resize(2);
    data.submesh_cards[1].is_mostly_two_sided = true;
    data.submesh_sdfs.resize(2);
    data.lods.resize(1);
    check(mesh::apply_skin_to_load_data(data), "the skin bakes");
    check(data.submesh_cards.size() == data.submeshes.size() && data.submesh_sdfs.size() == data.submeshes.size(),
          "the arrays indexed by submesh grow with the pieces");
    if(data.submesh_cards.size() == data.submeshes.size())
    {
        check(data.submesh_cards.back().is_mostly_two_sided && !data.submesh_cards.front().is_mostly_two_sided,
              "the static submesh keeps its own entry");
    }
    check(data.submesh_sdf_coarse_mips.empty(), "an empty array stays empty");
    check(data.lods.empty(), "LODs built before the split are dropped");
}

void test_corner_past_the_vertices_is_refused()
{
    std::printf("test_corner_past_the_vertices_is_refused\n");
    auto data = make_strip(10);
    data.triangle_data[0].indices[2] = data.vertex_count + 5;
    check(!mesh::apply_skin_to_load_data(data), "a corner past the vertices is refused");
    check(!data.skin_is_prepared && data.bone_palette_bones.empty() && data.submeshes.size() == 2,
          "and the data is left as it was");
}

void test_weight_past_the_vertices_is_skipped()
{
    std::printf("test_weight_past_the_vertices_is_skipped\n");
    auto data = make_strip(10);
    data.skin_data.get_bones()[0].influences.push_back({data.vertex_count + 100, 1.0f});
    check(mesh::apply_skin_to_load_data(data), "a weight past the vertices does not stop the bake");
    check(are_corners_skinned_to_their_rows(data), "the other weights still skin their vertices");
}

auto run_mesh_skinning_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_fitting_skin_keeps_its_submesh();
    test_large_skin_splits_into_fitting_pieces();
    test_split_keeps_per_submesh_arrays_in_step();
    test_corner_past_the_vertices_is_refused();
    test_weight_past_the_vertices_is_skipped();
    std::printf("  %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("mesh skinning", run_mesh_skinning_suite)
