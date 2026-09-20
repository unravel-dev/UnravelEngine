/*
 * Validation suite for the CPU side of render culling and batching.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "render culling"
 *
 * Pins the 2026-09 shadow culling work:
 *   - light::compute_world_bounds_sphere answers in WORLD space and sits on the cone axis (the
 *     old bounds were offset along the world direction and then rotated by the light transform
 *     a second time, which moved a rotated spot light's culling volume off its cone);
 *   - spot angles are FULL cone angles, so the bounding sphere is built from half of them;
 *   - compute_shadow_view_mask keeps the nested-cascade rule of the loops it replaced (a caster
 *     fully inside a cascade is not drawn into the farther ones), at model AND submesh
 *     granularity - checked case by case and against a verbatim copy of the old loop over
 *     thousands of random boxes;
 *   - batch_collector_t recycles its group slots: same grouping and counts as the node-based
 *     map it replaced, no stale instances or asset references after clear(), and the instance
 *     storage of a recycled slot is reused instead of reallocated.
 *
 * Headless safe: the collector only hashes and compares the pointers inside a key, so the
 * keys here carry addresses that are never dereferenced.
 */

#include "../tests.h"

#include <engine/rendering/batch_collector.h>
#include <engine/rendering/light.h>
#include <engine/rendering/model.h>
#include <engine/rendering/shadow.h>

#include <math/math.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr float BOUNDS_TOLERANCE = 1.0e-4f;
/// First fake mesh address; spaced like real heap blocks so the low bits carry no entropy.
constexpr std::uintptr_t FAKE_MESH_BASE_ADDRESS = 0x10000;
constexpr std::uintptr_t FAKE_MESH_ADDRESS_STRIDE = 64;
/// Far more keys than the collector's initial index, so it has to grow several times.
constexpr uint32_t MANY_KEYS = 5000;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(!(std::fabs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.6f, expected %.6f +/- %.6f)\n", what.c_str(), actual, expected, tolerance);
    }
}

// ---------------------------------------------------------------------------------
// Light bounds
// ---------------------------------------------------------------------------------

auto make_spot_light(float range, float inner_angle, float outer_angle) -> light
{
    light result;
    result.type = light_type::spot;
    result.spot_data.set_range(range);
    result.spot_data.set_outer_angle(outer_angle);
    result.spot_data.set_inner_angle(inner_angle);
    return result;
}

void test_point_light_bounds()
{
    light input_light;
    input_light.type = light_type::point;
    input_light.point_data.range = 7.5f;
    const math::vec3 input_position(4.0f, -2.0f, 9.0f);
    const auto actual_sphere = input_light.compute_world_bounds_sphere(input_position, math::vec3(0.0f, 0.0f, 1.0f));
    check_near(math::length(actual_sphere.position - input_position), 0.0f, BOUNDS_TOLERANCE, "point light sphere is centred on the light");
    check_near(actual_sphere.radius, 7.5f, BOUNDS_TOLERANCE, "point light sphere radius is the range");
}

void test_spot_light_bounds_follow_world_direction()
{
    const float input_range = 8.0f;
    const float input_outer_angle = 60.0f;
    const auto input_light = make_spot_light(input_range, 20.0f, input_outer_angle);
    const math::vec3 input_position(3.0f, 10.0f, -2.0f);
    // Straight down: the orientation the double rotation pushed sideways.
    const math::vec3 input_direction(0.0f, -1.0f, 0.0f);
    const auto actual_sphere = input_light.compute_world_bounds_sphere(input_position, input_direction);
    const math::vec3 expected_center = input_position + 0.5f * input_range * input_direction;
    check_near(math::length(actual_sphere.position - expected_center), 0.0f, BOUNDS_TOLERANCE, "spot sphere sits halfway down the WORLD axis");
    const float half_angle = math::radians(input_outer_angle * 0.5f);
    const float expected_radius = std::sqrt(1.25f - std::cos(half_angle)) * input_range;
    check_near(actual_sphere.radius, expected_radius, BOUNDS_TOLERANCE, "spot sphere is built from HALF the outer cone angle");
    check(actual_sphere.contains_point(input_position, BOUNDS_TOLERANCE), "spot sphere contains the apex");
    check(actual_sphere.contains_point(input_position + input_range * input_direction, BOUNDS_TOLERANCE), "spot sphere contains the end of the axis");
    constexpr int RIM_SAMPLES = 16;
    bool contains_rim = true;
    for(int sample = 0; sample < RIM_SAMPLES; ++sample)
    {
        const float azimuth = 2.0f * math::pi<float>() * float(sample) / float(RIM_SAMPLES);
        const math::vec3 side(std::cos(azimuth), 0.0f, std::sin(azimuth));
        const math::vec3 rim = input_position + input_range * (std::cos(half_angle) * input_direction + std::sin(half_angle) * side);
        contains_rim = contains_rim && actual_sphere.contains_point(rim, BOUNDS_TOLERANCE);
    }
    check(contains_rim, "spot sphere contains the rim of the far cap");
}

void test_directional_light_bounds()
{
    light input_light;
    input_light.type = light_type::directional;
    const auto actual_sphere = input_light.compute_world_bounds_sphere(math::vec3(0.0f), math::vec3(0.0f, -1.0f, 0.0f));
    check(actual_sphere.contains_point(math::vec3(1.0e6f, -1.0e6f, 1.0e6f)), "directional light bounds swallow a distant point");
}

// ---------------------------------------------------------------------------------
// Shadow view masks (nested cascades)
// ---------------------------------------------------------------------------------

constexpr uint8_t CASCADE_COUNT = 4;
/// Half widths of the cascade crops, nearest first: concentric squares under a light that looks
/// straight down, the way the CSM crops nest around the view.
constexpr std::array<float, CASCADE_COUNT> CASCADE_HALF_WIDTHS = {5.0f, 15.0f, 40.0f, 100.0f};
constexpr float CASCADE_LIGHT_HEIGHT = 100.0f;
constexpr float CASCADE_NEAR = 0.1f;
constexpr float CASCADE_FAR = 200.0f;

using cascade_frustums = std::array<math::frustum, CASCADE_COUNT>;

auto make_nested_cascades() -> cascade_frustums
{
    const math::mat4 view = math::lookAtLH(math::vec3(0.0f, CASCADE_LIGHT_HEIGHT, 0.0f), math::vec3(0.0f), math::vec3(0.0f, 0.0f, 1.0f));
    cascade_frustums result;
    for(uint8_t ii = 0; ii < CASCADE_COUNT; ++ii)
    {
        const float half_width = CASCADE_HALF_WIDTHS[ii];
        const math::mat4 proj = math::orthoLH_ZO(-half_width, half_width, -half_width, half_width, CASCADE_NEAR, CASCADE_FAR);
        result[ii].update(view, proj, false);
    }
    return result;
}

auto make_box(const math::vec3& center, float half_extent) -> math::bbox
{
    return math::bbox(center - math::vec3(half_extent), center + math::vec3(half_extent));
}

/// The per-cascade loop exactly as both shadow submit paths ran it before the view masks:
/// skip the cascades the bounds are outside of, draw into the others, and with nested cascades
/// break after the first one that fully contains the bounds. No model-level gate in front.
auto run_old_cascade_loop(const cascade_frustums& frustums, const math::bbox& bounds, bool nested_cascades) -> uint8_t
{
    uint8_t drawn_views = 0u;
    for(uint8_t ii = 0; ii < CASCADE_COUNT; ++ii)
    {
        auto query = frustums[ii].classify_aabb(bounds);
        if(query == math::volume_query::outside)
        {
            continue;
        }
        drawn_views |= uint8_t(1u << ii);
        if(nested_cascades && query == math::volume_query::inside)
        {
            break;
        }
    }
    return drawn_views;
}

void test_view_mask_nested_cascade_cases()
{
    const auto frustums = make_nested_cascades();
    const auto compute_nested = [&](const math::bbox& bounds) -> uint8_t
    {
        return compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, bounds, true);
    };
    check(compute_nested(make_box(math::vec3(0.0f), 1.0f)) == 0b0001, "fully inside cascade 0 -> cascade 0 ONLY");
    check(compute_nested(make_box(math::vec3(5.0f, 0.0f, 0.0f), 1.0f)) == 0b0011, "straddles cascade 0, inside cascade 1 -> cascades 0 and 1, nothing farther");
    check(compute_nested(make_box(math::vec3(10.0f, 0.0f, 0.0f), 1.0f)) == 0b0010, "outside cascade 0, inside cascade 1 -> cascade 1 ONLY");
    check(compute_nested(make_box(math::vec3(15.0f, 0.0f, 0.0f), 1.0f)) == 0b0110, "straddles cascade 1, inside cascade 2 -> cascades 1 and 2");
    check(compute_nested(make_box(math::vec3(0.0f, 0.0f, -70.0f), 1.0f)) == 0b1000, "only the last cascade reaches it -> cascade 3 ONLY");
    check(compute_nested(make_box(math::vec3(500.0f, 0.0f, 0.0f), 1.0f)) == 0b0000, "outside every cascade -> not drawn");
    check(compute_nested(make_box(math::vec3(0.0f), 500.0f)) == 0b1111, "contained by no cascade -> every cascade");
    const uint8_t actual_unnested = compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, make_box(math::vec3(0.0f), 1.0f), false);
    check(actual_unnested == 0b1111, "point / spot views are not nested: every touched view is drawn");
    const uint8_t actual_candidates = compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, make_box(math::vec3(0.0f), 1.0f), true, 0b1110);
    check(actual_candidates == 0b0010, "views outside the candidate mask are never drawn, the rule restarts at the first candidate");
}

void test_view_mask_submesh_granularity()
{
    const auto frustums = make_nested_cascades();
    // A model reaching from the middle of cascade 0 out into cascade 1.
    const math::bbox input_model_bounds(math::vec3(-1.0f, -1.0f, -1.0f), math::vec3(12.0f, 1.0f, 1.0f));
    const uint8_t model_mask = compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, input_model_bounds, true);
    check(model_mask == 0b0011, "the model straddles cascade 0 and is contained by cascade 1");
    const auto compute_submesh = [&](const math::bbox& bounds) -> uint8_t
    {
        return compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, bounds, true, model_mask);
    };
    check(compute_submesh(make_box(math::vec3(0.0f), 1.0f)) == 0b0001, "a submesh fully inside cascade 0 skips cascade 1 although its model is drawn there");
    check(compute_submesh(make_box(math::vec3(10.0f, 0.0f, 0.0f), 1.0f)) == 0b0010, "a submesh beyond cascade 0 is drawn into cascade 1 only");
    check(compute_submesh(make_box(math::vec3(5.0f, 0.0f, 0.0f), 1.0f)) == 0b0011, "a submesh on the cascade 0 edge is drawn into both");
}

void test_view_mask_matches_old_cascade_loop()
{
    const auto frustums = make_nested_cascades();
    constexpr int MODEL_COUNT = 20000;
    constexpr int SUBMESHES_PER_MODEL = 4;
    std::mt19937 random_engine(20260920u);
    std::uniform_real_distribution<float> plane_position(-120.0f, 120.0f);
    std::uniform_real_distribution<float> height_position(-20.0f, 20.0f);
    std::uniform_real_distribution<float> half_extent(0.1f, 30.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int mismatches = 0;
    int nested_breaks = 0;
    int model_rejects = 0;
    for(int model_index = 0; model_index < MODEL_COUNT; ++model_index)
    {
        const math::vec3 center(plane_position(random_engine), height_position(random_engine), plane_position(random_engine));
        const math::vec3 extents(half_extent(random_engine), half_extent(random_engine), half_extent(random_engine));
        const math::bbox model_bounds(center - extents, center + extents);
        for(const bool nested_cascades : {true, false})
        {
            const uint8_t model_mask = compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, model_bounds, nested_cascades);
            mismatches += model_mask != run_old_cascade_loop(frustums, model_bounds, nested_cascades) ? 1 : 0;
            model_rejects += model_mask == 0u ? 1 : 0;
            for(int submesh_index = 0; submesh_index < SUBMESHES_PER_MODEL; ++submesh_index)
            {
                // A box anywhere inside the model's, as a submesh's bounds are.
                const math::vec3 first = model_bounds.min + (model_bounds.max - model_bounds.min) * math::vec3(unit(random_engine), unit(random_engine), unit(random_engine));
                const math::vec3 second = model_bounds.min + (model_bounds.max - model_bounds.min) * math::vec3(unit(random_engine), unit(random_engine), unit(random_engine));
                const math::bbox submesh_bounds(math::min(first, second), math::max(first, second));
                // Old: the submesh loop ran ungated. New: gated by the model's mask.
                const uint8_t expected_views = run_old_cascade_loop(frustums, submesh_bounds, nested_cascades);
                const uint8_t actual_views = model_mask == 0u ? uint8_t(0u) : compute_shadow_view_mask(frustums.data(), CASCADE_COUNT, submesh_bounds, nested_cascades, model_mask);
                mismatches += actual_views != expected_views ? 1 : 0;
                nested_breaks += nested_cascades && expected_views != run_old_cascade_loop(frustums, submesh_bounds, false) ? 1 : 0;
            }
        }
    }
    std::printf("  cascade sweep: %d submesh cases, nested break fired in %d, model-level reject in %d\n", MODEL_COUNT * 2 * SUBMESHES_PER_MODEL, nested_breaks, model_rejects);
    check(mismatches == 0, "model gate + submesh mask draw exactly the views the old ungated loops drew (" + std::to_string(mismatches) + " mismatches)");
    check(nested_breaks > MODEL_COUNT / 10, "the sweep exercises the nested-cascade break (" + std::to_string(nested_breaks) + " cases)");
    check(model_rejects > 0, "the sweep exercises the model-level reject (" + std::to_string(model_rejects) + " cases)");
}

// ---------------------------------------------------------------------------------
// Light projections (linear depth)
// ---------------------------------------------------------------------------------

void test_light_projection_culling_frustum()
{
    constexpr float INPUT_NEAR = 0.2f;
    constexpr float INPUT_FAR = 25.0f;
    const math::mat4 input_view(1.0f);
    // bx projections are left handed: the light looks down +Z.
    const math::bbox in_range_caster = make_box(math::vec3(0.0f, 0.0f, 20.0f), 1.0f);
    const math::bbox beyond_far_caster = make_box(math::vec3(0.0f, 0.0f, 40.0f), 1.0f);
    const math::bbox behind_light_caster = make_box(math::vec3(0.0f, 0.0f, -5.0f), 1.0f);
    for(const bool homogeneous_depth : {false, true})
    {
        const std::string convention = homogeneous_depth ? " (homogeneous depth)" : " (zero to one depth)";
        for(const bool is_linear_depth : {false, true})
        {
            const std::string mode = (is_linear_depth ? "linear depth" : "inverse z") + convention;
            const auto actual = shadow::make_light_projection(90.0f, 1.0f, INPUT_NEAR, INPUT_FAR, homogeneous_depth, is_linear_depth);
            math::frustum actual_frustum;
            actual_frustum.update(input_view, math::make_mat4(actual.cull_proj), actual.homogeneous_depth);
            check(actual_frustum.classify_aabb(in_range_caster) != math::volume_query::outside, mode + ": a caster in range is kept");
            check(actual_frustum.classify_aabb(beyond_far_caster) == math::volume_query::outside, mode + ": a caster beyond the far plane is culled");
            check(actual_frustum.classify_aabb(behind_light_caster) == math::volume_query::outside, mode + ": a caster behind the light is culled");
            const bool is_same_matrix = std::memcmp(actual.render_proj, actual.cull_proj, sizeof(actual.cull_proj)) == 0;
            check(is_same_matrix == !is_linear_depth, mode + ": only linear depth rescales the render projection");
        }
        // Why cull_proj exists: a frustum extracted from the rescaled render projection loses
        // its far plane, so the light's range no longer culls anything.
        const auto linear = shadow::make_light_projection(90.0f, 1.0f, INPUT_NEAR, INPUT_FAR, homogeneous_depth, true);
        math::frustum rescaled_frustum;
        rescaled_frustum.update(input_view, math::make_mat4(linear.render_proj), linear.homogeneous_depth);
        check(rescaled_frustum.classify_aabb(beyond_far_caster) != math::volume_query::outside, "the rescaled render projection cannot cull by range" + convention);
    }
}

// ---------------------------------------------------------------------------------
// Batch collector
// ---------------------------------------------------------------------------------

/// A mesh pointer that is never dereferenced: it shares @p owner 's control block, so the
/// owner's use count tells how many references the collector still holds.
auto make_fake_mesh(const std::shared_ptr<int>& owner, uint32_t index) -> std::shared_ptr<mesh>
{
    const std::uintptr_t address = FAKE_MESH_BASE_ADDRESS + FAKE_MESH_ADDRESS_STRIDE * index;
    return std::shared_ptr<mesh>(owner, reinterpret_cast<mesh*>(address));
}

auto find_batch(const shadow_batch_collector& collector, const shadow_batch_key& key) -> const batch_group_t<shadow_batch_key>*
{
    for(const auto* batch : collector.get_prepared_batches())
    {
        if(batch->key == key)
        {
            return batch;
        }
    }
    return nullptr;
}

void test_collector_groups_by_key()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    const shadow_batch_key input_key_a(make_fake_mesh(owner, 0), 0, 0, cull_type::counter_clockwise);
    const shadow_batch_key input_key_b(make_fake_mesh(owner, 1), 0, 0, cull_type::counter_clockwise);
    const shadow_batch_key input_key_c(make_fake_mesh(owner, 1), 1, 0, cull_type::counter_clockwise);
    shadow_batch_collector collector;
    check(!collector.has_batches(), "a new collector has no batches");
    collector.collect_renderable(input_key_a, input_transform);
    collector.collect_renderable(input_key_b, input_transform);
    collector.collect_renderable(input_key_a, input_transform);
    collector.collect_renderable(input_key_c, input_transform);
    collector.collect_renderable(input_key_a, input_transform);
    collector.collect_renderable(shadow_batch_key{}, input_transform);
    collector.prepare_batches(submit_context{});
    check(collector.get_batch_count() == 3, "three distinct keys make three groups (the invalid key is dropped)");
    check(collector.get_instance_count() == 5, "every valid instance is collected");
    check(collector.get_prepared_batches().size() == 3, "every group is prepared");
    const auto* actual_a = find_batch(collector, input_key_a);
    const auto* actual_c = find_batch(collector, input_key_c);
    check(actual_a != nullptr && actual_a->instances.size() == 3, "key a holds its three instances");
    check(actual_c != nullptr && actual_c->instances.size() == 1, "a key differing only in lod gets its own group");
    check(collector.get_stats().total_batches == 3 && collector.get_stats().total_instances == 5, "stats match the prepared batches");
}

void test_collector_clear_releases_references_and_instances()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    shadow_batch_collector collector;
    const long expected_use_count = owner.use_count();
    collector.collect_renderable(shadow_batch_key(make_fake_mesh(owner, 0), 0, 0, cull_type::counter_clockwise), input_transform);
    collector.collect_renderable(shadow_batch_key(make_fake_mesh(owner, 1), 0, 0, cull_type::counter_clockwise), input_transform);
    check(owner.use_count() > expected_use_count, "collected keys hold their mesh");
    collector.clear();
    check(owner.use_count() == expected_use_count, "clear() drops every mesh reference");
    check(!collector.has_batches() && collector.get_instance_count() == 0, "clear() leaves no groups and no instances");
    check(collector.get_prepared_batches().empty(), "clear() drops the prepared list");
    const shadow_batch_key input_key(make_fake_mesh(owner, 7), 0, 0, cull_type::counter_clockwise);
    collector.collect_renderable(input_key, input_transform);
    collector.prepare_batches(submit_context{});
    const auto* actual_batch = find_batch(collector, input_key);
    check(collector.get_batch_count() == 1, "a recycled slot does not resurrect the old groups");
    check(actual_batch != nullptr && actual_batch->instances.size() == 1, "a recycled slot starts without stale instances");
}

void test_collector_reuses_instance_storage()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    const shadow_batch_key input_key(make_fake_mesh(owner, 3), 0, 2, cull_type::counter_clockwise);
    constexpr int INSTANCES_PER_FRAME = 100;
    shadow_batch_collector collector;
    const batch_instance* first_frame_storage = nullptr;
    bool is_storage_stable = true;
    constexpr int FRAMES = 4;
    for(int frame = 0; frame < FRAMES; ++frame)
    {
        collector.clear();
        for(int instance = 0; instance < INSTANCES_PER_FRAME; ++instance)
        {
            collector.collect_renderable(input_key, input_transform);
        }
        collector.prepare_batches(submit_context{});
        const auto* batch = find_batch(collector, input_key);
        if(batch == nullptr || batch->instances.size() != size_t(INSTANCES_PER_FRAME))
        {
            is_storage_stable = false;
            break;
        }
        const batch_instance* storage = &batch->instances[0];
        if(frame == 0)
        {
            first_frame_storage = storage;
        }
        is_storage_stable = is_storage_stable && storage == first_frame_storage;
    }
    check(is_storage_stable, "a steady frame reuses the instance storage of the frame before");
}

void test_collector_grows_past_its_initial_index()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    shadow_batch_collector collector;
    for(uint32_t pass = 0; pass < 2; ++pass)
    {
        for(uint32_t index = 0; index < MANY_KEYS; ++index)
        {
            collector.collect_renderable(shadow_batch_key(make_fake_mesh(owner, index), index % 3, index % 5, cull_type::counter_clockwise), input_transform);
        }
    }
    collector.prepare_batches(submit_context{});
    check(collector.get_batch_count() == MANY_KEYS, "every distinct key keeps exactly one group while the index grows");
    check(collector.get_instance_count() == size_t(MANY_KEYS) * 2, "the second pass finds every existing group");
    bool is_every_batch_paired = true;
    for(const auto* batch : collector.get_prepared_batches())
    {
        is_every_batch_paired = is_every_batch_paired && batch->instances.size() == 2;
    }
    check(is_every_batch_paired, "each group holds the instance of both passes");
}

void test_collector_drops_prepared_list_on_new_key()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    const shadow_batch_key input_key(make_fake_mesh(owner, 0), 0, 0, cull_type::counter_clockwise);
    shadow_batch_collector collector;
    collector.collect_renderable(input_key, input_transform);
    collector.prepare_batches(submit_context{});
    collector.collect_renderable(input_key, input_transform);
    check(collector.get_prepared_batches().size() == 1, "another instance of a known key keeps the prepared list");
    collector.collect_renderable(shadow_batch_key(make_fake_mesh(owner, 1), 0, 0, cull_type::counter_clockwise), input_transform);
    check(collector.get_prepared_batches().empty(), "a new key after prepare_batches drops the prepared list");
}

void test_material_collector_sorts_and_counts()
{
    const auto owner = std::make_shared<int>(0);
    const math::mat4 input_transform(1.0f);
    const auto input_mesh = make_fake_mesh(owner, 0);
    const std::shared_ptr<material> input_material_a(owner, reinterpret_cast<material*>(FAKE_MESH_BASE_ADDRESS * 4));
    const std::shared_ptr<material> input_material_b(owner, reinterpret_cast<material*>(FAKE_MESH_BASE_ADDRESS * 8));
    batch_collector collector;
    collector.collect_renderable(batch_key(input_mesh, input_material_b, 0, 0), input_transform);
    collector.collect_renderable(batch_key(input_mesh, input_material_a, 0, 0), input_transform);
    collector.collect_renderable(batch_key(input_mesh, input_material_b, 0, 0), input_transform);
    collector.prepare_batches(submit_context{});
    const auto& actual_batches = collector.get_prepared_batches();
    check(actual_batches.size() == 2, "two materials make two batches");
    check(actual_batches.size() == 2 && actual_batches[0]->key.material_ptr.get() < actual_batches[1]->key.material_ptr.get(), "prepared batches are sorted by material");
    check(collector.get_stats().draw_calls_saved == 1, "three instances in two batches save one draw call");
}

} // namespace

// ---------------------------------------------------------------------------------

auto run_render_culling_suite(rtti::context& /*ctx*/) -> int
{
    test_point_light_bounds();
    test_spot_light_bounds_follow_world_direction();
    test_directional_light_bounds();
    test_view_mask_nested_cascade_cases();
    test_view_mask_submesh_granularity();
    test_view_mask_matches_old_cascade_loop();
    test_light_projection_culling_frustum();
    test_collector_groups_by_key();
    test_collector_clear_releases_references_and_instances();
    test_collector_reuses_instance_storage();
    test_collector_grows_past_its_initial_index();
    test_collector_drops_prepared_list_on_new_key();
    test_material_collector_sorts_and_counts();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("render culling / batching", run_render_culling_suite)
