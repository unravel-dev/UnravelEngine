/*
 * A normal map's green direction (editor/core/asset_compiler/normal_map_green.h):
 *   - the texels of a surface read green up or green down as they were made, in each channel layout and at a
 *     reduced size;
 *   - an odd number of flipped axes reads as green down, an even number as green up;
 *   - maps that do not tell read as unknown: flat, ridges that run one way, small, noise, halves of both
 *     conventions;
 *   - a map that is mostly a baker's padding reads as unknown whole and by its direction through the coverage
 *     grid of the model that draws it; a grid of a repeating map or of a few vertices reads the map whole;
 *   - file names that state the convention, and names that only look as if they did;
 *   - files: a PNG's texels, a name that overrules them, a file that does not load;
 *   - the Normal Y setting: settings saved with the Invert Normal Y checkbox load as Down or Auto, what the
 *     settings and a file's name state about turning the map over, and the compile parameters of a map on Auto;
 *   - a mesh import leaves a normal map on Auto, which the texture compile reads, and records Up or Down only
 *     where the texels its meshes draw read differently from the whole map; a Normal Y someone chose stays, and
 *     the asset database takes the settings the import wrote;
 *   - glTF and OBJ files read by the importer: their UVs select the map's texels from the top of the image.
 *
 * Opt-in: UNRAVEL_NORMAL_MAP_CORPUS=<list> reads every file of a list, one "<label><tab><path>" a line, and prints
 * its direction, score and time, to compare the test against maps whose convention is known.
 */

#include "../tests.h"
#include "normal_map_fixture.h"

#include <asset_compiler/importers/mesh/mesh_import_materials.h>
#include <asset_compiler/importers/mesh_importer.h>
#include <asset_compiler/normal_map_green.h>
#include <asset_compiler/texture_target.h>
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/rendering/mesh.h>
#include <graphics/vertex_decl.h>

#include <assimp/material.h>
#include <assimp/scene.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace unravel::test;

namespace
{
namespace ac = unravel::asset_compiler;
using ac::normal_green;
using unravel::texture_importer_meta;

constexpr std::uint32_t MAP_SIDE = 512;

auto get_direction_name(normal_green direction) -> const char*
{
    switch(direction)
    {
        case normal_green::up:
            return "up";
        case normal_green::down:
            return "down";
        case normal_green::unknown:
        default:
            return "unknown";
    }
}

auto describe(const ac::normal_green_reading& reading) -> std::string
{
    return std::string(get_direction_name(reading.direction)) + ", score " + std::to_string(reading.score);
}

auto read_square(const std::vector<std::uint8_t>& texels,
                 std::uint32_t side = MAP_SIDE,
                 gfx::normal_map_channels channels = gfx::normal_map_channels::xyz)
    -> ac::normal_green_reading
{
    return ac::read_normal_green(texels.data(), side, side, side * 4, channels);
}

/// The texels with one channel's values turned around.
auto flip_channel(std::vector<std::uint8_t> texels, std::size_t channel) -> std::vector<std::uint8_t>
{
    for(std::size_t i = channel; i < texels.size(); i += 4)
    {
        texels[i] = static_cast<std::uint8_t>(255 - texels[i]);
    }
    return texels;
}

void test_surface_maps()
{
    const auto up = make_surface_normal_map({});
    const auto down = make_surface_normal_map({.is_green_up = false});

    const auto up_reading = read_square(up);
    check(up_reading.direction == normal_green::up && up_reading.score > 0.5f,
          "a surface made green up reads green up: " + describe(up_reading));
    const auto down_reading = read_square(down);
    check(down_reading.direction == normal_green::down && down_reading.score < -0.5f,
          "a surface made green down reads green down: " + describe(down_reading));

    check(read_square(flip_channel(up, 0)).direction == normal_green::down,
          "a green-up map with X flipped reads green down: the test counts flipped axes, it does not name them");
    check(read_square(flip_channel(flip_channel(up, 0), 1)).direction == normal_green::up,
          "a map with both axes flipped reads as one with none flipped");

    // X in red and Y in alpha, as a compiled ASTC normal map keeps them.
    auto red_alpha = down;
    for(std::size_t i = 0; i < red_alpha.size(); i += 4)
    {
        red_alpha[i + 3] = red_alpha[i + 1];
        red_alpha[i + 1] = red_alpha[i];
    }
    check(read_square(red_alpha, MAP_SIDE, gfx::normal_map_channels::x_red_y_alpha).direction == normal_green::down,
          "a map with Y in alpha reads through red and alpha");

    // X in alpha and a flat red, the layout older tools gave BC3 normal maps.
    auto alpha_green = down;
    for(std::size_t i = 0; i < alpha_green.size(); i += 4)
    {
        alpha_green[i + 3] = alpha_green[i];
        alpha_green[i] = 255;
    }
    check(read_square(alpha_green).direction == normal_green::down, "a map with a flat red and X in alpha reads through alpha and green");

    const std::uint32_t wide = 4096;
    const auto large = make_surface_normal_map({.width = wide, .height = MAP_SIDE, .is_green_up = false});
    const auto large_reading = ac::read_normal_green(large.data(), wide, MAP_SIDE, wide * 4);
    check(large_reading.direction == normal_green::down, "a map over 2048 texels a side reads at a reduced size: " + describe(large_reading));
}

void test_maps_that_do_not_tell()
{
    std::vector<std::uint8_t> flat(std::size_t(MAP_SIDE) * MAP_SIDE * 4);
    for(std::size_t i = 0; i < flat.size(); i += 4)
    {
        flat[i] = 128;
        flat[i + 1] = 128;
        flat[i + 2] = 255;
        flat[i + 3] = 255;
    }
    check(read_square(flat).direction == normal_green::unknown, "a flat map reads as unknown");

    const auto ridges = read_square(make_surface_normal_map({.has_two_axes = false}));
    check(ridges.direction == normal_green::unknown, "ridges that run one way read as unknown: " + describe(ridges));

    const std::uint32_t small_side = 64;
    const auto tiny = read_square(make_surface_normal_map({.width = small_side, .height = small_side, .is_green_up = false}), small_side);
    check(tiny.direction == normal_green::unknown, "a small map reads as unknown: " + describe(tiny));

    std::vector<std::uint8_t> noise(std::size_t(MAP_SIDE) * MAP_SIDE * 4, 255);
    std::uint32_t state = 12345;
    for(std::size_t i = 0; i < noise.size(); i += 4)
    {
        state = state * 1664525u + 1013904223u;
        noise[i] = static_cast<std::uint8_t>(96 + ((state >> 8) & 63));
        noise[i + 1] = static_cast<std::uint8_t>(96 + ((state >> 16) & 63));
    }
    const auto noise_reading = read_square(noise);
    check(noise_reading.direction == normal_green::unknown, "texels that are no surface read as unknown: " + describe(noise_reading));

    // The left half green up and the right half green down, as a map put together from two sources.
    auto halves = make_surface_normal_map({});
    const auto down = make_surface_normal_map({.is_green_up = false});
    for(std::uint32_t row = 0; row < MAP_SIDE; ++row)
    {
        for(std::uint32_t column = MAP_SIDE / 2; column < MAP_SIDE; ++column)
        {
            const std::size_t at = (std::size_t(row) * MAP_SIDE + column) * 4 + 1;
            halves[at] = down[at];
        }
    }
    const auto halves_reading = read_square(halves);
    check(halves_reading.direction == normal_green::unknown, "a map of both conventions reads as unknown: " + describe(halves_reading));

    check(ac::read_normal_green(nullptr, 0, 0, 0).direction == normal_green::unknown, "no texels read as unknown");
}

/// The coverage grid of a model whose vertices are spread evenly over a part of the UV square.
auto mark_part(float left, float top, float width, float height) -> ac::normal_map_coverage
{
    constexpr std::uint32_t VERTICES_A_SIDE = 96;
    ac::normal_map_coverage coverage;
    for(std::uint32_t row = 0; row < VERTICES_A_SIDE; ++row)
    {
        for(std::uint32_t column = 0; column < VERTICES_A_SIDE; ++column)
        {
            coverage.add_vertex(left + width * (float(column) + 0.5f) / float(VERTICES_A_SIDE),
                                top + height * (float(row) + 0.5f) / float(VERTICES_A_SIDE));
        }
    }
    return coverage;
}

void test_padded_maps()
{
    const auto padded = make_padded_normal_map(MAP_SIDE, false);
    const auto read_with = [&](const ac::normal_map_coverage& coverage)
    {
        return ac::read_normal_green(padded.data(), MAP_SIDE, MAP_SIDE, MAP_SIDE * 4, gfx::normal_map_channels::xyz, &coverage);
    };

    const auto whole = read_square(padded);
    check(whole.direction == normal_green::unknown, "a map that is mostly padding reads as unknown when it is read whole: " + describe(whole));

    const auto drawn = mark_part(0.0f, 0.0f, PADDED_MAP_USED_WIDTH, PADDED_MAP_USED_HEIGHT);
    check_near(drawn.get_marked_share(), PADDED_MAP_USED_WIDTH * PADDED_MAP_USED_HEIGHT, 0.03f, "vertices mark the cells of the part they lie in");
    check(drawn.is_usable(), "a grid marked over a third of the map tells what the model draws");
    const auto used = read_with(drawn);
    check(used.direction == normal_green::down, "read by the texels the model draws, the map reads green down: " + describe(used));

    // The grid's rows run down the image, like the rows of the map.
    constexpr float STRIP = 0.25f;
    check(read_with(mark_part(0.0f, 0.0f, PADDED_MAP_USED_WIDTH, STRIP)).direction == normal_green::down,
          "vertices along the top select the surface there");
    check(read_with(mark_part(0.0f, 1.0f - STRIP, PADDED_MAP_USED_WIDTH, STRIP)).direction == normal_green::up,
          "vertices along the bottom select the padding there, which reads the other way");

    auto repeating = drawn;
    for(int i = 0; i < 400; ++i)
    {
        repeating.add_vertex(3.5f, 0.25f);
    }
    check(!repeating.is_usable() && read_with(repeating).direction == normal_green::unknown,
          "a map that repeats over the model, with vertices outside the UV square, is read whole");

    ac::normal_map_coverage sparse;
    sparse.add_vertex(0.0f, 0.0f);
    sparse.add_vertex(PADDED_MAP_USED_WIDTH, 0.0f);
    sparse.add_vertex(0.0f, PADDED_MAP_USED_HEIGHT);
    sparse.add_vertex(PADDED_MAP_USED_WIDTH, PADDED_MAP_USED_HEIGHT);
    check(!sparse.is_usable() && read_with(sparse).direction == normal_green::unknown,
          "a map drawn by a few large triangles marks too little and is read whole");
    check(!ac::normal_map_coverage().is_usable(), "a grid without vertices tells nothing");
}

void test_file_names()
{
    const std::pair<const char*, normal_green> cases[] = {
        {"brick_wall_nor_gl_4k.png", normal_green::up},
        {"brick_wall_nor_dx_4k.png", normal_green::down},
        {"Bricks076_2K-JPG_NormalGL.jpg", normal_green::up},
        {"Bricks076_2K-JPG_NormalDX.jpg", normal_green::down},
        {"rock_normal_opengl.png", normal_green::up},
        {"rock_normal-directx.tga", normal_green::down},
        {"rock_Normal_DX11.png", normal_green::down},
        {"rock_nrm_ogl.png", normal_green::up},
        {"T_Rock_Normals_DX.png", normal_green::down},
        {"folder_nor_dx/rock_normal.png", normal_green::unknown},
        {"rock_normal.png", normal_green::unknown},
        {"rock_normal_dxt5.dds", normal_green::unknown},
        {"rock_normal_glossy.png", normal_green::unknown},
        {"minor_gl.png", normal_green::unknown},
        {"rock_gl.png", normal_green::unknown},
    };
    for(const auto& [name, expected] : cases)
    {
        const auto named = ac::get_named_normal_green(name);
        check(named == expected, std::string(name) + " states " + get_direction_name(expected) + ", read as " + get_direction_name(named));
    }
    check(ac::invert_normal_green(normal_green::up) == normal_green::down &&
              ac::invert_normal_green(normal_green::down) == normal_green::up &&
              ac::invert_normal_green(normal_green::unknown) == normal_green::unknown,
          "turning Y over turns a direction around and leaves an unknown one unknown");
}

void test_files(const fs::path& folder)
{
    const auto down = make_surface_normal_map({.is_green_up = false});
    require(write_rgba_png(folder / "bumps.png", down, MAP_SIDE, MAP_SIDE) &&
                write_rgba_png(folder / "bumps_nor_gl.png", down, MAP_SIDE, MAP_SIDE),
            "the test maps are written");
    check(ac::read_normal_green(folder / "bumps.png").direction == normal_green::down, "a PNG's texels are read");
    check(ac::read_compiled_normal_green(folder / "bumps.png").direction == normal_green::down,
          "an uncompressed compiled map reads through red and green");
    check(ac::find_normal_green(folder / "bumps.png") == normal_green::down, "a file whose name states nothing goes by its texels");
    check(ac::find_normal_green(folder / "bumps_nor_gl.png") == normal_green::up, "a file whose name states the convention goes by its name");
    check(ac::read_normal_green(folder / "missing.png").direction == normal_green::unknown, "a file that does not load reads as unknown");
}

/// The import settings the texture's .meta holds, or null.
auto read_texture_settings(const fs::path& texture) -> std::shared_ptr<texture_importer_meta>
{
    unravel::asset_meta meta;
    unravel::load_from_file(texture.string() + ".meta", meta);
    return safe_pointer_cast<texture_importer_meta>(meta.importer);
}

using normal_direction = texture_importer_meta::normal_direction;

/// Writes the .meta of a texture with these settings, as an import or a person leaves one.
auto write_texture_settings(const fs::path& texture,
                            const char* uid,
                            texture_importer_meta::texture_type type,
                            normal_direction normal_y) -> unravel::asset_meta
{
    unravel::asset_meta meta;
    meta.uid = hpp::uuid::from_string(uid).value();
    meta.type = ".png";
    auto settings = std::make_shared<texture_importer_meta>();
    settings->type = type;
    settings->normal_y = normal_y;
    meta.importer = settings;
    unravel::save_to_file(texture.string() + ".meta", meta);
    return meta;
}

auto read_text(const fs::path& file) -> std::string
{
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void test_normal_y_setting(const fs::path& folder)
{
    fs::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path texture = folder / "wall.png";
    const auto meta_file = texture.string() + ".meta";

    // The settings as they are saved now, and as the Invert Normal Y checkbox saved them.
    write_texture_settings(texture, "70000000-0000-4000-8000-000000000010", texture_importer_meta::texture_type::normal_map,
                           normal_direction::automatic);
    const std::string saved = read_text(meta_file);
    const std::string current_field = "\"normal_y\": 0";
    require(saved.find(current_field) != std::string::npos, "the settings are saved with a normal_y field: " + saved);
    check(saved.find("invert_normal_y") == std::string::npos, "and without the checkbox's field");
    const auto load_with = [&](const std::string& field)
    {
        std::string text = saved;
        text.replace(text.find(current_field), current_field.size(), field);
        std::ofstream(meta_file, std::ios::binary) << text;
        const auto settings = read_texture_settings(texture);
        return settings ? settings->normal_y : normal_direction::up;
    };
    check(load_with("\"invert_normal_y\": true") == normal_direction::down, "a checked Invert Normal Y loads as Down");
    check(load_with("\"invert_normal_y\": false") == normal_direction::automatic, "an unchecked one loads as Auto");
    check(load_with("\"normal_y\": 1") == normal_direction::up && load_with("\"normal_y\": 2") == normal_direction::down,
          "Up and Down load as saved");

    // What the settings and the file's name state about turning the map over.
    texture_importer_meta settings;
    settings.type = texture_importer_meta::texture_type::normal_map;
    const auto get_stated = [&](normal_direction normal_y, const char* name)
    {
        settings.normal_y = normal_y;
        return ac::get_stated_normal_y_inversion(settings, name);
    };
    check(get_stated(normal_direction::down, "wall.png") == hpp::optional<bool>(true) &&
              get_stated(normal_direction::up, "wall_nor_dx.png") == hpp::optional<bool>(false),
          "Down always turns the map over and Up never does, whatever its name");
    check(get_stated(normal_direction::automatic, "wall_nor_dx.png") == hpp::optional<bool>(true) &&
              get_stated(normal_direction::automatic, "wall_nor_gl.png") == hpp::optional<bool>(false),
          "Auto goes by a convention the name states");
    check(!get_stated(normal_direction::automatic, "wall.png").has_value(), "Auto on a name that states nothing leaves it to the texels");
    settings.type = texture_importer_meta::texture_type::automatic;
    check(get_stated(normal_direction::automatic, "wall_nor_dx.png") == hpp::optional<bool>(false) &&
              get_stated(normal_direction::down, "wall.png") == hpp::optional<bool>(true),
          "a texture that is no normal map is turned over only when Down says so");

    // Only a normal map on Auto is read at compile, and only its compile parameters say so.
    const auto get_parameters = [](texture_importer_meta::texture_type type, normal_direction normal_y)
    {
        texture_importer_meta importer;
        importer.type = type;
        importer.normal_y = normal_y;
        return ac::resolve_texture_parameters(importer, {});
    };
    const auto on_auto = get_parameters(texture_importer_meta::texture_type::normal_map, normal_direction::automatic);
    const auto on_down = get_parameters(texture_importer_meta::texture_type::normal_map, normal_direction::down);
    const auto color = get_parameters(texture_importer_meta::texture_type::automatic, normal_direction::automatic);
    check(on_auto.reads_normal_y && !on_down.reads_normal_y && !color.reads_normal_y,
          "the compile reads the direction of a normal map on Auto alone");
    check(ac::to_string(on_auto).find("/normal_y_") != std::string::npos && ac::to_string(on_down) == ac::to_string(color),
          "and its compile parameters alone carry the reading's version, so a change to the reading compiles only such maps again");
    fs::remove_all(folder, ec);
}

/// A mesh of the material at `material` whose vertices are spread evenly over a part of the UV square.
auto make_grid_mesh(unsigned int material, float left, float top, float width, float height) -> aiMesh*
{
    constexpr unsigned int VERTICES_A_SIDE = 96;
    auto* mesh = new aiMesh();
    mesh->mMaterialIndex = material;
    mesh->mNumVertices = VERTICES_A_SIDE * VERTICES_A_SIDE;
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
    mesh->mNumUVComponents[0] = 2;
    for(unsigned int row = 0; row < VERTICES_A_SIDE; ++row)
    {
        for(unsigned int column = 0; column < VERTICES_A_SIDE; ++column)
        {
            mesh->mTextureCoords[0][row * VERTICES_A_SIDE + column] =
                aiVector3D(left + width * (float(column) + 0.5f) / float(VERTICES_A_SIDE),
                           top + height * (float(row) + 0.5f) / float(VERTICES_A_SIDE),
                           0.0f);
        }
    }
    return mesh;
}

void test_import_records_normal_y(rtti::context& ctx)
{
    auto& am = ctx.get_cached<unravel::asset_manager>();
    const std::string folder_key = "app:/data/normal_green_import";
    const fs::path folder = fs::resolve_protocol(folder_key);
    fs::error_code ec;
    fs::remove_all(folder, ec);

    const auto down = make_surface_normal_map({.is_green_up = false});
    const auto padded = make_padded_normal_map(MAP_SIDE, false);
    // A green-down surface whose left quarter is flat.
    constexpr float FLAT_SHARE = 0.25f;
    auto flat_left = down;
    for(std::uint32_t row = 0; row < MAP_SIDE; ++row)
    {
        for(std::uint32_t column = 0; column < MAP_SIDE / 4; ++column)
        {
            std::uint8_t* texel = flat_left.data() + (std::size_t(row) * MAP_SIDE + column) * 4;
            texel[0] = 128;
            texel[1] = 128;
            texel[2] = 255;
        }
    }

    enum : unsigned int { DOWN, LISTED, PADDED, PADDED_ALONE, NAMED, CHOSEN, RETYPED, FLAT_DRAWN, MAP_COUNT };
    struct map_case
    {
        const char* name;
        const std::vector<std::uint8_t>* texels;
    };
    const map_case maps[MAP_COUNT] = {{"down.png", &down},           {"listed.png", &down},  {"padded.png", &padded},
                                      {"padded_alone.png", &padded}, {"padded_nor_gl.png", &padded},
                                      {"chosen.png", &padded},       {"retyped.png", &padded}, {"flat_drawn.png", &flat_left}};
    for(const auto& map : maps)
    {
        require(write_rgba_png(folder / map.name, *map.texels, MAP_SIDE, MAP_SIDE), "the test maps are written");
    }
    // A map the editor listed before the model was imported: a .meta with an id and the settings a new texture
    // gets, and the same in the asset database.
    const auto listed = write_texture_settings(folder / maps[LISTED].name, "70000000-0000-4000-8000-000000000003",
                                               texture_importer_meta::texture_type::automatic, normal_direction::automatic);
    am.add_asset_info_for_path(folder / maps[LISTED].name, listed, true);
    // A map whose Normal Y someone chose, and one an earlier import made a normal map and left on Auto.
    write_texture_settings(folder / maps[CHOSEN].name, "70000000-0000-4000-8000-000000000001",
                           texture_importer_meta::texture_type::normal_map, normal_direction::up);
    write_texture_settings(folder / maps[RETYPED].name, "70000000-0000-4000-8000-000000000004",
                           texture_importer_meta::texture_type::normal_map, normal_direction::automatic);

    aiScene scene;
    scene.mNumMaterials = MAP_COUNT;
    scene.mMaterials = new aiMaterial*[MAP_COUNT];
    for(unsigned int i = 0; i < MAP_COUNT; ++i)
    {
        scene.mMaterials[i] = new aiMaterial();
        const aiString path(maps[i].name);
        scene.mMaterials[i]->AddProperty(&path, AI_MATKEY_TEXTURE(aiTextureType_NORMALS, 0));
    }
    // Meshes draw the padded maps with the part that holds the surface, and the last map with its flat quarter.
    // Nothing draws down.png, listed.png and padded_alone.png.
    const unsigned int drawn_by_surface[] = {PADDED, NAMED, CHOSEN, RETYPED};
    scene.mNumMeshes = static_cast<unsigned int>(std::size(drawn_by_surface)) + 1;
    scene.mMeshes = new aiMesh*[scene.mNumMeshes];
    for(std::size_t i = 0; i < std::size(drawn_by_surface); ++i)
    {
        scene.mMeshes[i] = make_grid_mesh(drawn_by_surface[i], 0.0f, 0.0f, PADDED_MAP_USED_WIDTH, PADDED_MAP_USED_HEIGHT);
    }
    scene.mMeshes[scene.mNumMeshes - 1] = make_grid_mesh(FLAT_DRAWN, 0.0f, 0.0f, FLAT_SHARE, 1.0f);

    std::vector<unravel::importer::imported_material> materials;
    std::vector<unravel::importer::imported_texture> textures;
    unravel::importer::mesh_import::process_materials(am, folder / "model.gltf", folder, &scene, materials, textures);

    const auto is_normal_map_with = [&](unsigned int map, normal_direction normal_y)
    {
        const auto settings = read_texture_settings(folder / maps[map].name);
        return settings && settings->type == texture_importer_meta::texture_type::normal_map && settings->normal_y == normal_y;
    };
    check(is_normal_map_with(DOWN, normal_direction::automatic),
          "a map no mesh decides becomes a normal map on Auto: the texture compile reads it");
    check(is_normal_map_with(PADDED, normal_direction::down),
          "a padded green-down map, unknown when read whole, gets Down from the texels its mesh draws");
    check(is_normal_map_with(PADDED_ALONE, normal_direction::automatic), "the same map with no mesh to tell its padding stays on Auto");
    check(is_normal_map_with(NAMED, normal_direction::automatic), "a map whose name states the convention is not read");
    check(is_normal_map_with(CHOSEN, normal_direction::up), "a Normal Y someone chose stays");
    check(is_normal_map_with(RETYPED, normal_direction::down),
          "a map an earlier import made a normal map and left on Auto gets Down when its model is imported again");
    check(is_normal_map_with(FLAT_DRAWN, normal_direction::up),
          "a map that reads green down only where no mesh draws it gets Up, so the compile does not turn it over");
    check(is_normal_map_with(LISTED, normal_direction::automatic), "a map listed with a new texture's settings is tagged like a new one");
    const auto in_database = am.get_metadata_for_key(folder_key + "/" + maps[LISTED].name).meta;
    const auto listed_settings = safe_pointer_cast<texture_importer_meta>(in_database.importer);
    check(in_database.uid == listed.uid && listed_settings && listed_settings->type == texture_importer_meta::texture_type::normal_map,
          "the asset database takes the tagged settings and keeps the texture's id");
    const auto padded_settings =
        safe_pointer_cast<texture_importer_meta>(am.get_metadata_for_key(folder_key + "/" + maps[PADDED].name).meta.importer);
    check(padded_settings && padded_settings->normal_y == normal_direction::down, "and the Normal Y the import recorded");

    for(const auto& map : maps)
    {
        am.remove_asset_info_for_key(folder_key + "/" + map.name);
    }
    fs::remove_all(folder, ec);
}

/// A flat grid of vertices as a model file holds one: its UVs, v measured from the top of the image, and its
/// triangles.
struct uv_grid
{
    std::vector<float> uvs;
    std::vector<std::uint32_t> indices;
};

/// A grid over the left PADDED_MAP_USED_WIDTH of the UV square, from `top` down for `height`.
auto make_uv_grid(float top, float height) -> uv_grid
{
    constexpr std::uint32_t VERTICES_A_SIDE = 96;
    uv_grid grid;
    for(std::uint32_t row = 0; row < VERTICES_A_SIDE; ++row)
    {
        for(std::uint32_t column = 0; column < VERTICES_A_SIDE; ++column)
        {
            grid.uvs.push_back(PADDED_MAP_USED_WIDTH * (float(column) + 0.5f) / float(VERTICES_A_SIDE));
            grid.uvs.push_back(top + height * (float(row) + 0.5f) / float(VERTICES_A_SIDE));
        }
    }
    for(std::uint32_t row = 0; row + 1 < VERTICES_A_SIDE; ++row)
    {
        for(std::uint32_t column = 0; column + 1 < VERTICES_A_SIDE; ++column)
        {
            const std::uint32_t corner = row * VERTICES_A_SIDE + column;
            grid.indices.insert(grid.indices.end(),
                                {corner, corner + 1, corner + VERTICES_A_SIDE + 1, corner, corner + VERTICES_A_SIDE + 1,
                                 corner + VERTICES_A_SIDE});
        }
    }
    return grid;
}

auto encode_base64(const std::vector<std::uint8_t>& bytes) -> std::string
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::uint32_t chunk = (std::uint32_t(bytes[i]) << 16) |
                                    (i + 1 < bytes.size() ? std::uint32_t(bytes[i + 1]) << 8 : 0u) |
                                    (i + 2 < bytes.size() ? std::uint32_t(bytes[i + 2]) : 0u);
        out += alphabet[(chunk >> 18) & 63];
        out += alphabet[(chunk >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(chunk >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[chunk & 63] : '=';
    }
    return out;
}

/// Writes the grid as a glTF file whose one material binds `normal_map`. glTF measures v from the top.
void write_grid_gltf(const fs::path& file, const uv_grid& grid, const std::string& normal_map)
{
    const std::size_t vertices = grid.uvs.size() / 2;
    std::vector<float> positions;
    std::vector<float> normals;
    float lowest = 0.0f;
    for(std::size_t i = 0; i < vertices; ++i)
    {
        positions.insert(positions.end(), {grid.uvs[i * 2], -grid.uvs[i * 2 + 1], 0.0f});
        normals.insert(normals.end(), {0.0f, 0.0f, 1.0f});
        lowest = std::min(lowest, -grid.uvs[i * 2 + 1]);
    }
    std::vector<std::uint8_t> buffer;
    std::string views;
    const auto append = [&](const void* data, std::size_t bytes)
    {
        views += std::string(views.empty() ? "" : ",") + "{\"buffer\":0,\"byteOffset\":" + std::to_string(buffer.size()) +
                 ",\"byteLength\":" + std::to_string(bytes) + "}";
        const auto* first = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), first, first + bytes);
    };
    append(positions.data(), positions.size() * sizeof(float));
    append(normals.data(), normals.size() * sizeof(float));
    append(grid.uvs.data(), grid.uvs.size() * sizeof(float));
    append(grid.indices.data(), grid.indices.size() * sizeof(std::uint32_t));

    const std::string count = std::to_string(vertices);
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],";
    json += "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,"
            "\"material\":0}]}],";
    json += "\"materials\":[{\"name\":\"Grid\",\"normalTexture\":{\"index\":0}}],\"textures\":[{\"source\":0}],";
    json += "\"images\":[{\"uri\":\"" + normal_map + "\"}],";
    json += "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" + count +
            ",\"type\":\"VEC3\",\"min\":[0," + std::to_string(lowest) + ",0],\"max\":[1,0,0]},";
    json += "{\"bufferView\":1,\"componentType\":5126,\"count\":" + count + ",\"type\":\"VEC3\"},";
    json += "{\"bufferView\":2,\"componentType\":5126,\"count\":" + count + ",\"type\":\"VEC2\"},";
    json += "{\"bufferView\":3,\"componentType\":5125,\"count\":" + std::to_string(grid.indices.size()) + ",\"type\":\"SCALAR\"}],";
    json += "\"bufferViews\":[" + views + "],";
    json += "\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size()) + ",\"uri\":\"data:application/octet-stream;base64," +
            encode_base64(buffer) + "\"}]}";
    std::ofstream(file, std::ios::binary) << json;
}

/// Writes the grid as an OBJ file with a material library whose one material binds `normal_map`. OBJ measures v
/// from the bottom.
void write_grid_obj(const fs::path& file, const uv_grid& grid, const std::string& normal_map)
{
    const std::string library = file.stem().string() + ".mtl";
    std::ofstream(file.parent_path() / library, std::ios::binary) << "newmtl Grid\nKd 1 1 1\nnorm " << normal_map << "\n";
    std::ofstream obj(file, std::ios::binary);
    obj << "mtllib " << library << "\n";
    const std::size_t vertices = grid.uvs.size() / 2;
    for(std::size_t i = 0; i < vertices; ++i)
    {
        obj << "v " << grid.uvs[i * 2] << " " << -grid.uvs[i * 2 + 1] << " 0\n";
    }
    for(std::size_t i = 0; i < vertices; ++i)
    {
        obj << "vt " << grid.uvs[i * 2] << " " << 1.0f - grid.uvs[i * 2 + 1] << "\n";
    }
    obj << "vn 0 0 1\nusemtl Grid\n";
    for(std::size_t i = 0; i + 2 < grid.indices.size(); i += 3)
    {
        obj << "f";
        for(std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto index = grid.indices[i + corner] + 1;
            obj << " " << index << "/" << index << "/1";
        }
        obj << "\n";
    }
}

auto import_model(unravel::asset_manager& am, const fs::path& file) -> bool
{
    unravel::mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    std::vector<unravel::animation_clip> clips;
    std::vector<unravel::importer::imported_material> materials;
    std::vector<unravel::importer::imported_texture> textures;
    return unravel::importer::load_mesh_data_from_file(am, file, unravel::mesh_importer_meta{}, data, clips, materials, textures);
}

void test_imported_uvs_select_texels(rtti::context& ctx)
{
    auto& am = ctx.get_cached<unravel::asset_manager>();
    const std::string folder_key = "app:/data/normal_green_models";
    const fs::path folder = fs::resolve_protocol(folder_key);
    fs::error_code ec;
    fs::remove_all(folder, ec);

    const auto padded = make_padded_normal_map(MAP_SIDE, false);
    const char* const maps[] = {"gltf_top.png", "gltf_bottom.png", "obj_top.png"};
    for(const char* map : maps)
    {
        require(write_rgba_png(folder / map, padded, MAP_SIDE, MAP_SIDE), "the padded map is written");
    }
    // The surface is the top of the map. A grid whose rows ran the other way would select what `bottom` does:
    // the lower part, a third of it padding that reads the other way.
    const auto top = make_uv_grid(0.0f, PADDED_MAP_USED_HEIGHT);
    const auto bottom = make_uv_grid(1.0f - PADDED_MAP_USED_HEIGHT, PADDED_MAP_USED_HEIGHT);
    write_grid_gltf(folder / "top.gltf", top, maps[0]);
    write_grid_gltf(folder / "bottom.gltf", bottom, maps[1]);
    write_grid_obj(folder / "top.obj", top, maps[2]);
    require(import_model(am, folder / "top.gltf") && import_model(am, folder / "bottom.gltf") && import_model(am, folder / "top.obj"),
            "the three models import");

    const auto is_normal_map_with = [&](const char* map, normal_direction normal_y)
    {
        const auto settings = read_texture_settings(folder / map);
        return settings && settings->type == texture_importer_meta::texture_type::normal_map && settings->normal_y == normal_y;
    };
    check(is_normal_map_with(maps[0], normal_direction::down), "a glTF model's UVs select the map's texels from the top of the image");
    check(is_normal_map_with(maps[1], normal_direction::automatic),
          "UVs over the lower part select padding with the surface: nothing is recorded, and the map stays on Auto");
    check(is_normal_map_with(maps[2], normal_direction::down),
          "an OBJ model's UVs, measured from the bottom in its file, select the same texels");

    for(const char* map : maps)
    {
        am.remove_asset_info_for_key(folder_key + "/" + map);
    }
    fs::remove_all(folder, ec);
}

void test_corpus()
{
    const char* list = std::getenv("UNRAVEL_NORMAL_MAP_CORPUS");
    if(list == nullptr || *list == '\0')
    {
        skip("Set UNRAVEL_NORMAL_MAP_CORPUS to a list of normal maps to read them.");
    }
    std::ifstream stream(list);
    require(stream.good(), std::string("the corpus list opens: ") + list);
    std::size_t count = 0;
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto tab = line.find('\t');
        if(tab == std::string::npos)
        {
            continue;
        }
        const std::string label = line.substr(0, tab);
        const std::string path = line.substr(tab + 1);
        const auto start = std::chrono::steady_clock::now();
        const auto reading = ac::read_normal_green(fs::path(path));
        const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s\t%s\t%+.3f\t%.0f ms\t%s\t%s\n",
                    label.c_str(),
                    get_direction_name(reading.direction),
                    reading.score,
                    milliseconds,
                    get_direction_name(ac::get_named_normal_green(path)),
                    path.c_str());
        note_progress();
        ++count;
    }
    check(count > 0, "the corpus list names maps");
}

void run_normal_map_green_suite(rtti::context& ctx)
{
    const auto temp = temp_path("normal_map_green");
    fs::error_code ec;
    fs::remove_all(temp, ec);
    RUN_CASE(test_surface_maps());
    RUN_CASE(test_maps_that_do_not_tell());
    RUN_CASE(test_padded_maps());
    RUN_CASE(test_file_names());
    RUN_CASE(test_files(temp));
    RUN_CASE(test_normal_y_setting(temp / "setting"));
    RUN_CASE(test_import_records_normal_y(ctx));
    RUN_CASE(test_imported_uvs_select_texels(ctx));
    RUN_CASE(test_corpus());
    fs::remove_all(temp, ec);
}

} // namespace

REGISTER_TEST_SUITE("normal map green", run_normal_map_green_suite)
