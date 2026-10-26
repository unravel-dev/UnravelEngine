/*
 * Validation suite for what shadow map caching keys on outside the shadow pass.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "shadow caching"
 *
 * What it pins:
 *   - a model's materials revision moves when its shared material or a material instance override is edited;
 *   - model_component::update_shadow_change stamps the first update, a reported change and a material edit, and
 *     keeps the stamp otherwise;
 *   - the cached maps epoch moves on invalidate_cached_maps and when caching is switched, and not when the switch
 *     keeps its value.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/material.h>
#include <engine/rendering/model.h>
#include <engine/rendering/shadow.h>

#include <cstdio>
#include <string>

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

/// A PBR shared material registered under @p name, replacing an earlier one of that name.
auto make_shared_material(const std::string& name) -> asset_handle<material>
{
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/shadow_caching_test/" + name;
    am.unload_asset<material>(key);
    return am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
}

auto get_pbr(const asset_handle<material>& handle) -> std::shared_ptr<pbr_material>
{
    return std::static_pointer_cast<pbr_material>(handle.get());
}

void test_materials_revision()
{
    std::printf("a model's materials revision follows material edits\n");
    model empty;
    check(empty.get_materials_revision() == 0, "a model without materials has revision 0");
    const auto shared = make_shared_material("revision");
    model mdl;
    mdl.set_shared_material(shared, 0);
    mdl.update_material_instances();
    const uint64_t initial = mdl.get_materials_revision();
    get_pbr(shared)->set_roughness(0.3f);
    const uint64_t after_shared_edit = mdl.get_materials_revision();
    check(after_shared_edit != initial, "editing the shared material moves it");
    mdl.get_material_instance(0).set_override("metalness"_hs, 0.8f);
    mdl.update_material_instances();
    check(mdl.get_materials_revision() != after_shared_edit, "an instance override moves it");
}

void test_shadow_change_stamp()
{
    std::printf("the shadow change stamp follows reported changes and material edits\n");
    const auto shared = make_shared_material("stamp");
    model mdl;
    mdl.set_shared_material(shared, 0);
    model_component comp;
    comp.set_model(mdl);
    comp.update_material_instances();
    comp.update_shadow_change(10, false);
    check(comp.get_shadow_change_frame() == 10, "the first update stamps its frame");
    comp.update_shadow_change(11, false);
    check(comp.get_shadow_change_frame() == 10, "an update without a change keeps the stamp");
    comp.update_shadow_change(12, true);
    check(comp.get_shadow_change_frame() == 12, "a reported change stamps its frame");
    get_pbr(shared)->set_roughness(0.6f);
    comp.update_shadow_change(13, false);
    check(comp.get_shadow_change_frame() == 13, "a shared material edit stamps the next update");
    comp.update_shadow_change(14, false);
    check(comp.get_shadow_change_frame() == 13, "and only that one");
    check(comp.set_material_instance_value(0, "roughness"_hs, entt::meta_any{0.2f}), "an override applies");
    comp.update_material_instances();
    comp.update_shadow_change(15, false);
    check(comp.get_shadow_change_frame() == 15, "an instance override stamps the next update");
}

void test_cached_maps_epoch()
{
    std::printf("the cached maps epoch moves on invalidation and on a switch of caching\n");
    const bool was_enabled = shadow::is_map_caching_enabled();
    const uint64_t initial = shadow::get_cached_maps_epoch();
    shadow::invalidate_cached_maps();
    check(shadow::get_cached_maps_epoch() == initial + 1, "invalidate_cached_maps moves it");
    shadow::set_map_caching_enabled(was_enabled);
    check(shadow::get_cached_maps_epoch() == initial + 1, "setting the same value keeps it");
    shadow::set_map_caching_enabled(!was_enabled);
    check(shadow::get_cached_maps_epoch() == initial + 2, "switching caching moves it");
    check(shadow::is_map_caching_enabled() == !was_enabled, "and the switch reads back");
    shadow::set_map_caching_enabled(was_enabled);
    check(shadow::get_cached_maps_epoch() == initial + 3, "switching back moves it again");
}

auto run_shadow_caching_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_materials_revision();
    test_shadow_change_stamp();
    test_cached_maps_epoch();
    std::printf("\nshadow caching: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("shadow caching", run_shadow_caching_suite)
