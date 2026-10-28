/*
 * Validation suite for scripted instance draws (Graphics.DrawMeshInstanced -> instanced_draw_list).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "instanced draws"
 *
 * These pin:
 *   - instances are grouped into one cluster per draw and 8 m ground tile, each counting its instances;
 *   - a cluster's bounds hold its instances' meshes, scale included;
 *   - matrices handed over unaligned (a script's array) are copied exactly;
 *   - every instance reaches the batch collector once, and clear() empties the frame.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/instanced_draw_list.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <graphics/vertex_decl.h>

#include <cmath>
#include <cstdio>
#include <cstring>
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

constexpr const char* MESH_KEY = "engine:/embedded/instanced_draws_test/cube";
constexpr const char* MATERIAL_KEY = "engine:/embedded/instanced_draws_test/material";

auto make_settings() -> instanced_draw_list::draw_settings
{
    auto& am = engine::context().get_cached<asset_manager>();
    am.unload_asset<mesh>(MESH_KEY);
    am.unload_asset<material>(MATERIAL_KEY);
    auto cube = std::make_shared<mesh>();
    cube->create_cube(gfx::mesh_vertex::get_layout(), 1.0f, 1.0f, 1.0f, 1, 1, 1, mesh_create_origin::center, false);
    instanced_draw_list::draw_settings settings;
    settings.render_mesh = am.get_asset_from_instance<mesh>(MESH_KEY, cube);
    settings.render_material = am.get_asset_from_instance<material>(MATERIAL_KEY, std::make_shared<pbr_material>());
    settings.layer_mask = 1 << 3;
    return settings;
}

auto make_transform(float x, float z, float scale) -> math::mat4
{
    return math::translate(math::mat4(1.0f), math::vec3(x, 0.5f, z)) * math::scale(math::mat4(1.0f), math::vec3(scale));
}

void test_clusters_by_tile(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const std::vector<math::mat4> transforms{make_transform(1.0f, 1.0f, 1.0f),
                                             make_transform(3.0f, 2.0f, 1.0f),
                                             make_transform(5.0f, 7.0f, 4.0f),
                                             make_transform(9.0f, 1.0f, 1.0f),
                                             make_transform(12.0f, 3.0f, 1.0f)};
    list.add(settings, transforms, {});
    const auto& clusters = list.get_clusters();
    check(clusters.size() == 2u, "five instances on two tiles make two clusters");
    const float cube_reach = std::sqrt(0.75f) * 4.0f;
    bool holds_scaled = false;
    for(const auto& cluster : clusters)
    {
        const auto& bounds = cluster.get_mesh_bounds();
        holds_scaled = holds_scaled || bounds.contains_point(math::vec3(5.0f + cube_reach * 0.99f, 0.5f, 7.0f));
        check(cluster.get_layer_mask() == settings.layer_mask && cluster.casts_shadow(), "a cluster keeps its draw's settings");
    }
    check(holds_scaled, "a cluster's bounds reach as far as its largest instance");
    batch_collector collector;
    for(const auto& cluster : clusters)
    {
        cluster.collect_mesh_instances(collector, false);
    }
    check(collector.get_instance_count() == transforms.size(), "every instance reaches the batches once");
    list.clear();
    check(list.get_clusters().empty(), "clear empties the frame");
}

void test_unaligned_copy(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const math::mat4 first = make_transform(20.0f, 20.0f, 1.0f);
    const math::mat4 second = make_transform(40.0f, 20.0f, 1.0f);
    // A float before the matrices leaves them misaligned for math::mat4, as a script's array can be.
    std::vector<float> buffer(1 + 32, 0.0f);
    std::memcpy(buffer.data() + 1, &first, sizeof(first));
    std::memcpy(buffer.data() + 17, &second, sizeof(second));
    list.add_unaligned(settings, buffer.data() + 1, 2, nullptr);
    const auto& clusters = list.get_clusters();
    check(clusters.size() == 2u, "two unaligned instances on two tiles make two clusters");
    bool found_first = false;
    bool found_second = false;
    for(const auto& cluster : clusters)
    {
        found_first = found_first || cluster.get_mesh_bounds().contains_point(math::vec3(20.0f, 0.5f, 20.0f));
        found_second = found_second || cluster.get_mesh_bounds().contains_point(math::vec3(40.0f, 0.5f, 20.0f));
    }
    check(found_first && found_second, "unaligned matrices are read exactly");
}

} // namespace

auto run_instanced_draws_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto settings = make_settings();
    test_clusters_by_tile(settings);
    test_unaligned_copy(settings);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("instanced draws", run_instanced_draws_suite)
