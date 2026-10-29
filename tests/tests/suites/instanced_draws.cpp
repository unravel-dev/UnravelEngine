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
 *   - clusters are placed in tile order; the runs of neighbouring tiles merge into one draw, tiles apart stay apart;
 *   - every instance is packed once with its matrix; only draws with last frame's matrices add velocity runs, and
 *     only they are packed in the velocity layout;
 *   - instances at a non-finite position make a cluster of their own and leave the other clusters' bounds finite;
 *   - end_frame() keeps the draws for the next frame;
 *   - a draw with a material instance draws its resolved material in every slot, next to a plain draw of the same
 *     model; an instance without overrides draws the shared material;
 *   - clear() empties the frame.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/instanced_draw_list.h>
#include <engine/rendering/material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/mesh.h>
#include <graphics/vertex_decl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
    instanced_run_list runs;
    for(const auto& cluster : clusters)
    {
        cluster.collect_mesh_runs(runs, instanced_layout::base);
    }
    runs.merge();
    check(runs.get_runs().size() == 1u && runs.get_runs().front().count == transforms.size(),
          "the runs of neighbouring tiles merge into one draw of every instance");
    const uint32_t count = list.get_packed_count(instanced_layout::base, false);
    std::vector<uint8_t> packed(size_t(count) * instanced_draw_list::get_packed_stride(instanced_layout::base, false));
    list.write_packed(instanced_layout::base, false, packed.data(), count);
    const auto* data = reinterpret_cast<const instance_vertex_data*>(packed.data());
    bool is_packed_right = count == transforms.size();
    for(uint32_t index = 0; is_packed_right && index < count; ++index)
    {
        math::mat4 world = data[index].world_matrix;
        world[3][3] = 1.0f;
        const bool is_in_order = (index < 3u) == (world[3].x < 8.0f);
        const bool is_known = std::find(transforms.begin(), transforms.end(), world) != transforms.end();
        is_packed_right = is_in_order && is_known;
    }
    check(is_packed_right, "the packed instances keep their matrices, tile after tile");
    instanced_run_list velocity_runs;
    for(const auto& cluster : clusters)
    {
        cluster.collect_mesh_runs(velocity_runs, instanced_layout::velocity);
    }
    check(velocity_runs.empty(), "instances without last frame's matrices add no velocity runs");
    list.clear();
    check(list.get_clusters().empty(), "clear empties the frame");
}

void test_tile_order(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const std::vector<math::mat4> transforms{make_transform(20.0f, 1.0f, 1.0f),
                                             make_transform(-4.0f, 1.0f, 1.0f),
                                             make_transform(4.0f, 1.0f, 1.0f)};
    list.add(settings, transforms, transforms);
    const auto& clusters = list.get_clusters();
    const auto holds = [&](size_t cluster, float x)
    {
        return cluster < clusters.size() &&
               clusters[cluster].get_mesh_bounds().contains_point(math::vec3(x, 0.5f, 1.0f));
    };
    check(clusters.size() == 3u && holds(0, -4.0f) && holds(1, 4.0f) && holds(2, 20.0f),
          "clusters are placed in tile order, negative tiles first");
    if(clusters.size() != 3u)
    {
        return;
    }
    instanced_run_list runs;
    clusters[0].collect_mesh_runs(runs, instanced_layout::velocity);
    clusters[2].collect_mesh_runs(runs, instanced_layout::velocity);
    runs.merge();
    check(runs.get_runs().size() == 2u, "the runs of tiles apart stay two draws");
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

void test_velocity_packs_movers(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const std::vector<math::mat4> still{make_transform(1.0f, 1.0f, 1.0f),
                                        make_transform(2.0f, 1.0f, 1.0f),
                                        make_transform(3.0f, 1.0f, 1.0f)};
    const std::vector<math::mat4> moving{make_transform(1.0f, 2.0f, 1.0f), make_transform(2.0f, 2.0f, 1.0f)};
    list.add(settings, still, {});
    list.add(settings, moving, moving);
    list.get_clusters();
    check(list.get_packed_count(instanced_layout::base, false) == 5u, "the base layout packs every instance");
    check(list.get_packed_count(instanced_layout::velocity, false) == 2u,
          "the velocity layout packs only the instances with last frame's matrices");
    instanced_run_list runs;
    for(const auto& cluster : list.get_clusters())
    {
        cluster.collect_mesh_runs(runs, instanced_layout::velocity);
    }
    runs.merge();
    check(runs.get_runs().size() == 1u && runs.get_runs().front().first == 0u &&
              runs.get_runs().front().count == moving.size(),
          "the moving draw's velocity run starts the velocity layout");
}

void test_non_finite_positions(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const float not_a_number = std::numeric_limits<float>::quiet_NaN();
    const std::vector<math::mat4> transforms{make_transform(not_a_number, 1.0f, 1.0f),
                                             make_transform(1.0f, 1.0f, 1.0f),
                                             make_transform(1e30f, -1e30f, 1.0f)};
    list.add(settings, transforms, {});
    const auto& clusters = list.get_clusters();
    check(clusters.size() == 3u, "a non-finite and a far position make clusters of their own");
    bool holds_valid = false;
    for(const auto& cluster : clusters)
    {
        const auto& bounds = cluster.get_mesh_bounds();
        holds_valid = holds_valid || (bounds.contains_point(math::vec3(1.0f, 0.5f, 1.0f)) &&
                                      std::isfinite(bounds.min.x) && std::isfinite(bounds.max.x));
    }
    check(holds_valid, "the finite instance's cluster keeps finite bounds");
}

void test_end_frame_keeps_draws(const instanced_draw_list::draw_settings& settings)
{
    instanced_draw_list list;
    const std::vector<math::mat4> transforms{make_transform(1.0f, 1.0f, 1.0f)};
    list.add(settings, transforms, {});
    const size_t cluster_count = list.get_clusters().size();
    list.end_frame();
    check(cluster_count == 1u && list.get_clusters().size() == cluster_count &&
              list.get_packed_count(instanced_layout::base, false) == 1u,
          "end_frame keeps the draws for the next frame");
    list.clear();
    check(list.get_clusters().empty(), "clear drops the kept draws");
}

/// A material instance of no renderer over @p settings' material with its base color overridden by @p base_color.
auto make_material_instance(const instanced_draw_list::draw_settings& settings, const math::color& base_color)
    -> std::shared_ptr<material_instance>
{
    auto instance = std::make_shared<material_instance>();
    instance->set_shared_material(settings.render_material);
    instance->set_override(entt::hashed_string{"base_color"}, entt::meta_any{base_color});
    return instance;
}

/// The materials the G-buffer runs of @p list draw with.
auto collect_run_materials(instanced_draw_list& list) -> std::vector<const material*>
{
    instanced_run_list runs;
    for(const auto& cluster : list.get_clusters())
    {
        cluster.collect_mesh_runs(runs, instanced_layout::base);
    }
    runs.merge();
    std::vector<const material*> materials;
    for(const auto& run : runs.get_runs())
    {
        materials.push_back(run.part->material_ptr.get());
    }
    return materials;
}

void test_material_instance(const instanced_draw_list::draw_settings& settings)
{
    const math::color base_color(1.0f, 0.0f, 0.0f, 1.0f);
    const auto instance = make_material_instance(settings, base_color);
    auto instance_settings = settings;
    instance_settings.render_material_instance = instance;
    const std::vector<math::mat4> transforms{make_transform(1.0f, 1.0f, 1.0f)};
    const std::vector<math::mat4> plain_transforms{make_transform(5.0f, 1.0f, 1.0f)};
    instanced_draw_list list;
    list.add(instance_settings, transforms, {});
    const auto materials = collect_run_materials(list);
    const material* resolved = instance->get_resolved().get();
    check(resolved != nullptr && materials.size() == 1u && materials.front() == resolved,
          "a draw with a material instance draws its resolved material");
    check(resolved != nullptr && resolved->is<pbr_material>() &&
              static_cast<const pbr_material*>(resolved)->get_base_color() == base_color,
          "the resolved material has the instance's base color");
    list.clear();
    list.add(settings, plain_transforms, {});
    list.add(instance_settings, transforms, {});
    const auto both = collect_run_materials(list);
    const material* shared = settings.render_material.get().get();
    check(both.size() == 2u && std::count(both.begin(), both.end(), shared) == 1 &&
              std::count(both.begin(), both.end(), resolved) == 1,
          "a plain draw and a draw with a material instance of one model draw the shared and the resolved material");
    list.clear();
    instance->revert_all();
    list.add(instance_settings, transforms, {});
    const auto reverted = collect_run_materials(list);
    check(reverted.size() == 1u && reverted.front() == shared,
          "a material instance without overrides draws the shared material");
}

} // namespace

auto run_instanced_draws_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto settings = make_settings();
    test_clusters_by_tile(settings);
    test_unaligned_copy(settings);
    test_tile_order(settings);
    test_velocity_packs_movers(settings);
    test_non_finite_positions(settings);
    test_end_frame_keeps_draws(settings);
    test_material_instance(settings);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("instanced draws", run_instanced_draws_suite)
