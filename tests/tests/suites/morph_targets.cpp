/**
 * Validation suite: morph targets (blend shapes), from import to the deformed vertices.
 *
 * Run:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "morph targets"
 *
 * The glTF files are written by the suite (no third-party content): a closed mouth whose lips meet at rest and part
 * in a target, so welding must keep them apart, with a weights animation.
 */

#include "../tests.h"

#include <asset_compiler/importers/mesh/mesh_import_morphs.h>
#include <asset_compiler/importers/mesh_importer.h>
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/meta/ecs/components/model_component.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/rendering/mesh.h>
#include <engine/rendering/morph_targets.h>
#include <graphics/vertex_decl.h>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel::test;
using namespace unravel;

namespace
{

// ---------------------------------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------------------------------

auto make_delta(uint32_t vertex, const math::vec3& position, const math::vec3& normal = math::vec3(0.0f))
    -> morph_target_builder::vertex_delta
{
    return {vertex, position, normal};
}

void test_builder_merges_targets_by_name()
{
    morph_target_builder builder;
    builder.add("Blink", 0.0f, {make_delta(4, {0.0f, -0.01f, 0.0f}), make_delta(2, {0.0f, -0.02f, 0.0f})});
    builder.add("Smile", 0.25f, {make_delta(7, {0.01f, 0.0f, 0.0f})});
    // The eyelashes of a file are another mesh: their Blink is the face's.
    builder.add("Blink", 1.0f, {make_delta(9, {0.0f, -0.03f, 0.0f})});
    const auto data = builder.build(0.00001f, 0.001f);
    check_eq(data.targets.size(), 2, "two names make two targets");
    check(data.find_target("Blink") == 0 && data.find_target("Smile") == 1 && data.find_target("Frown") == -1,
          "targets are found by name");
    check_near(data.targets[0].default_weight, 0.0f, 0.0f, "a merged target keeps its first default weight");
    check_near(data.targets[1].default_weight, 0.25f, 0.0f, "a target keeps its default weight");
    check(data.morphed_vertices == std::vector<uint32_t>{2, 4, 7, 9}, "the morphed vertices are every moved vertex, ascending");
    check_eq(data.targets[0].delta_count, 3, "Blink moves the face's and the eyelashes' vertices");
    check_near(data.targets[0].max_displacement, 0.03f, 1e-6f, "the largest displacement is recorded");
    bool sorted = true;
    for(uint32_t i = data.targets[0].first_delta + 1; i < data.targets[0].first_delta + data.targets[0].delta_count; ++i)
    {
        sorted &= data.deltas[i - 1].slot < data.deltas[i].slot;
    }
    check(sorted, "a target's deltas are sorted by slot");
}

void test_builder_thresholds()
{
    morph_target_builder builder;
    builder.add("Tiny", 0.0f,
                {make_delta(0, {0.000001f, 0.0f, 0.0f}), make_delta(1, {0.0f, 0.0f, 0.0f}, {0.0f, 0.01f, 0.0f}),
                 make_delta(2, {0.0f, 0.02f, 0.0f})});
    const auto data = builder.build(0.00001f, 0.001f);
    check(data.morphed_vertices == std::vector<uint32_t>{1, 2},
          "a vertex moved under the threshold without turning is left out; a turned normal is kept");
    morph_target_builder empty_target;
    empty_target.add("Nothing", 0.0f, {make_delta(3, {0.0000001f, 0.0f, 0.0f})});
    const auto empty = empty_target.build(0.00001f, 0.001f);
    check(empty.targets.size() == 1 && empty.targets[0].delta_count == 0,
          "a target that moves nothing is kept by name with no deltas");
}

void test_normal_packing()
{
    float worst = 0.0f;
    for(const auto& delta : {math::vec3(0.0f), math::vec3(2.0f, -2.0f, 0.5f), math::vec3(-0.3f, 0.71f, -1.99f),
                             math::vec3(0.001f, -0.001f, 0.0f)})
    {
        const auto unpacked = unpack_morph_normal(pack_morph_normal(delta));
        worst = std::max(worst, math::length(unpacked - delta));
    }
    check(worst < 0.004f, "normal deltas round-trip within a 10-bit step (" + std::to_string(worst) + ")");
}

void test_vertex_copies_share_deltas()
{
    morph_target_builder builder;
    builder.add("Open", 0.0f, {make_delta(1, {0.0f, 0.1f, 0.0f}), make_delta(3, {0.0f, 0.2f, 0.0f})});
    auto data = builder.build(0.0f, 0.0f);
    add_morph_vertex_copies(data, {{10, 1}, {11, 2}});
    check(data.morphed_vertices == std::vector<uint32_t>{1, 3, 10}, "a copy of a morphed vertex is morphed; a copy of another is not");
    const auto displacements = get_morph_vertex_displacements(data, 12);
    check_near(displacements[10], displacements[1], 0.0f, "the copy moves as its original");
}

/// A load_data of four vertices: submesh 0 draws (0, 1, 2) with bone A, submesh 1 draws (1, 2, 3) with bones A and B,
/// so the skin bake copies vertices 1 and 2 for submesh 1's palette.
auto make_two_palette_mesh() -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    data.vertex_count = 4;
    data.vertex_data.resize(size_t(data.vertex_count) * data.vertex_format.getStride());
    for(uint32_t v = 0; v < 4; ++v)
    {
        const float position[4] = {float(v), 0.0f, 0.0f, 1.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), v);
    }
    skin_bind_data::bone_influence bone_a;
    bone_a.bone_id = "A";
    bone_a.influences = {{0, 1.0f}, {1, 1.0f}, {2, 1.0f}};
    skin_bind_data::bone_influence bone_b;
    bone_b.bone_id = "B";
    bone_b.influences = {{3, 1.0f}};
    data.skin_data.add_bone(bone_a);
    data.skin_data.add_bone(bone_b);
    data.triangle_data = {mesh::triangle{0, {0, 1, 2}}, mesh::triangle{1, {1, 2, 3}}};
    data.triangle_count = 2;
    mesh::submesh first;
    first.vertex_start = 0;
    first.vertex_count = 3;
    first.face_start = 0;
    first.face_count = 1;
    first.skinned = true;
    first.stable_id = 1;
    mesh::submesh second = first;
    second.data_group_id = 1;
    second.vertex_start = 1;
    second.face_start = 1;
    second.stable_id = 2;
    data.submeshes = {first, second};
    data.material_count = 2;
    morph_target_builder builder;
    builder.add("Bulge", 0.0f, {make_delta(1, {0.0f, 0.5f, 0.0f})});
    data.morph_targets = builder.build(0.0f, 0.0f);
    return data;
}

void test_skin_split_copies_morphs()
{
    auto data = make_two_palette_mesh();
    check(mesh::apply_skin_to_load_data(data), "the skin bakes");
    check(data.vertex_count > 4, "submesh 1 draws copies of the shared vertices");
    uint32_t copy_of_bulged = 0;
    for(const auto& triangle : data.triangle_data)
    {
        if(triangle.data_group_id == 1 && triangle.indices[0] >= 4)
        {
            copy_of_bulged = triangle.indices[0];
        }
    }
    const auto displacements = get_morph_vertex_displacements(data.morph_targets, data.vertex_count);
    check(copy_of_bulged >= 4 && displacements[copy_of_bulged] > 0.49f,
          "the copy of a morphed vertex that submesh 1 draws moves with its original");
    check_near(displacements[1], 0.5f, 1e-6f, "the original still moves");
}

void test_cpu_deform_matches_reference()
{
    const auto& layout = gfx::mesh_vertex::get_layout();
    std::vector<uint8_t> vertices(size_t(3) * layout.getStride());
    for(uint32_t v = 0; v < 3; ++v)
    {
        const float position[4] = {float(v), 1.0f, 0.0f, 1.0f};
        const float normal[4] = {0.0f, 0.0f, 1.0f, 0.0f};
        const float tangent[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        const float bitangent[4] = {0.0f, -1.0f, 0.0f, 0.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, layout, vertices.data(), v);
        bgfx::vertexPack(normal, true, bgfx::Attrib::Normal, layout, vertices.data(), v);
        bgfx::vertexPack(tangent, true, bgfx::Attrib::Tangent, layout, vertices.data(), v);
        bgfx::vertexPack(bitangent, true, bgfx::Attrib::Bitangent, layout, vertices.data(), v);
    }
    morph_target_builder builder;
    builder.add("Lift", 0.0f, {make_delta(1, {0.0f, 0.5f, 0.0f}, {0.0f, 0.5f, 0.0f})});
    builder.add("Push", 0.0f, {make_delta(1, {0.0f, 0.0f, 0.25f}), make_delta(2, {0.1f, 0.0f, 0.0f})});
    const auto data = builder.build(0.0f, 0.0f);
    const auto& deform_layout = get_morph_deform_layout();
    check_eq(deform_layout.getStride(), 24, "a deformed vertex is 24 bytes");
    std::vector<uint8_t> deformed(size_t(3) * deform_layout.getStride());
    write_rest_vertices(layout, vertices.data(), 3, deformed.data());
    const std::array<float, 2> weights{2.0f, -1.0f};
    std::vector<float> deltas;
    accumulate_morph_deltas(data, weights, deltas);
    write_morphed_vertices(data, layout, vertices.data(), deltas, deformed.data());
    float position[4] = {};
    bgfx::vertexUnpack(position, bgfx::Attrib::Position, deform_layout, deformed.data(), 1);
    check(position[0] == 1.0f && position[1] == 2.0f && position[2] == -0.25f,
          "a vertex moves by the weighted sum of its targets, weights past 1 and below 0 included");
    bgfx::vertexUnpack(position, bgfx::Attrib::Position, deform_layout, deformed.data(), 0);
    check(position[0] == 0.0f && position[1] == 1.0f, "a vertex no target moves keeps its rest position");
    float normal[4] = {};
    bgfx::vertexUnpack(normal, bgfx::Attrib::Normal, deform_layout, deformed.data(), 1);
    const math::vec3 expected = math::normalize(math::vec3(0.0f, 1.0f, 1.0f));
    check(math::length(math::vec3(normal[0], normal[1], normal[2]) - expected) < 0.02f,
          "the normal turns by the weighted normal delta, renormalized (within an 8-bit step)");
    float tangent[4] = {};
    float bitangent[4] = {};
    bgfx::vertexUnpack(tangent, bgfx::Attrib::Tangent, deform_layout, deformed.data(), 1);
    bgfx::vertexUnpack(bitangent, bgfx::Attrib::Bitangent, deform_layout, deformed.data(), 1);
    const math::vec3 t(tangent[0], tangent[1], tangent[2]);
    const math::vec3 b(bitangent[0], bitangent[1], bitangent[2]);
    check(std::abs(math::dot(t, expected)) < 0.02f && math::dot(b, math::vec3(0.0f, -1.0f, 0.0f)) > 0.6f,
          "the tangent stays orthogonal to the turned normal and the bitangent keeps its handedness");
    const std::array<float, 2> zero{0.0f, 0.0f};
    accumulate_morph_deltas(data, zero, deltas);
    write_morphed_vertices(data, layout, vertices.data(), deltas, deformed.data());
    std::vector<uint8_t> rest(deformed.size());
    write_rest_vertices(layout, vertices.data(), 3, rest.data());
    check(deformed == rest, "zero weights write the rest shape, byte for byte");

    const std::array<float, 2> not_finite{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    accumulate_morph_deltas(data, not_finite, deltas);
    check(std::all_of(deltas.begin(), deltas.end(), [](float value) { return value == 0.0f; }),
          "weights that are not finite numbers move nothing");
}

void test_morph_data_round_trips(const fs::path& folder)
{
    auto data = make_two_palette_mesh();
    const fs::path file = folder / "round_trip.bin";
    save_to_file_bin(file.string(), data);
    mesh::load_data loaded;
    load_from_file_bin(file.string(), loaded);
    fs::error_code err;
    fs::remove(file, err);
    const auto& a = data.morph_targets;
    const auto& b = loaded.morph_targets;
    bool same = a.targets.size() == b.targets.size() && a.morphed_vertices == b.morphed_vertices &&
                a.deltas.size() == b.deltas.size();
    for(size_t i = 0; same && i < a.deltas.size(); ++i)
    {
        same = a.deltas[i].slot == b.deltas[i].slot && a.deltas[i].position == b.deltas[i].position &&
               a.deltas[i].normal == b.deltas[i].normal;
    }
    for(size_t i = 0; same && i < a.targets.size(); ++i)
    {
        same = a.targets[i].name == b.targets[i].name && a.targets[i].first_delta == b.targets[i].first_delta &&
               a.targets[i].delta_count == b.targets[i].delta_count;
    }
    check(same, "a compiled mesh keeps its morph targets through the binary document");
}

// ---------------------------------------------------------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------------------------------------------------------

auto make_face_targets() -> morph_target_data
{
    morph_target_builder builder;
    builder.add("Open", 0.0f, {make_delta(0, {0.0f, 0.1f, 0.0f})});
    builder.add("Smile", 0.25f, {make_delta(1, {0.1f, 0.0f, 0.0f})});
    builder.add("Blink", 0.0f, {make_delta(2, {0.0f, -0.1f, 0.0f})});
    return builder.build(0.0f, 0.0f);
}

void test_model_weights()
{
    model_component model;
    const auto targets = make_face_targets();
    std::vector<float> weights;
    model.resolve_morph_weights(targets, weights);
    check(weights == std::vector<float>{0.0f, 0.25f, 0.0f}, "a model that sets no weight has the mesh's defaults");
    const auto version = model.get_morph_weights_version();
    model.set_morph_weight("Open", 1.5f);
    model.set_morph_weight("Smile", -0.5f);
    check(model.get_morph_weights_version() > version, "setting a weight changes the version the deform pass reads");
    model.resolve_morph_weights(targets, weights);
    check(weights == std::vector<float>{1.5f, -0.5f, 0.0f}, "weights past 1 and below 0 are kept, unclamped");
    const auto unchanged = model.get_morph_weights_version();
    model.set_morph_weight("Open", 1.5f);
    check(model.get_morph_weights_version() == unchanged, "setting a weight to what it is changes nothing");
    model.clear_morph_weight("Smile");
    model.resolve_morph_weights(targets, weights);
    check(weights[1] == 0.25f, "a cleared weight goes back to the mesh's default");
    model.set_morph_weight("Gone", 1.0f);
    model.resolve_morph_weights(targets, weights);
    check(weights.size() == 3, "a weight for a target the mesh lacks is kept by name but moves nothing");
    model.reset_morph_weights();
    check(model.get_morph_weights().empty() && !model.find_morph_weight("Open"), "Reset All clears every weight");
}

void test_model_weights_ignore_non_finite()
{
    model_component model;
    model.set_morph_weight("Open", 0.5f);
    const auto version = model.get_morph_weights_version();
    model.set_morph_weight("Open", std::numeric_limits<float>::quiet_NaN());
    model.set_morph_weight("Open", std::numeric_limits<float>::infinity());
    model.set_morph_weight("Smile", -std::numeric_limits<float>::infinity());
    check(model.find_morph_weight("Open") == 0.5f && !model.find_morph_weight("Smile") &&
              model.get_morph_weights_version() == version,
          "setting a weight that is not a finite number changes nothing");
    model.set_morph_weights({{"Open", 1.0f}, {"Smile", std::numeric_limits<float>::quiet_NaN()},
                             {"Blink", std::numeric_limits<float>::infinity()}});
    check(model.get_morph_weights() == std::vector<morph_weight>{{"Open", 1.0f}},
          "replacing the weights leaves out the entries that are not finite numbers");
}

void test_weight_versions_are_unique_across_models()
{
    model_component first;
    model_component second;
    first.set_morph_weight("Open", 1.0f);
    second.set_morph_weight("Open", 1.0f);
    check(first.get_morph_weights_version() != second.get_morph_weights_version(),
          "two models setting the same weight get different versions, so streams kept for one never match the other");
}

void test_model_weights_readable_round_trip()
{
    model_component saved;
    saved.set_morph_weights({{"Open", 1.0f}, {"Smile", -0.25f}});
    std::stringstream stream;
    {
        auto archive = ser20::create_oarchive_associative(stream);
        try_save(archive, ser20::make_nvp("model", saved));
    }
    model_component loaded;
    loaded.set_morph_weight("Blink", 0.7f);
    {
        auto archive = ser20::create_iarchive_associative(stream);
        try_load(archive, ser20::make_nvp("model", loaded));
    }
    auto weights = loaded.get_morph_weights();
    std::sort(weights.begin(),
              weights.end(),
              [](const morph_weight& lhs, const morph_weight& rhs)
              {
                  return lhs.name < rhs.name;
              });
    check(weights == std::vector<morph_weight>{{"Open", 1.0f}, {"Smile", -0.25f}},
          "a model's weights survive a readable save and load, and only they");
    check(stream.str().find("\"Smile\"") != std::string::npos, "the readable form keys each weight by its target's name");
}

/// @p model's weights after saving @p saved_weights in a model and loading that into @p model under @p filter.
auto load_weights(const std::vector<morph_weight>& saved_weights,
                  std::vector<morph_weight> current_weights,
                  const std::function<bool(const std::string&)>& filter) -> std::vector<morph_weight>
{
    model_component saved;
    saved.set_morph_weights(saved_weights);
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    serialization::write_binary(stream, [&](auto& ar) { return try_save(ar, ser20::make_nvp("model", saved)); });
    model_component loaded;
    loaded.set_morph_weights(current_weights);
    serialization::path_context context;
    serialization::path_context* previous = serialization::get_path_context();
    if(filter)
    {
        context.should_serialize_property_callback = filter;
        context.enable_recording();
        serialization::set_path_context(&context);
    }
    serialization::read_binary(stream,
                               [&](auto& ar)
                               {
                                   return try_load(ar, ser20::make_nvp("model", loaded));
                               });
    serialization::set_path_context(previous);
    auto weights = loaded.get_morph_weights();
    std::sort(weights.begin(),
              weights.end(),
              [](const morph_weight& lhs, const morph_weight& rhs)
              {
                  return lhs.name < rhs.name;
              });
    return weights;
}

void test_model_weights_save_by_name()
{
    const std::vector<morph_weight> saved{{"Open", 1.0f}, {"Smile", 0.5f}};
    const auto full = load_weights(saved, {{"Blink", 0.7f}}, {});
    check(full == std::vector<morph_weight>{{"Open", 1.0f}, {"Smile", 0.5f}}, "a model loads the weights it saved, and only them");
    // A prefab instance overriding Open and Blink: Open takes the saved value, Blink (not saved) reverts, Smile stays.
    const auto merged = load_weights(saved,
                                     {{"Open", 0.2f}, {"Blink", 0.7f}, {"Frown", 0.3f}},
                                     [](const std::string& path)
                                     {
                                         return path == "model" || path == "model/morph_weights/Open" ||
                                                path == "model/morph_weights/Blink";
                                     });
    check(merged == std::vector<morph_weight>{{"Frown", 0.3f}, {"Open", 1.0f}},
          "an override load sets and reverts the weights it lists by name and keeps the rest");
}

// ---------------------------------------------------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------------------------------------------------

auto base64(const std::vector<uint8_t>& bytes) -> std::string
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t chunk = (uint32_t(bytes[i]) << 16) | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0u) |
                               (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0u);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[chunk & 63] : '=';
    }
    return out;
}

/// Appends @p values to @p buffer and returns the byte offset they start at.
template<typename T>
auto append(std::vector<uint8_t>& buffer, const std::vector<T>& values) -> size_t
{
    while(buffer.size() % 4 != 0)
    {
        buffer.push_back(0);
    }
    const size_t offset = buffer.size();
    buffer.resize(offset + values.size() * sizeof(T));
    std::memcpy(buffer.data() + offset, values.data(), values.size() * sizeof(T));
    return offset;
}

/**
 * @brief A glTF mouth: a lower and an upper lip quad meeting along y = 0 with the same positions, normals and UVs
 *        there, so welding would join them. Target "Open" lifts the upper lip by 0.1; "Smile" (default weight 0.25)
 *        lowers the lower lip's corners by 0.05. Animation "Talk" keys both weights from 0 at 0 s to 1 and 0.5 at 1 s.
 */
auto make_mouth_gltf() -> std::string
{
    const std::vector<float> positions{-1, -1, 0, 1, -1, 0, 1, 0, 0, -1, 0, 0, -1, 0, 0, 1, 0, 0, 1, 1, 0, -1, 1, 0};
    std::vector<float> normals;
    for(int v = 0; v < 8; ++v)
    {
        normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
    }
    const std::vector<float> uvs{0, 0, 1, 0, 1, 0.5f, 0, 0.5f, 0, 0.5f, 1, 0.5f, 1, 1, 0, 1};
    const std::vector<uint16_t> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    std::vector<float> open(24, 0.0f);
    for(int v = 4; v < 8; ++v)
    {
        open[size_t(v) * 3 + 1] = 0.1f;
    }
    std::vector<float> smile(24, 0.0f);
    smile[1] = -0.05f;
    smile[4] = -0.05f;
    const std::vector<float> times{0.0f, 1.0f};
    const std::vector<float> weights{0.0f, 0.0f, 1.0f, 0.5f};
    std::vector<uint8_t> buffer;
    const size_t p = append(buffer, positions);
    const size_t n = append(buffer, normals);
    const size_t t = append(buffer, uvs);
    const size_t i = append(buffer, indices);
    const size_t o = append(buffer, open);
    const size_t s = append(buffer, smile);
    const size_t k = append(buffer, times);
    const size_t w = append(buffer, weights);
    auto view = [](size_t offset, size_t length)
    {
        return "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(length) + "}";
    };
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
    json += "\"nodes\":[{\"name\":\"Face\",\"mesh\":0}],";
    json += "\"meshes\":[{\"name\":\"Mouth\",\"weights\":[0,0.25],\"extras\":{\"targetNames\":[\"Open\",\"Smile\"]},";
    json += "\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,";
    json += "\"targets\":[{\"POSITION\":4},{\"POSITION\":5}]}]}],";
    json += "\"animations\":[{\"name\":\"Talk\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}}],";
    json += "\"samplers\":[{\"input\":6,\"output\":7,\"interpolation\":\"LINEAR\"}]}],";
    json += "\"accessors\":[";
    json += "{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\",\"min\":[-1,-1,0],\"max\":[1,1,0]},";
    json += "{\"bufferView\":1,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"},";
    json += "{\"bufferView\":2,\"componentType\":5126,\"count\":8,\"type\":\"VEC2\"},";
    json += "{\"bufferView\":3,\"componentType\":5123,\"count\":12,\"type\":\"SCALAR\"},";
    json += "{\"bufferView\":4,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0,0.1,0]},";
    json += "{\"bufferView\":5,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\",\"min\":[0,-0.05,0],\"max\":[0,0,0]},";
    json += "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},";
    json += "{\"bufferView\":7,\"componentType\":5126,\"count\":4,\"type\":\"SCALAR\"}],";
    json += "\"bufferViews\":[" + view(p, positions.size() * 4) + "," + view(n, normals.size() * 4) + "," +
            view(t, uvs.size() * 4) + "," + view(i, indices.size() * 2) + "," + view(o, open.size() * 4) + "," +
            view(s, smile.size() * 4) + "," + view(k, times.size() * 4) + "," + view(w, weights.size() * 4) + "],";
    json += "\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size()) +
            ",\"uri\":\"data:application/octet-stream;base64," + base64(buffer) + "\"}]}";
    return json;
}

struct imported_mouth
{
    bool loaded{false};
    mesh::load_data data;
    std::vector<animation_clip> clips;
};

auto import_mouth(asset_manager& am, const fs::path& folder, const mesh_importer_meta& import_meta) -> imported_mouth
{
    const fs::path file = folder / "Mouth.gltf";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << make_mouth_gltf();
    }
    imported_mouth result;
    std::vector<importer::imported_material> materials;
    std::vector<importer::imported_texture> textures;
    result.data.vertex_format = gfx::mesh_vertex::get_layout();
    result.loaded = importer::load_mesh_data_from_file(am, file, import_meta, result.data, result.clips, materials, textures);
    return result;
}

auto find_curve(const animation_clip& clip, const std::string& name) -> const animation_curve*
{
    for(const auto& curve : clip.curves)
    {
        if(curve.name == name)
        {
            return &curve;
        }
    }
    return nullptr;
}

void test_import_gltf_mouth(asset_manager& am, const fs::path& folder)
{
    mesh_importer_meta import_meta;
    const auto mouth = import_mouth(am, folder, import_meta);
    check(mouth.loaded, "the mouth imports");
    const auto& morphs = mouth.data.morph_targets;
    check_eq(mouth.data.vertex_count, 8, "welding keeps the lips apart where only a target parts them");
    check_eq(mouth.data.uv_set_count, 1, "the morph signature leaves no UV set behind");
    check(morphs.targets.size() == 2 && morphs.find_target("Open") == 0 && morphs.find_target("Smile") == 1,
          "the targets keep the file's names");
    if(morphs.targets.size() == 2)
    {
        check_near(morphs.targets[0].default_weight, 0.0f, 0.0f, "Open's default weight is the file's");
        check_near(morphs.targets[1].default_weight, 0.25f, 1e-6f, "Smile's default weight is the file's");
        check_eq(morphs.targets[0].delta_count, 4, "Open moves the upper lip's four vertices");
        check_eq(morphs.targets[1].delta_count, 2, "Smile moves the lower lip's two corners");
        check_near(morphs.targets[0].max_displacement, 0.1f, 1e-5f, "Open moves them by 0.1");
        bool lifts = true;
        for(uint32_t d = 0; d < morphs.targets[0].delta_count; ++d)
        {
            const auto& delta = morphs.deltas[morphs.targets[0].first_delta + d];
            lifts &= std::abs(delta.position[1] - 0.1f) < 1e-5f && std::abs(delta.position[0]) < 1e-6f;
        }
        check(lifts, "every Open delta lifts its vertex straight up");
    }
    bool bounds_hold_open = false;
    for(const auto& submesh : mouth.data.submeshes)
    {
        bounds_hold_open |= submesh.bbox.max.y > 1.09f;
    }
    check(bounds_hold_open, "the submesh bounds hold the opened mouth");
    check_eq(mouth.clips.size(), 1, "the animation imports as a clip");
    if(!mouth.clips.empty())
    {
        const auto* open = find_curve(mouth.clips[0], "Open");
        const auto* smile = find_curve(mouth.clips[0], "Smile");
        check(open != nullptr && smile != nullptr, "the clip has a curve per target, named after it");
        if(open != nullptr && smile != nullptr)
        {
            check_near(open->curve.sample(0.0f), 0.0f, 1e-5f, "Open starts closed");
            check_near(open->curve.sample(1.0f), 1.0f, 1e-5f, "Open ends at 1");
            check_near(open->curve.sample(0.5f), 0.5f, 1e-4f, "Open is linear between its keys");
            check_near(smile->curve.sample(1.0f), 0.5f, 1e-5f, "Smile ends at 0.5");
        }
    }
}

/// A hinge of two triangles in the z = 0 plane, normals +z: (0, 1, 2) and (1, 3, 2). Target "Fold" lifts vertex 3 by
/// 0.5 and stores the rest normals, as an exporter that writes a shape's normals as zero deltas does. Target "Turn"
/// moves nothing and turns vertex 3's normal toward +y.
auto make_hinge_mesh() -> aiMesh*
{
    auto* mesh = new aiMesh();
    mesh->mName = aiString("Hinge");
    mesh->mNumVertices = 4;
    mesh->mVertices = new aiVector3D[4]{aiVector3D(0.0f, 0.0f, 0.0f), aiVector3D(1.0f, 0.0f, 0.0f),
                                        aiVector3D(0.0f, 1.0f, 0.0f), aiVector3D(1.0f, 1.0f, 0.0f)};
    mesh->mNormals = new aiVector3D[4];
    for(unsigned int v = 0; v < 4; ++v)
    {
        mesh->mNormals[v] = aiVector3D(0.0f, 0.0f, 1.0f);
    }
    mesh->mTextureCoords[0] = new aiVector3D[4]{aiVector3D(0.0f, 0.0f, 0.0f), aiVector3D(1.0f, 0.0f, 0.0f),
                                                aiVector3D(0.0f, 1.0f, 0.0f), aiVector3D(1.0f, 1.0f, 0.0f)};
    mesh->mNumUVComponents[0] = 2;
    mesh->mNumFaces = 2;
    mesh->mFaces = new aiFace[2];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{0, 1, 2};
    mesh->mFaces[1].mNumIndices = 3;
    mesh->mFaces[1].mIndices = new unsigned int[3]{1, 3, 2};

    auto make_shape = [mesh](const char* name)
    {
        auto* shape = new aiAnimMesh();
        shape->mName = aiString(name);
        shape->mNumVertices = mesh->mNumVertices;
        shape->mVertices = new aiVector3D[4];
        shape->mNormals = new aiVector3D[4];
        for(unsigned int v = 0; v < 4; ++v)
        {
            shape->mVertices[v] = mesh->mVertices[v];
            shape->mNormals[v] = mesh->mNormals[v];
        }
        return shape;
    };
    auto* fold = make_shape("Fold");
    fold->mVertices[3].z = 0.5f;
    auto* turn = make_shape("Turn");
    turn->mNormals[3] = aiVector3D(0.0f, 1.0f, 1.0f).Normalize();
    mesh->mNumAnimMeshes = 2;
    mesh->mAnimMeshes = new aiAnimMesh*[2]{fold, turn};
    return mesh;
}

/// The normal delta @p target gives @p vertex, or zero when it leaves the vertex out.
auto find_normal_delta(const morph_target_data& data, int target, uint32_t vertex) -> math::vec3
{
    const auto& entry = data.targets[size_t(target)];
    for(uint32_t i = entry.first_delta; i < entry.first_delta + entry.delta_count; ++i)
    {
        if(data.morphed_vertices[data.deltas[i].slot] == vertex)
        {
            return unpack_morph_normal(data.deltas[i].normal);
        }
    }
    return math::vec3(0.0f);
}

void test_import_normals_without_turns_are_calculated()
{
    std::unique_ptr<aiMesh> mesh(make_hinge_mesh());
    importer::mesh_import::morph_import_options options;
    morph_target_builder builder;
    importer::mesh_import::read_morph_targets(mesh.get(), 0, options, builder);
    const auto data = builder.build(options.position_threshold, importer::mesh_import::k_morph_normal_threshold);
    const int fold = data.find_target("Fold");
    const int turn = data.find_target("Turn");
    require(fold >= 0 && turn >= 0, "both targets import");
    check(math::length(find_normal_delta(data, fold, 3)) > 0.1f,
          "a target that moves vertices with unturned file normals gets calculated normals");
    check(math::length(find_normal_delta(data, fold, 0)) < 1e-6f, "a vertex whose faces stay flat keeps its normal");
    const math::vec3 expected = math::normalize(math::vec3(0.0f, 1.0f, 1.0f)) - math::vec3(0.0f, 0.0f, 1.0f);
    check(math::length(find_normal_delta(data, turn, 3) - expected) < 0.01f,
          "a target whose file normals turn keeps the file's normals");
}

void test_signature_removal_finds_any_set()
{
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode("Root");
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1]{make_hinge_mesh()};
    aiMesh* mesh = scene->mMeshes[0];
    importer::mesh_import::morph_import_options options;
    importer::mesh_import::prepare_morph_targets(*scene, options, aiProcess_JoinIdenticalVertices);
    check_eq(mesh->GetNumUVChannels(), 2, "welding gets a signature set after the file's UV set");
    // Post-processing appends a generated set after the signature and rebuilds the list without its component counts.
    mesh->mNumUVComponents[1] = 0;
    mesh->mTextureCoords[2] = new aiVector3D[4];
    for(unsigned int v = 0; v < 4; ++v)
    {
        mesh->mTextureCoords[2][v] = aiVector3D(7.0f, 7.0f, 0.0f);
    }
    mesh->mNumUVComponents[2] = 2;
    importer::mesh_import::remove_morph_signatures(*scene);
    check(mesh->GetNumUVChannels() == 2 && mesh->mTextureCoords[0][3] == aiVector3D(1.0f, 1.0f, 0.0f) &&
              mesh->mTextureCoords[1][0] == aiVector3D(7.0f, 7.0f, 0.0f) && mesh->mNumUVComponents[1] == 2,
          "the signature is removed wherever it ended up, and the sets after it move down");
}

void test_import_without_morph_targets(asset_manager& am, const fs::path& folder)
{
    mesh_importer_meta import_meta;
    import_meta.model.import_morph_targets = false;
    const auto mouth = import_mouth(am, folder, import_meta);
    check(mouth.loaded, "the mouth imports without its targets");
    check(mouth.data.morph_targets.empty(), "Import Morph Targets off leaves the targets out");
    check_eq(mouth.data.vertex_count, 6, "and welding joins the lips as it does any equal vertices");
    check(mouth.clips.empty() || mouth.clips[0].curves.empty(), "and no morph curve is imported");
}

void test_import_settings_round_trip()
{
    mesh_importer_meta import_meta;
    import_meta.model.import_morph_targets = false;
    import_meta.model.morph_normal_mode = mesh_importer_meta::model_meta::morph_normals::none;
    import_meta.model.morph_threshold = 0.002f;
    std::stringstream stream;
    {
        auto archive = ser20::create_oarchive_associative(stream);
        try_save(archive, ser20::make_nvp("meta", import_meta));
    }
    mesh_importer_meta restored;
    auto archive = ser20::create_iarchive_associative(stream);
    try_load(archive, ser20::make_nvp("meta", restored));
    const auto& loaded = restored.model;
    check(!loaded.import_morph_targets && loaded.morph_normal_mode == mesh_importer_meta::model_meta::morph_normals::none &&
              std::abs(loaded.morph_threshold - 0.002f) < 1e-9f,
          "the morph import settings are saved");
    check(mesh_importer_meta{}.model.import_morph_targets, "Import Morph Targets is on by default");
}

// ---------------------------------------------------------------------------------------------------------------------
// Bounds, raycast shape and curves
// ---------------------------------------------------------------------------------------------------------------------

void test_bounds_padding()
{
    const auto targets = make_face_targets();
    const std::array<float, 3> weights{1.5f, -0.5f, std::numeric_limits<float>::quiet_NaN()};
    check_near(get_morph_bounds_padding(targets, weights), 0.2f, 1e-6f,
               "the padding adds every active target's reach by the size of its weight; a weight that is not a "
               "finite number adds nothing");
    const std::array<float, 3> rest{0.0f, 0.00001f, 0.0f};
    check(get_morph_bounds_padding(targets, rest) == 0.0f, "weights at rest pad nothing");
}

void test_position_deltas()
{
    morph_target_builder builder;
    builder.add("Lift", 0.0f, {make_delta(1, {0.0f, 0.5f, 0.0f})});
    builder.add("Push", 0.0f, {make_delta(1, {0.0f, 0.0f, 0.25f}), make_delta(2, {0.1f, 0.0f, 0.0f})});
    const auto data = builder.build(0.0f, 0.0f);
    std::vector<math::vec3> positions{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}};
    const std::array<float, 2> weights{2.0f, -1.0f};
    std::vector<float> deltas;
    accumulate_morph_deltas(data, weights, deltas);
    apply_morph_position_deltas(data, deltas, positions);
    check(positions[0] == math::vec3(0.0f) && math::length(positions[1] - math::vec3(1.0f, 1.0f, -0.25f)) < 1e-6f &&
              math::length(positions[2] - math::vec3(1.9f, 0.0f, 0.0f)) < 1e-6f,
          "a raycast meets each vertex moved by the weighted sum of its targets, as the deform pass draws it");
}

void test_curve_targets()
{
    const auto targets = make_face_targets();
    const auto curve_targets = make_morph_curve_targets(targets);
    check_eq(curve_targets.size(), size_t(3), "every target has its curve id");
    check(std::is_sorted(curve_targets.begin(),
                         curve_targets.end(),
                         [](const morph_curve_target& lhs, const morph_curve_target& rhs)
                         {
                             return lhs.curve_id < rhs.curve_id;
                         }),
          "the targets ascend by curve id");
    bool ids_match = true;
    for(const auto& entry : curve_targets)
    {
        ids_match = ids_match && entry.curve_id == get_animation_curve_id(targets.targets[entry.target].name);
    }
    check(ids_match, "each id is the curve id of the target's name");

    model_component model;
    model.set_morph_weight("Open", 0.4f);
    std::vector<animation_curve_value> curves{{get_animation_curve_id("Blink"), 0.75f},
                                              {get_animation_curve_id("Unrelated"), 0.3f},
                                              {get_animation_curve_id("Smile"), 1.25f}};
    std::sort(curves.begin(),
              curves.end(),
              [](const animation_curve_value& lhs, const animation_curve_value& rhs)
              {
                  return lhs.id < rhs.id;
              });
    model.apply_morph_curves(curves, curve_targets, targets);
    std::vector<float> weights;
    model.resolve_morph_weights(targets, weights);
    check(weights == std::vector<float>{0.4f, 1.25f, 0.75f},
          "curves named after targets set their weights; a target no curve keys keeps the script's weight");
    const auto& set = model.get_morph_weights();
    check(set.size() == 3 && set[1].name == "Smile" && set[2].name == "Blink",
          "the keyed weights are set in the targets' order");
    const auto version = model.get_morph_weights_version();
    model.apply_morph_curves({}, curve_targets, targets);
    check(model.get_morph_weights_version() == version, "a pose without curves changes no weight");
}

constexpr const char* MORPH_MESH_KEY = "engine:/embedded/morph_targets_test/lips";

/// A triangle whose vertex 0 rises 0.25 under Open and vertex 1 rises 0.5 under Lift; its bounds hold each alone.
auto make_morph_triangle() -> std::shared_ptr<mesh>
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    data.vertex_count = 3;
    data.vertex_data.resize(size_t(data.vertex_count) * data.vertex_format.getStride());
    const std::array<math::vec3, 3> positions{math::vec3(0.0f, 0.0f, 0.0f), math::vec3(1.0f, 0.0f, 0.0f), math::vec3(0.0f, 0.0f, 1.0f)};
    for(uint32_t v = 0; v < 3; ++v)
    {
        const float position[4] = {positions[v].x, positions[v].y, positions[v].z, 1.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), v);
    }
    data.triangle_data = {mesh::triangle{0, {0, 1, 2}}};
    data.triangle_count = 1;
    mesh::submesh only;
    only.vertex_start = 0;
    only.vertex_count = 3;
    only.face_start = 0;
    only.face_count = 1;
    data.submeshes = {only};
    data.material_count = 1;
    data.bbox = math::bbox(math::vec3(0.0f), math::vec3(1.0f, 0.5f, 1.0f));
    data.uv_set_count = 2;
    data.has_vertex_colors = true;
    morph_target_builder builder;
    builder.add("Open", 0.0f, {make_delta(0, {0.0f, 0.25f, 0.0f})});
    builder.add("Lift", 0.0f, {make_delta(1, {0.0f, 0.5f, 0.0f})});
    data.morph_targets = builder.build(0.0f, 0.0f);
    auto result = std::make_shared<mesh>();
    check(result->load_mesh(std::move(data), false), "the morph triangle loads");
    return result;
}

void test_model_bounds_grow_with_weights()
{
    auto& am = engine::context().get_cached<asset_manager>();
    am.unload_asset<mesh>(MORPH_MESH_KEY);
    const auto handle = am.get_asset_from_instance<mesh>(MORPH_MESH_KEY, make_morph_triangle());
    check(!handle.get()->get_morph_curve_targets().empty(), "a loaded mesh indexes its targets by curve id");
    model_component model;
    unravel::model lods;
    lods.set_lods({handle});
    model.set_model(lods);
    const math::transform identity;
    model.update_world_bounds(identity);
    const auto rest = model.get_world_bounds();
    check(rest.max.y == 0.5f, "at rest the model's bounds are the mesh's");
    model.set_morph_weight("Open", 1.0f);
    model.set_morph_weight("Lift", 1.0f);
    model.update_world_bounds(identity);
    check_near(model.get_world_bounds().max.y, 0.5f + 0.75f, 1e-5f,
               "two targets on at once grow the bounds by both their reaches, which the mesh's bounds do not hold");
    check(model.get_submit_extras(false).proxies == nullptr,
          "while the bounds grow, submeshes are culled by the model's box, not their own");
    math::transform scaled;
    scaled.set_scale(math::vec3(2.0f));
    model.update_world_bounds(scaled);
    check_near(model.get_world_bounds().max.y, 1.0f + 1.5f, 1e-5f, "the padding scales with the model");
    model.set_morph_weight("Open", 3.0f);
    model.update_world_bounds(identity);
    check_near(model.get_world_bounds().max.y, 0.5f + 0.75f + 0.5f, 1e-5f,
               "a weight past 1 grows them further, recomputed when the weights change");
    model.reset_morph_weights();
    model.update_world_bounds(identity);
    check(model.get_world_bounds().max.y == 0.5f, "back at rest the bounds are the mesh's again");
    am.unload_asset<mesh>(MORPH_MESH_KEY);
}

void test_dispose_clears_morph_data()
{
    auto loaded = make_morph_triangle();
    check(loaded->has_morph_targets() && loaded->get_info().uv_sets == 2, "the triangle has targets and two UV sets");
    check(loaded->create_plane(gfx::mesh_vertex::get_layout(), 1.0f, 1.0f, 1, 1, false), "a plane is built in its place");
    const auto info = loaded->get_info();
    check(!loaded->has_morph_targets() && loaded->get_morph_curve_targets().empty() && info.uv_sets == 1 &&
              !info.vertex_colors && info.morph_targets == 0,
          "a mesh built again keeps no targets, colours or second UV set of its previous load");
}

void run_morph_targets(rtti::context& ctx)
{
    RUN_CASE(test_builder_merges_targets_by_name());
    RUN_CASE(test_builder_thresholds());
    RUN_CASE(test_normal_packing());
    RUN_CASE(test_vertex_copies_share_deltas());
    RUN_CASE(test_skin_split_copies_morphs());
    RUN_CASE(test_cpu_deform_matches_reference());
    RUN_CASE(test_import_settings_round_trip());
    RUN_CASE(test_model_weights());
    RUN_CASE(test_model_weights_save_by_name());
    RUN_CASE(test_model_weights_ignore_non_finite());
    RUN_CASE(test_weight_versions_are_unique_across_models());
    RUN_CASE(test_model_weights_readable_round_trip());
    RUN_CASE(test_import_normals_without_turns_are_calculated());
    RUN_CASE(test_signature_removal_finds_any_set());
    RUN_CASE(test_bounds_padding());
    RUN_CASE(test_position_deltas());
    RUN_CASE(test_curve_targets());
    RUN_CASE(test_model_bounds_grow_with_weights());
    RUN_CASE(test_dispose_clears_morph_data());

    auto& am = ctx.get_cached<asset_manager>();
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    // A folder of this run's own, so runs of the suite from two builds at once never share a file.
    const fs::path folder = fs::resolve_protocol("app:/morph_targets") / ("run_" + std::to_string(stamp));
    fs::error_code err;
    fs::create_directories(folder, err);
    RUN_CASE(test_morph_data_round_trips(folder));
    RUN_CASE(test_import_gltf_mouth(am, folder));
    RUN_CASE(test_import_without_morph_targets(am, folder));
    fs::remove_all(folder, err);
    // The parent goes only when no other run still uses it.
    fs::remove(folder.parent_path(), err);
}

} // namespace

REGISTER_TEST_SUITE("morph targets", run_morph_targets)
