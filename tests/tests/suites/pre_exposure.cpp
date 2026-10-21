/*
 * Validation suite for the view pre-exposure: where its GPU share lives and its uniform layout.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "pre-exposure"
 *
 * What it pins:
 *   - the environment SH texel that holds the GPU's share is the same in pre_exposure_state::gpu_texel, in
 *     pre_exposure.sh, which reads it, and in cs_pre_exposure.sc, which writes it;
 *   - pre_exposure_state's uniform layout (pre_exposure.sh u_pre_exposure): the value, its inverse, the history
 *     correction and the previous value.
 */

#include "../tests.h"

#include <engine/rendering/pipeline/pre_exposure.h>
#include <filesystem/filesystem.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

using namespace unravel;

namespace
{

int g_pre_exposure_checks = 0;
int g_pre_exposure_failures = 0;

void pre_exposure_check(bool condition, const std::string& what)
{
    ++g_pre_exposure_checks;
    if(!condition)
    {
        ++g_pre_exposure_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// The shader header that reads the GPU's share.
constexpr const char* PRE_EXPOSURE_HEADER = "engine:/data/shaders/pre_exposure.sh";
/// The compute shader that writes the GPU's share.
constexpr const char* PRE_EXPOSURE_SHADER = "engine:/data/shaders/exposure/cs_pre_exposure.sc";
/// Tolerance of the uniform layout's float checks.
constexpr float LAYOUT_TOLERANCE = 1e-6f;

/// The text of the engine data file @p protocol_path, empty when it cannot be read.
auto read_shader_text(const char* protocol_path) -> std::string
{
    const auto path = fs::resolve_protocol(protocol_path);
    std::ifstream stream(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    pre_exposure_check(!text.empty(), std::string(protocol_path) + " is readable at " + path.string());
    return text;
}

/// The value of `#define <name> <n>` in @p text, or -1 without one.
auto read_shader_define(const std::string& text, const std::string& name) -> int
{
    const std::regex definition(R"(#define\s+)" + name + R"(\s+(\d+))");
    std::smatch match;
    if(!std::regex_search(text, match, definition))
    {
        return -1;
    }
    return std::stoi(match[1].str());
}

void test_gpu_texel_matches_shaders()
{
    std::printf("test_gpu_texel_matches_shaders\n");
    const int expected_texel = pre_exposure_state::gpu_texel;
    for(const char* shader : {PRE_EXPOSURE_HEADER, PRE_EXPOSURE_SHADER})
    {
        const int actual_texel = read_shader_define(read_shader_text(shader), "PRE_EXPOSURE_GPU_TEXEL");
        pre_exposure_check(actual_texel == expected_texel,
                           std::string("PRE_EXPOSURE_GPU_TEXEL in ") + shader + " (" + std::to_string(actual_texel) +
                               ") equals pre_exposure_state::gpu_texel (" + std::to_string(expected_texel) + ")");
    }
}

void test_uniform_layout()
{
    std::printf("test_uniform_layout\n");
    pre_exposure_state state;
    state.value = 4.0f;
    state.previous = 2.0f;
    const auto uniform = state.to_uniform();
    pre_exposure_check(std::abs(uniform[0] - 4.0f) < LAYOUT_TOLERANCE, "x is the value");
    pre_exposure_check(std::abs(uniform[1] - 0.25f) < LAYOUT_TOLERANCE, "y is its inverse");
    pre_exposure_check(std::abs(uniform[2] - 2.0f) < LAYOUT_TOLERANCE, "z is the history correction");
    pre_exposure_check(std::abs(uniform[3] - 2.0f) < LAYOUT_TOLERANCE, "w is the previous value");
}

auto run_pre_exposure_suite(rtti::context& /*ctx*/) -> int
{
    g_pre_exposure_checks = 0;
    g_pre_exposure_failures = 0;
    test_gpu_texel_matches_shaders();
    test_uniform_layout();
    std::printf("pre-exposure: %d checks, %d failures\n", g_pre_exposure_checks, g_pre_exposure_failures);
    return g_pre_exposure_failures;
}

} // namespace

REGISTER_TEST_SUITE("pre-exposure", run_pre_exposure_suite)
