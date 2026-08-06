/*
 * GI v2 Phase 0: the validation harness itself (plan: tasks/gi_rewrite_plan.md, sections 9-10).
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

#include "gi_v2_tests.h"
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

namespace unravel::gi_v2_tests
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
        const fs::path out_bin = out_dir / (name + ".bin");
        const fs::path out_log = out_dir / (name + ".log");
        std::string command = "\"\"" + shaderc.string() + "\" -f \"" + entry.path().string() +
                              "\" -o \"" + out_bin.string() + "\" -i \"" + include_dir.string() +
                              "\" --varyingdef \"" + varying.string() +
                              "\" --type " + (compute ? "compute" : "fragment") +
                              " --define BGFX_CONFIG_MAX_BONES=64 --platform windows -p s_5_0 > \"" +
                              out_log.string() + "\" 2>&1\"";
        const int exit_code = std::system(command.c_str());
        std::string log;
        {
            std::ifstream log_file(out_log);
            std::stringstream buffer;
            buffer << log_file.rdbuf();
            log = buffer.str();
        }
        // shaderc is quiet on success; any output or a non-zero exit is a failure worth the
        // full log, because the editor would have swallowed it.
        const bool ok = exit_code == 0 && log.find("Error") == std::string::npos &&
                        log.find("error") == std::string::npos;
        if(!ok)
        {
            std::printf("--- %s ---\n%.2000s\n", name.c_str(), log.c_str());
        }
        check(ok, name + " compiles for SM 5.0");
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

/// Places a baked field into a clipmap-instance slot at a translation, with GI v2 materials.
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
                        // The shader's cell-range argmin walk, transcribed.
                        float best_magnitude = attr_reach;
                        int best_index = -1;
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
                                        const auto& inst = instances[size_t(index)];
                                        const math::vec3 clamped = math::clamp(center,
                                                                               inst.world_bounds.min,
                                                                               inst.world_bounds.max);
                                        if(math::length(center - clamped) >= best_magnitude)
                                        {
                                            continue;
                                        }
                                        const math::vec4 local =
                                            inst.world_to_local * math::vec4(center, 1.0f);
                                        const float magnitude =
                                            std::fabs(sample_mesh_sdf(*inst.sdf, math::vec3(local)) *
                                                      inst.local_to_world_scale);
                                        if(magnitude < best_magnitude ||
                                           (magnitude == best_magnitude &&
                                            (best_index < 0 || index < best_index)))
                                        {
                                            best_magnitude = magnitude;
                                            best_index = index;
                                        }
                                    }
                                }
                            }
                        }
                        if(best_index >= 0)
                        {
                            const auto& winner = instances[size_t(best_index)];
                            const auto quantize = [](float v) -> uint32_t
                            { return uint32_t(math::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                            expected_albedo = quantize(winner.albedo.x) |
                                              (quantize(winner.albedo.y) << 8u) |
                                              (quantize(winner.albedo.z) << 16u) | (255u << 24u);
                            expected_emissive = winner.emissive;
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
}

} // namespace unravel::gi_v2_tests
