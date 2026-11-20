/*
 * Validation suite for the translucency pass: which surfaces it draws, the order it draws them in, and what a model
 * keeps of it.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "translucency"
 *
 * What it pins:
 *   - the passes: the G-buffer draws Opaque and Dithered surfaces, the translucency pass the other Blend Modes, and a
 *     G-buffer submit reports a model whose surfaces it left out; a pass of every material draws them all;
 *   - the order: a lower Sort Priority draws first, then the farther, as particle batches sort;
 *   - a scene round trip keeps a model's Sort Priority, and a scene without it reads 0.
 */

#include "../tests.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/model.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/pipeline/passes/gi_radiance_cache.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;
using namespace unravel::test;

namespace
{

/// A graph material of @p mode.
auto make_translucency_material(material_blend_mode mode) -> graph_material
{
    material_graph graph;
    ensure_material_graph_output(graph);
    set_material_graph_blend_mode(graph, mode);
    graph_material mat;
    mat.set_graph(graph);
    return mat;
}

void test_material_passes()
{
    std::printf("test_material_passes\n");
    for(uint32_t i = 0; i < MATERIAL_BLEND_MODE_COUNT; ++i)
    {
        const auto mode = material_blend_mode(i);
        const auto mat = make_translucency_material(mode);
        const bool is_forward = is_material_blend_mode_forward(mode);
        bool found_forward = false;
        check(is_material_drawn_in(mat, model_material_pass::deferred, &found_forward) == !is_forward &&
                  found_forward == is_forward,
              std::string("the G-buffer draws ") + get_material_blend_mode_label(mode) +
                  " surfaces exactly when the translucency pass does not, and reports those it leaves out");
        check(is_material_drawn_in(mat, model_material_pass::forward, nullptr) == is_forward,
              std::string("the translucency pass draws ") + get_material_blend_mode_label(mode) + " surfaces as its Blend Mode says");
        check(is_material_drawn_in(mat, model_material_pass::all, nullptr), "a pass of every material draws it");
        check(mat.is_forward() == is_forward && mat.casts_shadow() == (mode == material_blend_mode::opaque),
              "only an Opaque surface casts a shadow");
    }
}

void test_draw_order()
{
    std::printf("test_draw_order\n");
    using key = rendering::translucent_mesh_list::key;
    check(rendering::is_drawn_before(key{0, 9.0f}, key{0, 4.0f}) && !rendering::is_drawn_before(key{0, 4.0f}, key{0, 9.0f}),
          "of one Sort Priority, the farther draws first");
    check(rendering::is_drawn_before(key{-1, 1.0f}, key{0, 100.0f}) && !rendering::is_drawn_before(key{1, 100.0f}, key{0, 1.0f}),
          "a lower Sort Priority draws first, however near");
    check(!rendering::is_drawn_before(key{0, 4.0f}, key{0, 4.0f}), "equal draws keep their order");
    std::vector<key> keys = {{0, 1.0f}, {2, 50.0f}, {0, 30.0f}, {-3, 2.0f}, {0, 10.0f}};
    std::stable_sort(keys.begin(), keys.end(), rendering::is_drawn_before);
    const bool is_sorted = keys[0].sort_priority == -3 && keys[1].distance == 30.0f && keys[2].distance == 10.0f &&
                           keys[3].distance == 1.0f && keys[4].sort_priority == 2;
    check(is_sorted, "a list sorts by Sort Priority, then far to near");

    rendering::translucent_mesh_list list;
    check(list.is_empty(), "a list without draws is empty");
    list.has_order_independent = true;
    check(!list.is_empty(), "Order Independent surfaces alone are something to draw");
}

void test_gi_surface_points()
{
    std::printf("test_gi_surface_points\n");
    const math::vec3 eye(0.0f);
    const float cell = gi_radiance_cache::get_cell_size(0);
    std::vector<math::vec4> points;
    // A small model near the camera: one point in the middle.
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(4.0f, -0.4f, -0.4f), math::vec3(4.8f, 0.4f, 0.4f)), eye, 64, 1.0f, points) &&
              points.size() == 1 && math::distance(math::vec3(points[0]), math::vec3(4.4f, 0.0f, 0.0f)) < 1e-4f,
          "a model under a cell wide marks around its middle");
    // A pane a cell apart along its length: neighbours mark every probe between them.
    points.clear();
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(2.0f, 0.0f, -5.0f), math::vec3(2.1f, 2.0f, 5.0f)), eye, 1024, 1.0f, points),
          "a pane fits its budget");
    float widest_gap = 0.0f;
    for(size_t i = 1; i < points.size(); ++i)
    {
        if(points[i].y == points[i - 1].y)
        {
            widest_gap = std::max(widest_gap, std::abs(points[i].z - points[i - 1].z));
        }
    }
    check(!points.empty() && widest_gap <= cell + 1e-4f, "its points along the pane are at most a cell apart");
    // A large box keeps its points near its faces.
    points.clear();
    const math::bbox room(math::vec3(-6.0f), math::vec3(6.0f));
    check(gi_radiance_cache::make_surface_points(room, eye, 100000, 1.0f, points), "a room-sized box fits a large budget");
    const bool all_near_faces = std::all_of(points.begin(), points.end(), [&](const math::vec4& point)
    {
        const math::vec3 to_faces = math::min(math::vec3(point) - room.min, room.max - math::vec3(point));
        return std::min(to_faces.x, std::min(to_faces.y, to_faces.z)) <= cell + 1e-4f;
    });
    check(all_near_faces, "a box several cells wide marks near its faces, not its empty inside");
    // A model reaching past the finest clipmap: its far part is a coarser clipmap's cell apart, and no point there sits
    // in the finer clipmap's reach.
    points.clear();
    const float reach0 = gi_radiance_cache::get_reach(0);
    check(gi_radiance_cache::make_surface_points(math::bbox(math::vec3(0.0f, 0.0f, 1.0f), math::vec3(0.1f, 0.1f, reach0 * 3.0f)), eye, 100000, 1.0f, points),
          "a long model fits a large budget");
    size_t near_points = 0;
    size_t far_points = 0;
    for(const auto& point : points)
    {
        (point.z <= reach0 ? near_points : far_points)++;
    }
    check(near_points > 0 && far_points > 0 && far_points < near_points * 2 + 2,
          "past the finest clipmap the points spread to the coarser cells");
    // Twice the spacing for a model too large for its share: fewer points over the same box.
    std::vector<math::vec4> coarse;
    const math::bbox pool(math::vec3(-8.0f, 0.0f, -8.0f), math::vec3(8.0f, 0.2f, 8.0f));
    points.clear();
    check(gi_radiance_cache::make_surface_points(pool, eye, 100000, 1.0f, points) &&
              gi_radiance_cache::make_surface_points(pool, eye, 100000, 2.0f, coarse) && coarse.size() * 3 < points.size() && !coarse.empty(),
          "twice the spacing lays a quarter of the points over a flat model");
    // Past its budget a model marks nothing: it is left out whole.
    points.clear();
    check(!gi_radiance_cache::make_surface_points(room, eye, 8, 1.0f, points), "a model past its budget is refused");
}

void test_sort_priority_round_trip()
{
    std::printf("test_sort_priority_round_trip\n");
    scene src("src");
    auto entity = src.create_entity("glass");
    entity.emplace<model_component>().set_sort_priority(-5);
    std::stringstream stream;
    save_to_stream(stream, src);
    scene dst("dst");
    load_from_stream(stream, dst);
    int loaded = 1;
    dst.registry->view<model_component>().each(
        [&](auto, model_component& comp)
        {
            loaded = comp.get_sort_priority();
        });
    check(loaded == -5, "a model keeps its Sort Priority");
    check(model_component().get_sort_priority() == 0, "a model starts at Sort Priority 0");
}

void run_translucency_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_material_passes());
    RUN_CASE(test_draw_order());
    RUN_CASE(test_sort_priority_round_trip());
    RUN_CASE(test_gi_surface_points());
}

} // namespace

REGISTER_TEST_SUITE("translucency", run_translucency_suite)
