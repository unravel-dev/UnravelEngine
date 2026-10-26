/*
 * Validation suite: the rendering feature requirement table against the compiled compute shaders.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "render feature shaders"
 *
 * For every compute shader with a source in engine_data/data/shaders, it reads the committed SPIR-V binary's
 * uniform table (binding stages, storage image formats and dimensions) and the GLSL text (image access), and
 * pins:
 *   - every compute shader belongs to a rendering feature (the longest shader prefix of the table) or is a
 *     listed utility;
 *   - its highest binding stage plus one is within the feature's compute bindings;
 *   - no storage image binds at stage 8 or above (OpenGL has image units 0-7);
 *   - every storage image format, dimension and access is one the feature lists;
 *   - every shader prefix of the table matches a shader.
 * A pass that binds a new stage or writes a new format then fails here instead of breaking a device that lacks
 * it.
 */

#include "../tests.h"

#include <engine/rendering/render_feature_support.h>

#include <bimg/bimg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

using namespace unravel;
namespace fs = std::filesystem;

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

/// bgfx's SPIR-V shaderc shifts every binding by kSpirvBindShift (src/shader.h).
constexpr int SPIRV_BIND_SHIFT = 2;
/// Uniform type bits of a bgfx shader binary (src/bgfx_p.h).
constexpr std::uint8_t UNIFORM_SAMPLER_BIT = 0x20;
constexpr std::uint8_t UNIFORM_READ_ONLY_BIT = 0x40;
constexpr std::uint8_t UNIFORM_TYPE_MASK = 0x0F;
/// bgfx::UniformType::End marks a storage image or buffer in the table; its regCount names which.
constexpr std::uint8_t UNIFORM_TYPE_END = 1;
constexpr std::uint16_t DESCRIPTOR_STORAGE_IMAGE = 3;
/// bgfx::TextureDimension ids of a uniform (2D array and 3D; the rest are 2D-like or cubes).
constexpr std::uint8_t DIMENSION_2D_ARRAY = 3;
constexpr std::uint8_t DIMENSION_CUBE = 4;
constexpr std::uint8_t DIMENSION_3D = 6;
/// OpenGL guarantees image units 0-7.
constexpr int MAX_IMAGE_STAGE = 7;
/// Compute shaders that serve several features: they bind no image and stay within any device's bindings.
constexpr const char* UTILITY_SHADERS[] = {"cs_clear_uints"};
constexpr std::uint8_t UTILITY_COMPUTE_BINDINGS = 8;

struct shader_image
{
    std::string name;
    int stage{};
    bgfx::TextureFormat::Enum format{};
    std::uint8_t dimension{};
    bool is_read_only{};
};

struct shader_bindings
{
    int max_stage{-1};
    std::vector<shader_image> images;
};

auto read_file(const fs::path& path) -> std::string
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

template<typename T>
auto read_value(const std::string& data, std::size_t& offset) -> T
{
    T value{};
    if(offset + sizeof(T) <= data.size())
    {
        std::memcpy(&value, data.data() + offset, sizeof(T));
    }
    offset += sizeof(T);
    return value;
}

/// The binding stages and storage images of a compiled SPIR-V compute shader (bgfx shader binary 12+).
auto parse_spirv_bindings(const std::string& data) -> shader_bindings
{
    shader_bindings result;
    std::size_t offset = 4 + 4 + 4 + 8;
    const auto count = read_value<std::uint16_t>(data, offset);
    for(std::uint16_t index = 0; index < count && offset < data.size(); ++index)
    {
        const auto name_size = read_value<std::uint8_t>(data, offset);
        std::string name = data.substr(offset, name_size);
        offset += name_size;
        const auto type = read_value<std::uint8_t>(data, offset);
        read_value<std::uint8_t>(data, offset);
        const auto reg_index = read_value<std::uint16_t>(data, offset);
        const auto reg_count = read_value<std::uint16_t>(data, offset);
        read_value<std::uint8_t>(data, offset);
        const auto dimension = read_value<std::uint8_t>(data, offset);
        const auto format = read_value<std::uint16_t>(data, offset);
        const bool is_sampler = (type & UNIFORM_SAMPLER_BIT) != 0;
        const bool is_storage = (type & UNIFORM_TYPE_MASK) == UNIFORM_TYPE_END;
        if(!is_sampler && !is_storage)
        {
            continue;
        }
        const int stage = int(reg_index) - SPIRV_BIND_SHIFT;
        result.max_stage = std::max(result.max_stage, stage);
        if(is_storage && reg_count == DESCRIPTOR_STORAGE_IMAGE)
        {
            result.images.push_back({std::move(name),
                                     stage,
                                     static_cast<bgfx::TextureFormat::Enum>(format),
                                     dimension,
                                     (type & UNIFORM_READ_ONLY_BIT) != 0});
        }
    }
    return result;
}

/// Whether the GLSL text declares image @p name read and written (no readonly / writeonly qualifier).
auto is_read_write_in_glsl(const std::string& glsl, const std::string& name) -> bool
{
    const std::regex declaration("layout\\s*\\([^)]*\\)\\s*uniform\\s+((?:\\w+\\s+)*)\\w*image\\w+\\s+" + name +
                                 "\\s*;");
    std::smatch match;
    if(!std::regex_search(glsl, match, declaration))
    {
        return false;
    }
    const std::string qualifiers = match[1].str();
    return qualifiers.find("readonly") == std::string::npos && qualifiers.find("writeonly") == std::string::npos;
}

auto get_format_name(bgfx::TextureFormat::Enum format) -> std::string
{
    return bimg::getName(static_cast<bimg::TextureFormat::Enum>(format));
}

/// The feature whose longest shader prefix matches @p shader, or render_feature::count.
auto find_feature(const std::string& shader) -> render_feature
{
    render_feature best = render_feature::count;
    std::size_t best_length = 0;
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        for(const char* prefix : get_render_feature_requirements(feature).shader_prefixes)
        {
            const std::size_t length = std::strlen(prefix);
            if(length > best_length && shader.compare(0, length, prefix) == 0)
            {
                best = feature;
                best_length = length;
            }
        }
    }
    return best;
}

/// Whether the table's 2D or cube dimension covers a shader image of @p dimension.
auto is_dimension_covered(gfx::texture_dimension listed, std::uint8_t dimension) -> bool
{
    switch(listed)
    {
        case gfx::texture_dimension::three_d:
            return dimension == DIMENSION_3D;
        case gfx::texture_dimension::cube:
            return dimension == DIMENSION_CUBE || dimension == DIMENSION_2D_ARRAY;
        case gfx::texture_dimension::two_d:
        default:
            return dimension != DIMENSION_3D && dimension != DIMENSION_CUBE;
    }
}

auto is_image_listed(const render_feature_requirements& needs, const shader_image& image, bool is_read_write) -> bool
{
    return std::any_of(needs.images.begin(),
                       needs.images.end(),
                       [&](const image_need& need)
                       {
                           const bool access_matches = !is_read_write || need.access == image_access::read_write;
                           return need.format == image.format && access_matches &&
                                  is_dimension_covered(need.dimension, image.dimension);
                       });
}

void check_shader(const std::string& shader,
                  const shader_bindings& bindings,
                  const std::string& glsl,
                  std::vector<int>& matched_features)
{
    const bool is_utility = std::any_of(std::begin(UTILITY_SHADERS),
                                        std::end(UTILITY_SHADERS),
                                        [&](const char* utility)
                                        {
                                            return shader == utility;
                                        });
    if(is_utility)
    {
        check(bindings.images.empty() && bindings.max_stage + 1 <= UTILITY_COMPUTE_BINDINGS,
              shader + " stays a utility: no images, at most 8 bindings");
        return;
    }
    const auto feature = find_feature(shader);
    check(feature != render_feature::count, shader + " belongs to a rendering feature");
    if(feature == render_feature::count)
    {
        return;
    }
    ++matched_features[static_cast<std::size_t>(feature)];
    const auto& needs = get_render_feature_requirements(feature);
    check(bindings.max_stage + 1 <= needs.compute_bindings,
          shader + " binds stage " + std::to_string(bindings.max_stage) + ", within the " +
              std::to_string(needs.compute_bindings) + " bindings " + needs.name + " lists");
    for(const auto& image : bindings.images)
    {
        check(image.stage <= MAX_IMAGE_STAGE,
              shader + ": image " + image.name + " binds at stage " + std::to_string(image.stage) + ", below 8");
        const bool is_read_write = !image.is_read_only && is_read_write_in_glsl(glsl, image.name);
        check(is_image_listed(needs, image, is_read_write),
              shader + ": image " + image.name + " (" + get_format_name(image.format) +
                  (is_read_write ? ", read-write" : "") + ") is listed by " + needs.name);
    }
}

void test_compiled_compute_shaders()
{
    std::printf("every compiled compute shader stays within its feature's requirements\n");
#if !defined(ENGINE_DATA_DIR)
    std::printf("  SKIP: the engine_data location is not configured\n");
#else
    const fs::path data_dir = ENGINE_DATA_DIR;
    const fs::path source_dir = data_dir / "data" / "shaders";
    const fs::path compiled_dir = data_dir / "compiled" / "shaders";
    std::vector<int> matched_features(static_cast<std::size_t>(render_feature::count), 0);
    std::size_t scanned = 0;
    for(const auto& entry : fs::recursive_directory_iterator(source_dir))
    {
        const auto& path = entry.path();
        const std::string file_name = path.filename().string();
        if(!entry.is_regular_file() || path.extension() != ".sc" || file_name.rfind("cs_", 0) != 0)
        {
            continue;
        }
        const fs::path relative = fs::relative(path, source_dir);
        const fs::path compiled = compiled_dir / relative.parent_path() / (file_name + ".asset.spirv");
        const fs::path glsl = compiled_dir / relative.parent_path() / (file_name + ".asset.gl");
        std::string shader = (relative.parent_path() / path.stem()).generic_string();
        check(fs::exists(compiled), shader + " has a compiled SPIR-V binary");
        if(!fs::exists(compiled))
        {
            continue;
        }
        check_shader(shader, parse_spirv_bindings(read_file(compiled)), read_file(glsl), matched_features);
        ++scanned;
    }
    std::printf("  %zu compute shaders scanned\n", scanned);
    check(scanned > 0, "the compiled shader directory was found and scanned");
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto& needs = get_render_feature_requirements(static_cast<render_feature>(index));
        check(needs.shader_prefixes.empty() || matched_features[index] > 0,
              std::string(needs.name) + " has a compute shader for its prefixes");
    }
#endif
}

auto run_render_feature_shaders_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_compiled_compute_shaders();
    std::printf("\nrender feature shaders: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("render feature shaders", run_render_feature_shaders_suite)
