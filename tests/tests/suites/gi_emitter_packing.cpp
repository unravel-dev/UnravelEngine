/*
 * CPU/GPU packing contract for the GI emitter table.
 *
 * The table has four floats per piece and needs five values, so the upload spends the fourth
 * lane on the piece's extent and the shader rebuilds the ranking weight from what it decodes.
 * That makes the encoding a CONTRACT between two files that no compiler checks, and it has
 * already failed once: the upload replaced the power with a NEGATIVE packed lane, GiLoadEmitter
 * decoded the extent but left that negative value in e.power, and the reflection tier's
 * near-field top-K - whose scores start at zero - could therefore never select any piece. The
 * correction returned its identity value on every hit, silently, for every scene.
 *
 * So this suite checks both directions:
 *   - the C++ owner (gi_emitter_packing.h) round-trips an extent within its quantisation,
 *   - an INDEPENDENT transcription of the shader's decode agrees with it bit for bit,
 *   - the shader source still spells that decode the way the transcription assumes,
 *   - the reconstructed weight matches the CPU's ordering key and is strictly positive.
 *
 * Runs inside the unravel-tests runner:
 *   <build-dir>/bin/unravel-tests --suite "gi emitter packing"
 */

#include "../tests.h"

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/gi_emitter_packing.h>

#include <math/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace unravel::gi_emitter_packing_tests
{
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

void check_near(double actual, double expected, double tolerance, const std::string& what)
{
    ++g_checks;
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.8f, expected %.8f +/- %.8f)\n", what.c_str(), actual, expected, tolerance);
    }
}

/// The extents the contract has to survive: axis-aligned pieces up to one segment, the
/// degenerate flat and thin cases a real emissive quad produces, and the exact bounds.
auto make_extents() -> std::vector<math::vec3>
{
    const float segment = float(gi::GI_EMISSIVE_NEE_SEGMENT);
    return {
        math::vec3(0.0f, 0.0f, 0.0f),
        math::vec3(segment, segment, segment),
        math::vec3(segment, segment, 0.0f),
        math::vec3(segment, 0.0f, 0.0f),
        math::vec3(0.5f * segment, 0.25f * segment, 0.125f * segment),
        math::vec3(0.013f * segment, 0.87f * segment, 0.44f * segment),
        math::vec3(segment / 255.0f, 2.0f * segment / 255.0f, 254.0f * segment / 255.0f),
        math::vec3(0.3f * segment, 0.9999f * segment, 0.0001f * segment),
    };
}

// ---------------------------------------------------------------------------------------
// The shader mirror, transcribed
// ---------------------------------------------------------------------------------------

/// LITERAL transcription of GiLoadEmitter's extent decode in gi_emissive_nee.sh. Deliberately
/// not calling the C++ owner: an oracle that shared the implementation would agree with it by
/// construction and check nothing. GLSL mod(x, y) is x - y * floor(x / y), which for the
/// non-negative packed value is std::fmod.
auto shader_decode_extent(float lane) -> math::vec3
{
    if(!(lane < -0.5f))
    {
        return math::vec3(0.0f, 0.0f, 0.0f);
    }
    const float packed = -lane - 1.0f;
    const float x8 = std::floor(std::fmod(packed, 256.0f));
    const float y8 = std::floor(std::fmod(packed / 256.0f, 256.0f));
    const float z8 = std::floor(packed / 65536.0f);
    return math::vec3(x8, y8, z8) * (float(gi::GI_EMISSIVE_NEE_SEGMENT) / 255.0f);
}

/// LITERAL transcription of GiEmitterSurfaceArea and the weight GiLoadEmitter rebuilds.
auto shader_selection_weight(const math::vec3& radiance, const math::vec3& extent) -> float
{
    const float luminance = 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
    return luminance * (2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x));
}

/// The shader file with every space and tab removed, so a substring check pins the arithmetic
/// without pinning the formatting.
auto read_shader_source_compact(const std::string& relative_path) -> std::string
{
#ifndef GI_TESTS_SHADER_DIR
    (void)relative_path;
    return {};
#else
    std::ifstream file(std::string(GI_TESTS_SHADER_DIR) + "/" + relative_path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; }), text.end());
    return text;
#endif
}

// ---------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------

void test_lane_round_trip()
{
    std::printf("test_lane_round_trip\n");
    // Half a quantisation step per axis, plus float slack.
    const double tolerance = 0.5 * double(gi::GI_EMISSIVE_NEE_SEGMENT) / 255.0 + 1e-6;
    for(const math::vec3& extent : make_extents())
    {
        const float lane = gi::pack_emitter_extent_lane(extent);
        check(gi::has_emitter_extent_lane(lane), "a packed lane always reads as an extent, never as a legacy power");
        const math::vec3 decoded = gi::unpack_emitter_extent_lane(lane);
        check_near(decoded.x, extent.x, tolerance, "extent x survives the lane");
        check_near(decoded.y, extent.y, tolerance, "extent y survives the lane");
        check_near(decoded.z, extent.z, tolerance, "extent z survives the lane");
    }
    // A legacy table wrote a positive power; that must never decode as an extent.
    for(const float power : {0.0f, 1e-4f, 0.5f, 12.0f, 1e6f})
    {
        check(!gi::has_emitter_extent_lane(power), "a positive power lane reads as 'no extent'");
        const math::vec3 decoded = gi::unpack_emitter_extent_lane(power);
        check(decoded == math::vec3(0.0f, 0.0f, 0.0f), "a legacy power lane decodes to a zero extent");
    }
}

void test_shader_decode_matches_cpp()
{
    std::printf("test_shader_decode_matches_cpp\n");
    for(const math::vec3& extent : make_extents())
    {
        const float lane = gi::pack_emitter_extent_lane(extent);
        const math::vec3 cpu = gi::unpack_emitter_extent_lane(lane);
        const math::vec3 shader = shader_decode_extent(lane);
        // Both sides run the same float arithmetic on the same lane: exact, not near.
        check(cpu == shader, "the shader's decode reproduces the C++ owner's exactly");
    }
    // The lane is an integer in [1, 2^24] carried in a float32 mantissa; anything wider would
    // round and silently corrupt an axis.
    const float widest = gi::pack_emitter_extent_lane(
        math::vec3(float(gi::GI_EMISSIVE_NEE_SEGMENT), float(gi::GI_EMISSIVE_NEE_SEGMENT), float(gi::GI_EMISSIVE_NEE_SEGMENT)));
    check_near(double(widest), -16777216.0, 0.0, "the widest extent packs to exactly -2^24, the last exact float integer");
}

void test_reconstructed_weight()
{
    std::printf("test_reconstructed_weight\n");
    const math::vec3 radiance(3.0f, 2.0f, 1.0f);
    const float luminance = 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
    for(const math::vec3& extent : make_extents())
    {
        const float lane = gi::pack_emitter_extent_lane(extent);
        const math::vec3 decoded = gi::unpack_emitter_extent_lane(lane);
        const float cpu_weight = gi::emitter_selection_weight(luminance, extent);
        const float shader_weight = shader_selection_weight(radiance, decoded);
        // The shader ranks by the weight of the QUANTISED extent, so the two agree only to the
        // quantisation. One step per axis bounds the area error.
        const double step = double(gi::GI_EMISSIVE_NEE_SEGMENT) / 255.0;
        const double span = double(gi::GI_EMISSIVE_NEE_SEGMENT);
        const double tolerance = double(luminance) * 6.0 * step * span + 1e-5;
        check_near(shader_weight, cpu_weight, tolerance, "the reconstructed weight tracks the CPU ordering key");
        // THE REGRESSION: a descending top-K initialised at zero can only ever select a piece
        // whose score is strictly positive. Anything with area must qualify.
        const bool has_area = gi::emitter_surface_area(decoded) > 0.0f;
        check(!has_area || shader_weight > 0.0f, "a piece with emitting area scores above a zero-initialised top-K");
    }
}

void test_shader_source_still_matches()
{
    std::printf("test_shader_source_still_matches\n");
#ifndef GI_TESTS_SHADER_DIR
    check(false, "GI_TESTS_SHADER_DIR not defined by the build - the contract test cannot run");
#else
    const std::string nee = read_shader_source_compact("gi/gi_emissive_nee.sh");
    check(!nee.empty(), "gi_emissive_nee.sh found");
    // The transcriptions above assume these exact expressions. A change to either side that
    // does not update the other lands here instead of in a scene.
    const std::vector<std::string> expected = {
        "e.has_extent=e1.w<-0.5;",
        "floatpacked=-e1.w-1.0;",
        "floatx8=floor(mod(packed,256.0));",
        "floaty8=floor(mod(packed/256.0,256.0));",
        "floatz8=floor(packed/65536.0);",
        "e.extent=vec3(x8,y8,z8)*(GI_EMISSIVE_NEE_SEGMENT/255.0);",
        "e.power=GiEmitterLuminance(e)*GiEmitterSurfaceArea(e.extent);",
        "return2.0*(extent.x*extent.y+extent.y*extent.z+extent.z*extent.x);",
        "returndot(e.radiance,vec3(0.2126,0.7152,0.0722));",
    };
    for(const std::string& fragment : expected)
    {
        check(nee.find(fragment) != std::string::npos,
              "gi_emissive_nee.sh still spells '" + fragment + "' as the transcription assumes");
    }
    // The consumer that the negative lane disabled: it must still rank by the power, which is
    // now the positive reconstructed weight.
    const std::string reflection = read_shader_source_compact("gi/gi_reflection_kernel.sh");
    check(!reflection.empty(), "gi_reflection_kernel.sh found");
    check(reflection.find("floatscore=e.power/max(dot(to_center,to_center),1e-4);") != std::string::npos,
          "the reflection near-field still scores by e.power over distance squared");
    check(reflection.find("pick_score[k]=0.0;") != std::string::npos,
          "the reflection near-field's top-K still starts at zero, which is what needs a positive weight");
#endif
}
} // namespace

auto run_gi_emitter_packing_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_lane_round_trip();
    test_shader_decode_matches_cpp();
    test_reconstructed_weight();
    test_shader_source_still_matches();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("gi emitter packing / CPU-GPU lane contract", run_gi_emitter_packing_suite)
} // namespace unravel::gi_emitter_packing_tests
