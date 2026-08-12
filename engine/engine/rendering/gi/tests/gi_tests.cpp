/*
 * GI Phase 0: the validation harness itself (plan: tasks/gi_rewrite_plan.md, sections 9-10).
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

#include "gi_tests.h"
#include "gi_reference_tracer.h"

#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/mesh_sdf_baker.h>
#include <engine/rendering/gi/mesh_sdf_source.h>
#include <engine/rendering/gi/sdf_instance_grid.h>

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

int* g_checks = nullptr;
int* g_failures = nullptr;

void check(bool condition, const std::string& what)
{
    ++(*g_checks);
    if(!condition)
    {
        ++(*g_failures);
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(double actual, double expected, double tolerance, const std::string& what)
{
    ++(*g_checks);
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++(*g_failures);
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n", what.c_str(), actual, expected, tolerance);
    }
}

// ---------------------------------------------------------------------------------------
// Constants parity
// ---------------------------------------------------------------------------------------

/// Parses `#define GI_* <number>` lines from the shader mirror. Non-numeric defines (include
/// guards) are skipped by the GI_ prefix requirement plus the numeric parse.
auto parse_shader_constants(const std::string& path) -> std::map<std::string, double>
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
        if(directive != "#define" || name.rfind("GI_", 0) != 0)
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
    const fs::path shader_dir = fs::path(GI_TESTS_SHADER_DIR) / "gi";
    const fs::path include_dir = GI_TESTS_SHADER_DIR;
    const fs::path varying = shader_dir / "varying.def.io";
    const fs::path out_dir = fs::temp_directory_path() / "gi_shader_compile_test";
    fs::create_directories(out_dir);
    size_t compiled = 0;
    for(const auto& entry : fs::directory_iterator(shader_dir))
    {
        const auto name = entry.path().filename().string();
        if(entry.path().extension() != ".sc")
        {
            continue;
        }
        const bool compute = name.rfind("cs_", 0) == 0;
        const bool fragment = name.rfind("fs_", 0) == 0;
        if(!compute && !fragment)
        {
            continue;
        }
        // BOTH the D3D floor and the OpenGL profile: the backends disagree on real things -
        // GLSL reserves `packed`, rejects expressions in local_size, lacks scalar
        // equal()/notEqual() and legacy *Lod entry points - and every one of those shipped
        // as a D3D-only-tested regression before this second profile existed.
        struct profile_case
        {
            const char* platform;
            const char* profile;
            const char* label;
        };
        const profile_case profiles[] = {
            {"windows", "s_5_0", "SM 5.0"},
            {"linux", "440", "GLSL 440"},
        };
        for(const auto& profile : profiles)
        {
            const fs::path out_bin = out_dir / (name + "." + profile.profile + ".bin");
            const fs::path out_log = out_dir / (name + "." + profile.profile + ".log");
            std::string command = "\"\"" + shaderc.string() + "\" -f \"" + entry.path().string() +
                                  "\" -o \"" + out_bin.string() + "\" -i \"" + include_dir.string() +
                                  "\" --varyingdef \"" + varying.string() +
                                  "\" --type " + (compute ? "compute" : "fragment") +
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

/// Places a baked field into a clipmap-instance slot at a translation, with GI materials.
auto make_clipmap_instance(const mesh_sdf& sdf,
                           const math::vec3& position,
                           const math::vec3& albedo,
                           const math::vec3& emissive) -> global_sdf_instance
{
    global_sdf_instance instance;
    instance.sdf = &sdf;
    const math::mat4 local_to_world = translation(position);
    instance.world_to_local = glm::inverse(local_to_world);
    instance.world_bounds.reset();
    for(int corner = 0; corner < 8; ++corner)
    {
        const math::vec3 local((corner & 1) != 0 ? sdf.bounds.max.x : sdf.bounds.min.x,
                               (corner & 2) != 0 ? sdf.bounds.max.y : sdf.bounds.min.y,
                               (corner & 4) != 0 ? sdf.bounds.max.z : sdf.bounds.min.z);
        instance.world_bounds.add_point(math::vec3(local_to_world * math::vec4(local, 1.0f)));
    }
    instance.local_to_world_scale = 1.0f;
    instance.albedo = albedo;
    instance.emissive = emissive;
    return instance;
}

/// Analytic signed distance to an axis-aligned box centred at @p center.
auto analytic_box_distance(const math::vec3& p, const math::vec3& center, const math::vec3& half) -> float
{
    const math::vec3 q = math::abs(p - center) - half;
    const float outside = math::length(math::max(q, math::vec3(0.0f)));
    const float inside = math::min(math::max(q.x, math::max(q.y, q.z)), 0.0f);
    return outside + inside;
}

/// The attribute composer's contract, on a fixture that can falsify each clause: a THICK box
/// whose interior must not read as surface (the mesh field saturates inside the band there -
/// the exact trap the gradient gate exists for), faces that must, with the box's material, and
/// a surface list that is exactly the set of surface voxels.
void test_attribute_voxels_mark_the_surface_band()
{
    std::printf("test_attribute_voxels_mark_the_surface_band\n");
    const math::vec3 box_half(1.5f);
    mesh_sdf box;
    check(bake_slab(box_half, box), "thick box bakes");
    const math::vec3 albedo(0.8f, 0.2f, 0.1f);
    const math::vec3 emissive(0.0f, 0.0f, 3.0f);
    std::vector<global_sdf_instance> instances;
    instances.push_back(make_clipmap_instance(box, math::vec3(0.0f), albedo, emissive));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.resolution = 32;
    settings.base_extent = 8.0f;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(0.0f));
    const auto& lvl = clipmap.get_level(0);
    const uint32_t attr_res = clipmap.get_attr_resolution();
    const float attr_voxel = lvl.voxel_size * global_sdf_clipmap::attr_downsample;
    const float band = float(gi::GI_SURFACE_VOXEL_BAND) * attr_voxel;
    check(lvl.attr_albedo.size() == size_t(attr_res) * attr_res * attr_res, "attribute storage sized");
    const uint32_t expected_albedo = 204u | (51u << 8u) | (26u << 16u) | (255u << 24u);
    // Toroidal reconstruction, mirroring the composer: slot -> world cell -> centre.
    const int res = int(attr_res);
    const auto wrap = [res](int v) -> int { return ((v % res) + res) % res; };
    const math::ivec3 window_base(int(std::floor(lvl.origin.x / attr_voxel + 0.5f)),
                                  int(std::floor(lvl.origin.y / attr_voxel + 0.5f)),
                                  int(std::floor(lvl.origin.z / attr_voxel + 0.5f)));
    const math::ivec3 base_slot(wrap(window_base.x), wrap(window_base.y), wrap(window_base.z));
    size_t surface_count = 0;
    size_t misclassified_surface = 0;
    size_t misclassified_interior = 0;
    size_t misclassified_exterior = 0;
    size_t wrong_material = 0;
    for(uint32_t z = 0; z < attr_res; ++z)
    {
        for(uint32_t y = 0; y < attr_res; ++y)
        {
            for(uint32_t x = 0; x < attr_res; ++x)
            {
                const math::ivec3 cell = window_base + math::ivec3(wrap(int(x) - base_slot.x),
                                                                    wrap(int(y) - base_slot.y),
                                                                    wrap(int(z) - base_slot.z));
                const math::vec3 center = (math::vec3(cell) + math::vec3(0.5f)) * attr_voxel;
                const float d = analytic_box_distance(center, math::vec3(0.0f), box_half);
                const size_t offset =
                    size_t(x) + size_t(y) * attr_res + size_t(z) * attr_res * attr_res;
                const bool is_surface = (lvl.attr_albedo[offset] >> 24u) == 255u;
                surface_count += is_surface ? 1u : 0u;
                // Judged with slack: the composed field disagrees with the analytic box by up to
                // about a level voxel, so only voxels comfortably inside/outside the band assert.
                if(std::fabs(d) < 0.25f * band && !is_surface)
                {
                    ++misclassified_surface;
                }
                // Deep interior: covered by the band via the bake's conservative empty-inside
                // distances (measured before the gradient gate's removal: holds with no gate).
                if(d < -1.5f * band && is_surface)
                {
                    ++misclassified_interior;
                }
                if(d > 1.5f * band && is_surface)
                {
                    ++misclassified_exterior;
                }
                if(is_surface)
                {
                    if(lvl.attr_albedo[offset] != expected_albedo)
                    {
                        ++wrong_material;
                    }
                    if(lvl.attr_emissive[offset] != emissive)
                    {
                        ++wrong_material;
                    }
                }
            }
        }
    }
    std::printf("  %zu surface voxels; misclassified: %zu on-surface, %zu interior, %zu exterior;"
                " wrong material %zu\n",
                surface_count,
                misclassified_surface,
                misclassified_interior,
                misclassified_exterior,
                wrong_material);
    check(surface_count > 0, "the box produces surface voxels");
    check(misclassified_surface == 0, "every voxel on the surface is marked surface");
    check(misclassified_interior == 0,
          "no deep-interior voxel is marked surface (the gradient gate holds)");
    check(misclassified_exterior == 0, "no far-exterior voxel is marked surface");
    check(wrong_material == 0, "every surface voxel carries the box's albedo and emissive");
    // The list IS the set of surface voxels: same count, every entry unpacks to a marked voxel.
    check(lvl.attr_surface_list.size() == surface_count, "surface list length equals surface count");
    size_t bad_entries = 0;
    for(const uint32_t packed : lvl.attr_surface_list)
    {
        const uint32_t x = packed & 0xFFu;
        const uint32_t y = (packed >> 8u) & 0xFFu;
        const uint32_t z = (packed >> 16u) & 0xFFu;
        const uint32_t level = packed >> 24u;
        if(level != 0u || x >= attr_res || y >= attr_res || z >= attr_res)
        {
            ++bad_entries;
            continue;
        }
        const size_t offset = size_t(x) + size_t(y) * attr_res + size_t(z) * attr_res * attr_res;
        if((lvl.attr_albedo[offset] >> 24u) != 255u)
        {
            ++bad_entries;
        }
    }
    check(bad_entries == 0, "every list entry unpacks to a marked surface voxel");
}

/// Transcription parity for cs_gi_clipmap_attributes.sc: the shader gathers candidates from the
/// TRACER's world grid in cell-range order, while the CPU reference loops the full instance
/// list. The two can only disagree by a missed candidate or an argmin tie resolved differently
/// - which is exactly what a multi-instance fixture with overlapping, equal-distance surfaces
/// exercises. Same shape as test_clipmap_compose_shader_transcription_matches_cpu.
void test_clipmap_attribute_transcription_matches_cpu()
{
    std::printf("test_clipmap_attribute_transcription_matches_cpu\n");
    mesh_sdf box;
    check(bake_slab(math::vec3(0.6f), box), "fixture box bakes");
    // Overlapping instances with DISTINCT materials, spread so plenty of attribute voxels sit
    // near an instance without being inside its bounds, plus coincident pairs so ties occur.
    std::vector<global_sdf_instance> instances;
    for(int i = 0; i < 40; ++i)
    {
        const float t = float(i);
        const math::vec3 position(6.0f * std::sin(t * 1.7f), 2.0f * std::cos(t * 2.3f), 6.0f * std::sin(t * 0.9f));
        const math::vec3 albedo(0.1f + 0.02f * float(i % 7), 0.2f + 0.02f * float(i % 5), 0.3f);
        const math::vec3 emissive = (i % 9 == 0) ? math::vec3(0.0f, float(i) * 0.1f, 0.0f) : math::vec3(0.0f);
        instances.push_back(make_clipmap_instance(box, position, albedo, emissive));
    }
    // A coincident pair: identical placement, different materials - the pure tie case, decided
    // only by the index rule.
    instances.push_back(make_clipmap_instance(box, math::vec3(1.0f, 0.0f, 1.0f), math::vec3(0.9f, 0.1f, 0.1f), math::vec3(0.0f)));
    instances.push_back(make_clipmap_instance(box, math::vec3(1.0f, 0.0f, 1.0f), math::vec3(0.1f, 0.9f, 0.1f), math::vec3(0.0f)));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.resolution = 32;
    settings.base_extent = 10.0f;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(0.0f));
    // The tracer's grid over RAW bounds, as the dispatch binds it.
    std::vector<math::bbox> raw_bounds;
    for(const auto& inst : instances)
    {
        raw_bounds.push_back(inst.world_bounds);
    }
    sdf_instance_grid tracer_grid;
    tracer_grid.init({});
    tracer_grid.build(raw_bounds);
    check(tracer_grid.is_valid(), "tracer grid builds");
    const auto& offsets = tracer_grid.get_cell_offsets();
    const auto& cell_instances = tracer_grid.get_cell_instances();
    const math::vec3 grid_origin = tracer_grid.get_origin();
    const float cell_size = tracer_grid.get_cell_size();
    const math::uvec3 grid_dim = tracer_grid.get_dim();
    size_t compared = 0;
    size_t albedo_mismatches = 0;
    size_t emissive_mismatches = 0;
    size_t list_mismatches = 0;
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const auto& lvl = clipmap.get_level(level);
        if(!lvl.is_valid())
        {
            continue;
        }
        const uint32_t attr_res = clipmap.get_attr_resolution();
        const float attr_voxel = lvl.voxel_size * global_sdf_clipmap::attr_downsample;
        const float band = float(gi::GI_SURFACE_VOXEL_BAND) * attr_voxel;
        const float attr_reach = (float(gi::GI_SURFACE_VOXEL_BAND) + 1.0f) * attr_voxel;
        const int res = int(attr_res);
        const auto wrap = [res](int v) -> int { return ((v % res) + res) % res; };
        const math::ivec3 window_base(int(std::floor(lvl.origin.x / attr_voxel + 0.5f)),
                                      int(std::floor(lvl.origin.y / attr_voxel + 0.5f)),
                                      int(std::floor(lvl.origin.z / attr_voxel + 0.5f)));
        const math::ivec3 base_slot(wrap(window_base.x), wrap(window_base.y), wrap(window_base.z));
        std::vector<uint32_t> transcription_list;
        for(uint32_t z = 0; z < attr_res; ++z)
        {
            for(uint32_t y = 0; y < attr_res; ++y)
            {
                for(uint32_t x = 0; x < attr_res; ++x)
                {
                    const math::ivec3 cell =
                        window_base + math::ivec3(wrap(int(x) - base_slot.x),
                                                  wrap(int(y) - base_slot.y),
                                                  wrap(int(z) - base_slot.z));
                    const math::vec3 center = (math::vec3(cell) + math::vec3(0.5f)) * attr_voxel;
                    // Gates transcribed from the shader; both sides call the same sample_level.
                    uint32_t expected_albedo = 0u;
                    math::vec3 expected_emissive(0.0f);
                    // Band gate alone, as the composer now judges it (the gradient gate was
                    // removed for rejecting thin walls - see compose_level_attributes).
                    const float field = clipmap.sample_level(level, center);
                    const bool is_surface =
                        field < global_sdf_clipmap::outside_distance && std::fabs(field) <= band;
                    if(is_surface)
                    {
                        // The shader's cell-range TOP-2 walk, transcribed (idempotent under
                        // the grid's repeated candidate visits, blended by proximity).
                        float best_magnitude = attr_reach;
                        float second_magnitude = attr_reach;
                        int best_index = -1;
                        int second_index = -1;
                        const auto to_cell = [&](const math::vec3& p) -> math::ivec3
                        {
                            const math::vec3 f = math::floor((p - grid_origin) / cell_size);
                            return math::ivec3(int(math::clamp(f.x, 0.0f, float(grid_dim.x) - 1.0f)),
                                               int(math::clamp(f.y, 0.0f, float(grid_dim.y) - 1.0f)),
                                               int(math::clamp(f.z, 0.0f, float(grid_dim.z) - 1.0f)));
                        };
                        const math::ivec3 lo = to_cell(center - math::vec3(attr_reach));
                        const math::ivec3 hi = to_cell(center + math::vec3(attr_reach));
                        for(int cz = lo.z; cz <= hi.z; ++cz)
                        {
                            for(int cy = lo.y; cy <= hi.y; ++cy)
                            {
                                for(int cx = lo.x; cx <= hi.x; ++cx)
                                {
                                    const size_t cell = size_t(cx) + size_t(cy) * grid_dim.x +
                                                        size_t(cz) * size_t(grid_dim.x) * grid_dim.y;
                                    for(size_t c = offsets[cell]; c < offsets[cell + 1]; ++c)
                                    {
                                        const int index = int(cell_instances[c]);
                                        if(index == best_index || index == second_index)
                                        {
                                            continue;
                                        }
                                        const auto& inst = instances[size_t(index)];
                                        const math::vec3 clamped = math::clamp(center,
                                                                               inst.world_bounds.min,
                                                                               inst.world_bounds.max);
                                        if(math::length(center - clamped) >= second_magnitude)
                                        {
                                            continue;
                                        }
                                        const math::vec4 local =
                                            inst.world_to_local * math::vec4(center, 1.0f);
                                        // Shell de-bias, exactly as both composers apply it.
                                        const float shell_bias = inst.sdf->is_two_sided
                                                                     ? inst.sdf->two_sided_thickness
                                                                     : 0.0f;
                                        const float magnitude =
                                            std::fabs((sample_mesh_sdf(*inst.sdf, math::vec3(local)) +
                                                       shell_bias) *
                                                      inst.local_to_world_scale);
                                        if(magnitude < best_magnitude ||
                                           (magnitude == best_magnitude &&
                                            (best_index < 0 || index < best_index)))
                                        {
                                            second_magnitude = best_magnitude;
                                            second_index = best_index;
                                            best_magnitude = magnitude;
                                            best_index = index;
                                        }
                                        else if(magnitude < second_magnitude ||
                                                (magnitude == second_magnitude &&
                                                 (second_index < 0 || index < second_index)))
                                        {
                                            second_magnitude = magnitude;
                                            second_index = index;
                                        }
                                    }
                                }
                            }
                        }
                        if(best_index >= 0)
                        {
                            const auto& first = instances[size_t(best_index)];
                            math::vec3 blended_albedo = first.albedo;
                            math::vec3 blended_emissive = first.emissive;
                            if(second_index >= 0)
                            {
                                // Shell coverage scaling, exactly as both composers apply it.
                                const auto shell_coverage = [&](const global_sdf_instance& inst) -> float
                                {
                                    if(!inst.sdf->is_two_sided)
                                    {
                                        return 1.0f;
                                    }
                                    return math::clamp(2.0f * inst.sdf->two_sided_thickness *
                                                           inst.local_to_world_scale / attr_voxel,
                                                       0.0f,
                                                       1.0f);
                                };
                                const float w1 = (attr_reach - best_magnitude) *
                                                 shell_coverage(instances[size_t(best_index)]);
                                const float w2 = (attr_reach - second_magnitude) *
                                                 shell_coverage(instances[size_t(second_index)]);
                                const float w_sum = std::max(w1 + w2, 1e-6f);
                                blended_albedo = (first.albedo * w1 +
                                                  instances[size_t(second_index)].albedo * w2) /
                                                 w_sum;
                                blended_emissive = (first.emissive * w1 +
                                                    instances[size_t(second_index)].emissive * w2) /
                                                   w_sum;
                            }
                            const auto quantize = [](float v) -> uint32_t
                            { return uint32_t(math::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                            expected_albedo = quantize(blended_albedo.x) |
                                              (quantize(blended_albedo.y) << 8u) |
                                              (quantize(blended_albedo.z) << 16u) | (255u << 24u);
                            expected_emissive = blended_emissive;
                            transcription_list.push_back(
                                global_sdf_clipmap::pack_surface_voxel(x, y, z, level));
                        }
                    }
                    const size_t offset =
                        size_t(x) + size_t(y) * attr_res + size_t(z) * attr_res * attr_res;
                    ++compared;
                    if(lvl.attr_albedo[offset] != expected_albedo)
                    {
                        ++albedo_mismatches;
                        if(albedo_mismatches == 1)
                        {
                            std::printf("  first albedo mismatch: level %u voxel (%u,%u,%u) "
                                        "transcription %08x cpu %08x\n",
                                        level,
                                        x,
                                        y,
                                        z,
                                        expected_albedo,
                                        lvl.attr_albedo[offset]);
                        }
                    }
                    if(lvl.attr_emissive[offset] != expected_emissive)
                    {
                        ++emissive_mismatches;
                    }
                }
            }
        }
        // The lists are both built in z-major voxel order here, so exact equality is the right
        // comparison for the CPU pair; the GPU list is order-free and compared as a set via the
        // albedo alpha channel, which the semantic test already pins.
        if(transcription_list != lvl.attr_surface_list)
        {
            ++list_mismatches;
        }
    }
    std::printf("  %zu attribute voxels compared: %zu albedo, %zu emissive, %zu list mismatches\n",
                compared,
                albedo_mismatches,
                emissive_mismatches,
                list_mismatches);
    check(compared > 0, "the fixture composed attribute voxels");
    check(albedo_mismatches == 0, "the shader walk attributes byte-identical albedo");
    check(emissive_mismatches == 0, "the shader walk attributes identical emissive");
    check(list_mismatches == 0, "the shader walk produces the identical surface list");
}

// ---------------------------------------------------------------------------------------
// Shadow-ray blob diagnostic (round 15)
// ---------------------------------------------------------------------------------------

/// One recorded sample of the shadow march, for post-mortem printing.
struct shadow_march_step
{
    float t;
    float d;
    float voxel;
    float accept;
    bool suppressed;
};

enum class shadow_end
{
    miss,
    resolved_hit,
    redescent_hit,
    exhausted
};

struct shadow_trace_result
{
    bool hit = false;
    bool exhausted = false;
    float t = 0.0f;
    float clearance = 1e8f;
    int steps = 0;
    shadow_end end = shadow_end::miss;
};

/// Faithful CPU transcription of the shader's SdfTraceClipmap for SHADOW rays (expand off,
/// t_min 0, want_normal off), including the exhaustion contract and the saturation step boost.
/// Exists to make the GPU-only round-15 black-blob failure reproducible and classifiable on the
/// CPU; keep in step with sdf_common.sh by hand.
///
/// @param legacy_contract true reproduces the PRE-FIX shader - no saturation step boost, and
///        the exhaustion tail zeroes the accumulated clearance - so the test can still print
///        the blob it guards against; false matches the current sdf_common.sh.
auto trace_clipmap_shadow(const global_sdf_clipmap& clipmap,
                          const math::vec3& origin,
                          const math::vec3& direction,
                          float t_max,
                          bool legacy_contract,
                          std::vector<shadow_march_step>* log) -> shadow_trace_result
{
    shadow_trace_result result;
    const float surface_bias = float(gi::GI_SHADOW_SURFACE_BIAS);
    const float relaxation = float(gi::GI_SHADOW_RELAXATION);
    const int max_steps = int(gi::GI_TRACE_MAX_STEPS);
    float t = 0.0f;
    bool suppressed = false;
    float suppress_best = -1e8f;
    bool first_sample = true;
    for(int step = 0; step < max_steps; ++step)
    {
        if(t > t_max)
        {
            return result;
        }
        const math::vec3 p = origin + direction * t;
        float voxel = 0.0f;
        const float d = clipmap.sample_ex(p, voxel);
        const float d_raw = d;
        ++result.steps;
        const float base_threshold = std::max(surface_bias * voxel, 1e-6f);
        const float accept = std::min(std::max(base_threshold, t * relaxation), voxel);
        if(log != nullptr)
        {
            log->push_back({t, d, voxel, accept, suppressed});
        }
        if(first_sample)
        {
            first_sample = false;
            suppressed = d < accept;
        }
        bool redescent = false;
        if(suppressed)
        {
            if(d >= accept)
            {
                suppressed = false;
            }
            else if(d < suppress_best - voxel)
            {
                suppressed = false;
                redescent = true;
            }
            else
            {
                suppress_best = std::max(suppress_best, d);
                t += std::max(std::fabs(d), base_threshold);
                continue;
            }
        }
        if(d < accept)
        {
            result.hit = true;
            result.clearance = 0.0f;
            result.t = t;
            result.end = redescent ? shadow_end::redescent_hit : shadow_end::resolved_hit;
            return result;
        }
        result.clearance = std::min(result.clearance, d_raw);
        float step_distance = d;
        if(!legacy_contract && d_raw >= (float(mesh_sdf::encode_range) - 0.5f) * voxel)
        {
            for(int coarse = int(global_sdf_clipmap::level_count) - 1; coarse >= 0; --coarse)
            {
                const float coarse_distance = clipmap.sample_level(uint32_t(coarse), p);
                if(coarse_distance < global_sdf_clipmap::outside_distance)
                {
                    step_distance = std::max(step_distance, coarse_distance);
                    break;
                }
            }
        }
        t += std::max(step_distance, base_threshold);
    }
    result.hit = true;
    result.exhausted = true;
    result.t = t;
    result.end = shadow_end::exhausted;
    if(legacy_contract || result.clearance > 1e7f)
    {
        result.clearance = 0.0f;
    }
    return result;
}

/// Transcription of GiTraceShadow (gi_lighting.sh) over the clipmap tier alone - the blob
/// region sits beyond the mesh-exact near field, so this is the path that produces it.
auto trace_shadow_visibility(const global_sdf_clipmap& clipmap,
                             const math::vec3& world_position,
                             const math::vec3& world_normal,
                             const math::vec3& to_light,
                             float voxel_size,
                             bool legacy_contract,
                             shadow_trace_result* out_trace,
                             std::vector<shadow_march_step>* log) -> float
{
    const float offset = float(gi::GI_SHADOW_NORMAL_BIAS_VOXELS) * voxel_size;
    math::vec3 origin = world_position + world_normal * offset;
    float max_distance = float(gi::GI_SHADOW_DISTANCE);
    if(max_distance <= offset)
    {
        return 1.0f;
    }
    origin += to_light * (float(gi::GI_SHADOW_RAY_START_VOXELS) * voxel_size);
    const auto hit =
        trace_clipmap_shadow(clipmap, origin, to_light, max_distance, legacy_contract, log);
    if(out_trace != nullptr)
    {
        *out_trace = hit;
    }
    if(hit.hit && !hit.exhausted)
    {
        return 0.0f;
    }
    return math::clamp(hit.clearance / std::max(voxel_size, 1e-4f), 0.0f, 1.0f);
}

/// Analytic ray-box slab test: does the sun ray from @p origin genuinely hit the building?
auto ray_hits_box(const math::vec3& origin,
                  const math::vec3& direction,
                  const math::vec3& center,
                  const math::vec3& half) -> bool
{
    float t_near = 0.0f;
    float t_far = 1e9f;
    for(int axis = 0; axis < 3; ++axis)
    {
        const float o = origin[axis] - center[axis];
        const float dir = direction[axis];
        if(std::fabs(dir) < 1e-8f)
        {
            if(std::fabs(o) > half[axis])
            {
                return false;
            }
            continue;
        }
        float t0 = (-half[axis] - o) / dir;
        float t1 = (half[axis] - o) / dir;
        if(t0 > t1)
        {
            std::swap(t0, t1);
        }
        t_near = std::max(t_near, t0);
        t_far = std::min(t_far, t1);
    }
    return t_near <= t_far;
}

/// Round-15 regression: a big thin floor with one building under a low sun, shadow-traced
/// exactly as the DirectLight debug view does it, against the analytic box shadow.
///
/// The legacy contract (no saturation step boost + clearance zeroed on exhaustion) painted the
/// camera's fine cascade windows' PROJECTED SHADOW onto the floor along the sun azimuth: a 100 m
/// sun ray crossing level 0's window paid 32 of its 64 steps at saturated 4-fine-voxel readings
/// and exhausted mid-air, and exhaustion read as full occlusion. The mode is kept and printed so
/// the failure stays visible; the current contract must produce ZERO wrongly-dark texels, while
/// the building's true shadow stays dark at every distance (no leak traded in).
void test_shadow_blob_floor_building()
{
    std::printf("test_shadow_blob_floor_building\n");
    mesh_sdf floor_sdf;
    mesh_sdf building_sdf;
    check(bake_slab({30.0f, 0.2f, 30.0f}, floor_sdf), "floor bakes");
    check(bake_slab({4.0f, 6.0f, 4.0f}, building_sdf), "building bakes");
    const math::vec3 building_center(-14.0f, 5.8f, 0.0f);
    const math::vec3 building_half(4.0f, 6.0f, 4.0f);
    std::vector<global_sdf_instance> instances;
    instances.push_back(make_clipmap_instance(floor_sdf, {0.0f, -0.2f, 0.0f}, math::vec3(0.7f), math::vec3(0.0f)));
    instances.push_back(make_clipmap_instance(building_sdf, building_center, math::vec3(0.7f), math::vec3(0.0f)));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.resolution = 128;
    settings.base_extent = 16.0f;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(12.0f, 3.0f, 0.0f));
    const float elevations[] = {30.0f, 20.0f, 12.0f};
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    for(const float elevation : elevations)
    {
        const float rad = elevation * math::pi<float>() / 180.0f;
        const math::vec3 to_light(-std::cos(rad), std::sin(rad), 0.0f);
        for(const bool legacy : {true, false})
        {
            int wrong_dark = 0;
            int shadow_leaks = 0;
            int shadow_ok = 0;
            int lit_ok = 0;
            std::string map;
            math::vec3 sample_point{};
            shadow_end sample_kind = shadow_end::miss;
            for(int z = -14; z <= 14; z += 2)
            {
                for(int x = -28; x <= 40; ++x)
                {
                    // Launch points inside the building's footprint have no meaningful
                    // up-facing floor surface; skip them rather than classify nonsense.
                    if(std::fabs(float(x) - building_center.x) <= building_half.x + 0.5f &&
                       std::fabs(float(z) - building_center.z) <= building_half.z + 0.5f)
                    {
                        map += ' ';
                        continue;
                    }
                    const math::vec3 p(float(x), 0.0f, float(z));
                    float voxel = 0.0f;
                    clipmap.sample_ex(p, voxel);
                    shadow_trace_result trace;
                    const float visibility =
                        trace_shadow_visibility(clipmap, p, up, to_light, voxel, legacy, &trace, nullptr);
                    const bool expected_shadow = ray_hits_box(p, to_light, building_center, building_half);
                    // Cells bordering the analytic shadow edge are penumbra: the cone acceptance
                    // legitimately fattens the coarse building by about a voxel there, so they
                    // are mapped but not counted either way.
                    const bool boundary =
                        ray_hits_box({float(x) - 1.0f, 0.0f, float(z)}, to_light, building_center, building_half) !=
                            expected_shadow ||
                        ray_hits_box({float(x) + 1.0f, 0.0f, float(z)}, to_light, building_center, building_half) !=
                            expected_shadow;
                    const bool dark = visibility < 0.5f;
                    char cell = '.';
                    if(boundary)
                    {
                        cell = 'b';
                    }
                    else if(expected_shadow)
                    {
                        cell = dark ? 's' : 'L';
                        dark ? ++shadow_ok : ++shadow_leaks;
                    }
                    else if(dark)
                    {
                        switch(trace.end)
                        {
                            case shadow_end::resolved_hit:
                                cell = 'R';
                                break;
                            case shadow_end::redescent_hit:
                                cell = 'D';
                                break;
                            case shadow_end::exhausted:
                                cell = 'X';
                                break;
                            case shadow_end::miss:
                                cell = 'M';
                                break;
                        }
                        ++wrong_dark;
                        if(sample_kind == shadow_end::miss)
                        {
                            sample_point = p;
                            sample_kind = trace.end;
                        }
                    }
                    else
                    {
                        ++lit_ok;
                    }
                    map += cell;
                }
                map += '\n';
            }
            std::printf("  elevation %.0f deg, %s: wrong dark %d, shadow leaks %d (lit ok %d, shadow ok %d)\n",
                        elevation,
                        legacy ? "LEGACY contract" : "current",
                        wrong_dark,
                        shadow_leaks,
                        lit_ok,
                        shadow_ok);
            if(legacy)
            {
                // The blob on record: what the fix removed. Not asserted - it documents.
                std::printf("%s", map.c_str());
                continue;
            }
            check(wrong_dark == 0,
                  "no wrongly-dark floor texels at elevation " + std::to_string(int(elevation)) +
                      " (got " + std::to_string(wrong_dark) + ")");
            check(shadow_leaks == 0,
                  "building shadow stays dark at elevation " + std::to_string(int(elevation)) +
                      " (leaked " + std::to_string(shadow_leaks) + ")");
            check(shadow_ok > 0, "the analytic shadow region is sampled at all");
            if(wrong_dark > 0 && sample_kind != shadow_end::miss)
            {
                std::printf("%s", map.c_str());
                std::vector<shadow_march_step> log;
                float voxel = 0.0f;
                clipmap.sample_ex(sample_point, voxel);
                trace_shadow_visibility(clipmap, sample_point, up, to_light, voxel, false, nullptr, &log);
                std::printf("  march at (%.0f, 0, %.0f), launch voxel %.3f:\n",
                            sample_point.x,
                            sample_point.z,
                            voxel);
                for(const auto& s : log)
                {
                    std::printf("    t=%7.3f d=%7.3f voxel=%.3f accept=%.3f%s\n",
                                s.t,
                                s.d,
                                s.voxel,
                                s.accept,
                                s.suppressed ? " [suppressed]" : "");
                }
            }
        }
    }
}

/// Sponza-arcade regression: a corridor floor behind a row of columns, sun shining through the
/// gaps. The measured failure was the light volume converging BLACK corridor-wide - every sun
/// ray threading a real opening resolved as occluded - so this pins the opposite: floor texels
/// in line with a gap must read lit, texels behind a column must stay dark, at a straight and
/// an oblique sun azimuth, on the shipping cascade scale (base extent 16, resolution 64:
/// 0.25 m level-0 voxels against 1.0 m gaps).
void test_shadow_through_colonnade()
{
    std::printf("test_shadow_through_colonnade\n");
    mesh_sdf floor_sdf;
    mesh_sdf column_sdf;
    mesh_sdf roof_sdf;
    check(bake_slab({20.0f, 0.2f, 20.0f}, floor_sdf), "floor bakes");
    check(bake_slab({0.4f, 2.0f, 0.4f}, column_sdf), "column bakes");
    check(bake_slab({2.0f, 0.2f, 20.0f}, roof_sdf), "roof bakes");
    const math::vec3 column_half(0.4f, 2.0f, 0.4f);
    const math::vec3 roof_center(-2.0f, 4.2f, 0.0f);
    const math::vec3 roof_half(2.0f, 0.2f, 20.0f);
    std::vector<math::vec3> column_centers;
    std::vector<global_sdf_instance> instances;
    instances.push_back(make_clipmap_instance(floor_sdf, {0.0f, -0.2f, 0.0f}, math::vec3(0.7f), math::vec3(0.0f)));
    instances.push_back(make_clipmap_instance(roof_sdf, roof_center, math::vec3(0.7f), math::vec3(0.0f)));
    for(float z = -12.0f; z <= 12.0f; z += 2.0f)
    {
        column_centers.push_back({0.0f, 2.0f, z});
        instances.push_back(
            make_clipmap_instance(column_sdf, column_centers.back(), math::vec3(0.7f), math::vec3(0.0f)));
    }
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(-2.0f, 1.0f, 0.0f));
    const float elevation_rad = 30.0f * math::pi<float>() / 180.0f;
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const float azimuths[] = {0.0f, 25.0f};
    for(const float azimuth : azimuths)
    {
        const float azimuth_rad = azimuth * math::pi<float>() / 180.0f;
        const math::vec3 to_light(std::cos(elevation_rad) * std::cos(azimuth_rad),
                                  std::sin(elevation_rad),
                                  std::cos(elevation_rad) * std::sin(azimuth_rad));
        const auto blocked = [&](const math::vec3& p) -> bool
        {
            if(ray_hits_box(p, to_light, roof_center, roof_half))
            {
                return true;
            }
            for(const auto& center : column_centers)
            {
                if(ray_hits_box(p, to_light, center, column_half))
                {
                    return true;
                }
            }
            return false;
        };
        int wrong_dark = 0;
        int leaks = 0;
        int lit_ok = 0;
        int shadow_ok = 0;
        std::string map;
        math::vec3 wrong_dark_point{};
        bool have_wrong_dark = false;
        for(float x = -1.0f; x >= -5.5f; x -= 0.5f)
        {
            for(float z = -8.0f; z <= 8.0f; z += 0.25f)
            {
                const math::vec3 p(x, 0.0f, z);
                const bool expected_shadow = blocked(p);
                // The composed field legitimately fattens a column by about a voxel, so texels
                // whose analytic answer flips within 0.3 m of z are penumbra: mapped, not counted.
                const bool boundary = blocked({x, 0.0f, z - 0.3f}) != expected_shadow ||
                                      blocked({x, 0.0f, z + 0.3f}) != expected_shadow;
                float voxel = 0.0f;
                clipmap.sample_ex(p, voxel);
                const float visibility =
                    trace_shadow_visibility(clipmap, p, up, to_light, voxel, false, nullptr, nullptr);
                const bool dark = visibility < 0.5f;
                char cell = '.';
                if(boundary)
                {
                    cell = 'b';
                }
                else if(expected_shadow)
                {
                    cell = dark ? 's' : 'L';
                    dark ? ++shadow_ok : ++leaks;
                }
                else if(dark)
                {
                    cell = 'X';
                    ++wrong_dark;
                    if(!have_wrong_dark)
                    {
                        wrong_dark_point = p;
                        have_wrong_dark = true;
                    }
                }
                else
                {
                    ++lit_ok;
                }
                map += cell;
            }
            map += '\n';
        }
        std::printf("  azimuth %.0f deg: wrong dark %d, leaks %d (lit ok %d, shadow ok %d)\n",
                    azimuth,
                    wrong_dark,
                    leaks,
                    lit_ok,
                    shadow_ok);
        if(wrong_dark > 0 || leaks > 0)
        {
            std::printf("%s", map.c_str());
        }
        if(have_wrong_dark)
        {
            std::vector<shadow_march_step> log;
            float voxel = 0.0f;
            clipmap.sample_ex(wrong_dark_point, voxel);
            trace_shadow_visibility(clipmap, wrong_dark_point, up, to_light, voxel, false, nullptr, &log);
            std::printf("  march at (%.2f, 0, %.2f), launch voxel %.3f:\n",
                        wrong_dark_point.x,
                        wrong_dark_point.z,
                        voxel);
            for(const auto& s : log)
            {
                std::printf("    t=%7.3f d=%7.3f voxel=%.3f accept=%.3f%s\n",
                            s.t,
                            s.d,
                            s.voxel,
                            s.accept,
                            s.suppressed ? " [suppressed]" : "");
            }
        }
        check(wrong_dark == 0,
              "sun passes the colonnade gaps at azimuth " + std::to_string(int(azimuth)) + " (wrongly dark " +
                  std::to_string(wrong_dark) + ")");
        check(leaks == 0,
              "column shadows hold at azimuth " + std::to_string(int(azimuth)) + " (leaked " +
                  std::to_string(leaks) + ")");
        check(shadow_ok > 0, "the column shadow region is sampled at all");
        check(lit_ok > 0, "the gap region is sampled at all");
    }
}

/// Appends @p box-shaped geometry at @p center to @p g, for building multi-part single meshes.
void append_box(sdf_source_geometry& g, const math::vec3& center, const math::vec3& half)
{
    const sdf_source_geometry box = make_box_geometry(half);
    const uint32_t base = uint32_t(g.positions.size());
    for(const auto& p : box.positions)
    {
        g.positions.push_back(p + center);
        g.bounds.add_point(p + center);
    }
    for(const uint32_t index : box.indices)
    {
        g.indices.push_back(base + index);
    }
}

/// The SAME colonnade as test_shadow_through_colonnade, but as ONE submesh baked at the asset
/// compiler's production defaults - which is what a material-grouped arcade (Sponza: every
/// column, arch and rail of one stone material in one submesh spanning the building) actually
/// gets. The bounds then dictate the voxel size, and the question this pins is whether the
/// baked field and the cascade composed FROM it keep the real openings open.
void test_shadow_through_coarse_baked_colonnade()
{
    std::printf("test_shadow_through_coarse_baked_colonnade\n");
    const math::vec3 column_half(0.3f, 2.0f, 0.3f);
    sdf_source_geometry merged;
    merged.bounds.reset();
    std::vector<math::vec3> column_centers;
    for(float z = -12.0f; z <= 12.0f; z += 2.0f)
    {
        column_centers.push_back({0.0f, 2.0f, z});
        append_box(merged, column_centers.back(), column_half);
    }
    mesh_sdf colonnade_sdf;
    mesh_sdf_bake_settings production;
    check(bake_mesh_sdf(merged, production, colonnade_sdf), "merged colonnade bakes");
    std::printf("  merged bounds span %.1f m, production bake voxel %.3f m\n",
                merged.bounds.max.z - merged.bounds.min.z,
                colonnade_sdf.voxel_size);
    // Field openness at the gap centres: the smallest reading along a vertical line mid-gap on
    // the colonnade plane. Analytically that line is 0.7 m from the nearest column face; a
    // reading at or below the mesh-tier acceptance (0.35 voxel) means a sun ray through the
    // MIDDLE of a real gap terminates on geometry that is not there.
    const float accept = 0.35f * colonnade_sdf.voxel_size;
    float worst_gap_reading = 1e8f;
    for(size_t gap = 0; gap + 1 < column_centers.size(); ++gap)
    {
        const float gap_z = (column_centers[gap].z + column_centers[gap + 1].z) * 0.5f;
        for(float y = 0.3f; y <= 1.7f; y += 0.2f)
        {
            worst_gap_reading = std::min(worst_gap_reading, sample_mesh_sdf(colonnade_sdf, {0.0f, y, gap_z}));
        }
    }
    std::printf("  worst mid-gap field reading %.3f m (analytic 0.700, mesh-tier accept %.3f)\n",
                worst_gap_reading,
                accept);
    check(worst_gap_reading > accept, "the baked field keeps the gap centres open");
    // The cascade composed from that field, traced exactly as the light-voxel pass does.
    mesh_sdf floor_sdf;
    check(bake_slab({20.0f, 0.2f, 20.0f}, floor_sdf), "floor bakes");
    std::vector<global_sdf_instance> instances;
    instances.push_back(make_clipmap_instance(floor_sdf, {0.0f, -0.2f, 0.0f}, math::vec3(0.7f), math::vec3(0.0f)));
    instances.push_back(make_clipmap_instance(colonnade_sdf, {0.0f, 0.0f, 0.0f}, math::vec3(0.7f), math::vec3(0.0f)));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(-2.0f, 1.0f, 0.0f));
    const float elevation_rad = 30.0f * math::pi<float>() / 180.0f;
    const math::vec3 to_light(std::cos(elevation_rad), std::sin(elevation_rad), 0.0f);
    const math::vec3 up(0.0f, 1.0f, 0.0f);
    const auto blocked = [&](const math::vec3& p) -> bool
    {
        for(const auto& center : column_centers)
        {
            if(ray_hits_box(p, to_light, center, column_half))
            {
                return true;
            }
        }
        return false;
    };
    int wrong_dark = 0;
    int leaks = 0;
    int lit_ok = 0;
    int shadow_ok = 0;
    std::string map;
    math::vec3 wrong_dark_point{};
    bool have_wrong_dark = false;
    for(float x = -1.0f; x >= -5.5f; x -= 0.5f)
    {
        for(float z = -8.0f; z <= 8.0f; z += 0.25f)
        {
            const math::vec3 p(x, 0.0f, z);
            const bool expected_shadow = blocked(p);
            const bool boundary = blocked({x, 0.0f, z - 0.3f}) != expected_shadow ||
                                  blocked({x, 0.0f, z + 0.3f}) != expected_shadow;
            float voxel = 0.0f;
            clipmap.sample_ex(p, voxel);
            const float visibility =
                trace_shadow_visibility(clipmap, p, up, to_light, voxel, false, nullptr, nullptr);
            const bool dark = visibility < 0.5f;
            char cell = '.';
            if(boundary)
            {
                cell = 'b';
            }
            else if(expected_shadow)
            {
                cell = dark ? 's' : 'L';
                dark ? ++shadow_ok : ++leaks;
            }
            else if(dark)
            {
                cell = 'X';
                ++wrong_dark;
                if(!have_wrong_dark)
                {
                    wrong_dark_point = p;
                    have_wrong_dark = true;
                }
            }
            else
            {
                ++lit_ok;
            }
            map += cell;
        }
        map += '\n';
    }
    std::printf("  composed cascade: wrong dark %d, leaks %d (lit ok %d, shadow ok %d)\n",
                wrong_dark,
                leaks,
                lit_ok,
                shadow_ok);
    if(wrong_dark > 0 || leaks > 0)
    {
        std::printf("%s", map.c_str());
    }
    if(have_wrong_dark)
    {
        std::vector<shadow_march_step> log;
        float voxel = 0.0f;
        clipmap.sample_ex(wrong_dark_point, voxel);
        trace_shadow_visibility(clipmap, wrong_dark_point, up, to_light, voxel, false, nullptr, &log);
        std::printf("  march at (%.2f, 0, %.2f), launch voxel %.3f:\n",
                    wrong_dark_point.x,
                    wrong_dark_point.z,
                    voxel);
        for(const auto& s : log)
        {
            std::printf("    t=%7.3f d=%7.3f voxel=%.3f accept=%.3f%s\n",
                        s.t,
                        s.d,
                        s.voxel,
                        s.accept,
                        s.suppressed ? " [suppressed]" : "");
        }
    }
    check(wrong_dark == 0,
          "sun passes the coarse-baked colonnade gaps (wrongly dark " + std::to_string(wrong_dark) + ")");
    check(leaks == 0, "coarse-baked column shadows hold (leaked " + std::to_string(leaks) + ")");
    check(shadow_ok > 0, "the coarse column shadow region is sampled at all");
    check(lit_ok > 0, "the coarse gap region is sampled at all");
}

/// Sponza-curtain regression: a RED solid box with a GREEN two-sided sheet hanging nearby, the
/// sheet baked coarse so its shell floor is a huge half-metre-plus slab - the production
/// situation for fabric in material-merged submeshes. Attribution used to judge candidates by
/// raw |field distance|, and a shell's zero isosurface is a phantom skin half a thickness away
/// from the cloth - so cells whose true nearest surface is the box wore the sheet's albedo
/// (measured: curtain and rope colours painted onto Sponza's stone). With the shell de-bias,
/// every cell nearer the box than the CLOTH must blend box-dominant.
void test_attribution_prefers_true_surface_over_shell()
{
    std::printf("test_attribution_prefers_true_surface_over_shell\n");
    mesh_sdf box_sdf;
    check(bake_slab({1.0f, 1.0f, 1.0f}, box_sdf), "box bakes");
    // The sheet sits so a cell CENTRE (attr voxel 0.625, centres at 0.9375 and 1.5625) lands in
    // the flip zone: at x = 1.5625 the box face is 0.5625 away and the cloth 0.8375 - the box is
    // the true nearest - while the shell's phantom skin (cloth - 0.7) passes only 0.14 away,
    // which the old raw-|d| contest preferred.
    sdf_source_geometry sheet;
    add_quad(sheet,
             {2.4f, -1.0f, -1.0f},
             {2.4f, 1.0f, -1.0f},
             {2.4f, 1.0f, 1.0f},
             {2.4f, -1.0f, 1.0f});
    sheet.bounds.reset();
    for(const auto& p : sheet.positions)
    {
        sheet.bounds.add_point(p);
    }
    mesh_sdf sheet_sdf;
    mesh_sdf_bake_settings sheet_settings;
    // A fat shell AUTHORED on purpose (the baker's thin-geometry escalation would otherwise
    // spend resolution to thin a floored one): 0.7 m half-thickness reproduces what a
    // material-merged curtain used to get from the one-voxel floor at production scales.
    sheet_settings.resolution = 3;
    sheet_settings.two_sided = true;
    sheet_settings.two_sided_thickness = 0.7f;
    check(bake_mesh_sdf(sheet, sheet_settings, sheet_sdf), "sheet bakes");
    check(sheet_sdf.is_two_sided, "sheet baked as a two-sided shell");
    std::printf("  sheet shell half-thickness %.3f m\n", sheet_sdf.two_sided_thickness);
    check(sheet_sdf.two_sided_thickness > 0.5f, "the shell floor is production-fat");
    std::vector<global_sdf_instance> instances;
    instances.push_back(
        make_clipmap_instance(box_sdf, {0.0f, 0.0f, 0.0f}, math::vec3(0.9f, 0.1f, 0.1f), math::vec3(0.0f)));
    instances.push_back(
        make_clipmap_instance(sheet_sdf, {0.0f, 0.0f, 0.0f}, math::vec3(0.1f, 0.9f, 0.1f), math::vec3(0.0f)));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.resolution = 32;
    settings.base_extent = 10.0f;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(0.0f));
    const auto& lvl = clipmap.get_level(0);
    check(lvl.is_valid(), "level 0 composed");
    const uint32_t attr_res = clipmap.get_attr_resolution();
    const float attr_voxel = lvl.voxel_size * global_sdf_clipmap::attr_downsample;
    const int res = int(attr_res);
    const auto wrap = [res](int v) -> int { return ((v % res) + res) % res; };
    const math::ivec3 window_base(int(std::floor(lvl.origin.x / attr_voxel + 0.5f)),
                                  int(std::floor(lvl.origin.y / attr_voxel + 0.5f)),
                                  int(std::floor(lvl.origin.z / attr_voxel + 0.5f)));
    const math::ivec3 base_slot(wrap(window_base.x), wrap(window_base.y), wrap(window_base.z));
    // Cells whose true nearest surface is unambiguously the BOX: everything up to x = 1.6 is
    // nearer the box face (x = 1) than the cloth (x = 2.4, midpoint 1.7). The old |d| contest
    // handed these to the sheet wherever its phantom skin dipped closer.
    size_t probed = 0;
    size_t sheet_dominated = 0;
    for(uint32_t z = 0; z < attr_res; ++z)
    {
        for(uint32_t y = 0; y < attr_res; ++y)
        {
            for(uint32_t x = 0; x < attr_res; ++x)
            {
                const math::ivec3 cell = window_base + math::ivec3(wrap(int(x) - base_slot.x),
                                                                   wrap(int(y) - base_slot.y),
                                                                   wrap(int(z) - base_slot.z));
                const math::vec3 center = (math::vec3(cell) + math::vec3(0.5f)) * attr_voxel;
                if(center.x < 0.55f || center.x > 1.6f || std::fabs(center.y) > 0.6f ||
                   std::fabs(center.z) > 0.6f)
                {
                    continue;
                }
                const size_t offset =
                    size_t(x) + size_t(y) * attr_res + size_t(z) * attr_res * attr_res;
                const uint32_t packed = lvl.attr_albedo[offset];
                if((packed >> 24u) == 0u)
                {
                    continue;
                }
                ++probed;
                const uint32_t r = packed & 0xFFu;
                const uint32_t g = (packed >> 8u) & 0xFFu;
                if(g > r)
                {
                    ++sheet_dominated;
                }
            }
        }
    }
    std::printf("  probed %zu box-adjacent cells, %zu sheet-dominated\n", probed, sheet_dominated);
    check(probed > 0, "the box-adjacent region contains attributed surface cells");
    check(sheet_dominated == 0,
          "no box cell wears the sheet's albedo (got " + std::to_string(sheet_dominated) + ")");
}

// ---------------------------------------------------------------------------------------
// World-probe cage field visibility (the sealed-box silhouette leak)
// ---------------------------------------------------------------------------------------

/// The march's field read - the finest covering level, deliberately UNBLENDED (transcription
/// of GiCageVisibilitySample; the blend is for tracing continuity, and inside the cross-fade
/// band its coarse contamination floats thin walls above the conviction depth and dips
/// on-surface queries below it).
auto cage_visibility_sample(const global_sdf_clipmap& clipmap, const math::vec3& position) -> float
{
    float blend = 0.0f;
    const uint32_t level = clipmap.find_level(position, blend);
    if(level >= global_sdf_clipmap::level_count)
    {
        return global_sdf_clipmap::outside_distance;
    }
    return clipmap.sample_level(level, position);
}

/// Faithful CPU transcription of GiWorldProbeCageVisibility (gi_world_probes.sh); keep in step
/// with the shader by hand. Same contract: 1 when the field stays open along the guarded
/// segment, 0 when a closed surface separates query from probe.
auto cage_visibility(const global_sdf_clipmap& clipmap,
                     const math::vec3& from,
                     const math::vec3& to,
                     float spacing) -> float
{
    const math::vec3 delta = to - from;
    const float segment_length = math::length(delta);
    const float field_voxel = spacing / float(gi::GI_WORLD_PROBE_DIVISOR);
    const float guard = float(gi::GI_WORLD_PROBE_CAGE_VIS_GUARD_VOXELS) * field_voxel;
    float t = guard;
    const float t_end = segment_length - guard;
    if(t >= t_end)
    {
        return 1.0f;
    }
    const math::vec3 direction = delta / segment_length;
    const float half_length = 0.5f * segment_length;
    if(cage_visibility_sample(clipmap, from + direction * half_length) >= half_length)
    {
        return 1.0f;
    }
    const float accept = float(gi::GI_WORLD_PROBE_CAGE_VIS_ACCEPT_VOXELS) * field_voxel;
    const float base_step = (t_end - t) / float(gi::GI_WORLD_PROBE_CAGE_VIS_STEPS);
    for(int i = 0; i < int(gi::GI_WORLD_PROBE_CAGE_VIS_STEPS); ++i)
    {
        const float d = cage_visibility_sample(clipmap, from + direction * t);
        if(d < accept)
        {
            return 0.0f;
        }
        if(d >= t_end - t)
        {
            break;
        }
        t += std::max(d, base_step);
        if(t >= t_end)
        {
            break;
        }
    }
    return 1.0f;
}

/// The sealed-box leak regression (2026-08-12): probe cages whose members sit OUTSIDE a sealed
/// room import sky/sun through the Chebyshev test - 8x8 octahedral depth moments blur ~22-degree
/// cones, so at wall silhouettes the mean lands beyond the interior query (no test fires) and
/// the variance explodes (the test passes when it does fire). The field march is the defence:
/// this pins, over a composed six-slab sealed room at production clipmap resolution, that every
/// exterior probe is BLOCKED from every interior query (the leak), that interior probes stay
/// VISIBLE (no self-inflicted darkness), that a probe hugging the wall's room side survives the
/// arrival guard, and that open-air segments prove visible cheaply (the midpoint early-out).
void test_world_probe_cage_visibility_seals_box()
{
    std::printf("test_world_probe_cage_visibility_seals_box\n");
    // Interior [-2, 2]^3, walls 0.4 m thick (2.0 .. 2.4), corners sealed by the slabs' overlap.
    const float half = 2.0f;
    const float t = 0.2f;
    const float span = half + 2.0f * t;
    mesh_sdf horizontal;
    mesh_sdf wall_x;
    mesh_sdf wall_z;
    check(bake_slab({span, t, span}, horizontal), "horizontal slab bakes");
    check(bake_slab({t, span, span}, wall_x), "x wall bakes");
    check(bake_slab({span, span, t}, wall_z), "z wall bakes");
    const math::vec3 albedo(0.8f);
    const math::vec3 emissive(0.0f);
    std::vector<global_sdf_instance> instances;
    instances.push_back(make_clipmap_instance(horizontal, {0.0f, -half - t, 0.0f}, albedo, emissive));
    instances.push_back(make_clipmap_instance(horizontal, {0.0f, +half + t, 0.0f}, albedo, emissive));
    instances.push_back(make_clipmap_instance(wall_x, {-half - t, 0.0f, 0.0f}, albedo, emissive));
    instances.push_back(make_clipmap_instance(wall_x, {+half + t, 0.0f, 0.0f}, albedo, emissive));
    instances.push_back(make_clipmap_instance(wall_z, {0.0f, 0.0f, -half - t}, albedo, emissive));
    instances.push_back(make_clipmap_instance(wall_z, {0.0f, 0.0f, +half + t}, albedo, emissive));
    global_sdf_clipmap clipmap;
    global_sdf_clipmap::settings settings;
    settings.resolution = 128;
    settings.base_extent = 16.0f;
    settings.max_levels_per_update = global_sdf_clipmap::level_count;
    clipmap.init(settings);
    clipmap.update(instances, math::vec3(0.0f));
    // The level-0 probe spacing this clipmap implies - the spacing the shader would pass.
    const float spacing = clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    std::printf("  level-0 voxel %.3f m, probe spacing %.3f m\n",
                clipmap.get_level(0).voxel_size,
                spacing);
    // Every exterior probe is blocked from every interior query: a 3^3 grid of queries against
    // probes past each wall and on the corner diagonals - the octahedral-wedge directions the
    // depth moments could not reject.
    int blocked = 0;
    int wrongly_visible = 0;
    const float probe_distance = half + 2.0f;
    for(int qx = -1; qx <= 1; ++qx)
    {
        for(int qy = -1; qy <= 1; ++qy)
        {
            for(int qz = -1; qz <= 1; ++qz)
            {
                const math::vec3 query(float(qx) * 1.5f, float(qy) * 1.5f, float(qz) * 1.5f);
                for(int px = -1; px <= 1; ++px)
                {
                    for(int py = -1; py <= 1; ++py)
                    {
                        for(int pz = -1; pz <= 1; ++pz)
                        {
                            if(px == 0 && py == 0 && pz == 0)
                            {
                                continue;
                            }
                            const math::vec3 probe(float(px) * probe_distance,
                                                   float(py) * probe_distance,
                                                   float(pz) * probe_distance);
                            if(cage_visibility(clipmap, query, probe, spacing) > 0.0f)
                            {
                                ++wrongly_visible;
                            }
                            else
                            {
                                ++blocked;
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("  exterior segments: %d blocked, %d leaked\n", blocked, wrongly_visible);
    check(wrongly_visible == 0,
          "every exterior probe is field-blocked from every interior query (leaked " +
              std::to_string(wrongly_visible) + ")");
    // Interior probes stay visible: the defence must not buy its seal with a dark room.
    int visible = 0;
    int wrongly_blocked = 0;
    const math::vec3 interior_probes[] = {
        {1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f},
        {1.5f, -1.5f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {-1.5f, -1.5f, 1.5f},
    };
    for(int qx = -1; qx <= 1; ++qx)
    {
        for(int qy = -1; qy <= 1; ++qy)
        {
            for(int qz = -1; qz <= 1; ++qz)
            {
                const math::vec3 query(float(qx) * 1.5f, float(qy) * 1.5f, float(qz) * 1.5f);
                for(const auto& probe : interior_probes)
                {
                    if(cage_visibility(clipmap, query, probe, spacing) > 0.0f)
                    {
                        ++visible;
                    }
                    else
                    {
                        ++wrongly_blocked;
                    }
                }
            }
        }
    }
    std::printf("  interior segments: %d visible, %d wrongly blocked\n", visible, wrongly_blocked);
    check(wrongly_blocked == 0,
          "no interior probe is wrongly blocked (got " + std::to_string(wrongly_blocked) + ")");
    // The arrival guard: a probe on the ROOM side of a wall, closer to it than one cage voxel,
    // must stay readable - it is the probe carrying that wall's bounce.
    check(cage_visibility(clipmap, {0.0f, 0.0f, 0.0f}, {half - 0.1f, 0.0f, 0.0f}, spacing) > 0.0f,
          "a probe hugging the wall's room side survives the arrival guard");
    // And the guard must not excuse the wall itself: a probe just PAST the far face is blocked.
    check(cage_visibility(clipmap, {0.0f, 0.0f, 0.0f}, {half + 2.0f * t + 0.2f, 0.0f, 0.0f}, spacing) <= 0.0f,
          "a probe just past the wall's far side is blocked");
    // Open air: both endpoints outside, nothing between - the midpoint clearance proof.
    check(cage_visibility(clipmap, {5.0f, 0.0f, 0.0f}, {5.0f, 2.0f, 2.0f}, spacing) > 0.0f,
          "an open-air segment is visible");
    // Flat ground - the regression that forced the NEGATIVE acceptance: cage segments over a
    // floor run parallel to it at grazing height by construction (the biased query clears the
    // surface by ~0.4 voxel via the view-dominant bias, and four of the eight cage probes lie
    // in the floor plane itself, world lattices being what they are). Any positive acceptance
    // convicts those segments on proximity and the all-blocked contract then paints black
    // rings/donuts on open ground (measured in-editor, 2026-08-12).
    mesh_sdf ground;
    check(bake_slab({8.0f, 0.2f, 8.0f}, ground), "ground slab bakes");
    std::vector<global_sdf_instance> ground_instances;
    ground_instances.push_back(make_clipmap_instance(ground, {0.0f, -0.2f, 0.0f}, albedo, emissive));
    global_sdf_clipmap ground_clipmap;
    ground_clipmap.init(settings);
    ground_clipmap.update(ground_instances, math::vec3(0.0f));
    const float ground_spacing =
        ground_clipmap.get_level(0).voxel_size * float(gi::GI_WORLD_PROBE_DIVISOR);
    const float hug = 0.02f;
    int ground_visible = 0;
    int ground_blocked = 0;
    const math::vec3 ground_queries[] = {
        {0.3f, hug, -0.7f},
        {1.1f, hug, 1.3f},
        {-2.6f, hug, 0.4f},
    };
    for(const auto& query : ground_queries)
    {
        const math::vec3 cage_probes[] = {
            {query.x + ground_spacing, 0.0f, query.z},
            {query.x - ground_spacing, 0.0f, query.z},
            {query.x, 0.0f, query.z + ground_spacing},
            {query.x + ground_spacing, 0.0f, query.z + ground_spacing},
            {query.x, ground_spacing, query.z},
            {query.x + ground_spacing, ground_spacing, query.z},
        };
        for(const auto& probe : cage_probes)
        {
            if(cage_visibility(ground_clipmap, query, probe, ground_spacing) > 0.0f)
            {
                ++ground_visible;
            }
            else
            {
                ++ground_blocked;
            }
        }
    }
    std::printf("  flat-ground segments: %d visible, %d wrongly blocked\n",
                ground_visible,
                ground_blocked);
    check(ground_blocked == 0,
          "no flat-ground cage segment is wrongly blocked (got " + std::to_string(ground_blocked) +
              ")");
    // The floor still occludes DOWNWARD: a probe beneath it must not complete rays up through.
    check(cage_visibility(ground_clipmap, {0.3f, hug, -0.7f}, {0.3f, -2.0f, -0.7f}, ground_spacing) <=
              0.0f,
          "a probe beneath the floor is blocked");
}

} // namespace

void run(int& checks, int& failures)
{
    g_checks = &checks;
    g_failures = &failures;
    test_shader_constants_match_cpp();
    test_gi_shaders_compile_sm50();
    test_attribute_voxels_mark_the_surface_band();
    test_clipmap_attribute_transcription_matches_cpu();
    test_reference_is_deterministic();
    test_reference_furnace_conserves_energy();
    test_reference_sealed_room_is_black();
    test_reference_thin_wall_blocks_light();
    test_reference_cornell_bleeds_colour();
    test_shadow_blob_floor_building();
    test_shadow_through_colonnade();
    test_shadow_through_coarse_baked_colonnade();
    test_attribution_prefers_true_surface_over_shell();
    test_world_probe_cage_visibility_seals_box();
}

} // namespace unravel::gi_tests
