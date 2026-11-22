/*
 * Validation suite for the depth convention of camera views.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "depth convention"
 *
 * Camera views store reversed depth (gfx::camera_near_depth 1, gfx::camera_far_depth 0), and the shaders mirror it
 * with DEPTH_NEAR / DEPTH_FAR in shaderlib.sh. These check the reversed projection builders, that the camera builds
 * with them, that the shaders agree with the C++ constants, and that the sources keep the convention in the named
 * helpers: no raw camera depth test in C++ and none of the removed OpenGL clip helpers in shaders.
 */

#include "../tests.h"

#include <engine/rendering/camera.h>
#include <graphics/graphics.h>
#include <math/math.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

using namespace unravel::test;
using namespace unravel;

namespace
{

/// Clip depth of view depth @p z under @p projection (left-handed: the view looks down +z).
auto clip_depth(const math::mat4& projection, float z) -> double
{
    const math::vec4 clip = projection * math::vec4(0.0f, 0.0f, z, 1.0f);
    return double(clip.z) / double(clip.w);
}

void test_reversed_perspective_builder()
{
    std::printf("test_reversed_perspective_builder\n");
    struct range
    {
        float near_plane;
        float far_plane;
    };
    for(const range r : {range{0.1f, 1000.0f}, range{0.01f, 1000.0f}, range{0.001f, 10000.0f}})
    {
        const std::string what = "near " + std::to_string(r.near_plane) + ", far " + std::to_string(r.far_plane);
        const math::mat4 p = math::perspective_reversed_zo(math::radians(60.0f), 1.5f, r.near_plane, r.far_plane);
        const math::mat4 s = math::perspectiveZO(math::radians(60.0f), 1.5f, r.near_plane, r.far_plane);
        check_near(float(clip_depth(p, r.near_plane)), 1.0f, 1e-6f, what + ": the near plane maps to 1");
        check_near(float(clip_depth(p, r.far_plane)), 0.0f, 1e-6f, what + ": the far plane maps to 0");
        const double n = r.near_plane;
        const double f = r.far_plane;
        check_near(p[2][2], float(n / (n - f)), 1e-12f, what + ": m[2][2] is n / (n - f)");
        check_near(p[3][2], float(n * f / (f - n)), 1e-6f * float(n), what + ": m[3][2] is n f / (f - n)");
        check(p[0][0] == s[0][0] && p[1][1] == s[1][1] && p[2][3] == s[2][3], what + ": x, y and w match the standard projection");
        bool is_monotonic = true;
        double last = 2.0;
        for(int i = 0; i <= 64; ++i)
        {
            const double z = n * std::pow(f / n, double(i) / 64.0);
            const double depth = clip_depth(p, float(z));
            is_monotonic = is_monotonic && depth < last;
            last = depth;
        }
        check(is_monotonic, what + ": depth falls from the near plane to the far plane");
    }
}

void test_reversed_ortho_builder()
{
    std::printf("test_reversed_ortho_builder\n");
    const float n = -50.0f;
    const float f = 200.0f;
    const math::mat4 p = math::ortho_reversed_zo(-10.0f, 10.0f, -5.0f, 5.0f, n, f);
    const math::mat4 s = math::orthoZO(-10.0f, 10.0f, -5.0f, 5.0f, n, f);
    check_near(float(clip_depth(p, n)), 1.0f, 1e-6f, "the near plane maps to 1");
    check_near(float(clip_depth(p, f)), 0.0f, 1e-6f, "the far plane maps to 0");
    check_near(float(clip_depth(p, 75.0f)), 1.0f - float(clip_depth(s, 75.0f)), 1e-6f, "reversed depth is 1 - standard depth");
}

void test_camera_follows_the_convention()
{
    std::printf("test_camera_follows_the_convention\n");
    camera cam;
    cam.set_fov(60.0f);
    cam.set_aspect_ratio(1.5f, true);
    cam.set_near_clip(0.05f);
    cam.set_far_clip(800.0f);
    const math::mat4 p = cam.get_projection_unjittered().get_matrix();
    check_near(float(clip_depth(p, 0.05f)), gfx::camera_near_depth, 1e-5f, "the camera's near plane has the near device depth");
    check_near(float(clip_depth(p, 800.0f)), gfx::camera_far_depth, 1e-5f, "the camera's far plane has the far device depth");
    check(gfx::camera_near_depth == 1.0f && gfx::camera_far_depth == 0.0f, "camera depth is reversed");
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

auto repository_root() -> fs::path
{
#ifdef ENGINE_DATA_DIR
    return fs::path(ENGINE_DATA_DIR).parent_path();
#else
    return {};
#endif
}

void test_shaders_mirror_the_convention()
{
    std::printf("test_shaders_mirror_the_convention\n");
    const fs::path root = repository_root();
    require(!root.empty(), "ENGINE_DATA_DIR is defined by the build");
    const std::string shaderlib = read_text(root / "engine_data/data/shaders/shaderlib.sh");
    std::smatch near_match;
    std::smatch far_match;
    const bool has_near = std::regex_search(shaderlib, near_match, std::regex(R"(#define\s+DEPTH_NEAR\s+([0-9.]+))"));
    const bool has_far = std::regex_search(shaderlib, far_match, std::regex(R"(#define\s+DEPTH_FAR\s+([0-9.]+))"));
    require(has_near && has_far, "shaderlib.sh defines DEPTH_NEAR and DEPTH_FAR");
    check(std::stof(near_match[1].str()) == gfx::camera_near_depth, "DEPTH_NEAR in shaderlib.sh equals gfx::camera_near_depth");
    check(std::stof(far_match[1].str()) == gfx::camera_far_depth, "DEPTH_FAR in shaderlib.sh equals gfx::camera_far_depth");
    check(shaderlib.find("REVERSED_DEPTH") == std::string::npos, "shaderlib.sh has one depth convention, no switch");
}

/// Files allowed raw camera depth tests, with how many: the helpers themselves, and views that keep standard depth.
auto raw_depth_test_allowances() -> std::map<std::string, int>
{
    return {
        {"engine/core/graphics/graphics.h", 3},                         // the named depth tests
        {"engine/core/graphics/utils/debugdraw/debugdraw.cpp", 6},      // the encoder's LESS / GREATER switch
        {"engine/engine/rendering/shadow.cpp", 5},                      // shadow maps keep standard depth
        {"engine/engine/rendering/pipeline/deferred/pipeline_gi.cpp", 1}, // GI card captures keep standard depth
        {"engine/engine/ui/rmlui/RmlUi_RenderInterface.cpp", 1},        // the screen layers' own clip mask
    };
}

void test_sources_name_camera_depth_tests()
{
    std::printf("test_sources_name_camera_depth_tests\n");
    const fs::path root = repository_root();
    require(!root.empty(), "ENGINE_DATA_DIR is defined by the build");
    const std::regex raw(R"(BGFX_STATE_DEPTH_TEST_(LESS|LEQUAL|GREATER|GEQUAL)\b)");
    const auto allowances = raw_depth_test_allowances();
    std::map<std::string, int> found;
    for(const char* folder : {"engine", "editor", "game"})
    {
        fs::error_code err;
        for(fs::recursive_directory_iterator it(root / folder, err), end; it != end; it.increment(err))
        {
            const auto extension = it->path().extension().string();
            if(extension != ".cpp" && extension != ".h" && extension != ".hpp")
            {
                continue;
            }
            const std::string text = read_text(it->path());
            const auto count = int(std::distance(std::sregex_iterator(text.begin(), text.end(), raw), std::sregex_iterator()));
            if(count > 0)
            {
                found[fs::relative(it->path(), root).generic_string()] = count;
            }
        }
    }
    for(const auto& [file, count] : found)
    {
        const auto allowed = allowances.find(file);
        const int expected = allowed == allowances.end() ? 0 : allowed->second;
        check(count == expected,
              file + " has " + std::to_string(count) + " raw camera depth tests, " + std::to_string(expected) +
                  " allowed: camera views use gfx::depth_test_nearer and its siblings");
    }
    for(const auto& [file, count] : allowances)
    {
        check(found.count(file) != 0, file + " still has its allowed raw depth tests (update the allowances)");
    }
}

void test_shaders_use_the_shared_conventions()
{
    std::printf("test_shaders_use_the_shared_conventions\n");
    const fs::path root = repository_root();
    require(!root.empty(), "ENGINE_DATA_DIR is defined by the build");
    const std::regex removed(R"(\b(INVERTED_DEPTH_RANGE|toClipSpaceDepth|toDepthTextureZ|toClipSpaceY|bgfx_ndc)\b)");
    for(const char* folder : {"engine_data/data/shaders", "editor_data/data/shaders"})
    {
        fs::error_code err;
        for(fs::recursive_directory_iterator it(root / folder, err), end; it != end; it.increment(err))
        {
            const auto name = it->path().filename().string();
            const auto extension = it->path().extension().string();
            if((extension != ".sh" && extension != ".eshader") || name == "bgfx_shader.sh" || name == "bgfx_compute.sh")
            {
                continue;
            }
            const std::string text = read_text(it->path());
            check(!std::regex_search(text, removed),
                  fs::relative(it->path(), root).generic_string() +
                      " uses no OpenGL clip helper or depth-range switch: every backend shares one convention");
        }
    }
}

} // namespace

void run_depth_convention_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_reversed_perspective_builder());
    RUN_CASE(test_reversed_ortho_builder());
    RUN_CASE(test_camera_follows_the_convention());
    RUN_CASE(test_shaders_mirror_the_convention());
    RUN_CASE(test_sources_name_camera_depth_tests());
    RUN_CASE(test_shaders_use_the_shared_conventions());
}

REGISTER_TEST_SUITE("depth convention", run_depth_convention_suite)
