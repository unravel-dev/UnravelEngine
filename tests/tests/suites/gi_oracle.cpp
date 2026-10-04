/*
 * GI Phase 0: the validation suite itself (plan: tasks/gi_rewrite_plan.md, sections 9-10).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "gi constants"
 *
 * Two halves:
 *  - The constants contract: gi_constants.h is the single owner of every cross-pass constant,
 *    and the shader mirror gi_constants.sh is kept honest by PARSING it here - both directions,
 *    so an edit to either file alone fails the suite. This closes the duplicated-constant drift
 *    family (audit B4) by mechanism rather than by comment.
 *  - The reference oracle: golden scenes evaluated by the CPU path tracer in
 *    gi_reference_tracer.cpp. These pin the ORACLE's own physics (energy conservation,
 *    determinism, enclosure, occlusion) so later phases can compare GPU output against it
 *    with the oracle itself above suspicion.
 */

#include "../tests.h"
#include "gi_reference_tracer.h"

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/mesh_sdf_source.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace unravel::gi_tests
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
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n", what.c_str(), actual, expected, tolerance);
    }
}

// ---------------------------------------------------------------------------------------
// Constants parity
// ---------------------------------------------------------------------------------------

/// Parses `#define <prefix>* <number>` lines from a shader mirror. Non-numeric defines (include
/// guards) are skipped by the prefix requirement plus the numeric parse.
auto parse_shader_constants(const std::string& path, const std::string& prefix = "GI_")
    -> std::map<std::string, double>
{
    std::map<std::string, double> result;
    std::ifstream file(path);
    std::string line;
    while(std::getline(file, line))
    {
        std::istringstream stream(line);
        std::string directive;
        std::string name;
        double value = 0.0;
        stream >> directive >> name;
        if(directive != "#define" || name.rfind(prefix, 0) != 0)
        {
            continue;
        }
        if(!(stream >> value))
        {
            continue;
        }
        result[name] = value;
    }
    return result;
}

void test_shader_constants_match_cpp()
{
    std::printf("test_shader_constants_match_cpp\n");
#ifndef GI_TESTS_SHADER_DIR
    check(false, "GI_TESTS_SHADER_DIR not defined by the build - the parity test cannot run");
#else
    const std::string path = std::string(GI_TESTS_SHADER_DIR) + "/gi/gi_constants.sh";
    const auto shader_constants = parse_shader_constants(path);
    check(!shader_constants.empty(), "gi_constants.sh found and contains GI_ defines at " + path);
    // Every C++ table entry exists in the shader with the same value.
    size_t table_count = 0;
    for(const auto& row : unravel::gi::gi_constant_rows)
    {
        ++table_count;
        const auto found = shader_constants.find(row.name);
        if(found == shader_constants.end())
        {
            check(false, std::string(row.name) + " missing from gi_constants.sh");
            continue;
        }
        // Exact for integers, relative for floats; the mirror stores the same literal so this
        // is effectively exact either way.
        const double tolerance = 1e-6 * std::max(1.0, std::fabs(row.value));
        check_near(found->second, row.value, tolerance, std::string(row.name) + " value matches");
    }
    // No orphans: every shader-side GI_ define is owned by the table.
    for(const auto& [name, value] : shader_constants)
    {
        bool owned = false;
        for(const auto& row : unravel::gi::gi_constant_rows)
        {
            if(name == row.name)
            {
                owned = true;
                break;
            }
        }
        check(owned, name + " in gi_constants.sh is owned by the table in gi_constants.h");
    }
    std::printf("  %zu constants verified in both directions\n", table_count);
#endif
}

/// The Lumen gather's constants: lumen_constants.h owns them, lumen_constants.sh mirrors them,
/// checked in both directions like the GI table.
void test_lumen_constants_match_cpp()
{
    std::printf("test_lumen_constants_match_cpp\n");
#ifndef GI_TESTS_SHADER_DIR
    check(false, "GI_TESTS_SHADER_DIR not defined by the build - the parity test cannot run");
#else
    const std::string path = std::string(GI_TESTS_SHADER_DIR) + "/lumen/lumen_constants.sh";
    const auto shader_constants = parse_shader_constants(path, "LUMEN_");
    check(!shader_constants.empty(), "lumen_constants.sh found and contains LUMEN_ defines at " + path);
    for(const auto& row : unravel::gi::lumen::lumen_constant_rows)
    {
        const auto found = shader_constants.find(row.name);
        if(found == shader_constants.end())
        {
            check(false, std::string(row.name) + " missing from lumen_constants.sh");
            continue;
        }
        const double tolerance = 1e-6 * std::max(1.0, std::fabs(row.value));
        check_near(found->second, row.value, tolerance, std::string(row.name) + " value matches");
    }
    for(const auto& [name, value] : shader_constants)
    {
        const bool owned = std::any_of(std::begin(unravel::gi::lumen::lumen_constant_rows),
                                       std::end(unravel::gi::lumen::lumen_constant_rows),
                                       [&](const auto& row) { return name == row.name; });
        check(owned, name + " in lumen_constants.sh is owned by the table in lumen_constants.h");
    }
    std::printf("  %zu constants verified in both directions\n", std::size(unravel::gi::lumen::lumen_constant_rows));
#endif
}

/// Compiles every GI shader with shaderc for the SM 5.0 floor - the binding platform
/// constraint, tested first per the plan. An editor-side compile failure silently keeps the
/// previous shader binary, so it presents as wrong RENDERING (black GI, dead debug views)
/// rather than as an error anywhere; this is the harness's job to catch pre-ship. Skips with a
/// note when shaderc is not built alongside the tests.
void test_gi_shaders_compile_sm50()
{
    std::printf("test_gi_shaders_compile_sm50\n");
#if !defined(GI_TESTS_SHADERC) || !defined(GI_TESTS_SHADER_DIR)
    std::printf("  SKIP: shaderc location not configured\n");
#else
    namespace fs = std::filesystem;
    const fs::path shaderc = GI_TESTS_SHADERC;
    if(!fs::exists(shaderc))
    {
        std::printf("  SKIP: %s not built (build the editor first)\n", shaderc.string().c_str());
        return;
    }
    const fs::path include_dir = GI_TESTS_SHADER_DIR;
    // The varyings the engine's compiler gives a shader (asset_compiler.cpp): its own <name>.io, else its folder's
    // varying.def.io, else varying.def.sc.
    const auto get_varying = [](const fs::path& shader) -> fs::path
    {
        const fs::path dir = shader.parent_path();
        fs::path varying = dir / (shader.stem().string() + ".io");
        if(!fs::exists(varying))
        {
            varying = dir / "varying.def.io";
        }
        if(!fs::exists(varying))
        {
            varying = dir / "varying.def.sc";
        }
        return varying;
    };
    const fs::path out_dir = fs::temp_directory_path() / "gi_shader_compile_test";
    fs::create_directories(out_dir);
    size_t compiled = 0;
    std::vector<fs::directory_entry> entries;
    for(const char* folder : {"gi", "lumen", "shader_print"})
    {
        const fs::path shader_dir = include_dir / folder;
        if(fs::exists(shader_dir))
        {
            entries.insert(entries.end(), fs::directory_iterator(shader_dir), fs::directory_iterator());
        }
    }
    for(const auto& entry : entries)
    {
        const auto name = entry.path().filename().string();
        if(entry.path().extension() != ".sc")
        {
            continue;
        }
        const bool compute = name.rfind("cs_", 0) == 0;
        const bool fragment = name.rfind("fs_", 0) == 0;
        const bool vertex = name.rfind("vs_", 0) == 0;
        if(!compute && !fragment && !vertex)
        {
            continue;
        }
        const fs::path varying = get_varying(entry.path());
        // The D3D floor AND the OpenGL profile: the backends disagree on real things -
        // GLSL reserves `packed`, rejects expressions in local_size, lacks scalar
        // equal()/notEqual() and legacy *Lod entry points - and every one of those shipped
        // as a D3D-only-tested regression before this second profile existed.
        struct profile_case
        {
            const char* platform;
            const char* profile;
            const char* label;
        };
        // And SPIR-V: bgfx compiles it through the HLSL front-end, but not with fxc's
        // overload strictness - a 3D image atomic that fxc rejected outright was silently
        // matched to the 2D template and emitted a mistyped OpStore that only the Vulkan
        // runtime compile reported.
        const profile_case profiles[] = {
            {"windows", "s_5_0", "SM 5.0"},
            {"linux", "440", "GLSL 440"},
            {"windows", "spirv", "SPIR-V"},
        };
        for(const auto& profile : profiles)
        {
            const fs::path out_bin = out_dir / (name + "." + profile.profile + ".bin");
            const fs::path out_log = out_dir / (name + "." + profile.profile + ".log");
            std::string command = "\"\"" + shaderc.string() + "\" -f \"" + entry.path().string() +
                                  "\" -o \"" + out_bin.string() + "\" -i \"" + include_dir.string() +
                                  "\" --varyingdef \"" + varying.string() +
                                  "\" --type " + (compute ? "compute" : (vertex ? "vertex" : "fragment")) +
                                  " --define BGFX_CONFIG_MAX_BONES=64 --platform " + profile.platform +
                                  " -p " + profile.profile + " > \"" + out_log.string() + "\" 2>&1\"";
            const int exit_code = std::system(command.c_str());
            std::string log;
            {
                std::ifstream log_file(out_log);
                std::stringstream buffer;
                buffer << log_file.rdbuf();
                log = buffer.str();
            }
            // shaderc is quiet on success; any output or a non-zero exit is a failure worth
            // the full log, because the editor would have swallowed it.
            const bool ok = exit_code == 0 && log.find("Error") == std::string::npos &&
                            log.find("error") == std::string::npos;
            if(!ok)
            {
                std::printf("--- %s (%s) ---\n%.2000s\n", name.c_str(), profile.label, log.c_str());
            }
            check(ok, name + " compiles for " + profile.label);
        }
        ++compiled;
    }
    std::printf("  %zu shaders compiled\n", compiled);
    check(compiled > 0, "the shader directory was found and scanned");
#endif
}

// The probe-space temporal's stratum/walk transcription tests were removed with the
// feature (2026-08-29): all 64 octahedral texels trace fresh every frame now.


// ---------------------------------------------------------------------------------------
// Golden scene fixtures
// ---------------------------------------------------------------------------------------

void add_quad(sdf_source_geometry& g,
              const math::vec3& a,
              const math::vec3& b,
              const math::vec3& c,
              const math::vec3& d)
{
    const uint32_t base = uint32_t(g.positions.size());
    g.positions.push_back(a);
    g.positions.push_back(b);
    g.positions.push_back(c);
    g.positions.push_back(d);
    g.indices.insert(g.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
}

auto make_box_geometry(const math::vec3& half_extents) -> sdf_source_geometry
{
    sdf_source_geometry g;
    const float x = half_extents.x;
    const float y = half_extents.y;
    const float z = half_extents.z;
    add_quad(g, {x, -y, -z}, {x, y, -z}, {x, y, z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {-x, y, z}, {-x, y, -z}, {-x, -y, -z});
    add_quad(g, {-x, y, -z}, {-x, y, z}, {x, y, z}, {x, y, -z});
    add_quad(g, {-x, -y, z}, {-x, -y, -z}, {x, -y, -z}, {x, -y, z});
    add_quad(g, {-x, -y, z}, {x, -y, z}, {x, y, z}, {-x, y, z});
    add_quad(g, {x, -y, -z}, {-x, -y, -z}, {-x, y, -z}, {x, y, -z});
    g.bounds.reset();
    for(const auto& p : g.positions)
    {
        g.bounds.add_point(p);
    }
    return g;
}

auto bake_slab(const math::vec3& half_extents, mesh_sdf& out_sdf) -> bool
{
    mesh_sdf_bake_settings settings;
    settings.resolution = 48;
    settings.min_voxel_size = 0.001f;
    return bake_mesh_sdf(make_box_geometry(half_extents), settings, out_sdf);
}

auto translation(const math::vec3& t) -> math::mat4
{
    return math::translate(math::mat4(1.0f), t);
}

/// A room of six slabs enclosing [-half, +half]^3, walls @p thickness thick, from three shared
/// slab bakes (floor/ceiling pair, and two wall pairs). Returned instances borrow the bakes.
struct room_slabs
{
    mesh_sdf horizontal; // extends in XZ
    mesh_sdf wall_x;     // normal along X, extends in YZ
    mesh_sdf wall_z;     // normal along Z, extends in XY
};

auto make_room(room_slabs& slabs,
               gi_reference::reference_scene& scene,
               float half,
               float thickness,
               const math::vec3& albedo) -> bool
{
    const float t = thickness * 0.5f;
    // Oversized in their planar axes so the corners seal.
    const float span = half + thickness;
    if(!bake_slab({span, t, span}, slabs.horizontal) || !bake_slab({t, span, span}, slabs.wall_x) ||
       !bake_slab({span, span, t}, slabs.wall_z))
    {
        return false;
    }
    using gi_reference::make_instance;
    scene.instances.push_back(make_instance(slabs.horizontal, translation({0, -half - t, 0}), albedo));
    scene.instances.push_back(make_instance(slabs.horizontal, translation({0, +half + t, 0}), albedo));
    scene.instances.push_back(make_instance(slabs.wall_x, translation({-half - t, 0, 0}), albedo));
    scene.instances.push_back(make_instance(slabs.wall_x, translation({+half + t, 0, 0}), albedo));
    scene.instances.push_back(make_instance(slabs.wall_z, translation({0, 0, -half - t}), albedo));
    scene.instances.push_back(make_instance(slabs.wall_z, translation({0, 0, +half + t}), albedo));
    return true;
}

// ---------------------------------------------------------------------------------------
// Oracle physics tests
// ---------------------------------------------------------------------------------------

/// White furnace: a perfectly reflective object in a uniform sky. At equilibrium the radiance
/// field is L_sky EVERYWHERE, so irradiance at any surface point is pi * L_sky, regardless of
/// geometry. Any transport bug - lost bounces, wrong cosine weighting, wrong estimator
/// normalisation, double-counted sky - shows up as an energy error here.
void test_reference_furnace_conserves_energy()
{
    std::printf("test_reference_furnace_conserves_energy\n");
    mesh_sdf box;
    check(bake_slab(math::vec3(0.5f), box), "furnace box bakes");
    gi_reference::reference_scene scene;
    scene.instances.push_back(gi_reference::make_instance(box, math::mat4(1.0f), math::vec3(1.0f)));
    scene.sky_radiance = math::vec3(0.5f);
    gi_reference::integrate_params params;
    params.sample_count = 2048;
    params.max_bounces = 8;
    const math::vec3 top(0.0f, 0.5f, 0.0f);
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const math::vec3 irradiance = gi_reference::integrate_irradiance(scene, top, up, params);
    const float expected = math::pi<float>() * scene.sky_radiance.x;
    // Tolerance covers Monte Carlo variance at 2048 samples plus depth-8 truncation; the
    // truncated energy is the albedo^9 tail of paths still bouncing, small for a convex box
    // where most directions escape immediately.
    check_near(irradiance.x, expected, 0.05 * expected, "furnace irradiance = pi * L_sky (r)");
    check_near(irradiance.y, expected, 0.05 * expected, "furnace irradiance = pi * L_sky (g)");
    check_near(irradiance.z, expected, 0.05 * expected, "furnace irradiance = pi * L_sky (b)");
}

void test_reference_is_deterministic()
{
    std::printf("test_reference_is_deterministic\n");
    mesh_sdf box;
    check(bake_slab(math::vec3(0.5f), box), "box bakes");
    gi_reference::reference_scene scene;
    scene.instances.push_back(gi_reference::make_instance(box, math::mat4(1.0f), math::vec3(0.7f)));
    scene.sky_radiance = math::vec3(0.3f, 0.5f, 0.8f);
    scene.point_lights.push_back({{1.5f, 1.5f, 0.0f}, math::vec3(2.0f)});
    gi_reference::integrate_params params;
    params.sample_count = 256;
    params.max_bounces = 4;
    const math::vec3 p(0.0f, 0.5f, 0.0f);
    const math::vec3 n(0.0f, 1.0f, 0.0f);
    const math::vec3 first = gi_reference::integrate_irradiance(scene, p, n, params);
    const math::vec3 second = gi_reference::integrate_irradiance(scene, p, n, params);
    check(first.x == second.x && first.y == second.y && first.z == second.z,
          "two runs with one seed are bit-identical (world structures must not depend on"
          " traversal order)");
}

/// A sealed room with no lights converges to black - the property occlude-on-miss existed for
/// in the old system, now demanded of the oracle: no sky can reach an enclosed point.
void test_reference_sealed_room_is_black()
{
    std::printf("test_reference_sealed_room_is_black\n");
    room_slabs slabs;
    gi_reference::reference_scene scene;
    check(make_room(slabs, scene, 1.0f, 0.2f, math::vec3(0.8f)), "room bakes");
    scene.sky_radiance = math::vec3(10.0f); // bright sky OUTSIDE - none of it may get in
    gi_reference::integrate_params params;
    params.sample_count = 512;
    params.max_bounces = 6;
    const math::vec3 floor_point(0.0f, -1.0f, 0.0f);
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const math::vec3 irradiance = gi_reference::integrate_irradiance(scene, floor_point, up, params);
    const float total = irradiance.x + irradiance.y + irradiance.z;
    // Not exactly zero: the room is a field representation with finite walls, so a grazing
    // path may terminate ON a wall and pick up nothing - but nothing may bring sky in.
    check(total < 1e-3f, "sealed room irradiance is black (got " + std::to_string(total) + ")");
}

/// R3's shape, run against the oracle: a wall between a lit and an unlit room. Pins that the
/// representation + tracer occlude through authored-thickness geometry, and records the
/// leak ratio the runtime will be held to.
void test_reference_thin_wall_blocks_light()
{
    std::printf("test_reference_thin_wall_blocks_light\n");
    room_slabs slabs;
    gi_reference::reference_scene scene;
    // Outer room spans x in [-2, 2]; dividing wall at x = 0, 10 cm thick.
    check(make_room(slabs, scene, 2.0f, 0.2f, math::vec3(0.7f)), "outer room bakes");
    mesh_sdf divider;
    check(bake_slab({0.05f, 2.2f, 2.2f}, divider), "10 cm divider bakes");
    scene.instances.push_back(gi_reference::make_instance(divider, translation({0, 0, 0}), math::vec3(0.7f)));
    scene.point_lights.push_back({{1.0f, 0.5f, 0.0f}, math::vec3(5.0f)});
    gi_reference::integrate_params params;
    params.sample_count = 2048;
    params.max_bounces = 4;
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const math::vec3 lit =
        gi_reference::integrate_irradiance(scene, {1.0f, -2.0f, 0.0f}, up, params);
    const math::vec3 dark =
        gi_reference::integrate_irradiance(scene, {-1.0f, -2.0f, 0.0f}, up, params);
    const float lit_total = lit.x + lit.y + lit.z;
    const float dark_total = dark.x + dark.y + dark.z;
    check(lit_total > 0.1f, "lit side receives light (got " + std::to_string(lit_total) + ")");
    check(dark_total < 0.02f * lit_total,
          "dark side below 2% of lit side (R3): got " + std::to_string(dark_total) + " vs lit " +
              std::to_string(lit_total));
}

/// Colour bleed, measured as an A/B against the same scene with the coloured wall neutralised.
/// Self-referential on purpose: no magic tint threshold, just "the red wall must redden the
/// floor by a clear margin over an all-white room".
void test_reference_cornell_bleeds_colour()
{
    std::printf("test_reference_cornell_bleeds_colour\n");
    // Shared bakes: floor/ceiling/back slabs and the two side walls.
    mesh_sdf horizontal;
    mesh_sdf side;
    mesh_sdf back;
    check(bake_slab({1.2f, 0.1f, 1.2f}, horizontal), "cornell horizontal slab bakes");
    check(bake_slab({0.1f, 1.2f, 1.2f}, side), "cornell side slab bakes");
    check(bake_slab({1.2f, 1.2f, 0.1f}, back), "cornell back slab bakes");
    auto build = [&](const math::vec3& left_wall_albedo) -> gi_reference::reference_scene
    {
        gi_reference::reference_scene scene;
        using gi_reference::make_instance;
        const math::vec3 white(0.75f);
        scene.instances.push_back(make_instance(horizontal, translation({0, -1.1f, 0}), white));
        scene.instances.push_back(make_instance(horizontal, translation({0, +1.1f, 0}), white));
        scene.instances.push_back(make_instance(back, translation({0, 0, -1.1f}), white));
        scene.instances.push_back(make_instance(side, translation({-1.1f, 0, 0}), left_wall_albedo));
        scene.instances.push_back(make_instance(side, translation({+1.1f, 0, 0}), white));
        // Light near the ceiling centre; the open front face admits no sky (sky black).
        scene.point_lights.push_back({{0.0f, 0.7f, 0.0f}, math::vec3(3.0f)});
        return scene;
    };
    gi_reference::integrate_params params;
    params.sample_count = 2048;
    params.max_bounces = 4;
    // Floor point close to the left wall, where its bounce dominates the indirect term.
    const math::vec3 p(-0.8f, -1.0f, 0.0f);
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const auto red_scene = build({0.75f, 0.05f, 0.05f});
    const auto white_scene = build(math::vec3(0.75f));
    const math::vec3 with_red = gi_reference::integrate_irradiance(red_scene, p, up, params);
    const math::vec3 all_white = gi_reference::integrate_irradiance(white_scene, p, up, params);
    check(all_white.x > 0.0f && all_white.y > 0.0f, "white cornell floor is lit");
    const float ratio_red = with_red.x / math::max(with_red.y, 1e-6f);
    const float ratio_white = all_white.x / math::max(all_white.y, 1e-6f);
    std::printf("  r/g near red wall = %.3f, in all-white room = %.3f\n", ratio_red, ratio_white);
    check(ratio_red > 1.1f * ratio_white,
          "red wall reddens the near floor by >10% over the all-white room");
    // And the green channel must have LOST energy relative to white, not the red gained by
    // renormalisation: the red wall absorbs green, it does not emit red.
    check(with_red.y < all_white.y, "green channel loses energy to the absorbing red wall");
}

} // namespace

auto run_gi_oracle_suite(rtti::context& /*ctx*/) -> int
{
    test_shader_constants_match_cpp();
    test_lumen_constants_match_cpp();
    test_gi_shaders_compile_sm50();
    test_reference_is_deterministic();
    test_reference_furnace_conserves_energy();
    test_reference_sealed_room_is_black();
    test_reference_thin_wall_blocks_light();
    test_reference_cornell_bleeds_colour();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("gi constants / reference oracle", run_gi_oracle_suite)

} // namespace unravel::gi_tests
