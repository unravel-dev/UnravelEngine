/*
 * Validation suite for what shadow map caching keys on outside the shadow pass.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "shadow caching"
 *
 * What it pins:
 *   - a model's materials revisions move per aspect: a shared material edit or a material instance override moves
 *     only the aspects it changed;
 *   - model_component::update_change_stamps stamps every aspect on the first update, then only what changed: the
 *     reported placement, a setter's reasons, material aspects; an edit shadows do not read leaves their stamp;
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
#include <engine/threading/threader.h>

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

void test_materials_revisions()
{
    std::printf("a model's materials revisions move per aspect\n");
    model empty;
    check(empty.get_materials_change_keys() == render_change_values{}, "a model without materials has no keys");
    const auto shared = make_shared_material("revision");
    model mdl;
    mdl.set_shared_material(shared, 0);
    mdl.update_material_instances();
    const auto initial = mdl.get_materials_change_keys();
    get_pbr(shared)->set_roughness(0.6f);
    const auto after_shared_edit = mdl.get_materials_change_keys();
    check(after_shared_edit != initial, "editing the shared material moves them");
    check(get_changed_aspects(initial, after_shared_edit) == render_change::specular, "a roughness edit moves only specular");
    mdl.get_material_instance(0).set_override("emissive_intensity"_hs, 4.0f);
    mdl.update_material_instances();
    check(get_changed_aspects(after_shared_edit, mdl.get_materials_change_keys()) == render_change::emission,
          "a first emissive override moves only emission");
}

void test_change_stamps()
{
    std::printf("the change stamps follow what changed, aspect by aspect\n");
    const auto shared = make_shared_material("stamps");
    model mdl;
    mdl.set_shared_material(shared, 0);
    model_component comp;
    comp.set_model(mdl);
    const auto update = [&comp](uint64_t frame, render_change::mask changes)
    {
        comp.update_material_instances();
        comp.update_change_stamps(frame, changes);
    };
    const auto& stamps = comp.get_change_stamps();
    update(10, render_change::none);
    check(stamps.get_last_change(render_change::all) == 10 && stamps.get_last_change(render_change::emission) == 10,
          "the first update stamps every aspect");
    update(11, render_change::none);
    check(stamps.get_last_change(render_change::all) == 10, "an update without a change stamps nothing");
    update(12, render_change::placement);
    check(stamps.get_last_change(render_interest::shadow_caster) == 12, "a move stamps what shadows read");
    check(stamps.get_last_change(render_interest::gi_card_capture) == 10, "and not what a card capture reads");
    get_pbr(shared)->set_emissive_intensity(3.0f);
    update(13, render_change::none);
    check(stamps.get_last_change(render_change::emission) == 13, "an emissive edit stamps emission");
    check(stamps.get_last_change(render_interest::shadow_caster) == 12, "and leaves the shadow stamp");
    get_pbr(shared)->set_alpha_cutoff(0.75f);
    update(14, render_change::none);
    check(stamps.get_last_change(render_interest::shadow_caster) == 14, "a cutoff edit stamps the shadow aspects");
    math::color color = get_pbr(shared)->get_base_color();
    color.value.r = 0.5f;
    check(comp.set_material_instance_value(0, "base_color"_hs, entt::meta_any{color}), "an override applies");
    update(15, render_change::none);
    check(stamps.get_last_change(render_change::albedo) == 15, "a color override stamps albedo");
    check(stamps.get_last_change(render_interest::shadow_caster) == 14, "and leaves the shadow stamp");
    comp.set_enabled(false);
    update(16, render_change::none);
    check(stamps.get_last_change(render_change::membership) == 16, "disabling stamps membership");
    check(stamps.has_changed_within(render_interest::shadow_caster, 4, 19), "a change 3 frames ago is still settling");
    check(!stamps.has_changed_within(render_interest::shadow_caster, 4, 20), "4 frames later it settled");
    check(!stamps.has_changed_since(render_interest::shadow_caster, 16), "nothing changed since frame 16");
}

void test_material_reload_stamps(tpp::thread_pool& pool)
{
    std::printf("a reloaded shared material stamps what shadows read\n");
    const auto shared = make_shared_material("reload");
    model mdl;
    mdl.set_shared_material(shared, 0);
    model_component comp;
    comp.set_model(mdl);
    const auto update = [&comp](uint64_t frame)
    {
        comp.update_material_instances();
        comp.update_change_stamps(frame, render_change::none);
    };
    update(10);
    update(11);
    check(comp.get_change_stamps().get_last_change(render_interest::shadow_caster) == 10, "a steady model stamps nothing");
    // A reload loads a new object straight into its members: it counts as many edits as the object it replaces.
    const std::shared_ptr<material> reloaded = std::make_shared<pbr_material>();
    auto handle = shared;
    handle.set_internal_job(pool.schedule("shadow_caching_reload",
                                          [reloaded]()
                                          {
                                              return reloaded;
                                          })
                                .share());
    check(handle.get() == reloaded, "the handle holds the reloaded material");
    update(12);
    check(comp.get_change_stamps().get_last_change(render_interest::shadow_caster) == 12,
          "a reloaded shared material stamps the shadow aspects");
}

void test_model_swap_stamps()
{
    std::printf("a model set again stamps only what it draws differently\n");
    const auto shared = make_shared_material("swap");
    model mdl;
    mdl.set_shared_material(shared, 0);
    model_component comp;
    comp.set_model(mdl);
    const auto update = [&comp](uint64_t frame)
    {
        comp.update_material_instances();
        comp.update_change_stamps(frame, render_change::none);
    };
    const auto& stamps = comp.get_change_stamps();
    update(10);
    model edited = comp.get_model();
    edited.get_material_instance(0).set_override("emissive_intensity"_hs, 5.0f);
    comp.set_model(edited);
    update(11);
    check(stamps.get_last_change(render_change::emission) == 11, "an emissive override set with the model stamps emission");
    check(stamps.get_last_change(render_interest::shadow_caster) == 10, "and leaves the shadow stamp");
    model recolored = comp.get_model();
    math::color color = get_pbr(shared)->get_base_color();
    color.value.b = 0.25f;
    recolored.get_material_instance(0).set_override("base_color"_hs, color);
    comp.set_model(recolored);
    update(12);
    check(stamps.get_last_change(render_change::albedo) == 12, "a color override set with the model stamps albedo");
    check(stamps.get_last_change(render_interest::shadow_caster) == 10, "and leaves the shadow stamp: the alpha kept");
    const model same = comp.get_model();
    comp.set_model(same);
    update(13);
    check(stamps.get_last_change(render_interest::shadow_caster) == 10, "the same model set again leaves the shadow stamp");
    model other = comp.get_model();
    other.set_shared_material(make_shared_material("swap_other"), 0);
    comp.set_model(other);
    update(14);
    check(stamps.get_last_change(render_interest::shadow_caster) == 14, "another shared material stamps the shadow aspects");
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
    test_materials_revisions();
    test_change_stamps();
    test_model_swap_stamps();
    test_material_reload_stamps(*ctx.get_cached<threader>().pool);
    test_cached_maps_epoch();
    std::printf("\nshadow caching: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("shadow caching", run_shadow_caching_suite)
