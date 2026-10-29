/*
 * Validation suite for generated assets: files an import writes next to its source as a by-product, such as a
 * model's default materials.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "generated assets"
 *
 * An import rewrites its generated files only while they still hold what it last wrote, so these pin:
 *   - a first write creates the file and records its source and fingerprint in the .meta;
 *   - a write of the same content touches nothing, and line endings alone are not an edit;
 *   - an untouched file takes a new import, an edited one is kept, and releasing it lets the next import in;
 *   - a file without a record is adopted when it holds the import and kept otherwise;
 *   - a file under the name an earlier import gave it moves to the new name with its .meta, keeping its id;
 *   - two models in one folder with a same-named material at the same index write two materials with two ids,
 *     also when both used to share one file under the earlier name;
 *   - a texture a model embeds, extracted by the model's first import, gets an id there, and the default material
 *     binds it by that id, as every reimport does.
 *
 * Every run writes into a fresh folder under app:/generated_assets and nothing is deleted.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <asset_compiler/asset_compiler.h>
#include <asset_compiler/generated_asset.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/rendering/mesh.h>
#include <filesystem/file_fingerprint.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

using namespace unravel;

namespace
{

/// One triangle, (0,0,0) (1,0,0) (0,1,0), as nine little-endian floats.
constexpr const char* k_triangle_buffer = "data:application/octet-stream;base64,"
                                          "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
/// A 1x1 RGB PNG.
constexpr const char* k_pixel_image = "data:image/png;base64,"
                                      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR42mM4UaEBAAN0AWn1BwN7AAAAAElFTkSuQmCC";
constexpr const char* k_material_name = "Brass";
/// The name the default material of slot 0 had before it was named after its model.
constexpr const char* k_former_material_file = "[0] Brass.mat";

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

auto get_run_root() -> fs::path
{
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const fs::path root = fs::resolve_protocol("app:/generated_assets") / ("run_" + std::to_string(stamp));
    fs::error_code err;
    fs::create_directories(root, err);
    return root;
}

void write_text(const fs::path& path, const std::string& text)
{
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

auto get_meta_path(const fs::path& path) -> fs::path
{
    return fs::path(path.string() + ".meta");
}

auto load_meta(const fs::path& path) -> asset_meta
{
    asset_meta meta;
    load_from_file(get_meta_path(path).string(), meta);
    return meta;
}

auto get_registered_uid(asset_manager& am, const fs::path& path) -> hpp::uuid
{
    return am.get_metadata_for_path(path).meta.uid;
}

auto is_on_disk(const fs::path& path) -> bool
{
    fs::error_code err;
    return fs::exists(path, err);
}

auto make_text_writer(const std::string& text) -> generated_asset::content_writer_t
{
    return [text](const fs::path& temp)
    {
        write_text(temp, text);
    };
}

/// Writes @p text to @p path as a generated asset of @p source_uid, registered under the path's own id.
auto write_generated(asset_manager& am, const fs::path& path, const hpp::uuid& source_uid, const std::string& text)
    -> generated_asset::write_result
{
    const auto uid = am.add_asset_for_path(path, false);
    return generated_asset::write_file(am, path, uid, source_uid, make_text_writer(text));
}

void test_write_cycle(asset_manager& am, const fs::path& run_root)
{
    using generated_asset::write_result;
    const fs::path path = run_root / "cycle" / "Model_[0] Wood.mat";
    const auto source_uid = generate_uuid();
    fs::error_code err;
    fs::create_directories(path.parent_path(), err);
    check(write_generated(am, path, source_uid, "first\n") == write_result::written, "a first write writes");
    const auto meta = load_meta(path);
    check(meta.generated.source_uid == source_uid, "the .meta records the source");
    check(meta.generated.fingerprint == fs::hash_file_fingerprint(path, true), "the .meta records the fingerprint");
    check(meta.uid == get_registered_uid(am, path), "the .meta carries the registered id");
    const auto written_at = fs::last_write_time(path, err);
    check(write_generated(am, path, source_uid, "first\n") == write_result::unchanged, "the same content is unchanged");
    check(fs::last_write_time(path, err) == written_at, "unchanged content is not rewritten");
    write_text(path, "first\r\n");
    check(write_generated(am, path, source_uid, "first\n") == write_result::unchanged, "line endings are not an edit");
    check(write_generated(am, path, source_uid, "second\n") == write_result::written, "an untouched file takes a new import");
    check(read_text(path) == "second\n", "the new import is on disk");
    write_text(path, "edited\n");
    check(write_generated(am, path, source_uid, "third\n") == write_result::kept_edited, "an edited file is kept");
    check(read_text(path) == "edited\n", "the edit is still on disk");
    check(generated_asset::is_edited(path, load_meta(path).generated), "the kept file reads as edited");
    check(generated_asset::release_to_import(am, path), "an edited file can be released");
    check(write_generated(am, path, source_uid, "third\n") == write_result::written, "a released file takes the import");
    check(read_text(path) == "third\n", "the released file holds the import");
    check(!generated_asset::is_edited(path, load_meta(path).generated), "the released file is no longer edited");
}

void test_files_without_record(asset_manager& am, const fs::path& run_root)
{
    using generated_asset::write_result;
    const auto source_uid = generate_uuid();
    const fs::path same = run_root / "unrecorded" / "Model_[0] Same.mat";
    write_text(same, "import\n");
    check(write_generated(am, same, source_uid, "import\n") == write_result::unchanged,
          "an unrecorded file holding the import is adopted");
    check(load_meta(same).generated.source_uid == source_uid, "the adopted file records its source");
    check(!generated_asset::is_edited(same, load_meta(same).generated), "the adopted file is not edited");
    const fs::path other = run_root / "unrecorded" / "Model_[1] Other.mat";
    write_text(other, "user\n");
    check(write_generated(am, other, source_uid, "import\n") == write_result::kept_edited,
          "an unrecorded file holding something else is kept");
    check(read_text(other) == "user\n", "the kept file is untouched");
    check(load_meta(other).generated.source_uid == source_uid, "the kept file records its source");
    check(generated_asset::is_edited(other, load_meta(other).generated), "the kept file reads as edited");
}

void test_adopt_former_file(asset_manager& am, const fs::path& run_root)
{
    const fs::path dir = run_root / "former";
    const fs::path former = dir / k_former_material_file;
    const fs::path first = dir / "First_[0] Brass.mat";
    const fs::path second = dir / "Second_[0] Brass.mat";
    write_text(former, "former\n");
    const auto former_uid = am.add_asset_for_path(former, false);
    asset_meta former_meta;
    former_meta.uid = former_uid;
    former_meta.type = ".mat";
    save_to_file(get_meta_path(former).string(), former_meta);
    check(generated_asset::adopt_former_file(am, former, first), "the first claim takes the former file");
    check(!is_on_disk(former) && !is_on_disk(get_meta_path(former)), "the former file and its .meta are gone");
    check(read_text(first) == "former\n", "the content moved");
    check(load_meta(first).uid == former_uid, "the moved .meta keeps the id");
    check(get_registered_uid(am, first) == former_uid, "the database has the id at the new path");
    check(!generated_asset::adopt_former_file(am, former, second), "a second claim finds nothing to take");
    check(!is_on_disk(second), "the second claim writes nothing");
    write_text(former, "again\n");
    check(!generated_asset::adopt_former_file(am, former, first), "an existing target is not replaced");
    check(read_text(first) == "former\n" && is_on_disk(former), "both files stay as they were");
}

/**
 * @brief A one-triangle glTF model at @p path whose one material, named k_material_name, has @p pbr_properties.
 * @param texture_arrays The "textures" and "images" members the properties reference, each followed by a comma.
 */
void write_gltf_model(const fs::path& path, const std::string& pbr_properties, const std::string& texture_arrays)
{
    const auto gltf = fmt::format(R"({{
  "asset": {{"version": "2.0"}},
  "scene": 0,
  "scenes": [{{"nodes": [0]}}],
  "nodes": [{{"mesh": 0}}],
  "meshes": [{{"primitives": [{{"attributes": {{"POSITION": 0}}, "material": 0}}]}}],
  "materials": [{{"name": "{}", "pbrMetallicRoughness": {{{}}}}}],{}
  "buffers": [{{"byteLength": 36, "uri": "{}"}}],
  "bufferViews": [{{"buffer": 0, "byteOffset": 0, "byteLength": 36}}],
  "accessors": [{{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                  "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]}}]
}}
)",
                                 k_material_name,
                                 pbr_properties,
                                 texture_arrays,
                                 k_triangle_buffer);
    write_text(path, gltf);
}

void write_model(const fs::path& path, float red, float green, float blue)
{
    write_gltf_model(path, fmt::format(R"("baseColorFactor": [{}, {}, {}, 1.0])", red, green, blue), "");
}

/// A model whose base color map is an image embedded in the file, as a .glb packs it.
void write_embedded_texture_model(const fs::path& path)
{
    write_gltf_model(path,
                     R"("baseColorTexture": {"index": 0})",
                     fmt::format(R"(
  "textures": [{{"source": 0}}],
  "images": [{{"uri": "{}"}}],)",
                                 k_pixel_image));
}

/// A .meta for the model at @p path that skips the costly bakes, which these checks do not need.
void write_model_meta(asset_manager& am, const fs::path& path)
{
    auto importer = std::make_shared<mesh_importer_meta>();
    importer->model.generate_lods = false;
    importer->sdf.generate_sdf = false;
    importer->cards.generate_cards = false;
    asset_meta meta;
    meta.uid = generate_uuid();
    meta.type = path.extension().string();
    meta.importer = importer;
    save_to_file(get_meta_path(path).string(), meta);
    am.add_asset_info_for_path(path, meta, true);
}

auto compile_model(asset_manager& am, const fs::path& path) -> bool
{
    const fs::path output = path.parent_path() / "compiled" / (path.filename().string() + ".asset");
    fs::error_code err;
    fs::create_directories(output.parent_path(), err);
    return asset_compiler::compile<mesh>(am, get_meta_path(path), output);
}

auto get_material_path(const fs::path& model) -> fs::path
{
    return model.parent_path() / (model.stem().string() + "_[0] " + k_material_name + ".mat");
}

/// Moves @p model's default material back to the name an earlier import gave it, without a record.
void make_former_material(asset_manager& am, const fs::path& model)
{
    const auto path = get_material_path(model);
    const fs::path former = model.parent_path() / k_former_material_file;
    auto meta = load_meta(path);
    meta.generated = {};
    fs::error_code err;
    fs::rename(path, former, err);
    fs::remove(get_meta_path(path), err);
    save_to_file(get_meta_path(former).string(), meta);
    am.rename_asset_info_for_path(path, former);
}

void test_models_in_one_folder(asset_manager& am, const fs::path& run_root)
{
    const fs::path dir = run_root / "models";
    const fs::path bowl = dir / "Wheel_Bowl.gltf";
    const fs::path lamp = dir / "Lamp.gltf";
    write_model(bowl, 0.8f, 0.6f, 0.2f);
    write_model(lamp, 0.2f, 0.3f, 0.9f);
    write_model_meta(am, bowl);
    write_model_meta(am, lamp);
    check(compile_model(am, bowl) && compile_model(am, lamp), "both models compile");
    const auto bowl_material = get_material_path(bowl);
    const auto lamp_material = get_material_path(lamp);
    check(is_on_disk(bowl_material) && is_on_disk(lamp_material), "each model writes its own material");
    check(!is_on_disk(dir / k_former_material_file), "nothing is written under the shared name");
    check(read_text(bowl_material) != read_text(lamp_material), "the two materials hold their own model's values");
    check(load_meta(bowl_material).uid != load_meta(lamp_material).uid, "the two materials have two ids");
    check(load_meta(bowl_material).generated.source_uid == load_meta(bowl).uid, "the bowl material names the bowl");
    const auto lamp_text = read_text(lamp_material);
    write_text(lamp_material, lamp_text + " ");
    check(compile_model(am, lamp), "the lamp compiles again");
    check(read_text(lamp_material) == lamp_text + " ", "a reimport keeps the edited material");
    check(generated_asset::release_to_import(am, lamp_material) && compile_model(am, lamp), "a reset reimports");
    check(read_text(lamp_material) == lamp_text, "the reset material holds the import");
    // One model's material under the earlier name: it moves back under its model's name with its id.
    const auto bowl_uid = load_meta(bowl_material).uid;
    make_former_material(am, bowl);
    check(compile_model(am, lamp) && compile_model(am, bowl), "both models compile over the former name");
    check(!is_on_disk(dir / k_former_material_file), "the former file is gone, not duplicated");
    check(load_meta(bowl_material).uid == bowl_uid, "the renamed material keeps its id");
    check(get_registered_uid(am, bowl_material) == bowl_uid, "the database has the id at the new name");
    check(!generated_asset::is_edited(bowl_material, load_meta(bowl_material).generated),
          "the renamed material is the import's again");
    // Both models used to share the former file. The first to compile takes it as it is, the other writes its own.
    make_former_material(am, bowl);
    fs::error_code err;
    fs::remove(lamp_material, err);
    fs::remove(get_meta_path(lamp_material), err);
    check(compile_model(am, lamp) && compile_model(am, bowl), "both models compile over a shared former file");
    check(!is_on_disk(dir / k_former_material_file), "the shared former file is gone, not duplicated");
    check(load_meta(lamp_material).uid == bowl_uid, "the first model takes the shared file and its id");
    check(generated_asset::is_edited(lamp_material, load_meta(lamp_material).generated),
          "the taken file keeps the other model's values, as an edit");
    check(is_on_disk(bowl_material) && load_meta(bowl_material).uid != bowl_uid, "the other model writes a new material");
    check(!generated_asset::is_edited(bowl_material, load_meta(bowl_material).generated),
          "the new material is the import's");
}

void test_embedded_texture_binding(asset_manager& am, const fs::path& run_root)
{
    const fs::path model = run_root / "embedded" / "Crate.gltf";
    write_embedded_texture_model(model);
    write_model_meta(am, model);
    check(compile_model(am, model), "the model with an embedded texture compiles");
    // The first import writes the texture, so neither a .meta nor a database entry existed for it before.
    const fs::path texture = model.parent_path() / "[0] BaseColor Crate.png";
    check(is_on_disk(texture), "the embedded texture is extracted next to the model");
    const auto texture_uid = load_meta(texture).uid;
    check(!texture_uid.is_nil(), "the extracted texture's .meta has an id");
    check(get_registered_uid(am, texture) == texture_uid, "the database has the texture under its .meta id");
    const auto material = get_material_path(model);
    const auto material_text = read_text(material);
    check(material_text.find(hpp::to_string(texture_uid)) != std::string::npos,
          "the default material binds the extracted texture by its id");
    check(compile_model(am, model), "the model compiles again");
    check(load_meta(texture).uid == texture_uid, "a reimport keeps the texture's id");
    check(read_text(material) == material_text, "a reimport binds the texture as the first import did");
}

auto run_generated_assets_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    auto& am = ctx.get_cached<asset_manager>();
    const fs::path run_root = get_run_root();
    std::printf("scratch folder %s\n", run_root.string().c_str());
    test_write_cycle(am, run_root);
    test_files_without_record(am, run_root);
    test_adopt_former_file(am, run_root);
    test_models_in_one_folder(am, run_root);
    test_embedded_texture_binding(am, run_root);
    std::printf("generated assets: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("generated assets", run_generated_assets_suite)
