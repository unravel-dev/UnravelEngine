/*
 * Validation suite for material instances: a model slot's overrides of its shared material.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "material instances"
 *
 * What it pins:
 *   - a value the instance does not override follows the shared material, also after the shared material changes;
 *   - an override takes only a value its property's type accepts, and reverting it follows the shared material again;
 *   - shares_draw is true while every override is a per-instance property;
 *   - the resolved material applies the overrides, keeps its identity when the shared material changes, and
 *     counts per-instance edits apart from the others;
 *   - an instance survives a JSON and a binary round trip;
 *   - an older save, a whole material per slot, turns into the properties where it differs from the shared material,
 *     through the model's own load;
 *   - a model without overrides saves an empty instance list, as before;
 *   - a reload that skips one slot's instance (a prefab instance's override of it) keeps that slot's live instance.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/rendering/material_instance.hpp>
#include <engine/meta/rendering/model.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/model.h>

#include <hpp/string_view.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/serialization.h>
#include <serialization/types/vector.hpp>

#include <cstdio>
#include <functional>
#include <sstream>
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

const math::color SHARED_BASE_COLOR{0.2f, 0.4f, 0.6f, 1.0f};
const math::color INSTANCE_EMISSIVE_COLOR{1.0f, 0.1f, 0.1f, 1.0f};

/// A PBR shared material registered under @p name, replacing an earlier one of that name.
auto make_shared_material(const std::string& name) -> asset_handle<material>
{
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/material_instance_test/" + name;
    am.unload_asset<material>(key);
    auto mat = std::make_shared<pbr_material>();
    mat->set_base_color(SHARED_BASE_COLOR);
    return am.get_asset_from_instance<material>(key, mat);
}

auto get_pbr(const asset_handle<material>& handle) -> std::shared_ptr<pbr_material>
{
    return std::static_pointer_cast<pbr_material>(handle.get());
}

auto get_color(const entt::meta_any& value) -> math::color
{
    const auto* color = value.try_cast<math::color>();
    return color ? *color : math::color{};
}

auto make_instance(const asset_handle<material>& shared) -> material_instance
{
    material_instance instance;
    instance.set_shared_material(shared);
    return instance;
}

void test_values_follow_the_shared_material()
{
    std::printf("values the instance does not override follow the shared material\n");
    const auto shared = make_shared_material("follow");
    auto instance = make_instance(shared);
    check(!instance.has_overrides(), "a new instance has no overrides");
    check(get_color(instance.get_value("base_color"_hs)) == SHARED_BASE_COLOR, "it reads the shared base color");
    check(instance.set_override("emissive_color"_hs, INSTANCE_EMISSIVE_COLOR), "an emissive color override applies");
    const math::color new_base{0.9f, 0.8f, 0.7f, 1.0f};
    get_pbr(shared)->set_base_color(new_base);
    check(get_color(instance.get_value("base_color"_hs)) == new_base, "the base color follows the shared material");
    check(get_color(instance.get_value("emissive_color"_hs)) == INSTANCE_EMISSIVE_COLOR,
          "the overridden emissive color keeps the instance's value");
}

void test_override_rules()
{
    std::printf("an override takes only what its property accepts\n");
    const auto shared = make_shared_material("rules");
    auto instance = make_instance(shared);
    check(!instance.set_override("base_color"_hs, 0.5f), "a float is refused for a color");
    check(!instance.set_override("no_such_property"_hs, 1.0f), "an unknown property is refused");
    check(!instance.has_overrides(), "refused overrides leave no trace");
    check(instance.set_override("roughness"_hs, 0.75f), "a float applies to roughness");
    check(instance.is_overridden("roughness"_hs), "roughness is overridden");
    instance.revert("roughness"_hs);
    check(!instance.is_overridden("roughness"_hs), "a reverted property is no longer overridden");
    const auto roughness = instance.get_value("roughness"_hs);
    check(roughness.allow_cast<float>() && roughness.cast<float>() == get_pbr(shared)->get_roughness(),
          "a reverted property reads the shared value");
    instance.set_override("metalness"_hs, 1.0f);
    instance.set_override("bumpiness"_hs, 2.0f);
    instance.revert_all();
    check(!instance.has_overrides(), "revert_all drops every override");
}

void test_shares_draw()
{
    std::printf("shares_draw follows the per_instance attribute\n");
    const auto shared = make_shared_material("shares_draw");
    auto instance = make_instance(shared);
    check(instance.shares_draw(), "an instance without overrides shares the draw");
    instance.set_override("base_color"_hs, math::color{1.0f, 0.0f, 0.0f, 0.5f});
    instance.set_override("emissive_intensity"_hs, 4.0f);
    check(instance.shares_draw(), "colors and numbers keep sharing the draw");
    instance.set_override("alpha_mode"_hs, alpha_mode::blend);
    check(!instance.shares_draw(), "an alpha mode override gives the slot a draw of its own");
    instance.revert("alpha_mode"_hs);
    check(instance.shares_draw(), "reverting it shares the draw again");
    instance.set_override("tiling"_hs, math::vec2{2.0f, 2.0f});
    check(!instance.shares_draw(), "a tiling override gives the slot a draw of its own");
}

void test_resolved_material()
{
    std::printf("the resolved material applies the overrides in place\n");
    const auto shared = make_shared_material("resolved");
    auto instance = make_instance(shared);
    instance.update_resolved();
    check(instance.get_resolved() == nullptr, "an instance without overrides resolves to nothing");
    instance.set_override("emissive_color"_hs, INSTANCE_EMISSIVE_COLOR);
    instance.update_resolved();
    const auto resolved = std::static_pointer_cast<pbr_material>(instance.get_resolved());
    check(resolved != nullptr, "an override resolves to a material");
    if(!resolved)
    {
        return;
    }
    check(resolved->get_emissive_color() == INSTANCE_EMISSIVE_COLOR, "the resolved material has the override");
    check(resolved->get_base_color() == SHARED_BASE_COLOR, "and the shared base color");
    const auto state_revision = resolved->get_state_revision();
    const auto values_revision = resolved->get_instance_values_revision();
    instance.set_override("emissive_intensity"_hs, 3.0f);
    instance.update_resolved();
    check(resolved->get_emissive_intensity() == 3.0f, "a new override reaches the resolved material");
    check(resolved->get_state_revision() == state_revision, "a per-instance override leaves the state revision");
    check(resolved->get_instance_values_revision() > values_revision, "and counts as a per-instance edit");
    const math::color new_base{0.1f, 0.9f, 0.1f, 1.0f};
    get_pbr(shared)->set_base_color(new_base);
    instance.update_resolved();
    check(instance.get_resolved() == resolved, "a shared material edit keeps the resolved material's identity");
    check(resolved->get_base_color() == new_base, "and reaches it");
    check(resolved->get_emissive_color() == INSTANCE_EMISSIVE_COLOR, "the overrides stay on top");
    instance.revert("emissive_intensity"_hs);
    instance.update_resolved();
    check(resolved->get_emissive_intensity() == get_pbr(shared)->get_emissive_intensity(),
          "a reverted override takes the shared value again");
    instance.revert_all();
    instance.update_resolved();
    check(instance.get_resolved() == nullptr, "no overrides, no resolved material");
}

template<typename OutputArchive, typename InputArchive>
auto round_trip(const material_instance& saved, const asset_handle<material>& shared, std::ios::openmode mode)
    -> material_instance
{
    std::stringstream stream(std::ios::in | std::ios::out | mode);
    {
        OutputArchive ar(stream);
        try_save(ar, ser20::make_nvp("instance", saved));
    }
    material_instance loaded;
    {
        InputArchive ar(stream);
        try_load(ar, ser20::make_nvp("instance", loaded));
    }
    loaded.set_shared_material(shared);
    return loaded;
}

void test_round_trips()
{
    std::printf("an instance round-trips through JSON and binary\n");
    const auto shared = make_shared_material("round_trip");
    auto instance = make_instance(shared);
    instance.set_override("emissive_color"_hs, INSTANCE_EMISSIVE_COLOR);
    instance.set_override("roughness"_hs, 0.65f);
    instance.set_override("tiling"_hs, math::vec2{3.0f, 1.5f});
    instance.set_override("alpha_mode"_hs, alpha_mode::mask);
    instance.set_override("cull_type"_hs, cull_type::none);
    const auto json = round_trip<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(instance, shared, {});
    check(json == instance, "the JSON round trip keeps every override");
    const auto binary =
        round_trip<ser20::oarchive_binary_t, ser20::iarchive_binary_t>(instance, shared, std::ios::binary);
    check(binary == instance, "the binary round trip keeps every override");
    const auto empty = make_instance(shared);
    const auto empty_json = round_trip<ser20::oarchive_associative_t, ser20::iarchive_associative_t>(empty, shared, {});
    check(!empty_json.has_overrides(), "an instance without overrides loads without overrides");
}

void test_legacy_copy()
{
    std::printf("a whole material from an older save becomes its differences\n");
    const auto shared = make_shared_material("legacy_copy");
    auto copy = std::static_pointer_cast<pbr_material>(get_pbr(shared)->clone());
    copy->set_emissive_color(INSTANCE_EMISSIVE_COLOR);
    copy->set_roughness(0.9f);
    auto instance = make_instance(shared);
    instance.set_legacy_copy(copy);
    check(instance.has_overrides() && !instance.shares_draw(), "a waiting copy counts as overrides");
    check(instance.collect_overrides().size() == 2, "saving it writes only the two differences");
    check(instance.convert_legacy_copy(true), "it converts once the shared material is loaded");
    check(!instance.has_legacy_copy(), "the copy is gone");
    check(instance.get_overrides().size() == 2 && instance.is_overridden("emissive_color"_hs) &&
              instance.is_overridden("roughness"_hs),
          "the overrides are exactly the differing properties");
    get_pbr(shared)->set_metalness(0.5f);
    const auto metalness = instance.get_value("metalness"_hs);
    check(metalness.allow_cast<float>() && metalness.cast<float>() == 0.5f,
          "a value the copy did not change follows the shared material from then on");
}

/// The shape of a model saved before material instances held overrides: a whole material per slot.
struct legacy_model_document
{
    std::vector<asset_handle<material>> materials;
    std::vector<material::sptr> material_instances;

    template<typename Archive>
    void save(Archive& ar) const
    {
        std::vector<asset_handle<mesh>> lods;
        try_save(ar, ser20::make_nvp("lods", lods));
        try_save(ar, ser20::make_nvp("materials", materials));
        try_save(ar, ser20::make_nvp("material_instances", material_instances));
    }
};

void test_legacy_model_document()
{
    std::printf("a model saved with whole materials per slot loads them as overrides\n");
    const auto shared = make_shared_material("legacy_model");
    auto copy = std::static_pointer_cast<pbr_material>(get_pbr(shared)->clone());
    copy->set_base_color(math::color{1.0f, 1.0f, 0.0f, 1.0f});
    legacy_model_document document{{shared}, {copy}};
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", document));
    }
    model loaded;
    {
        ser20::iarchive_associative_t ar(stream);
        check(try_load(ar, ser20::make_nvp("model", loaded)), "the old document loads as a model");
    }
    auto& instance = loaded.get_material_instance(0);
    check(instance.has_legacy_copy(), "the slot's whole material waits to be compared");
    check(instance.get_shared_material() == shared, "the instance knows its shared material");
    instance.convert_legacy_copy(true);
    check(instance.get_overrides().size() == 1 && instance.is_overridden("base_color"_hs),
          "only the base color differed, so only it is overridden");
}

void test_model_saves_an_empty_list_without_overrides()
{
    std::printf("a model without overrides saves an empty instance list\n");
    const auto shared = make_shared_material("empty_list");
    model mdl;
    mdl.set_shared_material(shared, 0);
    mdl.set_shared_material(shared, 1);
    mdl.set_shared_material(shared, 2);
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", mdl));
    }
    const auto text = stream.str();
    check(text.find("\"material_instances\": []") != std::string::npos, "the list is empty");
    mdl.get_material_instance(1).set_override("roughness"_hs, 0.2f);
    std::stringstream overridden;
    {
        ser20::oarchive_associative_t ar(overridden);
        try_save(ar, ser20::make_nvp("model", mdl));
    }
    model loaded;
    {
        ser20::iarchive_associative_t ar(overridden);
        try_load(ar, ser20::make_nvp("model", loaded));
    }
    check(loaded.get_material_instances().size() == 3, "every slot has an instance after loading");
    check(loaded.get_material_instance(1).is_overridden("roughness"_hs), "the second slot keeps its override");
    check(!loaded.get_material_instance(2).has_overrides(), "the slot after it has none");
}

/// Loads @p document over @p live, skipping the paths @p is_skipped names, as a prefab instance's reload skips
/// its overrides.
auto load_model_over(const model& live, const std::string& document, std::function<bool(hpp::string_view)> is_skipped)
    -> model
{
    model loaded = live;
    serialization::path_context path_ctx;
    path_ctx.enable_recording();
    path_ctx.should_serialize_property_callback = [&is_skipped](const std::string& path)
    {
        return !is_skipped(path);
    };
    auto* previous = serialization::get_path_context();
    serialization::set_path_context(&path_ctx);
    {
        std::stringstream stream(document);
        ser20::iarchive_associative_t ar(stream);
        try_load(ar, ser20::make_nvp("model", loaded));
    }
    serialization::set_path_context(previous);
    return loaded;
}

void test_reload_keeps_skipped_slots()
{
    std::printf("a reload that skips a slot's instance keeps it, and restates the others\n");
    const auto shared = make_shared_material("reload");
    model prefab_model;
    prefab_model.set_shared_material(shared, 0);
    prefab_model.set_shared_material(shared, 1);
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("model", prefab_model));
    }
    const auto document = stream.str();
    model live = prefab_model;
    live.get_material_instance(0).set_override("emissive_color"_hs, INSTANCE_EMISSIVE_COLOR);
    live.get_material_instance(1).set_override("roughness"_hs, 0.2f);
    const auto slot_reloaded = load_model_over(live,
                                               document,
                                               [](hpp::string_view path)
                                               {
                                                   return path.ends_with("material_instances[0]");
                                               });
    check(slot_reloaded.get_material_instance(0).is_overridden("emissive_color"_hs),
          "the skipped slot keeps its live override");
    check(!slot_reloaded.get_material_instance(1).has_overrides(), "the other slot is restated by the document");
    const auto list_reloaded = load_model_over(live,
                                               document,
                                               [](hpp::string_view path)
                                               {
                                                   return path.ends_with("material_instances");
                                               });
    check(list_reloaded.get_material_instance(0).has_overrides() &&
              list_reloaded.get_material_instance(1).has_overrides(),
          "skipping the whole list keeps every live slot");
    const auto plain = load_model_over(live,
                                       document,
                                       [](hpp::string_view)
                                       {
                                           return false;
                                       });
    check(!plain.get_material_instance(0).has_overrides() && !plain.get_material_instance(1).has_overrides(),
          "a load that skips nothing restates every slot");
    check(plain.get_material_instances().size() == 2, "and keeps one instance per slot");
}

void test_material_value_helpers()
{
    std::printf("material values read and write through reflection, base properties included\n");
    pbr_material source;
    source.set_roughness(0.7f);
    source.set_cull_type(cull_type::none);
    const auto roughness = get_material_value(source, "roughness"_hs);
    check(roughness.allow_cast<float>() && roughness.cast<float>() == 0.7f, "a PBR property reads");
    const auto cull = get_material_value(source, "cull_type"_hs);
    check(cull.allow_cast<cull_type>() && cull.cast<cull_type>() == cull_type::none, "a base material property reads");
    check(!get_material_value(source, "no_such_property"_hs), "an unknown property reads empty");
    pbr_material target;
    check(!set_material_value(target, "roughness"_hs, math::color{}), "a color is refused for a float");
    check(set_material_value(target, "metalness"_hs, 0.4f) && target.get_metalness() == 0.4f, "a float writes");
    const auto* address = &target;
    const auto state_revision = target.get_state_revision();
    copy_material_values(source, target);
    check(&target == address && target.get_roughness() == 0.7f && target.get_cull_type() == cull_type::none,
          "copying gives the target the source's values in place");
    check(target.get_state_revision() > state_revision, "a copied state value counts as a state edit");
}

auto run_material_instances_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_material_value_helpers();
    test_values_follow_the_shared_material();
    test_override_rules();
    test_shares_draw();
    test_resolved_material();
    test_round_trips();
    test_legacy_copy();
    test_legacy_model_document();
    test_model_saves_an_empty_list_without_overrides();
    test_reload_keeps_skipped_slots();
    std::printf("\nmaterial instances: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("material instances", run_material_instances_suite)
