/*
 * Validation suite for render change tracking: what an edit changes, per aspect.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "render change"
 *
 * What it pins:
 *   - render_change_revisions counts per aspect and sums the asked aspects;
 *   - every reflected material property has a material_property_id entry, and its "per_instance" attribute is the
 *     table's;
 *   - an edit of each property moves exactly the aspects its table entry lists, with the per-instance or the state
 *     values as the entry says;
 *   - writing a value a property already has moves nothing, and a base color edit that keeps the alpha leaves
 *     coverage alone;
 *   - the interest masks keep the aspects their systems do not read out;
 *   - a texture's content identity lands in the change keys of its property's aspects only.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/standard_material.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/render_change.h>

#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;
int g_texture_count = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// The revisions of one material, per aspect and per kind of value.
struct revision_snapshot
{
    uint64_t state[render_change::count] = {};
    uint64_t instance_values[render_change::count] = {};
};

auto take_snapshot(const material& mat) -> revision_snapshot
{
    revision_snapshot snapshot;
    for(uint32_t i = 0; i < render_change::count; ++i)
    {
        snapshot.state[i] = mat.get_state_revision(1u << i);
        snapshot.instance_values[i] = mat.get_instance_values_revision(1u << i);
    }
    return snapshot;
}

/// The aspects whose state (or per-instance) revisions differ between the snapshots.
auto get_changed(const revision_snapshot& before, const revision_snapshot& after, bool is_per_instance)
    -> render_change::mask
{
    render_change::mask changed = render_change::none;
    for(uint32_t i = 0; i < render_change::count; ++i)
    {
        const bool is_moved = is_per_instance ? before.instance_values[i] != after.instance_values[i]
                                              : before.state[i] != after.state[i];
        if(is_moved)
        {
            changed |= 1u << i;
        }
    }
    return changed;
}

auto make_texture() -> asset_handle<gfx::texture>
{
    auto& am = engine::context().get_cached<asset_manager>();
    const std::string key = "engine:/embedded/render_change_test/texture_" + std::to_string(g_texture_count++);
    return am.get_asset_from_instance<gfx::texture>(key, std::make_shared<gfx::texture>());
}

/// A value of the same type as @p current that differs from it; empty for a type the suite does not know.
auto make_different_value(const entt::meta_any& current) -> entt::meta_any
{
    if(auto* color = current.try_cast<math::color>())
    {
        math::color value = *color;
        value.value.r = value.value.r > 0.5f ? value.value.r - 0.25f : value.value.r + 0.25f;
        value.value.a = value.value.a > 0.5f ? value.value.a - 0.25f : value.value.a + 0.25f;
        return value;
    }
    if(auto* number = current.try_cast<float>())
    {
        return *number + 0.125f;
    }
    if(auto* mode = current.try_cast<alpha_mode>())
    {
        return *mode == alpha_mode::opaque ? alpha_mode::mask : alpha_mode::opaque;
    }
    if(auto* cull = current.try_cast<cull_type>())
    {
        return *cull == cull_type::none ? cull_type::counter_clockwise : cull_type::none;
    }
    if(auto* vector = current.try_cast<math::vec2>())
    {
        return *vector + math::vec2(0.5f);
    }
    if(current.try_cast<asset_handle<gfx::texture>>() != nullptr)
    {
        return make_texture();
    }
    return {};
}

auto get_property_name(const entt::meta_data& data) -> std::string
{
    return entt::get_attribute_as<std::string>(data, "name");
}

void test_revisions_sum()
{
    std::printf("revisions count per aspect and sum the asked aspects\n");
    render_change_revisions revisions;
    check(revisions.get(render_change::material_aspects) == 0, "a new counter is 0");
    revisions.bump(render_change::coverage | render_change::albedo);
    check(revisions.get(render_change::coverage) == 1, "a bumped aspect counts 1");
    check(revisions.get(render_change::emission) == 0, "an aspect not bumped stays 0");
    check(revisions.get(render_change::coverage | render_change::albedo) == 2, "the asked aspects sum");
}

void test_every_property_has_traits()
{
    std::printf("every reflected material property has a table entry\n");
    for_each_material_property(entt::resolve<pbr_material>(),
                               [](entt::id_type, const entt::meta_data& data)
                               {
                                   const std::string name = get_property_name(data);
                                   auto property = entt::get_attribute(data, "material_property");
                                   check(property.allow_cast<material_property_id>(), name + " has a material_property");
                                   if(!property.allow_cast<material_property_id>())
                                   {
                                       return;
                                   }
                                   const auto traits = get_material_property_traits(property.cast<material_property_id>());
                                   check(traits.changes != render_change::none, name + " changes some aspect");
                                   check((traits.changes & ~render_change::material_aspects) == 0u,
                                         name + " changes only material aspects");
                                   check(entt::get_attribute_as<bool>(data, "per_instance") == traits.is_per_instance,
                                         name + " per_instance matches the table");
                               });
}

void test_edits_bump_their_aspects()
{
    std::printf("an edit moves exactly its table aspects\n");
    for_each_material_property(entt::resolve<pbr_material>(),
                               [](entt::id_type id, const entt::meta_data& data)
                               {
                                   const std::string name = get_property_name(data);
                                   auto property = entt::get_attribute(data, "material_property");
                                   if(!property.allow_cast<material_property_id>())
                                   {
                                       return;
                                   }
                                   const auto traits = get_material_property_traits(property.cast<material_property_id>());
                                   pbr_material mat;
                                   const auto value = make_different_value(get_material_value(mat, id));
                                   check(static_cast<bool>(value), name + " has a test value");
                                   if(!value)
                                   {
                                       return;
                                   }
                                   const auto before = take_snapshot(mat);
                                   check(set_material_value(mat, id, value), name + " takes the value");
                                   const auto after = take_snapshot(mat);
                                   check(get_changed(before, after, traits.is_per_instance) == traits.changes,
                                         name + " moves its table aspects");
                                   check(get_changed(before, after, !traits.is_per_instance) == render_change::none,
                                         name + " moves only its kind of value");
                               });
}

void test_unchanged_values_bump_nothing()
{
    std::printf("writing a value a property already has moves nothing\n");
    for_each_material_property(entt::resolve<pbr_material>(),
                               [](entt::id_type id, const entt::meta_data& data)
                               {
                                   pbr_material mat;
                                   const auto before = mat.get_revision();
                                   set_material_value(mat, id, get_material_value(mat, id));
                                   check(mat.get_revision() == before, get_property_name(data) + " unchanged moves nothing");
                               });
}

void test_base_color_alpha()
{
    std::printf("a base color edit that keeps the alpha leaves coverage alone\n");
    pbr_material mat;
    const auto coverage = mat.get_revision(render_change::coverage);
    const auto albedo = mat.get_revision(render_change::albedo);
    math::color color = mat.get_base_color();
    color.value.g = 0.25f;
    mat.set_base_color(color);
    check(mat.get_revision(render_change::albedo) != albedo, "the color moves albedo");
    check(mat.get_revision(render_change::coverage) == coverage, "the color alone leaves coverage");
    color.value.a = 0.5f;
    mat.set_base_color(color);
    check(mat.get_revision(render_change::coverage) != coverage, "the alpha moves coverage");
}

void test_interest_masks()
{
    std::printf("the interest masks leave out what their systems do not read\n");
    constexpr render_change::mask unread_by_shadows =
        render_change::albedo | render_change::normal | render_change::specular | render_change::occlusion |
        render_change::emission;
    check((render_interest::shadow_caster & unread_by_shadows) == 0u, "shadows read no shading aspect");
    check((render_interest::gi_card_capture & (render_change::specular | render_change::occlusion)) == 0u,
          "GI cards read no roughness or AO");
    check((render_interest::gi_card_direct & (render_change::albedo | render_change::emission)) == 0u,
          "GI direct lighting reads no albedo or emission");
}

void test_content_versions()
{
    std::printf("a texture's content lands in its property's aspects\n");
    pbr_material mat;
    check(mat.get_content_version() == 0, "a material without textures has no content version");
    // Resolved, as rendering resolves a texture it draws with: the content identity is the resolved asset.
    const auto texture = make_texture();
    texture.try_get();
    mat.set_color_map(texture);
    check(mat.get_content_version(render_change::albedo) != 0, "a color map is albedo content");
    check(mat.get_content_version(render_change::coverage) != 0, "and coverage content");
    check(mat.get_content_version(render_change::emission | render_change::normal) == 0, "and nothing else");
    render_change_values keys{};
    mat.add_change_keys(keys);
    check(keys[6] == mat.get_revision(render_change::albedo) + mat.get_content_version(render_change::albedo),
          "a change key is the edits plus the content");
}

auto run_render_change_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_revisions_sum();
    test_every_property_has_traits();
    test_edits_bump_their_aspects();
    test_unchanged_values_bump_nothing();
    test_base_color_alpha();
    test_interest_masks();
    test_content_versions();
    std::printf("\nrender change: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("render change", run_render_change_suite)
