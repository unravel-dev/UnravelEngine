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
}

} // namespace

REGISTER_TEST_SUITE("translucency", run_translucency_suite)
