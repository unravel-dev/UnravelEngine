/*
 * Validation suite for mesh shapes: the .emesh files that describe the engine's built-in meshes (cube, sphere, ...)
 * for a generator, and their compile into mesh assets.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "mesh shapes"
 *
 * The built-in shapes were meshes the engine generated at every start, each with a distance field baked on the
 * spot. They are assets now, so these pin that nothing changed for a scene that uses them:
 *   - every kind has a name an .emesh file can hold, and an unknown name builds nothing;
 *   - each shape file builds the vertices, triangles and bounds its generator call built at startup;
 *   - each shape's .meta holds the id the shape had as a generated mesh, with the import settings of that bake;
 *   - a key in the earlier spelling ("engine:/embedded/cube") loads the shape's file, and other embedded keys
 *     are left alone;
 *   - a compiled shape carries the distance field chain a bake of the generated mesh gives, and cards;
 *   - the plane keeps the triangles global illumination represents it by through compile and load;
 *   - a mesh built in code gets a field only when asked, by the create call or by mesh::generate_sdf.
 *
 * The compile checks run for a few shapes; --bench runs them for all. Every run compiles into a fresh folder
 * under app:/mesh_shapes and nothing is deleted.
 */

#include "../tests.h"

#include <asset_compiler/asset_compiler.h>
#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/defaults/engine_asset_keys.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/mesh_shape.hpp>
#include <engine/rendering/mesh.h>
#include <engine/rendering/mesh_shape.h>
#include <graphics/graphics.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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

/// A built-in shape and the generator call the engine made for it at startup while the shapes were generated.
struct built_in_shape
{
    std::string name;
    std::function<bool(mesh&, primitive_sdf)> create;
};

constexpr uint32_t ROUND_SEGMENTS = 20;
constexpr int ICOSPHERE_LEVELS = 20;
constexpr uint32_t TERRAIN_SEGMENTS = 64;
constexpr float TERRAIN_HALF_EXTENT = 25.0f;
constexpr float TERRAIN_HEIGHT = 4.0f;
/// The shapes whose compile is checked on every run; --bench checks every shape.
const std::vector<std::string> COMPILED_SHAPES = {"cube", "cube_rounded", "plane", "capsule_1m", "torus", "icosphere2"};

auto get_layout() -> const bgfx::VertexLayout&
{
    return gfx::mesh_vertex::get_layout();
}

/// The heights the engine gave its test terrain: sin along x times cos along z over the square.
auto make_terrain_heights() -> std::vector<float>
{
    constexpr float pi = 3.14159265f;
    const uint32_t row = TERRAIN_SEGMENTS + 1;
    std::vector<float> heights(size_t(row) * row);
    for(uint32_t z = 0; z < row; ++z)
    {
        const float nz = float(z) / float(TERRAIN_SEGMENTS) * 2.0f - 1.0f;
        for(uint32_t x = 0; x < row; ++x)
        {
            const float nx = float(x) / float(TERRAIN_SEGMENTS) * 2.0f - 1.0f;
            heights[size_t(z) * row + x] = std::sin(nx * pi * 2.0f) * std::cos(nz * pi * 2.0f) * 0.5f + 0.5f;
        }
    }
    return heights;
}

auto make_built_in_shapes() -> std::vector<built_in_shape>
{
    std::vector<built_in_shape> shapes = {
        {"cube", [](mesh& m, primitive_sdf sdf) { return m.create_cube(get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, false, sdf); }},
        {"cube_rounded",
         [](mesh& m, primitive_sdf sdf) { return m.create_rounded_cube(get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, false, sdf); }},
        {"sphere",
         [](mesh& m, primitive_sdf sdf) { return m.create_sphere(get_layout(), 0.5f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf); }},
        {"plane", [](mesh& m, primitive_sdf sdf) { return m.create_plane(get_layout(), 10.0f, 10.0f, 1, 1, false, sdf); }},
        {"cylinder",
         [](mesh& m, primitive_sdf sdf)
         {
             return m.create_cylinder(get_layout(), 0.5f, 2.0f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf);
         }},
        {"capsule_2m",
         [](mesh& m, primitive_sdf sdf)
         {
             return m.create_capsule(get_layout(), 0.5f, 2.0f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf);
         }},
        {"capsule_1m",
         [](mesh& m, primitive_sdf sdf)
         {
             return m.create_capsule(get_layout(), 0.5f, 1.0f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf);
         }},
        {"cone",
         [](mesh& m, primitive_sdf sdf) { return m.create_cone(get_layout(), 0.5f, 2.0f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf); }},
        {"torus",
         [](mesh& m, primitive_sdf sdf) { return m.create_torus(get_layout(), 1.0f, 0.5f, ROUND_SEGMENTS, ROUND_SEGMENTS, false, sdf); }},
        {"terrain_test",
         [](mesh& m, primitive_sdf sdf)
         {
             const auto heights = make_terrain_heights();
             return m.create_heightfield(get_layout(),
                                         heights,
                                         TERRAIN_SEGMENTS,
                                         TERRAIN_SEGMENTS,
                                         TERRAIN_HALF_EXTENT,
                                         TERRAIN_HALF_EXTENT,
                                         TERRAIN_HEIGHT,
                                         false,
                                         sdf);
         }},
        {"teapot", [](mesh& m, primitive_sdf sdf) { return m.create_teapot(get_layout(), false, sdf); }},
        {"icosahedron", [](mesh& m, primitive_sdf sdf) { return m.create_icosahedron(get_layout(), false, sdf); }},
        {"dodecahedron", [](mesh& m, primitive_sdf sdf) { return m.create_dodecahedron(get_layout(), false, sdf); }},
    };
    for(int level = 0; level < ICOSPHERE_LEVELS; ++level)
    {
        shapes.push_back({"icosphere" + std::to_string(level),
                          [level](mesh& m, primitive_sdf sdf) { return m.create_icosphere(get_layout(), level, false, sdf); }});
    }
    return shapes;
}

auto get_shape_path(const std::string& name) -> fs::path
{
    return fs::resolve_protocol(get_built_in_mesh_key(name));
}

auto get_meta_path(const fs::path& path) -> fs::path
{
    return fs::path(path.string() + ".meta");
}

auto get_embedded_key(const std::string& name) -> std::string
{
    return std::string(EMBEDDED_MESH_PREFIX) + name;
}

auto has_same_geometry(mesh& actual, mesh& expected) -> bool
{
    if(actual.get_vertex_count() != expected.get_vertex_count() || actual.get_face_count() != expected.get_face_count())
    {
        return false;
    }
    const size_t stride = expected.get_vertex_format().getStride();
    if(actual.get_vertex_format().getStride() != stride)
    {
        return false;
    }
    const size_t vertex_bytes = size_t(expected.get_vertex_count()) * stride;
    const size_t index_bytes = size_t(expected.get_face_count()) * 3 * sizeof(uint32_t);
    return std::memcmp(actual.get_system_vb(), expected.get_system_vb(), vertex_bytes) == 0 &&
           std::memcmp(actual.get_system_ib(), expected.get_system_ib(), index_bytes) == 0;
}

auto has_same_bounds(const math::bbox& a, const math::bbox& b) -> bool
{
    return a.min == b.min && a.max == b.max;
}

auto is_same_field(const mesh_sdf& a, const mesh_sdf& b) -> bool
{
    return a.voxel_size == b.voxel_size && a.grid_dim == b.grid_dim && a.brick_dim == b.brick_dim &&
           a.is_two_sided == b.is_two_sided && a.indirection == b.indirection && a.brick_voxels == b.brick_voxels;
}

void test_kind_names()
{
    std::printf("test_kind_names\n");
    const mesh_shape_kind kinds[] = {mesh_shape_kind::cube,        mesh_shape_kind::rounded_cube, mesh_shape_kind::sphere,
                                     mesh_shape_kind::plane,       mesh_shape_kind::cylinder,     mesh_shape_kind::capsule,
                                     mesh_shape_kind::cone,        mesh_shape_kind::torus,        mesh_shape_kind::teapot,
                                     mesh_shape_kind::icosahedron, mesh_shape_kind::dodecahedron, mesh_shape_kind::icosphere,
                                     mesh_shape_kind::wave_terrain};
    for(const auto kind : kinds)
    {
        const auto name = get_shape_kind_name(kind);
        const auto found = find_shape_kind(name);
        check(!name.empty() && found && *found == kind, "a kind's name finds the kind: " + std::string(name));
    }
    check(get_shape_kind_name(mesh_shape_kind::none).empty(), "no generator has no name");
    check(!find_shape_kind("pyramid") && !find_shape_kind(""), "an unknown name finds no kind");
    mesh built;
    check(!create_shape_mesh(built, get_layout(), mesh_shape{}, false), "a shape without a generator builds nothing");
    mesh::load_data data;
    check(!build_shape_load_data(mesh_shape{}, data), "a shape without a generator has no load data");
}

/// The shape a built-in shape's file describes; kind none when the file is missing or names no generator.
auto load_shape(const std::string& name) -> mesh_shape
{
    mesh_shape shape;
    load_from_file(get_shape_path(name).string(), shape);
    return shape;
}

void test_shape_file(const built_in_shape& expected_shape)
{
    const auto& name = expected_shape.name;
    const auto shape = load_shape(name);
    check(shape.kind != mesh_shape_kind::none, name + ": the file names a generator");
    mesh::load_data data;
    const bool is_built = build_shape_load_data(shape, data);
    check(is_built && data.submeshes.size() == 1 && data.triangle_count == data.triangle_data.size(),
          name + ": the file builds one submesh");
    mesh expected;
    check(expected_shape.create(expected, primitive_sdf::none), name + ": the generator call builds a mesh");
    if(!is_built)
    {
        return;
    }
    const auto bounds = data.bbox;
    mesh loaded;
    check(loaded.load_mesh(std::move(data), false), name + ": the load data loads");
    check(has_same_geometry(loaded, expected), name + ": the vertices and triangles are the generator call's");
    check(has_same_bounds(bounds, expected.get_bounds()) && has_same_bounds(loaded.get_bounds(), expected.get_bounds()),
          name + ": the bounds are the generator call's");
    check(loaded.get_sdf_mip_count(0) == 0, name + ": a shape built from its description has no field of its own");
}

void test_shape_files(const std::vector<built_in_shape>& shapes)
{
    std::printf("test_shape_files\n");
    for(const auto& shape : shapes)
    {
        test_shape_file(shape);
    }
}

auto load_meta(const std::string& name) -> asset_meta
{
    asset_meta meta;
    load_from_file(get_meta_path(get_shape_path(name)).string(), meta);
    return meta;
}

void test_meta_files(const std::vector<built_in_shape>& shapes)
{
    std::printf("test_meta_files\n");
    for(const auto& shape : shapes)
    {
        const auto& name = shape.name;
        const auto meta = load_meta(name);
        check(meta.uid == generate_uuid(get_embedded_key(name)), name + ": the .meta holds the id of the generated mesh");
        check(meta.type == ex::get_mesh_shape_format(), name + ": the .meta names the shape format");
        const auto importer = std::dynamic_pointer_cast<mesh_importer_meta>(meta.importer);
        check(importer != nullptr, name + ": the .meta holds mesh import settings");
        if(!importer)
        {
            continue;
        }
        const auto runtime = mesh::runtime_sdf_bake_settings();
        check(!importer->model.generate_lods, name + ": a shape gets no generated LODs, as the generated mesh had none");
        check(importer->sdf.generate_sdf && importer->sdf.resolution == runtime.resolution &&
                  importer->sdf.target_voxel_size == runtime.target_voxel_size &&
                  importer->sdf.min_voxel_size == runtime.min_voxel_size &&
                  importer->sdf.max_voxel_size == runtime.max_voxel_size &&
                  importer->sdf.max_total_voxels == runtime.max_total_voxels &&
                  importer->sdf.two_sided == runtime.two_sided &&
                  importer->sdf.two_sided_thickness == runtime.two_sided_thickness &&
                  importer->sdf.max_component_spread == runtime.max_component_spread,
              name + ": the field's bake settings are those of a bake at run time");
        check(importer->cards.generate_cards, name + ": a shape gets cards");
    }
}

/// Puts the built-in shapes into the engine's asset index, as the editor's asset watcher does when it lists them.
void register_shapes(asset_manager& am, const std::vector<built_in_shape>& shapes)
{
    for(const auto& shape : shapes)
    {
        am.add_asset_info_for_path(get_shape_path(shape.name), load_meta(shape.name), true);
    }
}

void test_key_rule(rtti::context& ctx, const std::vector<built_in_shape>& shapes)
{
    std::printf("test_key_rule\n");
    auto& am = ctx.get_cached<asset_manager>();
    check(get_built_in_mesh_key("cube") == "engine:/data/meshes/cube.emesh", "a shape's key is its file in the meshes folder");
    check(get_built_in_mesh_key_of_embedded("engine:/embedded/capsule_2m") == "engine:/data/meshes/capsule_2m.emesh",
          "a key in the earlier spelling names the shape's file");
    check(get_built_in_mesh_key_of_embedded("engine:/embedded/").empty() &&
              get_built_in_mesh_key_of_embedded("engine:/data/meshes/cube.emesh").empty() &&
              get_built_in_mesh_key_of_embedded("app:/embedded/cube").empty(),
          "other keys name no shape");
    const std::string instance_key = "engine:/embedded/mesh_shapes_test/cube";
    const auto instance = am.get_asset_from_instance<mesh>(instance_key, std::make_shared<mesh>());
    check(am.find_asset<mesh>(instance_key).id() == instance_key, "an embedded key that names no shape is kept as it is");
    check(!am.find_asset<mesh>("engine:/embedded/cube"), "before the engine lists the shapes the earlier key finds nothing");
    register_shapes(am, shapes);
    for(const auto& shape : shapes)
    {
        const auto handle =
            am.get_asset<mesh>(get_embedded_key(shape.name), load_flags::standard, load_mode::deferred);
        check(handle.id() == get_built_in_mesh_key(shape.name) && handle.uid() == generate_uuid(get_embedded_key(shape.name)),
              shape.name + ": the earlier key loads the shape's file under the id scenes hold");
    }
    check(am.find_asset<mesh>("engine:/embedded/cube").id() == get_built_in_mesh_key("cube"),
          "the earlier key finds the loaded shape");
    const auto entry = am.get_metadata_for_key("engine:/embedded/cube");
    check(entry.location == get_built_in_mesh_key("cube") && entry.meta.uid == generate_uuid("engine:/embedded/cube"),
          "the earlier key finds the shape's entry in the asset index");
    check(am.get_metadata_for_key(instance_key).location == instance_key &&
              am.get_metadata_for_key("engine:/embedded/mesh_shapes_test/none").location.empty(),
          "an embedded key that names no shape finds its own entry or none");
    check(am.find_asset<mesh>(instance_key).id() == instance_key, "the instance key is still its own");
    check(defaults::has_built_in_mesh(ctx, "Cube") && defaults::has_built_in_mesh(ctx, "Capsule 2m") &&
              defaults::has_built_in_mesh(ctx, "Icosphere19"),
          "a shape is found by the name a menu shows");
    check(!defaults::has_built_in_mesh(ctx, "Pyramid"), "an unknown name is no shape");
}

auto get_run_root() -> fs::path
{
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const fs::path root = fs::resolve_protocol("app:/mesh_shapes") / ("run_" + std::to_string(stamp));
    fs::error_code err;
    fs::create_directories(root, err);
    return root;
}

/// Copies a built-in shape into `folder` under an id of its own, with the shape's import settings, and compiles it.
auto compile_shape_copy(asset_manager& am, const std::string& name, const fs::path& folder, mesh::load_data& compiled) -> bool
{
    const fs::path source = folder / (name + ex::get_mesh_shape_format());
    fs::error_code err;
    fs::create_directories(folder, err);
    fs::copy_file(get_shape_path(name), source, fs::copy_options::overwrite_existing, err);
    auto meta = load_meta(name);
    meta.uid = generate_uuid();
    save_to_file(get_meta_path(source).string(), meta);
    am.add_asset_info_for_path(source, meta, true);
    const fs::path output = folder / "compiled" / (source.filename().string() + ".asset");
    fs::create_directories(output.parent_path(), err);
    if(!asset_compiler::compile<mesh>(am, get_meta_path(source), output))
    {
        return false;
    }
    load_from_file_bin(output.string(), compiled);
    return true;
}

/// The field level `level` of a compiled mesh's first submesh: the finest, then the coarser ones.
auto get_compiled_field(const mesh::load_data& compiled, uint32_t level) -> const mesh_sdf*
{
    if(compiled.submesh_sdfs.empty())
    {
        return nullptr;
    }
    if(level == 0)
    {
        return &compiled.submesh_sdfs.front();
    }
    if(compiled.submesh_sdf_coarse_mips.empty() || level > compiled.submesh_sdf_coarse_mips.front().size())
    {
        return nullptr;
    }
    return &compiled.submesh_sdf_coarse_mips.front()[level - 1];
}

void test_compiled_shape(asset_manager& am, const built_in_shape& shape, const fs::path& folder)
{
    const auto& name = shape.name;
    mesh::load_data compiled;
    check(compile_shape_copy(am, name, folder, compiled), name + ": the shape compiles");
    mesh baked;
    check(shape.create(baked, primitive_sdf::baked), name + ": the generator call with a field builds a mesh");
    const uint32_t levels = baked.get_sdf_mip_count(0);
    check(levels > 0, name + ": a create call that asks for the field gets one");
    for(uint32_t level = 0; level < levels; ++level)
    {
        const auto* field = get_compiled_field(compiled, level);
        check(field != nullptr && is_same_field(*field, baked.get_sdf(0, level)),
              name + ": compiled field level " + std::to_string(level) + " is the bake of the generated mesh");
    }
    check(get_compiled_field(compiled, levels) == nullptr, name + ": the compiled chain has no more levels than the bake");
    check(compiled.submesh_cards.size() == 1 && !compiled.submesh_cards.front().cards.empty(), name + ": the compiled shape has cards");
    check(compiled.lods.empty(), name + ": the compiled shape has no generated LODs");
    mesh loaded;
    check(loaded.load_mesh(std::move(compiled), false) && has_same_geometry(loaded, baked),
          name + ": the compiled shape loads as the generated mesh");
    check(loaded.get_sdf_mip_count(0) == levels, name + ": the loaded shape has the compiled field chain");
}

void test_compiled_shapes(asset_manager& am, const std::vector<built_in_shape>& shapes, const fs::path& run_root)
{
    std::printf("test_compiled_shapes\n");
    const bool is_every_shape = tests::wants("bench");
    for(const auto& shape : shapes)
    {
        const bool is_chosen = std::find(COMPILED_SHAPES.begin(), COMPILED_SHAPES.end(), shape.name) != COMPILED_SHAPES.end();
        if(is_every_shape || is_chosen)
        {
            test_compiled_shape(am, shape, run_root);
        }
    }
}

void test_plane_gi_geometry(asset_manager& am, const fs::path& run_root)
{
    std::printf("test_plane_gi_geometry\n");
    constexpr uint32_t sheet_triangles = 2;
    mesh::load_data built;
    check(build_shape_load_data(load_shape("plane"), built) && built.gi_source_geometry.is_valid() &&
              built.gi_source_geometry.get_triangle_count() == sheet_triangles,
          "the plane's load data holds the upward sheet for global illumination");
    mesh::load_data cube;
    check(build_shape_load_data(load_shape("cube"), cube) && !cube.gi_source_geometry.is_valid(),
          "a shape global illumination represents by its own triangles holds none");
    mesh::load_data compiled;
    check(compile_shape_copy(am, "plane", run_root / "plane_geometry", compiled), "the plane compiles");
    check(compiled.gi_source_geometry.is_valid() && compiled.gi_source_geometry.get_triangle_count() == sheet_triangles &&
              has_same_bounds(compiled.gi_source_geometry.bounds, built.gi_source_geometry.bounds),
          "the compiled plane keeps the upward sheet and its bounds");
    mesh loaded;
    check(loaded.load_mesh(std::move(compiled), false) && loaded.get_gi_source_geometry() != nullptr &&
              loaded.get_gi_source_geometry()->get_triangle_count() == sheet_triangles,
          "the loaded plane hands the upward sheet to cards built at run time");
}

void test_field_on_request()
{
    std::printf("test_field_on_request\n");
    mesh plain;
    check(plain.create_cube(get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, false) && plain.get_sdf_mip_count(0) == 0,
          "a create call bakes no field unless asked");
    check(plain.generate_sdf() && plain.get_sdf_mip_count(0) > 0, "mesh::generate_sdf bakes the field of a mesh that exists");
    mesh asked;
    check(asked.create_cube(get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, false, primitive_sdf::baked) &&
              asked.get_sdf_mip_count(0) == plain.get_sdf_mip_count(0) && is_same_field(asked.get_sdf(0), plain.get_sdf(0)),
          "a create call that asks bakes the same field");
    mesh shape_mesh;
    mesh_shape sphere;
    sphere.kind = mesh_shape_kind::sphere;
    check(create_shape_mesh(shape_mesh, get_layout(), sphere, false, primitive_sdf::baked) && shape_mesh.get_sdf_mip_count(0) > 0,
          "a shape built in code takes the same request");
}

auto run_mesh_shapes_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    auto& am = ctx.get_cached<asset_manager>();
    const auto shapes = make_built_in_shapes();
    const fs::path run_root = get_run_root();
    std::printf("scratch folder %s\n", run_root.string().c_str());
    test_kind_names();
    test_shape_files(shapes);
    test_meta_files(shapes);
    test_key_rule(ctx, shapes);
    test_field_on_request();
    test_compiled_shapes(am, shapes, run_root);
    test_plane_gi_geometry(am, run_root);
    std::printf("mesh shapes: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("mesh shapes", run_mesh_shapes_suite)
