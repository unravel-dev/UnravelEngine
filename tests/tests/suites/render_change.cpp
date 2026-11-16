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
 *   - writing a value a property already has moves nothing;
 *   - a graph material's parameter edit is a per-instance edit, a texture edit a state edit, each of the aspects the
 *     channels it reaches feed;
 *   - a graph edit of values alone (a parameter default, a texture node's texture) moves the same, and one of new
 *     code (a Static Switch) every aspect;
 *   - the interest masks keep the aspects their systems do not read out;
 *   - a texture's content identity lands in the change keys of its property's aspects only.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/graph_material.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/standard_material.h>
#include <engine/rendering/material_instance.h>
#include <engine/rendering/render_change.h>

#include <cstdio>
#include <string>

using namespace unravel::test;

using namespace unravel;

namespace
{

int g_texture_count = 0;

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
    auto texture = am.get_asset_from_instance<gfx::texture>(key, std::make_shared<gfx::texture>());
    // Ready before the checks read it without waiting: right after startup the pool is still busy with loads.
    texture.get();
    return texture;
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

/// Calls @p visit(id, data) for every reflected property of a graph material, the read-only ones too (which
/// for_each_material_property leaves out).
template<typename Visitor>
void for_each_reflected_property(Visitor&& visit)
{
    const auto type = entt::resolve<graph_material>();
    for(auto [base_id, base] : type.base())
    {
        for(auto [id, data] : base.data())
        {
            visit(id, data);
        }
    }
    for(auto [id, data] : type.data())
    {
        visit(id, data);
    }
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
    for_each_reflected_property(
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
            check((traits.changes & ~render_change::material_aspects) == 0u, name + " changes only material aspects");
            check(entt::get_attribute_as<bool>(data, "per_instance") == traits.is_per_instance,
                  name + " per_instance matches the table");
        });
}

void test_edits_bump_their_aspects()
{
    std::printf("an edit moves exactly its table aspects\n");
    for_each_material_property(entt::resolve<graph_material>(),
                               [](entt::id_type id, const entt::meta_data& data)
                               {
                                   const std::string name = get_property_name(data);
                                   auto property = entt::get_attribute(data, "material_property");
                                   if(!property.allow_cast<material_property_id>())
                                   {
                                       return;
                                   }
                                   const auto traits = get_material_property_traits(property.cast<material_property_id>());
                                   auto standard = make_standard_material({});
                                   auto& mat = *standard;
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
    for_each_material_property(entt::resolve<graph_material>(),
                               [](entt::id_type id, const entt::meta_data& data)
                               {
                                   auto standard = make_standard_material({});
                                   auto& mat = *standard;
                                   const auto before = mat.get_revision();
                                   set_material_value(mat, id, get_material_value(mat, id));
                                   check(mat.get_revision() == before, get_property_name(data) + " unchanged moves nothing");
                               });
}

void test_graph_values()
{
    std::printf("a graph parameter edit is per instance, a texture edit is state\n");
    auto standard = make_standard_material({});
    auto& mat = *standard;
    const auto before = take_snapshot(mat);
    check(mat.set_parameter("Base Color", math::vec4(0.5f, 0.25f, 0.125f, 1.0f)), "the Base Color parameter takes a value");
    const auto after_parameter = take_snapshot(mat);
    check(get_changed(before, after_parameter, true) == render_change::albedo,
          "a parameter moves the per-instance revision of the aspects its channels feed: the base color's albedo");
    check(get_changed(before, after_parameter, false) == render_change::none, "and no state revision");
    check(mat.set_texture("Color Map", make_texture()), "the Color Map takes a texture");
    const auto after_texture = take_snapshot(mat);
    check(get_changed(after_parameter, after_texture, false) == render_change::albedo,
          "a texture moves the state revision of the aspects it feeds");
    check(get_changed(after_parameter, after_texture, true) == render_change::none, "and no per-instance revision");
}

void test_graph_value_edits()
{
    std::printf("a graph edit of values alone moves what they feed, one of new code everything\n");
    auto standard = make_standard_material({});
    auto& mat = *standard;
    const auto* base_color = mat.find_parameter("Base Color");
    const auto* color_map = mat.find_texture("Color Map");
    check(base_color != nullptr && color_map != nullptr && !mat.get_static_switches().empty(),
          "the Standard graph has a Base Color, a Color Map and a Static Switch");
    if(base_color == nullptr || color_map == nullptr || mat.get_static_switches().empty())
    {
        return;
    }
    auto graph = mat.get_graph();
    check(set_material_graph_parameter_default(graph, base_color->node_id, math::vec4(0.5f, 0.25f, 0.125f, 1.0f)),
          "the Base Color takes a default");
    const auto before = take_snapshot(mat);
    mat.set_graph(graph);
    const auto after_parameter = take_snapshot(mat);
    check(get_changed(before, after_parameter, true) == render_change::albedo,
          "a parameter default moves the per-instance revision of the aspects it feeds");
    check(get_changed(before, after_parameter, false) == render_change::none, "and no state revision");
    check(set_material_graph_node_texture(graph, color_map->node_id, make_texture()), "the Color Map node takes a texture");
    mat.set_graph(graph);
    const auto after_texture = take_snapshot(mat);
    check(get_changed(after_parameter, after_texture, false) == render_change::albedo,
          "a texture node's texture moves the state revision of the aspects it feeds");
    check(get_changed(after_parameter, after_texture, true) == render_change::none, "and no per-instance revision");
    graph.nodes.front().position += math::vec2(16.0f, 8.0f);
    mat.set_graph(graph);
    const auto after_move = take_snapshot(mat);
    check(get_changed(after_texture, after_move, true) == render_change::none &&
              get_changed(after_texture, after_move, false) == render_change::none,
          "moving a node moves nothing");
    const auto& packed = mat.get_static_switches().front();
    check(set_material_graph_static_switch(graph, packed.node_id, !packed.value), "the Static Switch flips");
    mat.set_graph(graph);
    const auto after_switch = take_snapshot(mat);
    const auto graph_traits = get_material_property_traits(material_property_id::graph);
    check(get_changed(after_move, after_switch, graph_traits.is_per_instance) == graph_traits.changes,
          "new code moves every aspect of the graph");
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
    std::printf("a texture's content lands in the aspects it feeds\n");
    auto standard = make_standard_material({});
    auto& mat = *standard;
    const auto without = mat.get_content_version(render_change::albedo);
    // Resolved, as rendering resolves a texture it draws with: the content identity is the resolved asset.
    const auto texture = make_texture();
    texture.try_get();
    mat.set_texture("Color Map", texture);
    check(mat.get_content_version(render_change::albedo) != without, "a color map is albedo content");
    check(mat.get_content_version(render_change::emission | render_change::normal) == 0, "and nothing else");
    render_change_values keys{};
    mat.add_change_keys(keys);
    check(keys[6] == mat.get_revision(render_change::albedo) + mat.get_content_version(render_change::albedo),
          "a change key is the edits plus the content");
}

void run_render_change_suite(rtti::context& ctx)
{
    RUN_CASE(test_revisions_sum());
    RUN_CASE(test_every_property_has_traits());
    RUN_CASE(test_edits_bump_their_aspects());
    RUN_CASE(test_unchanged_values_bump_nothing());
    RUN_CASE(test_graph_values());
    RUN_CASE(test_graph_value_edits());
    RUN_CASE(test_interest_masks());
    RUN_CASE(test_content_versions());
}

} // namespace

REGISTER_TEST_SUITE("render change", run_render_change_suite)
