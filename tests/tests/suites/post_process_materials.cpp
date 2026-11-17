/*
 * Validation suite for post-process materials: the entries volumes and cameras hold, and how the resolver blends and
 * orders them.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "post process materials"
 *
 * What it pins:
 *   - Post Process Materials: add gives a material one entry (a second add returns it), find and remove go by
 *     material;
 *   - the resolver's blend: a later entry of the same material moves the draw's weight and parameters toward its own
 *     by its contribution (a volume fading in), and takes the draw's material over from a half on;
 *   - the order: draws of no weight are dropped, the rest run by Location, then Priority, then the order they came in;
 *   - a scene round trip keeps each entry's material, weight, enabled and parameter overrides;
 *   - find_global_volume takes the active global volume of the highest priority.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/pipeline/volume_resolver.h>

#include <cstdio>
#include <sstream>
#include <string>

using namespace unravel::test;

using namespace unravel;

namespace
{

constexpr float POST_TEST_TOLERANCE = 0.0001f;

auto post_test_is_near(const math::vec4& a, const math::vec4& b) -> bool
{
    return math::all(math::lessThanEqual(math::abs(a - b), math::vec4(POST_TEST_TOLERANCE)));
}

/// A Post Process material with a Strength parameter, registered under @p name.
auto make_post_material(const std::string& name) -> asset_handle<material>
{
    material_graph graph;
    material_graph_node output;
    output.id = "output";
    output.type = MATERIAL_GRAPH_POST_PROCESS_OUTPUT_TYPE;
    graph.nodes.push_back(output);
    material_graph_node strength;
    strength.id = "strength";
    strength.type = "scalar_parameter";
    strength.settings.push_back({"name", "Strength"});
    graph.nodes.push_back(strength);
    graph.nodes.front().inputs.push_back({MATERIAL_OPACITY_PIN, "strength", {}});
    auto mat = std::make_shared<graph_material>();
    mat->set_graph(graph);
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/post_process_material_test/" + name;
    am.unload_asset<material>(key);
    return am.get_asset_from_instance<material>(key, mat);
}

void test_entries()
{
    std::printf("test_entries\n");
    const auto grade = make_post_material("grade");
    const auto outline = make_post_material("outline");
    post_process_material_component materials;
    auto& first = materials.add(grade, 0.5f);
    check(materials.entries.size() == 1 && first.weight == 0.5f && first.enabled, "add makes an enabled entry of the weight");
    materials.add(grade, 0.9f).enabled = false;
    check(materials.entries.size() == 1 && !materials.entries.front().enabled, "a second add returns the material's entry");
    materials.add(outline);
    check(materials.find(outline) == &materials.entries.back(), "find goes by material");
    check(materials.remove(grade) && materials.entries.size() == 1 && materials.find(grade) == nullptr, "remove takes the entry off");
    check(!materials.remove(grade), "removing a material without an entry changes nothing");
    check(materials.entries.front().get_material_instance().get_shared_material() == outline,
          "an entry's overrides are of its material");
}

void test_blend()
{
    std::printf("test_blend\n");
    post_process_material_draw draw;
    draw.registers = {math::vec4(0.0f), math::vec4(1.0f)};
    draw.weight = 1.0f;
    const auto later = std::make_shared<graph_material>();
    blend_post_process_material_draw(draw, later, {math::vec4(1.0f), math::vec4(3.0f)}, 0.0f, 0.25f);
    check(post_test_is_near(draw.registers[0], math::vec4(0.25f)) && post_test_is_near(draw.registers[1], math::vec4(1.5f)),
          "a quarter contribution moves the parameters a quarter of the way");
    check_near(draw.weight, 0.75f, POST_TEST_TOLERANCE, "and the weight with them");
    check(draw.material != later, "below a half the draw keeps its material");
    blend_post_process_material_draw(draw, later, {math::vec4(1.0f), math::vec4(3.0f)}, 0.0f, 1.0f);
    check(post_test_is_near(draw.registers[0], math::vec4(1.0f)) && draw.weight == 0.0f && draw.material == later,
          "a whole contribution replaces the draw: the inner volume wins");
    post_process_material_draw short_draw;
    short_draw.registers = {math::vec4(2.0f)};
    blend_post_process_material_draw(short_draw, nullptr, {math::vec4(0.0f), math::vec4(5.0f)}, 1.0f, 0.5f);
    check(short_draw.registers.size() == 1 && post_test_is_near(short_draw.registers[0], math::vec4(1.0f)),
          "registers past the draw's are left out");
}

void test_order()
{
    std::printf("test_order\n");
    const auto make_draw = [](material_post_process_location location, int priority, uint32_t order, float weight)
    {
        post_process_material_draw draw;
        draw.location = location;
        draw.priority = priority;
        draw.order = order;
        draw.weight = weight;
        return draw;
    };
    std::vector<post_process_material_draw> draws = {
        make_draw(material_post_process_location::after_tonemapping, 0, 0, 1.0f),
        make_draw(material_post_process_location::before_transparent, 5, 1, 1.0f),
        make_draw(material_post_process_location::before_transparent, 0, 2, 0.0f),
        make_draw(material_post_process_location::before_transparent, 0, 3, 0.5f),
        make_draw(material_post_process_location::before_transparent, 0, 4, 0.5f),
    };
    finish_post_process_material_draws(draws);
    check(draws.size() == 4, "a draw of no weight is dropped");
    check(draws.size() == 4 && draws[0].order == 3 && draws[1].order == 4 && draws[2].order == 1 && draws[3].order == 0,
          "draws run by Location, then Priority, then the order they came in");
}

void test_round_trip()
{
    std::printf("test_round_trip\n");
    const auto grade = make_post_material("round_trip");
    scene src("src");
    auto volume = src.create_entity("volume");
    auto& materials = volume.emplace<post_process_material_component>();
    auto& entry = materials.add(grade, 0.3f);
    entry.enabled = false;
    auto overrides = entry.get_material_instance();
    overrides.set_graph_parameter("Strength", math::vec4(0.25f, 0.0f, 0.0f, 0.0f));
    entry.set_material_instance(overrides);

    std::stringstream stream;
    save_to_stream(stream, src);
    scene dst("dst");
    load_from_stream(stream, dst);
    const post_process_material_component* loaded = nullptr;
    dst.registry->view<post_process_material_component>().each(
        [&](auto, post_process_material_component& comp)
        {
            loaded = &comp;
        });
    require(loaded != nullptr && loaded->entries.size() == 1, "the entry survives the round trip");
    const auto& loaded_entry = loaded->entries.front();
    check(loaded_entry.get_material() == grade, "it keeps its material");
    check_near(loaded_entry.weight, 0.3f, POST_TEST_TOLERANCE, "it keeps its weight");
    check(!loaded_entry.enabled, "it keeps Enabled");
    const auto strength = loaded_entry.get_material_instance().get_graph_parameter("Strength");
    check(strength && post_test_is_near(*strength, math::vec4(0.25f, 0.0f, 0.0f, 0.0f)), "it keeps its parameter overrides");
}

void test_find_global_volume()
{
    std::printf("test_find_global_volume\n");
    scene scn("global volumes");
    check(!defaults::find_global_volume(scn), "a scene without volumes has no global volume");
    const auto add_volume = [&](volume_mode mode, int priority, bool is_active)
    {
        auto entity = scn.create_entity("volume");
        auto& volume = entity.emplace<volume_component>();
        volume.mode = mode;
        volume.priority = priority;
        entity.get<transform_component>().set_active(is_active);
        return entity;
    };
    add_volume(volume_mode::local, 9, true);
    check(!defaults::find_global_volume(scn), "a local volume is no global one");
    const auto low = add_volume(volume_mode::global, 1, true);
    const auto high = add_volume(volume_mode::global, 2, true);
    add_volume(volume_mode::global, 5, false);
    check(defaults::find_global_volume(scn) == high, "the active global volume of the highest priority, an inactive one passed over");
    high.get<transform_component>().set_active(false);
    check(defaults::find_global_volume(scn) == low, "with it inactive, the next active one");
}

void run_post_process_materials_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_entries());
    RUN_CASE(test_blend());
    RUN_CASE(test_order());
    RUN_CASE(test_round_trip());
    RUN_CASE(test_find_global_volume());
}

} // namespace

REGISTER_TEST_SUITE("post process materials", run_post_process_materials_suite)
