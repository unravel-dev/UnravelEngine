/*
 * Validation suite: the vertex colour and second UV set every mesh vertex carries.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "vertex attributes"
 *
 * Pins:
 *   - the mesh layout: Color0 (4 x Uint8 normalized) and TexCoord1 (2 x float) after TexCoord0, 44 bytes, every
 *     attribute 4-byte aligned (vertex pulling reads the vertex as floats);
 *   - import of synthetic Assimp meshes: the first colour set clamped to 0..1 and rounded to the nearest byte, the
 *     second UV set, and the defaults where a mesh has neither (white, a copy of the first set); the channels the
 *     mesh records, across meshes of one file, reported by mesh::info;
 *   - the Import Vertex Colors setting survives a save and a load;
 *   - built-in shapes: white and a copy of the first UV set.
 */
#include "../tests.h"
#include <asset_compiler/importers/mesh/mesh_import_geometry.h>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/rendering/mesh.h>
#include <graphics/vertex_decl.h>
#include <serialization/associative_archive.h>

#include <assimp/scene.h>

#include <array>
#include <cmath>
#include <memory>
#include <sstream>

using namespace unravel::test;
using namespace unravel;
using namespace unravel::importer::mesh_import;

namespace
{

/// A triangle with a first UV set, and a colour set and a second UV set when asked for. The colours step outside
/// 0..1 on vertex 2 and fall between two bytes on vertex 1.
auto make_vertex_attribute_triangle(const char* name, bool has_colors, bool has_second_uv_set) -> aiMesh*
{
    auto* mesh = new aiMesh();
    mesh->mName = aiString(name);
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3]{aiVector3D(0.0f, 0.0f, 0.0f), aiVector3D(1.0f, 0.0f, 0.0f),
                                        aiVector3D(0.0f, 1.0f, 0.0f)};
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned int[3]{0, 1, 2};
    mesh->mTextureCoords[0] = new aiVector3D[3]{aiVector3D(0.25f, 0.5f, 0.0f), aiVector3D(0.75f, 0.5f, 0.0f),
                                                aiVector3D(0.5f, 1.0f, 0.0f)};
    mesh->mNumUVComponents[0] = 2;
    if(has_second_uv_set)
    {
        mesh->mTextureCoords[1] = new aiVector3D[3]{aiVector3D(2.0f, 3.0f, 0.0f), aiVector3D(4.0f, 5.0f, 0.0f),
                                                    aiVector3D(6.0f, 7.0f, 0.0f)};
        mesh->mNumUVComponents[1] = 2;
    }
    if(has_colors)
    {
        mesh->mColors[0] = new aiColor4D[3]{aiColor4D(1.0f, 0.0f, 0.0f, 1.0f), aiColor4D(0.0f, 0.5f, 1.0f, 0.25f),
                                            aiColor4D(2.0f, -1.0f, 0.5f, 1.0f)};
    }
    return mesh;
}

/// A scene holding @p meshes under its root.
auto make_scene(std::initializer_list<aiMesh*> meshes) -> std::unique_ptr<aiScene>
{
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode("Root");
    scene->mNumMeshes = static_cast<unsigned int>(meshes.size());
    scene->mMeshes = new aiMesh*[meshes.size()];
    unsigned int index = 0;
    for(auto* mesh : meshes)
    {
        scene->mMeshes[index++] = mesh;
    }
    return scene;
}

auto import_scene(const aiScene& scene) -> mesh::load_data
{
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    process_meshes(&scene, data);
    return data;
}

/// Vertex @p vertex's colour as bytes.
auto get_color_bytes(const bgfx::VertexLayout& layout, const uint8_t* vertices, uint32_t vertex) -> std::array<int, 4>
{
    float color[4] = {};
    bgfx::vertexUnpack(color, bgfx::Attrib::Color0, layout, vertices, vertex);
    return {int(std::lround(color[0] * 255.0f)),
            int(std::lround(color[1] * 255.0f)),
            int(std::lround(color[2] * 255.0f)),
            int(std::lround(color[3] * 255.0f))};
}

auto get_texcoords(const bgfx::VertexLayout& layout,
                   const uint8_t* vertices,
                   uint32_t vertex,
                   bgfx::Attrib::Enum set) -> math::vec2
{
    float texcoords[4] = {};
    bgfx::vertexUnpack(texcoords, set, layout, vertices, vertex);
    return {texcoords[0], texcoords[1]};
}

constexpr std::array<int, 4> WHITE{255, 255, 255, 255};

void test_layout()
{
    const auto& layout = gfx::mesh_vertex::get_layout();
    check(layout.has(bgfx::Attrib::Color0) && layout.has(bgfx::Attrib::TexCoord1),
          "the mesh vertex holds a colour and a second UV set");
    check(layout.getStride() == 44, "the mesh vertex is 44 bytes");
    check(layout.getOffset(bgfx::Attrib::Color0) == 32 && layout.getOffset(bgfx::Attrib::TexCoord1) == 36,
          "the colour and the second UV set follow the first UV set");
    bool is_aligned = layout.getStride() % 4 == 0;
    for(uint32_t attribute = 0; attribute < bgfx::Attrib::Count; ++attribute)
    {
        const auto attrib = bgfx::Attrib::Enum(attribute);
        if(layout.has(attrib))
        {
            is_aligned = is_aligned && layout.getOffset(attrib) % 4 == 0;
        }
    }
    check(is_aligned, "every attribute and the stride stay 4-byte aligned");
}

void test_import_with_channels()
{
    const auto scene = make_scene({make_vertex_attribute_triangle("Painted", true, true)});
    const auto data = import_scene(*scene);
    const auto& layout = data.vertex_format;
    const auto* vertices = data.vertex_data.data();
    check(data.has_vertex_colors && data.uv_set_count == 2, "the mesh records its colour set and both UV sets");
    check((get_color_bytes(layout, vertices, 0) == std::array<int, 4>{255, 0, 0, 255}), "vertex 0 keeps its red");
    check((get_color_bytes(layout, vertices, 1) == std::array<int, 4>{0, 128, 255, 64}),
          "vertex 1 rounds to the nearest byte (0.5 -> 128, 0.25 -> 64)");
    check((get_color_bytes(layout, vertices, 2) == std::array<int, 4>{255, 0, 128, 255}),
          "vertex 2 is clamped to 0..1");
    check(get_texcoords(layout, vertices, 0, bgfx::Attrib::TexCoord1) == math::vec2(2.0f, 3.0f) &&
              get_texcoords(layout, vertices, 2, bgfx::Attrib::TexCoord1) == math::vec2(6.0f, 7.0f),
          "the second UV set is the file's");
    check(get_texcoords(layout, vertices, 1, bgfx::Attrib::TexCoord0) == math::vec2(0.75f, 0.5f),
          "the first UV set is unchanged");
}

void test_import_defaults()
{
    const auto scene = make_scene({make_vertex_attribute_triangle("Plain", false, false)});
    const auto data = import_scene(*scene);
    const auto& layout = data.vertex_format;
    const auto* vertices = data.vertex_data.data();
    check(!data.has_vertex_colors && data.uv_set_count == 1, "the mesh records that it has no colours and one UV set");
    bool is_default = true;
    for(uint32_t vertex = 0; vertex < data.vertex_count; ++vertex)
    {
        is_default = is_default && get_color_bytes(layout, vertices, vertex) == WHITE &&
                     get_texcoords(layout, vertices, vertex, bgfx::Attrib::TexCoord1) ==
                         get_texcoords(layout, vertices, vertex, bgfx::Attrib::TexCoord0);
    }
    check(is_default, "every vertex is white and its second UV set copies the first");
}

void test_import_mixed_meshes()
{
    const auto scene = make_scene({make_vertex_attribute_triangle("Painted", true, true),
                                   make_vertex_attribute_triangle("Plain", false, false)});
    auto data = import_scene(*scene);
    const auto& layout = data.vertex_format;
    const auto* vertices = data.vertex_data.data();
    check(data.has_vertex_colors && data.uv_set_count == 2, "one painted mesh marks the file's colours and UV sets");
    check(get_color_bytes(layout, vertices, 3) == WHITE &&
              get_texcoords(layout, vertices, 4, bgfx::Attrib::TexCoord1) == math::vec2(0.75f, 0.5f),
          "the plain mesh's vertices keep the defaults");

    mesh loaded;
    check(loaded.load_mesh(std::move(data), false), "the imported data loads");
    const auto info = loaded.get_info();
    check(info.vertex_colors && info.uv_sets == 2, "mesh::info reports the channels");
}

void test_import_vertex_colors_setting_is_saved()
{
    mesh_importer_meta meta;
    meta.model.import_vertex_colors = false;
    std::stringstream stream;
    {
        auto archive = ser20::create_oarchive_associative(stream);
        try_save(archive, ser20::make_nvp("meta", meta));
    }
    mesh_importer_meta restored;
    auto archive = ser20::create_iarchive_associative(stream);
    try_load(archive, ser20::make_nvp("meta", restored));
    check(!restored.model.import_vertex_colors, "Import Vertex Colors off survives a save and a load");
    check(mesh_importer_meta{}.model.import_vertex_colors, "Import Vertex Colors is on by default");
}

void test_shapes()
{
    mesh cube;
    check(cube.create_cube(gfx::mesh_vertex::get_layout(), 1.0f, 1.0f, 1.0f, 2, 2, 2, false), "a cube is built");
    const auto& layout = cube.get_vertex_format();
    const auto* vertices = cube.get_system_vb();
    bool is_default = cube.get_vertex_count() > 0;
    for(uint32_t vertex = 0; vertex < cube.get_vertex_count(); ++vertex)
    {
        is_default = is_default && get_color_bytes(layout, vertices, vertex) == WHITE &&
                     get_texcoords(layout, vertices, vertex, bgfx::Attrib::TexCoord1) ==
                         get_texcoords(layout, vertices, vertex, bgfx::Attrib::TexCoord0);
    }
    check(is_default, "a shape's vertices are white and their second UV set copies the first");
    const auto info = cube.get_info();
    check(!info.vertex_colors && info.uv_sets == 1, "a shape reports no colours and one UV set");
}

void run_vertex_attributes(rtti::context&)
{
    RUN_CASE(test_layout());
    RUN_CASE(test_import_with_channels());
    RUN_CASE(test_import_defaults());
    RUN_CASE(test_import_mixed_meshes());
    RUN_CASE(test_import_vertex_colors_setting_is_saved());
    RUN_CASE(test_shapes());
}

} // namespace

REGISTER_TEST_SUITE("vertex attributes", run_vertex_attributes)
