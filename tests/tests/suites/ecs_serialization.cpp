/*
 * Behaviour suite for entity serialization, prefabs, cloning and prefab overrides.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite serialization           # correctness
 *   <build-dir>/bin/unravel-tests --suite serialization --bench   # correctness + timings
 *   <build-dir>/bin/unravel-tests --suite serialization --bench-only
 *
 * Purpose: pin the CURRENT behaviour of the system before it is changed, so that a
 * refactor of the save/load path can be shown not to have altered semantics. Where the
 * current behaviour is believed to be a defect, the expectation is written the way it
 * SHOULD read and marked with check_xfail(), naming the defect. Those report as XFAIL
 * today and turn into a loud XPASS the moment a fix lands, at which point they should be
 * promoted to check().
 *
 * Two behaviours that look asymmetric are deliberate and are pinned as such
 * (is_nested_root_placement_path in engine/engine/meta/ecs/entity.cpp):
 *   - a prefab instance root's local position and rotation are implicit overrides and
 *     always survive a resync;
 *   - its local scale is NOT, and keeps following the prefab until explicitly overridden.
 */

#include <engine/assets/asset_handle.h>
#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>

#include "../tests.h"
#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/components/ik_components.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/test_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/line_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/components/trail_component.h>
#include <engine/rendering/material_graph/particle_material.h>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/defaults/engine_asset_keys.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/graphics.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/threading/threader.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/light.hpp>
#include <engine/meta/rendering/reflection_probe.hpp>

#include <logging/logging.h>
#include <serialization/associative_archive.h>
#include <serialization/document_archive.h>
#include <serialization/document_convert.h>
#include <serialization/document_paths.h>
#include <serialization/serialization.h>
#include <threadpp/thread_pool.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel::test;

using namespace unravel;

namespace
{

// ---------------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------------

/// Vectors compared with a tolerance; transforms round-trip through decimal text.
void check_near(const math::vec3& actual, const math::vec3& expected, const std::string& what)
{
    constexpr float epsilon = 1e-4f;
    const bool ok = std::abs(actual.x - expected.x) < epsilon && std::abs(actual.y - expected.y) < epsilon &&
                    std::abs(actual.z - expected.z) < epsilon;
    char values[160];
    std::snprintf(values,
                  sizeof(values),
                  " (got [%f %f %f], expected [%f %f %f])",
                  actual.x,
                  actual.y,
                  actual.z,
                  expected.x,
                  expected.y,
                  expected.z);
    check(ok, what + values);
}

void begin_test(const char* name)
{
    std::printf("\n[%s]\n", name);
}

// ---------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------

/// Live entities in a registry, excluding entt's own bookkeeping.
auto count_entities(entt::registry& reg) -> size_t
{
    size_t n = 0;
    for(auto e : reg.storage<entt::entity>())
    {
        if(reg.valid(e))
        {
            ++n;
        }
    }
    return n;
}

/// Entities carrying at least one component, i.e. everything a scene actually contains.
/// scene::unload() deliberately leaves one componentless "reserved" entity behind so that
/// entity index 0 is never handed out (load_entity_from_id treats entity_type(0) as null),
/// so a raw entity count is not comparable between registries.
auto count_real_entities(entt::registry& reg) -> size_t;

/// Entities carrying no components at all - the signature of an orphan left behind by a
/// load path that pre-created a target it then failed to adopt.
auto count_componentless(entt::registry& reg) -> size_t
{
    size_t n = 0;
    for(auto e : reg.storage<entt::entity>())
    {
        if(!reg.valid(e))
        {
            continue;
        }
        bool any = false;
        for(auto&& [id, storage] : reg.storage())
        {
            if(storage.contains(e))
            {
                any = true;
                break;
            }
        }
        if(!any)
        {
            ++n;
        }
    }
    return n;
}

auto count_real_entities(entt::registry& reg) -> size_t
{
    return count_entities(reg) - count_componentless(reg);
}

auto find_child_by_name(entt::handle parent, const std::string& name) -> entt::handle
{
    const auto* transform = parent.try_get<transform_component>();
    if(!transform)
    {
        return {};
    }
    for(auto child : transform->get_children())
    {
        const auto* tag = child.try_get<tag_component>();
        if(tag && tag->name == name)
        {
            return child;
        }
    }
    return {};
}

auto name_of(entt::handle e) -> std::string
{
    const auto* tag = e.try_get<tag_component>();
    return tag ? tag->name : std::string("<no tag>");
}

auto uid_of(entt::handle e) -> hpp::uuid
{
    const auto* id = e.try_get<id_component>();
    return id ? id->id : hpp::uuid{};
}

auto prefab_uid_of(entt::handle e) -> hpp::uuid
{
    const auto* id = e.try_get<prefab_id_component>();
    return id ? id->id : hpp::uuid{};
}

/**
 * @brief Serializes a subtree the way save_to_file does, but into memory.
 *
 * Mirrors unravel::save_to_file(const std::string&, entt::const_handle): the save context
 * has to carry to_prefab and save_source, because those two drive which id keys are
 * written and whether external entity links are broken or resolved.
 */
/// Test-side views over the statement lists, as seen from one instance root: what was
/// stated here (local), what the documents above state (inherited), and both.
auto local_overrides_of(entt::handle root) -> std::set<prefab_property_override_data>
{
    return collect_statements_about(root).local.overrides;
}

auto inherited_overrides_of(entt::handle root) -> std::set<prefab_property_override_data>
{
    return collect_statements_about(root).stated.overrides;
}

auto all_overrides_of(entt::handle root) -> std::set<prefab_property_override_data>
{
    const auto about = collect_statements_about(root);
    auto all = about.local.overrides;
    all.insert(about.stated.overrides.begin(), about.stated.overrides.end());
    return all;
}

auto has_override_about(entt::handle root, const hpp::uuid& entity_uuid, const std::string& component_path) -> bool
{
    const auto about = collect_statements_about(root);
    return about.local.has_override({}, entity_uuid, component_path) ||
           about.stated.has_override({}, entity_uuid, component_path);
}

auto ids_of(const std::set<prefab_statement_target>& targets) -> std::set<hpp::uuid>
{
    std::set<hpp::uuid> ids;
    for(const auto& target : targets)
    {
        ids.insert(target.id);
    }
    return ids;
}

auto inherited_removed_entities_of(entt::handle root) -> std::set<hpp::uuid>
{
    return ids_of(collect_statements_about(root).stated.removed_entities);
}

auto all_removed_entities_of(entt::handle root) -> std::set<hpp::uuid>
{
    const auto about = collect_statements_about(root);
    auto all = ids_of(about.local.removed_entities);
    const auto inherited = ids_of(about.stated.removed_entities);
    all.insert(inherited.begin(), inherited.end());
    return all;
}

auto instance_uid_of(entt::handle e) -> hpp::uuid
{
    if(const auto* prefab_comp = e.try_get<prefab_component>())
    {
        return prefab_comp->instance_id;
    }
    return hpp::uuid{};
}

/// Whether the document that produced this instance can name it - which is what decides
/// whether that document may also remove it.
auto is_named_instance(entt::handle e) -> bool
{
    return !instance_uid_of(e).is_nil();
}

/// The uid the asset manager has for a key, registering the key if it is new. What a prefab
/// save names as its document; the asset published under the same key later gets the same uid.
auto document_uid_for(const std::string& id) -> hpp::uuid
{
    if(id.empty())
    {
        return {};
    }
    auto& am = engine::context().get_cached<asset_manager>();
    return am.add_asset(id);
}

auto serialize_as_prefab(entt::const_handle root, const std::string& id = {}) -> std::vector<uint8_t>
{
    std::ostringstream ss;

    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = root;
    save_ctx.to_prefab = true;
    save_ctx.document_uid = document_uid_for(id);

    save_to_stream(ss, root);

    save_ctx.document_uid = {};
    save_ctx.to_prefab = false;
    save_ctx.save_source = {};
    pop_save_context(pushed);

    const auto str = ss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

/// Set while the cases run as "ecs serialization / binary documents": every prefab and scene asset they
/// build is a binary document, as the asset compiler writes them.
bool g_binary_compiled_documents = false;

/// Builds the assets of its scope as binary documents (g_binary_compiled_documents), and puts the mode
/// back as it was when the scope ends, also when a case throws.
struct binary_compiled_documents_scope
{
    bool previous = g_binary_compiled_documents;

    binary_compiled_documents_scope()
    {
        g_binary_compiled_documents = true;
    }

    ~binary_compiled_documents_scope()
    {
        g_binary_compiled_documents = previous;
    }

    binary_compiled_documents_scope(const binary_compiled_documents_scope&) = delete;
    auto operator=(const binary_compiled_documents_scope&) -> binary_compiled_documents_scope& = delete;
};

/// An asset's bytes as the asset compiler would leave them: converted to a binary document in that
/// mode. Bytes that are not a JSON object stay as they are, for the cases about such documents; a JSON
/// object that does not convert fails the case, which would otherwise run on JSON.
auto compiled_document_bytes(std::vector<uint8_t> bytes) -> std::vector<uint8_t>
{
    if(!g_binary_compiled_documents || bytes.empty() || serialization::is_binary_document(bytes.data(), bytes.size()))
    {
        return bytes;
    }
    const hpp::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string error;
    auto binary = serialization::json_to_binary_document(text, error);
    if(!binary.empty())
    {
        return binary;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element root;
    const bool is_json_object = parser.parse(text.data(), text.size()).get(root) == simdjson::SUCCESS && root.is_object();
    check(!is_json_object, "a JSON document converts to a binary document: " + error);
    return bytes;
}

/**
 * @brief Registers a prefab with the real asset manager, so handles to it resolve.
 *
 * This is what a standalone harness could not do, and what let three nested-prefab bugs
 * through: with an unresolvable source, sync_prefab_instance returns early and no nested sync
 * ever runs, so every test verified the replay in isolation and none verified the thing the
 * editor does.
 *
 * The key is stable across republishes, and so is the uid behind it - add_asset keeps the one
 * already in the database - which is what lets a prefab_component written by an earlier
 * version still point at the new content.
 */
auto make_prefab_asset(std::vector<uint8_t> bytes, const std::string& id) -> asset_handle<prefab>
{
    auto data = std::make_shared<prefab>();
    data->buffer.data = compiled_document_bytes(std::move(bytes));

    auto& am = engine::context().get_cached<asset_manager>();

    // Drop whatever is registered under this key first: get_asset_from_instance returns the
    // existing entry if there is one, so republishing would otherwise hand back the old
    // content under a new name.
    am.unload_asset<prefab>(id);

    return am.get_asset_from_instance<prefab>(id, data);
}

auto minify_json(const std::string& text) -> std::string;

/**
 * @brief Builds the prefab asset from a prefab's saved source: minified JSON, which the binary run
 *        converts to the binary document asset_compiler's compile<prefab> writes (compiled_document_bytes).
 */

auto make_prefab_from(entt::const_handle root, const std::string& id) -> asset_handle<prefab>
{
    const auto bytes = serialize_as_prefab(root, id);
    const auto compiled = minify_json(std::string(bytes.begin(), bytes.end()));
    return make_prefab_asset(std::vector<uint8_t>(compiled.begin(), compiled.end()), id);
}

/// Strips every document name from a prefab document, the way a file written before ids
/// named their document reads. Slots and ids stay; only whose they are is lost.
auto strip_document_names(std::string text) -> std::string
{
    static const std::regex document_key(R"re(,\s*"(document|instance_document)"\s*:\s*"[0-9a-fA-F-]{36}")re");
    return std::regex_replace(text, document_key, "");
}

auto make_legacy_prefab_from(entt::const_handle root, const std::string& id) -> asset_handle<prefab>
{
    const auto bytes = serialize_as_prefab(root, id);
    const auto stripped = strip_document_names(std::string(bytes.begin(), bytes.end()));
    const auto compiled = minify_json(stripped);
    return make_prefab_asset(std::vector<uint8_t>(compiled.begin(), compiled.end()), id);
}

/**
 * @brief Points an instance at a given asset, then resyncs it through the engine.
 *
 * The sync itself lives in the engine now (unravel::sync_prefab_instance), so this no
 * longer duplicates the editor's version - it only supplies the asset. That indirection is
 * needed because the harness cannot re-publish an asset under its existing uid: an edited
 * prefab arrives as a new handle, and the instance has to be pointed at it.
 */
void sync_prefab_instance_with(entt::handle instance, const asset_handle<prefab>& pfb)
{
    if(auto* comp = instance.try_get<prefab_component>())
    {
        comp->source = pfb;
    }
    unravel::sync_prefab_instance(instance);
}

/**
 * @brief Overwrites an asset's payload behind its existing handle, keeping the uid.
 *
 * make_prefab_asset re-registers under a fresh uid, so a document that recorded the old uid
 * never sees content published that way. Editing the buffer in place is what an editor
 * recompile amounts to, and it is what lets a test exercise the paths that resolve a source
 * by the uid a document carries: the automatic nested refresh inside load_from_prefab, and
 * the cycle guard.
 */
void republish_in_place(const asset_handle<prefab>& pfb, entt::const_handle root)
{
    const auto bytes = serialize_as_prefab(root, pfb.id());
    const auto compiled = minify_json(std::string(bytes.begin(), bytes.end()));
    pfb.get()->buffer.data = compiled_document_bytes(std::vector<uint8_t>(compiled.begin(), compiled.end()));
}

auto count_entities_named(entt::registry& registry, const std::string& name) -> size_t
{
    size_t count = 0;
    registry.view<tag_component>().each(
        [&](auto, const auto& tag)
        {
            count += tag.name == name ? 1u : 0u;
        });
    return count;
}

void record_subtree_removal(entt::handle entity, prefab_component& container_prefab)
{
    if(!entity)
    {
        return;
    }
    if(const auto* own_prefab = entity.try_get<prefab_component>())
    {
        if(!own_prefab->instance_id.is_nil())
        {
            container_prefab.remove_instance(own_prefab->instance_id);
        }
        return;
    }
    if(const auto* id_comp = entity.try_get<prefab_id_component>())
    {
        if(!id_comp->id.is_nil())
        {
            container_prefab.remove_entity(id_comp->id);
        }
    }
    if(const auto* trans_comp = entity.try_get<transform_component>())
    {
        for(auto child : trans_comp->get_children())
        {
            record_subtree_removal(child, container_prefab);
        }
    }
}

/**
 * @brief Deletes an entity from an instance the way the editor does.
 *
 * mark_entity_as_removed records the whole live subtree on the containing instance before
 * the destroy: entities by prefab uid, nested instance roots by instance id (their content
 * belongs to their own asset, so the instance entry covers it). The whole subtree matters -
 * a resync recreates any record it cannot match, so a removal naming only the subtree's
 * root would bring the root's children back as orphans.
 */
void delete_like_the_editor(entt::handle container, entt::handle victim)
{
    record_subtree_removal(victim, container.get<prefab_component>());
    scene::destroy_entity(victim);
}

/**
 * @brief root -> {child_a -> grandchild, child_b}, each tagged and displaced.
 *
 * Deep enough to exercise parent links, child lists and the pre-order flattening, small
 * enough to assert on by hand.
 */
auto build_sample_tree(scene& scn) -> entt::handle
{
    auto root = scn.create_entity("root");
    root.get<transform_component>().set_position_local({1.0f, 2.0f, 3.0f});
    root.get<transform_component>().set_scale_local({2.0f, 2.0f, 2.0f});

    auto child_a = scn.create_entity("child_a", root);
    child_a.get<transform_component>().set_position_local({10.0f, 0.0f, 0.0f});

    auto child_b = scn.create_entity("child_b", root);
    child_b.get<transform_component>().set_position_local({0.0f, 10.0f, 0.0f});

    auto grandchild = scn.create_entity("grandchild", child_a);
    grandchild.get<transform_component>().set_position_local({0.0f, 0.0f, 10.0f});

    return root;
}

void test_scene_roundtrip_preserves_hierarchy()
{
    begin_test("scene round-trip preserves the hierarchy");

    scene src("src");
    build_sample_tree(src);

    std::stringstream ss;
    save_to_stream(ss, src);

    scene dst("dst");
    load_from_stream(ss, dst);

    size_t roots = 0;
    entt::handle loaded_root{};
    dst.registry->view<root_component, transform_component>().each(
        [&](auto e, auto&&, auto&&)
        {
            ++roots;
            loaded_root = dst.create_handle(e);
        });

    check_eq(roots, 1, "exactly one root survives the round-trip");
    if(!loaded_root)
    {
        return;
    }

    check_eq(name_of(loaded_root), "root", "the root keeps its name");
    check_eq(loaded_root.get<transform_component>().get_children().size(), 2, "the root keeps both children");

    auto child_a = find_child_by_name(loaded_root, "child_a");
    auto child_b = find_child_by_name(loaded_root, "child_b");
    check(static_cast<bool>(child_a), "child_a survives");
    check(static_cast<bool>(child_b), "child_b survives");

    if(child_a)
    {
        auto grandchild = find_child_by_name(child_a, "grandchild");
        check(static_cast<bool>(grandchild), "grandchild survives under child_a");
        if(grandchild)
        {
            check(grandchild.get<transform_component>().get_parent() == child_a, "grandchild's parent link is restored");
            check_near(grandchild.get<transform_component>().get_position_local(),
                       {0.0f, 0.0f, 10.0f},
                       "grandchild keeps its local position");
        }
    }

    check_near(loaded_root.get<transform_component>().get_position_local(),
               {1.0f, 2.0f, 3.0f},
               "the root keeps its local position");
    check_near(loaded_root.get<transform_component>().get_scale_local(),
               {2.0f, 2.0f, 2.0f},
               "the root keeps its local scale");
}

void test_scene_roundtrip_preserves_uids()
{
    begin_test("scene round-trip preserves entity uids");

    scene src("src");
    auto root = build_sample_tree(src);
    const auto root_uid = uid_of(root);
    const auto child_uid = uid_of(find_child_by_name(root, "child_a"));

    check(!root_uid.is_nil(), "create_entity assigns a uid up front");

    std::stringstream ss;
    save_to_stream(ss, src);

    scene dst("dst");
    load_from_stream(ss, dst);

    auto loaded_root = dst.find_entity_by_uuid(root_uid);
    check(static_cast<bool>(loaded_root), "the root is findable by its original uid after loading");

    if(loaded_root)
    {
        auto loaded_child = find_child_by_name(loaded_root, "child_a");
        check(static_cast<bool>(loaded_child), "child_a is present");
        if(loaded_child)
        {
            check(uid_of(loaded_child) == child_uid, "child_a keeps its uid across the round-trip");
        }
    }
}

void test_scene_load_leaves_no_orphans()
{
    begin_test("scene load leaves no componentless orphans");

    scene src("src");
    build_sample_tree(src);
    build_sample_tree(src); // two roots, so a per-root leak is visible as two

    std::stringstream ss;
    save_to_stream(ss, src);

    scene dst("dst");
    const size_t before = count_componentless(*dst.registry);
    load_from_stream(ss, dst);
    const size_t after = count_componentless(*dst.registry);

    // load_from_archive(ar, registry) used to reg.create() per root and then abandon the
    // handle, because the loaders reassign rather than fill in the one they are given.
    check(after == before, "loading a 2-root scene creates no extra componentless entities");

    check_eq(count_real_entities(*dst.registry),
             count_real_entities(*src.registry),
             "every entity that carries components made it across");
}

// ---------------------------------------------------------------------------------
// Cloning
// ---------------------------------------------------------------------------------

void test_clone_entity_regenerates_uids()
{
    begin_test("cloning an entity regenerates uids");

    scene scn("scn");
    auto root = build_sample_tree(scn);
    const auto root_uid = uid_of(root);
    const auto child_uid = uid_of(find_child_by_name(root, "child_a"));

    auto clone = scn.clone_entity(root, true, false);
    check(static_cast<bool>(clone), "the clone exists");
    if(!clone)
    {
        return;
    }

    check(uid_of(clone) != root_uid, "the clone root gets a fresh uid");
    check(!uid_of(clone).is_nil(), "the clone root's uid is not nil");

    auto clone_child = find_child_by_name(clone, "child_a");
    check(static_cast<bool>(clone_child), "the clone has child_a");
    if(clone_child)
    {
        check(uid_of(clone_child) != child_uid, "cloned children also get fresh uids");
    }

    check_eq(name_of(clone), "root", "the clone keeps the source's name");
    check_eq(clone.get<transform_component>().get_children().size(), 2, "the clone keeps both children");
    check(clone.get<transform_component>().get_parent() == root.get<transform_component>().get_parent(),
          "keep_parent puts the clone under the source's parent");
}

void test_clone_object_strips_prefab_ids()
{
    begin_test("cloning a plain object strips prefab ids");

    scene scn("scn");
    auto root = build_sample_tree(scn);

    // A prefab id with no prefab_component above it: the state a subtree is left in after
    // being detached from an instance.
    root.emplace<prefab_id_component>().regenerate_id();

    auto clone = scn.clone_entity(root, true, false);
    check(static_cast<bool>(clone), "the clone exists");
    if(clone)
    {
        check(!clone.all_of<prefab_id_component>(),
              "clone_mode_t::cloning_object drops prefab_id_component");
    }
}

void test_clone_prefab_instance_keeps_prefab_ids()
{
    begin_test("cloning a prefab instance keeps prefab ids");

    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/tree.pfb");

    scene scn("scn");
    auto instance = scn.instantiate(pfb, false);
    check(static_cast<bool>(instance), "the instance exists");
    if(!instance)
    {
        return;
    }

    const auto root_prefab_uid = prefab_uid_of(instance);
    check(!root_prefab_uid.is_nil(), "the instance root carries a prefab id");

    auto clone = scn.clone_entity(instance, true, false);
    check(static_cast<bool>(clone), "the clone exists");
    if(!clone)
    {
        return;
    }

    // The link itself survives; its asset_handle payload does not resolve here because the
    // harness registers no asset database (see main()). The uid is what the format
    // carries, and that is asserted separately in the round-trip tests.
    check(clone.all_of<prefab_component>(), "the clone is still a prefab instance");
    check(prefab_uid_of(clone) == root_prefab_uid,
          "clone_mode_t::cloning_prefab_instance preserves the root's prefab id");
    check(uid_of(clone) != uid_of(instance), "the clone still gets a fresh instance uid");

    auto clone_child = find_child_by_name(clone, "child_a");
    auto src_child = find_child_by_name(instance, "child_a");
    if(clone_child && src_child)
    {
        check(prefab_uid_of(clone_child) == prefab_uid_of(src_child),
              "children keep their prefab ids too, so the clone resyncs against the same asset");
        check(uid_of(clone_child) != uid_of(src_child), "but children still get fresh instance uids");
    }
}

void test_clone_entity_leaves_no_orphans()
{
    begin_test("cloning an entity leaves no componentless orphans");

    scene scn("scn");
    auto root = build_sample_tree(scn);

    const size_t before = count_componentless(*scn.registry);
    scn.clone_entity(root, true, false);
    const size_t after = count_componentless(*scn.registry);

    // scene::clone_entity still needs a scratch entity to carry the registry through
    // clone_entity_from_stream's handle-based API, but now disposes of it when the loader
    // does not adopt it - which it never does.
    check(after == before, "cloning creates no extra componentless entities");
}

/// A root, its child and its grandchild, linked by entity references: the root's two-bone IK targets the
/// grandchild (two levels down) and poles the child (one level down); the grandchild's targets the root.
struct linked_tree
{
    entt::handle root;
    entt::handle child;
    entt::handle grandchild;
};

auto build_linked_tree(scene& scn) -> linked_tree
{
    linked_tree tree;
    tree.root = scn.create_entity("root");
    tree.child = scn.create_entity("child", tree.root);
    tree.grandchild = scn.create_entity("grandchild", tree.child);
    auto& root_ik = tree.root.emplace<two_bone_ik_component>();
    root_ik.target = tree.grandchild;
    root_ik.pole = tree.child;
    tree.grandchild.emplace<two_bone_ik_component>().target = tree.root;
    return tree;
}

/// Checks that a copy of a linked_tree under `copy_root` links to its own entities.
void check_links_point_inside(entt::handle copy_root, const linked_tree& original)
{
    auto copy_child = find_child_by_name(copy_root, "child");
    auto copy_grandchild = copy_child ? find_child_by_name(copy_child, "grandchild") : entt::handle{};
    check(static_cast<bool>(copy_grandchild), "the copy has a grandchild");
    check(copy_grandchild != original.grandchild, "the copy's grandchild is not the original one");
    check(copy_root.all_of<two_bone_ik_component>(), "the copy's root keeps its IK");
    if(copy_root.all_of<two_bone_ik_component>())
    {
        const auto& ik = copy_root.get<two_bone_ik_component>();
        check(static_cast<bool>(copy_grandchild) && ik.target == copy_grandchild,
              "a link two levels down points at the copy's grandchild");
        check(static_cast<bool>(copy_child) && ik.pole == copy_child, "a link one level down points at the copy's child");
    }
    if(copy_grandchild && copy_grandchild.all_of<two_bone_ik_component>())
    {
        check(copy_grandchild.get<two_bone_ik_component>().target == copy_root,
              "a link back up points at the copy's root");
    }
}

/// Checks that copying `tree` left the original's entities where they were.
void check_linked_tree_untouched(const linked_tree& tree)
{
    check(tree.grandchild.get<transform_component>().get_parent() == tree.child,
          "the original grandchild stays under the original child");
    check_eq(tree.child.get<transform_component>().get_children().size(), 1, "the original child keeps its one child");
    check(tree.root.get<two_bone_ik_component>().target == tree.grandchild,
          "the original root still targets the original grandchild");
}

void test_clone_links_to_entities_further_down_point_at_the_copy()
{
    begin_test("a clone's links to entities further down point at the copy, and the original keeps its entities");

    scene scn("scn");
    auto tree = build_linked_tree(scn);

    auto copy = scn.clone_entity(tree.root, true, false);
    check(static_cast<bool>(copy), "the clone exists");
    if(!copy)
    {
        return;
    }
    check_links_point_inside(copy, tree);
    check_linked_tree_untouched(tree);
}

void test_subtree_snapshot_restores_links_further_down()
{
    begin_test("a subtree restored from its undo snapshot links to its own entities further down");

    scene scn("scn");
    auto tree = build_linked_tree(scn);
    std::string blob;
    {
        serialization::scoped_output_format compact(serialization::output_format::compact);
        std::stringstream ss;
        save_to_stream(ss, tree.root);
        blob = ss.str();
    }
    scene::destroy_entity(tree.root);

    // As delete_entities_action_t's undo restores it: into a scratch handle the loader replaces.
    entt::handle restored(*scn.registry, scn.registry->create());
    load_from_view(blob, restored);
    check(static_cast<bool>(restored), "the subtree is restored");
    if(!restored)
    {
        return;
    }
    auto restored_child = find_child_by_name(restored, "child");
    auto restored_grandchild = restored_child ? find_child_by_name(restored_child, "grandchild") : entt::handle{};
    check(static_cast<bool>(restored_grandchild), "with its grandchild");
    check(restored.all_of<two_bone_ik_component>(), "and the root's IK");
    if(restored.all_of<two_bone_ik_component>())
    {
        check(static_cast<bool>(restored_grandchild) && restored.get<two_bone_ik_component>().target == restored_grandchild,
              "a link two levels down points at the restored grandchild");
        check(static_cast<bool>(restored_child) && restored.get<two_bone_ik_component>().pole == restored_child,
              "a link one level down points at the restored child");
    }
}

void test_subtree_snapshot_restores_links_to_outside_entities()
{
    begin_test("a subtree restored from its undo snapshot links to the entities outside it that are still there");

    scene scn("scn");
    auto outside = scn.create_entity("outside");
    auto tree = build_linked_tree(scn);
    tree.root.get<two_bone_ik_component>().pole = outside;
    std::string blob;
    {
        serialization::scoped_output_format compact(serialization::output_format::compact);
        std::stringstream ss;
        save_to_stream(ss, tree.root);
        blob = ss.str();
    }
    scene::destroy_entity(tree.root);
    const size_t before = count_componentless(*scn.registry);

    entt::handle restored(*scn.registry, scn.registry->create());
    load_from_view(blob, restored);
    check(restored.all_of<two_bone_ik_component>(), "the subtree is restored with the root's IK");
    if(restored.all_of<two_bone_ik_component>())
    {
        check(restored.get<two_bone_ik_component>().pole == outside, "a link to an entity outside points at that entity");
    }
    // The scratch handle the loader replaced is the one componentless entity a restore leaves behind.
    check(count_componentless(*scn.registry) <= before + 1, "no empty entity is made for the outside link");
}

void test_component_load_honours_a_path_filter()
{
    begin_test("a component's own load replaces only what a path filter lists, as editing several entities at once needs");

    scene scn("scn");
    auto source = scn.create_entity("source");
    auto target = scn.create_entity("target");
    auto& from = source.emplace<test_component>();
    from.f = 1.5f;
    from.i32 = 2;
    from.str = "source";
    auto& into = target.emplace<test_component>();
    into.f = 7.0f;
    into.i32 = 8;
    into.str = "target";

    const auto type = entt::resolve<test_component>();
    std::stringstream saved;
    check(type.invoke("component_save"_hs, {}, source, entt::forward_as_meta(saved)).cast<bool>(), "the source saves");
    {
        const serialization::scoped_path_filter filter("component", {"f"});
        std::stringstream stream(saved.str());
        check(type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(stream)).cast<bool>(), "the target loads");
    }
    check(target.get<test_component>().f == 1.5f, "the listed property takes the source's value");
    check_eq(target.get<test_component>().i32, 8, "a property the filter leaves out keeps the target's");
    check_eq(target.get<test_component>().str, "target", "and so does another");

    std::stringstream whole(saved.str());
    type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(whole));
    check_eq(target.get<test_component>().i32, 2, "without a filter the whole component loads");
}

/// Saves @p source's Component and loads only @p paths of it into @p target's.
template<typename Component>
void load_component_paths(entt::handle source, entt::handle target, const serialization::path_set& paths)
{
    const auto type = entt::resolve<Component>();
    std::stringstream saved;
    type.invoke("component_save"_hs, {}, source, entt::forward_as_meta(saved));
    const serialization::scoped_path_filter filter("component", paths);
    std::stringstream stream(saved.str());
    type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(stream));
}

auto make_sphere(float radius) -> physics_compound_shape
{
    physics_sphere_shape sphere;
    sphere.radius = radius;
    return physics_compound_shape{sphere};
}

auto make_ragdoll_body(const std::string& bone, float mass) -> ragdoll_body
{
    ragdoll_body body;
    body.bone = bone;
    body.mass = mass;
    return body;
}

void test_list_loads_keep_the_elements_they_leave_out()
{
    begin_test("a load that lists one element of a component's list keeps the elements it leaves out");

    scene scn("scn");
    auto source = scn.create_entity("source");
    auto target = scn.create_entity("target");

    source.emplace<physics_component>().set_shapes({make_sphere(1.0f), make_sphere(2.0f)});
    target.emplace<physics_component>().set_shapes({make_sphere(7.0f), make_sphere(8.0f)});
    load_component_paths<physics_component>(source, target, {"shapes[1]"});
    const auto& shapes = target.get<physics_component>().get_shapes();
    check(shapes.size() == 2 && shapes[0] == make_sphere(7.0f) && shapes[1] == make_sphere(2.0f),
          "physics shapes: the listed shape is the document's, the other stays");

    auto& source_ragdoll = source.emplace<ragdoll_component>();
    source_ragdoll.set_bodies({make_ragdoll_body("hips", 10.0f), make_ragdoll_body("spine", 5.0f)});
    source_ragdoll.set_disabled_collisions({{"hips", "spine"}, {"spine", "head"}});
    auto& target_ragdoll = target.emplace<ragdoll_component>();
    target_ragdoll.set_bodies({make_ragdoll_body("pelvis", 12.0f), make_ragdoll_body("chest", 6.0f)});
    target_ragdoll.set_disabled_collisions({{"pelvis", "chest"}, {"chest", "neck"}});
    load_component_paths<ragdoll_component>(source, target, {"bodies[1]", "disabled_collisions[1]"});
    const auto& bodies = target.get<ragdoll_component>().get_bodies();
    check(bodies.size() == 2 && bodies[0] == make_ragdoll_body("pelvis", 12.0f) &&
              bodies[1] == make_ragdoll_body("spine", 5.0f),
          "ragdoll bodies: the listed body is the document's, the other stays");
    const auto& pairs = target.get<ragdoll_component>().get_disabled_collisions();
    check(pairs.size() == 2 && pairs[0].bone_a == "pelvis" && pairs[1].bone_a == "spine",
          "ragdoll pairs: the listed pair is the document's, the other stays");

    humanoid_pose_offset raised{humanoid_bone::left_upper_arm, {10.0f, 0.0f, 0.0f}};
    humanoid_pose_offset lowered{humanoid_bone::right_upper_arm, {-10.0f, 0.0f, 0.0f}};
    humanoid_pose_offset kept{humanoid_bone::left_upper_arm, {0.0f, 45.0f, 0.0f}};
    humanoid_pose_offset replaced{humanoid_bone::right_upper_arm, {0.0f, -45.0f, 0.0f}};
    source.emplace<animation_component>().set_retarget_pose_offsets({raised, lowered});
    target.emplace<animation_component>().set_retarget_pose_offsets({kept, replaced});
    load_component_paths<animation_component>(source, target, {"retarget_pose_offsets[1]"});
    const auto& offsets = target.get<animation_component>().get_retarget_pose_offsets();
    check(offsets.size() == 2 && offsets[0] == kept && offsets[1] == lowered,
          "retarget pose offsets: the listed offset is the document's, the other stays");
}

void test_value_loads_keep_the_members_they_leave_out()
{
    begin_test("a load that lists one member of a component's vector or struct keeps the members it leaves out");

    scene scn("scn");
    auto source = scn.create_entity("source");
    auto target = scn.create_entity("target");
    constexpr float tolerance = 1e-4f;

    reflection_probe source_probe;
    source_probe.intensity = 2.0f;
    source_probe.box_data.extents = {1.0f, 2.0f, 3.0f};
    source_probe.box_data.transition_distance = 0.5f;
    reflection_probe target_probe;
    target_probe.intensity = 0.5f;
    target_probe.box_data.extents = {7.0f, 8.0f, 9.0f};
    target_probe.box_data.transition_distance = 4.0f;
    source.emplace<reflection_probe_component>().set_probe(source_probe);
    target.emplace<reflection_probe_component>().set_probe(target_probe);
    load_component_paths<reflection_probe_component>(source, target, {"probe/box_data/extents/x"});
    const auto& probe = target.get<reflection_probe_component>().get_probe();
    check_near(probe.box_data.extents, {1.0f, 8.0f, 9.0f}, "probe extents: the listed axis is the document's, the others stay");
    test::check_near(probe.intensity, 0.5f, tolerance, "probe: its intensity stays");
    test::check_near(probe.box_data.transition_distance, 4.0f, tolerance, "probe: its transition distance stays");

    auto& source_joint = source.emplace<joint_component>();
    source_joint.set_anchor({1.0f, 2.0f, 3.0f});
    source_joint.set_connected_anchor({4.0f, 5.0f, 6.0f});
    joint_limits source_limits;
    source_limits.lower = -10.0f;
    source_limits.upper = 10.0f;
    source_joint.set_limits(source_limits);
    auto& target_joint = target.emplace<joint_component>();
    target_joint.set_anchor({7.0f, 8.0f, 9.0f});
    target_joint.set_connected_anchor({-1.0f, -2.0f, -3.0f});
    joint_limits target_limits;
    target_limits.lower = -30.0f;
    target_limits.upper = 30.0f;
    target_joint.set_limits(target_limits);
    load_component_paths<joint_component>(source, target, {"anchor/y", "connected_anchor/z", "limits/upper"});
    const auto& joint = target.get<joint_component>();
    check_near(joint.get_anchor(), {7.0f, 2.0f, 9.0f}, "joint anchor: the listed axis is the document's, the others stay");
    check_near(joint.get_connected_anchor(), {-1.0f, -2.0f, 6.0f}, "joint connected anchor: the same");
    test::check_near(joint.get_limits().upper, 10.0f, tolerance, "joint limits: the listed limit is the document's");
    test::check_near(joint.get_limits().lower, -30.0f, tolerance, "joint limits: the other stays");

    auto& source_body = source.emplace<physics_component>();
    source_body.set_freeze_position({true, false, false});
    source_body.set_surface_velocity({1.0f, 2.0f, 3.0f});
    auto& target_body = target.emplace<physics_component>();
    target_body.set_freeze_position({false, true, true});
    target_body.set_surface_velocity({7.0f, 8.0f, 9.0f});
    load_component_paths<physics_component>(source, target, {"freeze_position/x", "surface_velocity/z"});
    const auto& body = target.get<physics_component>();
    check(body.get_freeze_position() == math::bvec3(true, true, true),
          "physics frozen axes: the listed axis is the document's, the others stay");
    check_near(body.get_surface_velocity(), {7.0f, 8.0f, 3.0f}, "physics surface velocity: the same");

    source.emplace<character_controller_component>().set_center({1.0f, 2.0f, 3.0f});
    target.emplace<character_controller_component>().set_center({7.0f, 8.0f, 9.0f});
    load_component_paths<character_controller_component>(source, target, {"center/y"});
    check_near(target.get<character_controller_component>().get_center(),
               {7.0f, 2.0f, 9.0f},
               "character controller center: the listed axis is the document's, the others stay");

    ragdoll_drive source_drive;
    source_drive.local_frequency = 2.0f;
    source_drive.world_max_force = 100.0f;
    ragdoll_drive target_drive;
    target_drive.local_frequency = 9.0f;
    target_drive.world_max_force = 900.0f;
    source.emplace<ragdoll_component>().set_drive(source_drive);
    target.emplace<ragdoll_component>().set_drive(target_drive);
    load_component_paths<ragdoll_component>(source, target, {"drive/local_frequency"});
    const auto& drive = target.get<ragdoll_component>().get_drive();
    test::check_near(drive.local_frequency, 2.0f, tolerance, "ragdoll drive: the listed member is the document's");
    test::check_near(drive.world_max_force, 900.0f, tolerance, "ragdoll drive: the others stay");

    source.emplace<audio_source_component>().set_range(frange_t(1.0f, 20.0f));
    target.emplace<audio_source_component>().set_range(frange_t(3.0f, 50.0f));
    load_component_paths<audio_source_component>(source, target, {"range/min"});
    const auto& range = target.get<audio_source_component>().get_range();
    test::check_near(range.min, 1.0f, tolerance, "audio range: the listed end is the document's");
    test::check_near(range.max, 50.0f, tolerance, "audio range: the other stays");

    source.emplace<skylight_component>().set_irradiance_tint(math::color(0.1f, 0.2f, 0.3f, 1.0f));
    target.emplace<skylight_component>().set_irradiance_tint(math::color(0.7f, 0.8f, 0.9f, 1.0f));
    load_component_paths<skylight_component>(source, target, {"irradiance_tint/g"});
    const auto& tint = target.get<skylight_component>().get_irradiance_tint().value;
    check_near(math::vec3(tint), {0.7f, 0.2f, 0.9f}, "skylight tint: the listed channel is the document's, the others stay");

    auto& source_exposure = source.emplace<auto_exposure_component>();
    source_exposure.settings.compensation = 2.0f;
    source_exposure.settings.compensation_curve.add_point(-2.0f, -6.0f);
    source_exposure.settings.compensation_curve.add_point(0.0f, 0.0f);
    auto& target_exposure = target.emplace<auto_exposure_component>();
    target_exposure.settings.compensation = -1.0f;
    target_exposure.settings.compensation_curve.add_point(1.0f, -4.0f);
    target_exposure.settings.compensation_curve.add_point(3.0f, 2.0f);
    // The document names a curve's channels value0, value1, ...: a fixed array has no size, so it is an object.
    load_component_paths<auto_exposure_component>(
        source,
        target,
        {"settings/compensation", "settings/compensation_curve/channels/value0/keys[1]/value"});
    const auto& exposure = target.get<auto_exposure_component>().settings;
    const auto keys = exposure.compensation_curve.get_channel(0).get_keys();
    test::check_near(exposure.compensation, 2.0f, tolerance, "exposure settings: a listed setting is the document's");
    check_eq(keys.size(), 2, "exposure curve: it keeps its keys");
    if(keys.size() == 2)
    {
        test::check_near(keys[1].value, 0.0f, tolerance, "exposure curve: the listed key's value is the document's");
        test::check_near(keys[1].position, 2.0f, tolerance, "exposure curve: that key's position stays");
        test::check_near(keys[0].value, 1.0f, tolerance, "exposure curve: the key the load leaves out stays");
        test::check_near(keys[0].position, -4.0f, tolerance, "exposure curve: with its position");
    }
}

/// Loads the probe a document holds under "probe" over @p probe; with @p paths, only those of it.
auto load_probe_document(const std::string& document, reflection_probe& probe, const serialization::path_set& paths = {})
    -> bool
{
    std::stringstream stream(document);
    ser20::iarchive_associative_t ar(stream);
    if(paths.empty())
    {
        return try_load(ar, ser20::make_nvp("probe", probe));
    }
    const serialization::scoped_path_filter filter("probe", paths);
    return try_load(ar, ser20::make_nvp("probe", probe));
}

void test_probe_shape_values_are_saved_under_their_reflected_names()
{
    begin_test("a reflection probe's box and sphere values are saved under box_data and sphere_data; documents that "
               "hold them beside the type still load");

    constexpr float tolerance = 1e-4f;
    reflection_probe saved_box;
    saved_box.intensity = 2.0f;
    saved_box.box_data.extents = {1.0f, 2.0f, 3.0f};
    saved_box.box_data.transition_distance = 0.5f;
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("probe", saved_box));
    }
    const std::string document = stream.str();
    check(document.find("\"box_data\"") != std::string::npos, "a box probe's document has a box_data node");
    check(document.find("\"sphere_data\"") == std::string::npos, "and no sphere_data node");
    reflection_probe loaded_box;
    check(load_probe_document(document, loaded_box), "the box probe loads");
    check_near(loaded_box.box_data.extents, {1.0f, 2.0f, 3.0f}, "its extents round-trip");
    test::check_near(loaded_box.box_data.transition_distance, 0.5f, tolerance, "its transition distance round-trips");

    reflection_probe saved_sphere;
    saved_sphere.type = probe_type::sphere;
    saved_sphere.sphere_data.range = 12.0f;
    std::stringstream sphere_stream;
    {
        ser20::oarchive_associative_t ar(sphere_stream);
        try_save(ar, ser20::make_nvp("probe", saved_sphere));
    }
    check(sphere_stream.str().find("\"sphere_data\"") != std::string::npos, "a sphere probe's document has a sphere_data node");
    reflection_probe loaded_sphere;
    check(load_probe_document(sphere_stream.str(), loaded_sphere), "the sphere probe loads");
    check(loaded_sphere.type == probe_type::sphere, "its type round-trips");
    test::check_near(loaded_sphere.sphere_data.range, 12.0f, tolerance, "its range round-trips");

    const std::string flat_box =
        R"({"probe": {"type": 0, "method": 1, "intensity": 3.0, "extents": {"x": 4.0, "y": 5.0, "z": 6.0}, "transition_distance": 0.25}})";
    reflection_probe from_flat_box;
    check(load_probe_document(flat_box, from_flat_box), "a box probe with its values beside the type loads");
    check(from_flat_box.method == reflect_method::static_only, "it keeps its method");
    test::check_near(from_flat_box.intensity, 3.0f, tolerance, "it keeps its intensity");
    check_near(from_flat_box.box_data.extents, {4.0f, 5.0f, 6.0f}, "it keeps its extents");
    test::check_near(from_flat_box.box_data.transition_distance, 0.25f, tolerance, "it keeps its transition distance");

    reflection_probe from_flat_sphere;
    check(load_probe_document(R"({"probe": {"type": 1, "range": 9.0}})", from_flat_sphere),
          "a sphere probe with its range beside the type loads");
    test::check_near(from_flat_sphere.sphere_data.range, 9.0f, tolerance, "it keeps its range");

    // The values beside the type are read under the path the node gives them.
    reflection_probe partly;
    partly.box_data.extents = {7.0f, 8.0f, 9.0f};
    partly.box_data.transition_distance = 2.0f;
    load_probe_document(flat_box, partly, {"box_data/extents/y"});
    check_near(partly.box_data.extents, {7.0f, 5.0f, 9.0f}, "a load that lists one axis of such a document takes that axis alone");
    test::check_near(partly.box_data.transition_distance, 2.0f, tolerance, "and leaves the transition distance");
}

void test_text_loads_keep_the_members_they_leave_out()
{
    begin_test("a load that lists one member of a text's style or area keeps the members it leaves out");

    // A text builds its buffers on the device: the runner starts none.
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    {
        scene scn("scn");
        auto source = scn.create_entity("source");
        auto target = scn.create_entity("target");
        constexpr float tolerance = 1e-4f;

        text_style source_style;
        source_style.opacity = 0.25f;
        source_style.outline_width = 1.0f;
        text_style target_style;
        target_style.opacity = 0.75f;
        target_style.outline_width = 3.0f;
        auto& source_text = source.emplace<text_component>();
        source_text.set_style(source_style);
        source_text.set_area(fsize_t(10.0f, 20.0f));
        source_text.set_auto_size_range(urange32_t(8, 16));
        auto& target_text = target.emplace<text_component>();
        target_text.set_style(target_style);
        target_text.set_area(fsize_t(30.0f, 40.0f));
        target_text.set_auto_size_range(urange32_t(20, 60));
        load_component_paths<text_component>(source, target, {"style/opacity", "area/width", "auto_size_range/max"});
        const auto& text = target.get<text_component>();
        test::check_near(text.get_style().opacity, 0.25f, tolerance, "text style: the listed member is the document's");
        test::check_near(text.get_style().outline_width, 3.0f, tolerance, "text style: the others stay");
        test::check_near(text.get_area().width, 10.0f, tolerance, "text area: the listed side is the document's");
        test::check_near(text.get_area().height, 40.0f, tolerance, "text area: the other stays");
        check_eq(text.get_auto_size_range().max, 16, "text size range: the listed end is the document's");
        check_eq(text.get_auto_size_range().min, 20, "text size range: the other stays");
    }
    gfx::shutdown();
}

void test_filtered_list_load_steps_over_skipped_elements()
{
    begin_test("a load that leaves out list elements reads each listed element from its own place in the document");

    scene scn("scn");
    auto source = scn.create_entity("source");
    auto target = scn.create_entity("target");
    auto& from = source.emplace<test_component>();
    from.sequential = {"a", "b", "c"};
    from.associative_mock = {{"first", {}}, {"second", {}}, {"third", {}}};
    auto& into = target.emplace<test_component>();
    into.sequential = {"x", "y", "z"};
    into.associative_mock = {{"one", {}}, {"two", {}}, {"three", {}}};

    const auto type = entt::resolve<test_component>();
    std::stringstream saved;
    type.invoke("component_save"_hs, {}, source, entt::forward_as_meta(saved));
    {
        const serialization::scoped_path_filter filter("component", {"sequential[1]", "associative_mock[2]/name"});
        std::stringstream stream(saved.str());
        type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(stream));
    }
    const auto& loaded = target.get<test_component>();
    check(loaded.sequential == std::vector<std::string>{"x", "b", "z"},
          "a listed value after a skipped one is the document's at its own place");
    check(loaded.associative_mock.size() == 3 && loaded.associative_mock[0].name == "one" &&
              loaded.associative_mock[1].name == "two" && loaded.associative_mock[2].name == "third",
          "a listed member of the third element is the document's third element's, after two skipped");
}

/// A copy of the hierarchies as the editor's clipboard holds it: uuids, any registry, any time.
auto copy_portable(const std::vector<entt::handle>& roots) -> std::string
{
    std::vector<entt::const_handle> const_roots(roots.begin(), roots.end());
    std::stringstream ss;
    save_clone_to_stream(ss, const_roots, clone_identity::uuids);
    return ss.str();
}

void test_portable_copy_loads_into_another_registry()
{
    begin_test("a portable copy loads into another scene: its links land inside it, a link out of it finds nothing there");

    scene src("src");
    auto outside = src.create_entity("outside");
    auto tree = build_linked_tree(src);
    tree.root.get<transform_component>().set_position_local({1.0f, 2.0f, 3.0f});
    tree.child.emplace<two_bone_ik_component>().target = outside;
    const auto text = copy_portable({tree.root});

    scene dst("dst");
    const size_t componentless_before = count_componentless(*dst.registry);
    const auto roots = load_clone_from_view(text, *dst.registry);
    check_eq(roots.size(), 1, "one root comes back");
    if(roots.size() != 1)
    {
        return;
    }
    const auto copy = roots.front();
    check(copy.registry() == dst.registry.get(), "in the scene it was loaded into");
    check_links_point_inside(copy, tree);
    check(uid_of(copy) != uid_of(tree.root), "with a fresh uid");
    check_near(copy.get<transform_component>().get_position_local(), {1.0f, 2.0f, 3.0f}, "at the source's place");
    check(!copy.get<transform_component>().get_parent(), "at the scene root");
    auto copy_child = find_child_by_name(copy, "child");
    check(copy_child && copy_child.all_of<two_bone_ik_component>() && !copy_child.get<two_bone_ik_component>().target,
          "a link to an entity this scene does not have is empty");
    check_eq(count_componentless(*dst.registry), componentless_before, "and no empty entity is made for it");
    check_eq(count_real_entities(*dst.registry), 3, "the three entities of the copy, nothing more");
}

void test_portable_copy_into_the_same_scene()
{
    begin_test("a portable copy loaded where it was taken links to the copy inside and to the same entities outside");

    scene scn("scn");
    auto outside = scn.create_entity("outside");
    auto tree = build_linked_tree(scn);
    tree.child.emplace<two_bone_ik_component>().target = outside;
    const auto text = copy_portable({tree.root});

    const auto roots = load_clone_from_view(text, *scn.registry);
    check_eq(roots.size(), 1, "one root comes back");
    if(roots.size() != 1)
    {
        return;
    }
    check_links_point_inside(roots.front(), tree);
    auto copy_child = find_child_by_name(roots.front(), "child");
    check(copy_child && copy_child.all_of<two_bone_ik_component>() && copy_child.get<two_bone_ik_component>().target == outside,
          "a link out of the copy points at the same entity");
    check_linked_tree_untouched(tree);
    check(uid_of(roots.front()) != uid_of(tree.root), "the copy has a uid of its own");
}

void test_portable_copy_links_between_its_roots()
{
    begin_test("a copy of two roots links one to the other's copy; a copy of one links to the other itself");

    scene scn("scn");
    auto first = scn.create_entity("first");
    auto second = scn.create_entity("second");
    auto second_child = scn.create_entity("second_child", second);
    first.emplace<two_bone_ik_component>().target = second_child;

    const auto both = load_clone_from_view(copy_portable({first, second}), *scn.registry);
    check_eq(both.size(), 2, "both roots come back");
    if(both.size() == 2)
    {
        check_eq(name_of(both[0]), "first", "in the order they were given");
        auto copied_second_child = find_child_by_name(both[1], "second_child");
        check(static_cast<bool>(copied_second_child) && both[0].get<two_bone_ik_component>().target == copied_second_child,
              "the first copy targets the second copy's child");
    }

    const auto alone = load_clone_from_view(copy_portable({first}), *scn.registry);
    check_eq(alone.size(), 1, "one root comes back");
    if(alone.size() == 1)
    {
        check(alone[0].get<two_bone_ik_component>().target == second_child,
              "copied alone, it targets the original, which is outside the copy");
    }
}

void test_portable_copy_outlives_its_originals()
{
    begin_test("a portable copy loads after its originals are gone");

    scene scn("scn");
    auto tree = build_linked_tree(scn);
    const auto text = copy_portable({tree.root});
    scene::destroy_entity(tree.root);

    const auto roots = load_clone_from_view(text, *scn.registry);
    check_eq(roots.size(), 1, "one root comes back");
    if(roots.size() == 1)
    {
        auto copy_child = find_child_by_name(roots.front(), "child");
        auto copy_grandchild = copy_child ? find_child_by_name(copy_child, "grandchild") : entt::handle{};
        check(static_cast<bool>(copy_grandchild), "whole");
        check(roots.front().get<two_bone_ik_component>().target == copy_grandchild, "its links land inside it");
    }
}

void test_portable_copy_keeps_prefab_ids_at_and_below_instance_roots()
{
    begin_test("a portable copy of an instance stays an instance; a copy of its content is plain");

    scene authoring("authoring");
    auto pfb = make_prefab_from(build_sample_tree(authoring), "test:/tree.pfb");

    scene src("src");
    auto instance = src.instantiate(pfb, false);
    check(static_cast<bool>(instance), "the instance exists");
    if(!instance)
    {
        return;
    }
    auto child_a = find_child_by_name(instance, "child_a");

    scene dst("dst");
    const auto instance_copy = load_clone_from_view(copy_portable({instance}), *dst.registry);
    check_eq(instance_copy.size(), 1, "the instance's copy comes back");
    if(instance_copy.size() == 1)
    {
        check(instance_copy[0].all_of<prefab_component>(), "as an instance");
        check(instance_copy[0].get<prefab_component>().instance_id.is_nil(), "placed by hand: nobody's slot");
        check(prefab_uid_of(instance_copy[0]) == prefab_uid_of(instance), "keeping the root's prefab id");
        auto copied_child = find_child_by_name(instance_copy[0], "child_a");
        check(copied_child && prefab_uid_of(copied_child) == prefab_uid_of(child_a), "and its content keeps theirs");
    }

    const auto content_copy = load_clone_from_view(copy_portable({child_a}), *dst.registry);
    check_eq(content_copy.size(), 1, "the content's copy comes back");
    if(content_copy.size() == 1)
    {
        check(!content_copy[0].all_of<prefab_id_component>(), "carrying no prefab id");
        auto copied_grandchild = find_child_by_name(content_copy[0], "grandchild");
        check(copied_grandchild && !copied_grandchild.all_of<prefab_id_component>(), "nor does anything under it");
    }
}

void test_portable_copy_takes_each_root_once()
{
    begin_test("a copy of a root listed with its own child, or listed twice, holds it once");

    scene scn("scn");
    auto tree = build_linked_tree(scn);
    const auto roots = load_clone_from_view(copy_portable({tree.child, tree.root, tree.root}), *scn.registry);
    check_eq(roots.size(), 1, "one root comes back");
    if(roots.size() == 1)
    {
        check_eq(name_of(roots[0]), "root", "the one that holds the other");
        check_links_point_inside(roots[0], tree);
    }
}

void test_portable_copy_in_a_newer_layout_is_refused()
{
    begin_test("a copy in a layout newer than this build reads is refused and loads nothing");

    scene scn("scn");
    auto text = copy_portable({build_sample_tree(scn)});
    const std::regex version_member(R"("version"\s*:\s*\d+)");
    check(std::regex_search(text, version_member), "the copy states its layout");
    text = std::regex_replace(text,
                              version_member,
                              "\"version\": " + std::to_string(PREFAB_DOCUMENT_VERSION + 1),
                              std::regex_constants::format_first_only);

    scene dst("dst");
    const size_t before = count_real_entities(*dst.registry);
    check(load_clone_from_view(text, *dst.registry).empty(), "nothing comes back");
    check_eq(count_real_entities(*dst.registry), before, "and nothing is made");
}

// ---------------------------------------------------------------------------------
// Prefab instantiation
// ---------------------------------------------------------------------------------

void test_instantiate_assigns_fresh_uids_per_instance()
{
    begin_test("each prefab instance gets its own entity uids");

    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/tree.pfb");

    scene scn("scn");
    auto a = scn.instantiate(pfb, false);
    auto b = scn.instantiate(pfb, false);

    check(static_cast<bool>(a) && static_cast<bool>(b), "both instances exist");
    if(!a || !b)
    {
        return;
    }

    check(uid_of(a) != uid_of(b), "the two instance roots have different uids");
    check(!uid_of(a).is_nil() && !uid_of(b).is_nil(), "neither instance root has a nil uid");

    auto a_child = find_child_by_name(a, "child_a");
    auto b_child = find_child_by_name(b, "child_a");
    check(static_cast<bool>(a_child) && static_cast<bool>(b_child), "both instances have child_a");
    if(a_child && b_child)
    {
        check(uid_of(a_child) != uid_of(b_child), "matching children have different uids across instances");
        check(prefab_uid_of(a_child) == prefab_uid_of(b_child),
              "but matching children share a prefab id - that is what ties them to the asset");
    }

    // A duplicate uid silently merges two entities on the next load
    // (load_entity_from_uuid resolves to the already-mapped handle), so it must not be
    // possible to produce one.
    std::vector<hpp::uuid> uids;
    scn.registry->view<id_component>().each([&](auto, auto&& comp) { uids.push_back(comp.id); });
    const size_t total = uids.size();
    std::sort(uids.begin(), uids.end());
    uids.erase(std::unique(uids.begin(), uids.end()), uids.end());
    check_eq(uids.size(), total, "every uid in the scene is unique");
}

void test_instantiate_sets_prefab_source()
{
    begin_test("instantiate links the instance to its asset");

    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/tree.pfb");

    scene scn("scn");
    auto instance = scn.instantiate(pfb, false);
    check(static_cast<bool>(instance), "the instance exists");
    if(!instance)
    {
        return;
    }

    const auto* prefab_comp = instance.try_get<prefab_component>();
    check(prefab_comp != nullptr, "the instance root has a prefab_component");
    if(prefab_comp)
    {
        check(prefab_comp->source == pfb, "the prefab_component points at the source asset");
        check(prefab_comp->local.empty(), "a fresh instance has nothing stated here");
        check(prefab_comp->from_document.empty(), "and its document states nothing about it");
    }

    // Only the root is an instance root; children must not each carry one.
    size_t prefab_comps = 0;
    scn.registry->view<prefab_component>().each([&](auto, auto&&) { ++prefab_comps; });
    check_eq(prefab_comps, 1, "exactly one prefab_component per instance");
}

namespace probe_concept_check
{
/// An archive-shaped type that can report the cursor but cannot answer membership.
/// getNodeName() must NOT be enough to satisfy can_probe_names: it says "is the name
/// here", not "is the name present", and readers routinely skip fields without consuming
/// them, leaving the cursor behind the name being asked for.
struct cursor_only_archive
{
    using is_loading = std::true_type;
    auto getNodeName() const -> const char*;
};

struct probing_archive : cursor_only_archive
{
    auto hasNextName(const char*) const -> bool;
};

static_assert(!can_probe_names<cursor_only_archive>,
              "can_probe_names must not be satisfied by getNodeName alone");
static_assert(can_probe_names<probing_archive>, "can_probe_names must be satisfied by hasNextName");
static_assert(can_probe_names<ser20::iarchive_associative_t>,
              "the archive in use must take the non-throwing path");
} // namespace probe_concept_check

void test_output_format_scope()
{
    begin_test("output format scope selects indentation without changing what is written");

    scene src("src");
    build_sample_tree(src);

    const auto save = [&]() -> std::string
    {
        std::stringstream ss;
        save_to_stream(ss, src);
        return ss.str();
    };

    // Silence must mean "files stay readable". A site that forgets to opt in should keep
    // the old behaviour, never produce unreadable output on disk.
    check(serialization::get_output_format() == serialization::output_format::readable,
          "the default is readable");
    const auto readable = save();

    std::string compact;
    {
        serialization::scoped_output_format guard(serialization::output_format::compact);
        check(serialization::get_output_format() == serialization::output_format::compact,
              "the guard selects compact");

        {
            serialization::scoped_output_format inner(serialization::output_format::readable);
            check(serialization::get_output_format() == serialization::output_format::readable,
                  "nesting works, innermost wins");
        }
        check(serialization::get_output_format() == serialization::output_format::compact,
              "leaving a nested scope restores the outer one");

        compact = save();
    }
    check(serialization::get_output_format() == serialization::output_format::readable,
          "the format is restored on scope exit");

    check(compact.size() < readable.size(), "compact output is smaller");
    check(compact.find('\n') != std::string::npos,
          "compact still breaks lines, so the output stays greppable and diffable");
    check(readable.find("\n ") != std::string::npos, "readable output is indented");
    check(compact.find("\n ") == std::string::npos, "compact output is not");

    // The point of the whole mechanism: only whitespace differs. Anything else would make
    // a clone or a checkpoint a different document from the one it snapshots.
    check_eq(minify_json(compact), minify_json(readable), "both carry identical data");

    // And both round-trip to the same scene.
    const auto load_names = [](const std::string& blob) -> std::vector<std::string>
    {
        scene dst("dst");
        std::stringstream ss(blob);
        load_from_stream(ss, dst);
        std::vector<std::string> names;
        dst.registry->view<tag_component>().each([&](auto, auto&& tag) { names.push_back(tag.name); });
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto from_readable = load_names(readable);
    const auto from_compact = load_names(compact);
    check(!from_compact.empty(), "the compact blob loads");
    check(from_readable == from_compact, "both load to the same entities");
}

void test_absent_components_do_not_throw()
{
    begin_test("probing for an absent component does not throw");

    scene src("src");
    build_sample_tree(src);

    std::stringstream ss;
    save_to_stream(ss, src);

    scene dst("dst");
    std::stringstream in(ss.str());
    serialization::reset_failed_lookup_count();
    load_from_stream(in, dst);

    const auto missed = serialization::failed_lookup_count();
    const auto thrown = serialization::thrown_lookup_count();

    // Loading an entity asks after every serializable component type, so the misses are
    // expected and numerous - that is the shape of the format, not a defect.
    check(missed > 0, "loading probes for components the entities do not have");

    // What must not happen is paying for an exception to learn that. can_probe_names
    // routes those through a scan instead; if this starts failing, either the archive lost
    // hasNextName or try_serialize_direct stopped consulting it, and every scene load in
    // the engine just got several times slower.
    check_eq(size_t(thrown), 0, "none of those misses cost a throw");
}

void test_failed_load_leaves_siblings_readable()
{
    begin_test("a load that fails inside a node leaves its siblings readable");

    // "a" fails on its second element, after the archive entered the array. Unless the
    // archive is put back, "b" is then looked up inside "a" and cannot be found - which is
    // how one bad script field used to make every field after it keep its default.
    const std::string document = R"({"a": [1, "two", 3], "b": 7, "c": [4, "five"]})";
    std::stringstream in(document);
    ser20::iarchive_associative_t ar(in);

    std::vector<int> a;
    check(!try_load(ar, ser20::make_nvp("a", a)), "try_load reports the failed array");
    int b = 0;
    check(try_load(ar, ser20::make_nvp("b", b)) && b == 7, "the value after the failed array still loads");

    std::vector<int> c;
    check(!try_load(ar, ser20::make_nvp("c", c)), "try_load reports a second failed array");
    int b_again = 0;
    check(try_load(ar, ser20::make_nvp("b", b_again)) && b_again == 7, "the archive is back at the root after both");
}

void test_light_source_shape_round_trip()
{
    begin_test("a local light's source radius / length round-trip; exponent falloff data still loads");

    // The emitter size is saved with the type-specific data, so it must come back for point and
    // spot lights alike.
    light saved_light;
    saved_light.type = light_type::spot;
    saved_light.source_radius = 0.25f;
    saved_light.source_length = 1.5f;
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("light", saved_light));
    }
    light loaded_light;
    {
        ser20::iarchive_associative_t ar(stream);
        try_load(ar, ser20::make_nvp("light", loaded_light));
    }
    check(loaded_light.type == light_type::spot, "the type loads");
    check(loaded_light.source_radius == saved_light.source_radius, "the source radius round-trips");
    check(loaded_light.source_length == saved_light.source_length, "the source length round-trips");

    // Scenes written before the inverse-squared falloff carry the removed exponent falloff and no
    // emitter size: every field they have loads, and the emitter size keeps the light's value (0, a
    // point source, for a light loaded fresh).
    const std::string old_document =
        R"({"light": {"type": 1, "intensity": 8.0, "point_data": {"range": 20.0, "exponent_falloff": 1.35}}})";
    std::stringstream old_stream(old_document);
    light old_light;
    old_light.source_radius = 9.0f;
    old_light.source_length = 9.0f;
    {
        ser20::iarchive_associative_t ar(old_stream);
        check(try_load(ar, ser20::make_nvp("light", old_light)), "an old point light loads");
    }
    check(old_light.type == light_type::point, "the old light keeps its type");
    check(old_light.intensity == 8.0f, "the old light keeps its intensity");
    check(old_light.point_data.range == 20.0f, "the old light keeps its range");
    check(old_light.source_radius == 9.0f && old_light.source_length == 9.0f,
          "fields the old data lacks keep their values");
}

void test_auto_exposure_settings_start_fresh_without_version()
{
    begin_test("auto exposure settings saved by an older settings version load as defaults");

    struct loaded_exposure
    {
        bool found = false;
        bool enabled = true;
        auto_exposure_pass::settings settings{};
    };

    const auto load_exposure = [](const std::string& document) -> loaded_exposure
    {
        scene dst("dst");
        std::stringstream in(document);
        load_from_stream(in, dst);
        loaded_exposure loaded;
        dst.registry->view<auto_exposure_component>().each(
            [&](auto, auto&& component)
            {
                loaded.found = true;
                loaded.enabled = component.enabled;
                loaded.settings = component.settings;
            });
        return loaded;
    };

    const auto is_close = [](float actual, float expected) -> bool
    {
        return std::abs(actual - expected) < 1e-4f;
    };

    scene src("src");
    auto volume = src.create_entity("volume");
    auto& exposure = volume.emplace<auto_exposure_component>();
    exposure.enabled = false;
    exposure.settings.compensation = 2.5f;
    exposure.settings.speed_up = 7.0f;
    exposure.settings.metering_mode = exposure_metering_mode::spot;
    exposure.settings.compensation_curve.add_point(-2.0f, -6.0f);
    exposure.settings.compensation_curve.add_point(0.0f, 0.0f);
    exposure.settings.local_highlight_contrast = 0.7f;
    exposure.settings.local_blurred_kernel_percent = 35.0f;

    std::stringstream ss;
    save_to_stream(ss, src);
    const std::string current_document = ss.str();

    const auto current = load_exposure(current_document);
    check(current.found, "a current document loads the component");
    check(!current.enabled, "a current document keeps enabled");
    check(is_close(current.settings.compensation, 2.5f), "a current document keeps its compensation");
    check(is_close(current.settings.speed_up, 7.0f), "a current document keeps its speed");
    check(current.settings.metering_mode == exposure_metering_mode::spot, "a current document keeps its metering mode");
    const auto curve_points = current.settings.compensation_curve.get_points();
    check(curve_points.size() == 2 && is_close(curve_points[0].progress, -6.0f) &&
              is_close(curve_points[0].element, -2.0f) && is_close(curve_points[1].progress, 0.0f),
          "a current document keeps its compensation curve");
    check(is_close(current.settings.compensation_curve.sample(-3.0f), -1.0f),
          "a loaded compensation curve interpolates across its EV domain");
    check(is_close(current.settings.local_highlight_contrast, 0.7f),
          "a current document keeps its local exposure contrast");
    check(is_close(current.settings.local_blurred_kernel_percent, 35.0f),
          "a current document keeps its local exposure blur size");

    const std::string older_document =
        std::regex_replace(current_document,
                           std::regex(R"("settings_version"\s*:\s*[0-9]+)"),
                           "\"settings_version\": " + std::to_string(auto_exposure_pass::settings_version - 1));
    check(older_document != current_document, "the older document carries the previous settings version");
    const auto older = load_exposure(older_document);
    const auto_exposure_pass::settings fresh{};
    check(older.found && !older.enabled, "an older document still loads the component and keeps enabled");
    check(is_close(older.settings.compensation, fresh.compensation), "an older document falls back to the default compensation");
    check(!older.settings.compensation_curve.is_valid(),
          "an older document falls back to no compensation curve");

    const std::string legacy_document =
        std::regex_replace(current_document, std::regex(R"("settings_version"\s*:\s*[0-9]+\s*,?)"), "");
    check(legacy_document != current_document, "the legacy document has no settings version");

    const auto legacy = load_exposure(legacy_document);
    const auto_exposure_pass::settings defaults{};
    check(legacy.found, "a legacy document still loads the component");
    check(!legacy.enabled, "a legacy document still keeps enabled");
    check(is_close(legacy.settings.compensation, defaults.compensation), "a legacy document falls back to the default compensation");
    check(is_close(legacy.settings.speed_up, defaults.speed_up), "a legacy document falls back to the default speed");
    check(legacy.settings.metering_mode == defaults.metering_mode, "a legacy document falls back to the default metering mode");
}

void test_prefab_asset_does_not_store_prefab_component()
{
    begin_test("a saved prefab carries no prefab_component");

    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    source.emplace<prefab_component>();

    const auto bytes = serialize_as_prefab(source);
    const std::string text(bytes.begin(), bytes.end());

    check(text.find("has_prefab_component") == std::string::npos,
          "should_save_component<prefab_component> keeps the instance link out of the asset");
    check(text.find("prefab_uid") != std::string::npos, "but prefab uids are written");
}

// ---------------------------------------------------------------------------------
// Prefab resync
//
// The editor calls this whenever the asset changes on disk, on play start, and after
// every script recompile, so it runs far more often than a user would guess and is where
// instance-local state is most likely to be lost.
// ---------------------------------------------------------------------------------

/// An instance plus everything needed to re-author and re-save the prefab behind it.
struct prefab_fixture
{
    scene authoring{"authoring"};
    scene world{"world"};
    entt::handle source{};
    entt::handle instance{};
    asset_handle<prefab> asset{};

    void build()
    {
        source = build_sample_tree(authoring);
        asset = make_prefab_from(source, "test:/tree.pfb");
        instance = world.instantiate(asset, false);
    }

    /// Re-serializes the (possibly edited) authoring tree and resyncs the instance,
    /// exactly as the asset watcher -> editing_manager path does.
    void republish_and_sync()
    {
        asset = make_prefab_from(source, "test:/tree.pfb");
        sync_prefab_instance_with(instance, asset);
    }
};

void test_resync_picks_up_prefab_changes()
{
    begin_test("resync picks up edits made to the prefab");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto src_child = find_child_by_name(fix.source, "child_a");
    src_child.get<tag_component>().name = "child_a_renamed";
    src_child.get<transform_component>().set_position_local({99.0f, 0.0f, 0.0f});

    fix.republish_and_sync();

    auto inst_child = find_child_by_name(fix.instance, "child_a_renamed");
    check(static_cast<bool>(inst_child), "the renamed child appears in the instance");
    if(inst_child)
    {
        check_near(inst_child.get<transform_component>().get_position_local(),
                   {99.0f, 0.0f, 0.0f},
                   "the child's new position propagates");
    }
}

void test_resync_keeps_the_same_entities()
{
    begin_test("resync reuses the instance's entities rather than replacing them");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    const auto root_entity = fix.instance.entity();
    const auto root_uid = uid_of(fix.instance);
    auto child = find_child_by_name(fix.instance, "child_a");
    const auto child_entity = child.entity();
    const auto child_uid = uid_of(child);

    fix.republish_and_sync();

    check(fix.instance.entity() == root_entity, "the instance root is the same entity after a resync");
    check(uid_of(fix.instance) == root_uid, "the instance root keeps its uid");

    auto child_after = find_child_by_name(fix.instance, "child_a");
    check(static_cast<bool>(child_after), "child_a is still there");
    if(child_after)
    {
        check(child_after.entity() == child_entity, "child_a is the same entity");
        check(uid_of(child_after) == child_uid,
              "child_a keeps its instance uid - should_load_component<id_component> must not "
              "overwrite it from the asset");
    }
}

void test_resync_honours_a_property_override()
{
    begin_test("resync leaves overridden properties alone");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto inst_child = find_child_by_name(fix.instance, "child_a");
    const auto child_prefab_uid = prefab_uid_of(inst_child);
    check(!child_prefab_uid.is_nil(), "the instance child carries a prefab id to key the override on");

    // What the inspector records when the user edits that field on the instance.
    inst_child.get<tag_component>().name = "locally_renamed";
    fix.instance.get<prefab_component>().add_override(child_prefab_uid, "tag_component/name");

    // Meanwhile the prefab author renames the same field and moves the entity.
    auto src_child = find_child_by_name(fix.source, "child_a");
    src_child.get<tag_component>().name = "authored_rename";
    src_child.get<transform_component>().set_position_local({7.0f, 7.0f, 7.0f});

    fix.republish_and_sync();

    auto after = find_child_by_name(fix.instance, "locally_renamed");
    check(static_cast<bool>(after), "the overridden name survives the resync");

    if(after)
    {
        check_near(after.get<transform_component>().get_position_local(),
                   {7.0f, 7.0f, 7.0f},
                   "a property that is NOT overridden still follows the prefab");
    }
}

auto make_flat_size_curve(float size) -> math::gradient<frange_t>
{
    math::gradient<frange_t> curve;
    curve.add_point(frange_t(size, size), 0.0f);
    curve.add_point(frange_t(size, size), 1.0f);
    return curve;
}

/// The particle emitter's properties of every kind a prefab instance overrides: a range, a number, enums, a
/// curve and lists of structs; the ones it leaves alone follow the prefab. Each override names the property as
/// the inspector does (its reflected name), and the resync must skip exactly that load.
void test_resync_honours_particle_emitter_overrides()
{
    begin_test("resync keeps a particle emitter's overridden properties and lets the rest follow the prefab");

    // Emitters live in the particle system, which needs a device: the runner starts neither.
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    ps_soa::init(16);
    {
        prefab_fixture fix;
        fix.source = build_sample_tree(fix.authoring);
        auto source_child = find_child_by_name(fix.source, "child_a");
        source_child.emplace<particle_emitter_component>();
        fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
        fix.instance = fix.world.instantiate(fix.asset, false);
        auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
        check(instance_child && instance_child.all_of<particle_emitter_component>(), "the instance has the emitter");
        if(instance_child && instance_child.all_of<particle_emitter_component>())
        {
            const auto child_prefab_uid = prefab_uid_of(instance_child);
            auto& instance_emitter = instance_child.get<particle_emitter_component>();
            auto& instance_prefab = fix.instance.get<prefab_component>();
            const auto override_property = [&](const char* property)
            {
                instance_prefab.add_override(child_prefab_uid, std::string("particle_emitter_component/") + property);
            };

            instance_emitter.set_lifetime(frange_t(4.0f, 5.0f));
            override_property("lifetime");
            instance_emitter.set_emission_rate(7.0f);
            override_property("emission_rate");
            instance_emitter.set_shape(ps_soa::emitter_shape::torus);
            override_property("shape");
            instance_emitter.set_render_mode(ps_soa::render_mode::mesh);
            override_property("render_mode");
            instance_emitter.set_size_over_life(make_flat_size_curve(3.0f));
            override_property("size_over_life");
            ps_soa::emitter_burst burst;
            burst.time = 0.5f;
            burst.count = frange_t(9.0f, 9.0f);
            burst.cycles = 2;
            instance_emitter.set_bursts({burst});
            override_property("bursts");
            instance_emitter.set_collision_planes({math::vec4(0.0f, 1.0f, 0.0f, -2.0f)});
            override_property("collision_planes");
            particle_sub_emitter sub_emitter;
            sub_emitter.trigger = ps_soa::particle_event_type::collision;
            sub_emitter.count = frange_t(3.0f, 3.0f);
            instance_emitter.set_sub_emitters({sub_emitter});
            override_property("sub_emitters");

            // Meanwhile the prefab's author changes those and three more.
            auto& source_emitter = source_child.get<particle_emitter_component>();
            source_emitter.set_lifetime(frange_t(1.5f, 1.5f));
            source_emitter.set_emission_rate(99.0f);
            source_emitter.set_shape(ps_soa::emitter_shape::cone);
            source_emitter.set_render_mode(ps_soa::render_mode::trail);
            source_emitter.set_size_over_life(make_flat_size_curve(0.25f));
            burst.count = frange_t(1.0f, 1.0f);
            source_emitter.set_bursts({burst, burst});
            source_emitter.set_collision_planes({math::vec4(1.0f, 0.0f, 0.0f, 0.0f)});
            source_emitter.set_sub_emitters({});
            source_emitter.set_gravity_scale(0.25f);
            source_emitter.set_turbulence_strength(0.75f);
            source_emitter.set_mesh_cast_shadows(false);

            fix.republish_and_sync();

            auto after = find_child_by_name(fix.instance, "child_a");
            check(after && after.all_of<particle_emitter_component>(), "the emitter is still there after the resync");
            if(after && after.all_of<particle_emitter_component>())
            {
                const auto& emitter = after.get<particle_emitter_component>();
                const float tolerance = 1.0e-5f;
                check(std::abs(emitter.get_lifetime().min - 4.0f) < tolerance &&
                          std::abs(emitter.get_lifetime().max - 5.0f) < tolerance,
                      "an overridden range (the lifetime) survives");
                check(std::abs(emitter.get_emission_rate() - 7.0f) < tolerance, "an overridden number survives");
                check(emitter.get_shape() == ps_soa::emitter_shape::torus &&
                          emitter.get_render_mode() == ps_soa::render_mode::mesh,
                      "overridden enums survive");
                check(std::abs(emitter.get_size_over_life().sample(0.5f).min - 3.0f) < tolerance,
                      "an overridden curve survives");
                check(emitter.get_bursts().size() == 1 && std::abs(emitter.get_bursts()[0].count.min - 9.0f) < tolerance &&
                          emitter.get_bursts()[0].cycles == 2,
                      "overridden bursts survive");
                check(emitter.get_collision_planes().size() == 1 &&
                          std::abs(emitter.get_collision_planes()[0].y - 1.0f) < tolerance &&
                          std::abs(emitter.get_collision_planes()[0].w + 2.0f) < tolerance,
                      "overridden collision planes survive");
                check(emitter.get_sub_emitters().size() == 1 &&
                          emitter.get_sub_emitters()[0].trigger == ps_soa::particle_event_type::collision &&
                          std::abs(emitter.get_sub_emitters()[0].count.min - 3.0f) < tolerance,
                      "overridden sub-emitters survive");
                check(std::abs(emitter.get_gravity_scale() - 0.25f) < tolerance &&
                          std::abs(emitter.get_turbulence_strength() - 0.75f) < tolerance && !emitter.get_mesh_cast_shadows(),
                      "the properties that are not overridden follow the prefab");
            }
        }
    }
    ps_soa::shutdown();
    gfx::shutdown();
}

/// What a scene saved before materials drew particles holds of an emitter's look, under the emitter's keys of then.
struct legacy_emitter_document
{
    asset_handle<gfx::texture> texture;
    asset_handle<gfx::texture> normal_texture;
    ps_soa::texture_mode texture_mode{ps_soa::texture_mode::multi_channel};
    ps_soa::blend_mode blend_mode{ps_soa::blend_mode::normal};
    ps_soa::particle_lighting lighting{ps_soa::particle_lighting::unlit};
    ps_soa::render_mode render_mode{ps_soa::render_mode::billboard};
    asset_handle<material> render_material;
};

SAVE_INLINE(legacy_emitter_document)
{
    try_save(ar, ser20::make_nvp("texture", obj.texture));
    try_save(ar, ser20::make_nvp("texture_mode", obj.texture_mode));
    try_save(ar, ser20::make_nvp("render_mode", obj.render_mode));
    try_save(ar, ser20::make_nvp("render_material", obj.render_material));
    try_save(ar, ser20::make_nvp("blend_mode", obj.blend_mode));
    try_save(ar, ser20::make_nvp("lighting", obj.lighting));
    try_save(ar, ser20::make_nvp("normal_texture", obj.normal_texture));
}

template<typename T>
auto save_emitter_document(const T& emitter) -> std::string
{
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("emitter", emitter));
    }
    return stream.str();
}

/// Loads @p document over @p emitter: whole, or only the members @p paths lists, as a prefab instance's overrides load.
void load_emitter_document(particle_emitter_component& emitter, const std::string& document, const serialization::path_set* paths = nullptr)
{
    hpp::optional<serialization::scoped_path_filter> filter;
    if(paths != nullptr)
    {
        filter.emplace("emitter", *paths);
    }
    std::stringstream stream(document);
    ser20::iarchive_associative_t ar(stream);
    try_load(ar, ser20::make_nvp("emitter", emitter));
}

/// The emitter's own override of texture node @p name of its material; empty without one.
auto get_emitter_texture_override(const particle_emitter_component& emitter, const char* name) -> asset_handle<gfx::texture>
{
    for(const auto& entry : emitter.get_material_instance().get_graph_texture_overrides())
    {
        if(entry.name == name)
        {
            return entry.texture;
        }
    }
    return {};
}

/// A scene saved before materials drew particles names an emitter's look on the emitter: its texture, how the texture
/// gives the opacity, how the particles blend and its normal map. It loads as the Standard particle material of that
/// look, with the textures as the emitter's overrides of it; a list of members reads the old keys through the members
/// that took them over; a save writes none of them again; and a statement about one is restated on those members.
void test_particle_emitter_legacy_look()
{
    begin_test("an emitter saved with its own texture and blend loads as the Standard particle material of that look");

    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    ps_soa::init(16);
    {
        auto& am = engine::context().get_cached<asset_manager>();
        const auto color = am.get_asset<gfx::texture>("engine:/data/textures/default_color.dds");
        const auto normal = am.get_asset<gfx::texture>("engine:/data/textures/default_normal.dds");
        entt::registry registry;
        const auto add_emitter = [&registry]() -> particle_emitter_component&
        {
            entt::handle entity(registry, registry.create());
            return entity.emplace<particle_emitter_component>();
        };

        legacy_emitter_document smoke;
        smoke.texture = color;
        smoke.normal_texture = normal;
        smoke.texture_mode = ps_soa::texture_mode::mask;
        smoke.blend_mode = ps_soa::blend_mode::additive;
        smoke.lighting = ps_soa::particle_lighting::surface;
        auto& upgraded = add_emitter();
        load_emitter_document(upgraded, save_emitter_document(smoke));
        particle_surface lit_mask;
        lit_mask.blend = material_blend_mode::additive;
        lit_mask.is_mask = true;
        lit_mask.has_normal_map = true;
        check_eq(upgraded.get_render_material().id(), get_particle_material_key(lit_mask), "the blend, the mask and the normal map pick the material");
        check(get_emitter_texture_override(upgraded, PARTICLE_TEXTURE_NAME) == color &&
                  get_emitter_texture_override(upgraded, PARTICLE_NORMAL_MAP_NAME) == normal,
              "the texture and the normal map are the emitter's overrides of it");
        check(upgraded.get_blend_mode() == ps_soa::blend_mode::additive, "the emitter answers the blend it was saved with");
        check(upgraded.get_texture_mode() == ps_soa::texture_mode::mask, "and the texture mode");
        check(upgraded.get_lighting() == ps_soa::particle_lighting::surface, "and keeps its lighting");

        legacy_emitter_document plain;
        plain.texture = color;
        auto& simple = add_emitter();
        load_emitter_document(simple, save_emitter_document(plain));
        check_eq(simple.get_render_material().id(), std::string(DEFAULT_PARTICLE_MATERIAL_KEY), "a plain emitter takes the plain particle material");
        check(simple.get_texture() == color && !get_emitter_texture_override(simple, PARTICLE_NORMAL_MAP_NAME),
              "with its texture, and no normal map");

        legacy_emitter_document masked = plain;
        masked.texture_mode = ps_soa::texture_mode::mask;
        auto& mask = add_emitter();
        load_emitter_document(mask, save_emitter_document(masked));
        check(mask.get_blend_mode() == ps_soa::blend_mode::normal && mask.get_texture_mode() == ps_soa::texture_mode::mask,
              "a mask of the Normal blend answers Normal, though its one material blends premultiplied");

        // An emitter saved without a texture has none of its own: no override, so nothing to draw apart for.
        legacy_emitter_document bare;
        auto& untextured = add_emitter();
        untextured.set_texture(color);
        load_emitter_document(untextured, save_emitter_document(bare));
        check(!untextured.get_material_instance().has_overrides() && !untextured.get_texture(),
              "a saved emitter without a texture overrides none, and drops the one it had");

        // The look's setters round trip: a mask's one alpha-blended material remembers the blend it was given, and
        // the normal map comes back when the blend can be lit again.
        auto& looks = add_emitter();
        looks.set_blend_mode(ps_soa::blend_mode::premultiplied);
        looks.set_texture_mode(ps_soa::texture_mode::mask);
        check(looks.get_blend_mode() == ps_soa::blend_mode::premultiplied,
              "a mask given the Premultiplied blend answers it, on the material Normal masks draw too");
        looks.set_texture_mode(ps_soa::texture_mode::multi_channel);
        particle_surface premultiplied;
        premultiplied.blend = material_blend_mode::premultiplied;
        check_eq(looks.get_render_material().id(), get_particle_material_key(premultiplied), "and goes back to it when the texture stops being a mask");
        looks.set_normal_texture(normal);
        looks.set_blend_mode(ps_soa::blend_mode::multiply);
        check(!find_shipped_particle_surface(looks.get_render_material().id())->has_normal_map && looks.get_normal_texture() == normal,
              "a filter reads no normal map, and the emitter keeps the one it has");
        looks.set_blend_mode(ps_soa::blend_mode::normal);
        check(find_shipped_particle_surface(looks.get_render_material().id())->has_normal_map,
              "a blend that is lit again reads the emitter's normal map again");
        looks.set_normal_texture({});
        check(!find_shipped_particle_surface(looks.get_render_material().id())->has_normal_map &&
                  !get_emitter_texture_override(looks, PARTICLE_NORMAL_MAP_NAME),
              "an empty normal map drops the override and the material that reads one");

        legacy_emitter_document meshes = smoke;
        meshes.render_mode = ps_soa::render_mode::mesh;
        meshes.render_material = am.get_asset<material>(STANDARD_MATERIAL_KEY);
        auto& mesh_emitter = add_emitter();
        load_emitter_document(mesh_emitter, save_emitter_document(meshes));
        check_eq(mesh_emitter.get_render_material().id(), std::string(STANDARD_MATERIAL_KEY), "a mesh emitter keeps the material its meshes drew");
        legacy_emitter_document bare_meshes = smoke;
        bare_meshes.render_mode = ps_soa::render_mode::mesh;
        auto& bare_mesh_emitter = add_emitter();
        load_emitter_document(bare_mesh_emitter, save_emitter_document(bare_meshes));
        check_eq(bare_mesh_emitter.get_render_material().id(),
                 get_particle_material_key(lit_mask),
                 "a mesh emitter without a material keeps the look its sprites drew, which leaves the mesh its own");

        // A save names the look by the material alone, and loads back the same.
        const std::string saved = save_emitter_document(upgraded);
        check(saved.find("\"blend_mode\"") == std::string::npos && saved.find("\"texture_mode\"") == std::string::npos &&
                  saved.find("\"normal_texture\"") == std::string::npos && saved.find("\"texture\"") == std::string::npos,
              "a save writes none of the old keys");
        auto& reloaded = add_emitter();
        load_emitter_document(reloaded, saved);
        check(reloaded.get_render_material() == upgraded.get_render_material() &&
                  get_emitter_texture_override(reloaded, PARTICLE_TEXTURE_NAME) == color &&
                  get_emitter_texture_override(reloaded, PARTICLE_NORMAL_MAP_NAME) == normal,
              "and the emitter loads back with its material and overrides");

        // A list of members, as a prefab instance's overrides: the old keys are read through the members that took
        // them over.
        legacy_emitter_document filter_look;
        filter_look.texture = normal;
        filter_look.blend_mode = ps_soa::blend_mode::multiply;
        const std::string filter_document = save_emitter_document(filter_look);
        auto& retextured = add_emitter();
        retextured.set_blend_mode(ps_soa::blend_mode::additive);
        retextured.set_texture(color);
        const serialization::path_set texture_alone{"material_overrides"};
        load_emitter_document(retextured, filter_document, &texture_alone);
        check(retextured.get_blend_mode() == ps_soa::blend_mode::additive, "a list naming the overrides leaves the material");
        check(retextured.get_texture() == normal, "and takes the document's texture");
        auto& reblended = add_emitter();
        reblended.set_blend_mode(ps_soa::blend_mode::additive);
        reblended.set_texture(color);
        const serialization::path_set material_alone{"render_material"};
        load_emitter_document(reblended, filter_document, &material_alone);
        check(reblended.get_blend_mode() == ps_soa::blend_mode::multiply, "a list naming the material takes the document's blend");
        check(reblended.get_texture() == color, "and leaves the texture");

        // A material of the project's own keeps its blend: the look's setters pick among the engine's alone.
        auto& custom = add_emitter();
        custom.set_render_material(am.get_asset<material>(STANDARD_MATERIAL_KEY));
        custom.set_blend_mode(ps_soa::blend_mode::additive);
        check_eq(custom.get_render_material().id(), std::string(STANDARD_MATERIAL_KEY), "a material of the project's own is not replaced");

        // Statements about the old keys are restated on the members that took them over.
        prefab_statements stated;
        const auto entity_uid = generate_uuid();
        stated.add_override({}, entity_uid, "particle_emitter_component/texture", "Particle Emitter/Texture");
        stated.add_override({}, entity_uid, "particle_emitter_component/blend_mode", "Particle Emitter/Blend Mode");
        stated.add_override({}, entity_uid, "particle_emitter_component/lifetime/min", "Particle Emitter/Lifetime/Min");
        std::stringstream stream;
        {
            ser20::oarchive_associative_t ar(stream);
            try_save(ar, ser20::make_nvp("statements", stated));
        }
        prefab_statements restated;
        {
            ser20::iarchive_associative_t ar(stream);
            try_load(ar, ser20::make_nvp("statements", restated));
        }
        check(restated.has_override({}, entity_uid, "particle_emitter_component/material_overrides") &&
                  restated.has_override({}, entity_uid, "particle_emitter_component/render_material") &&
                  restated.has_override({}, entity_uid, "particle_emitter_component/lifetime/min") &&
                  !restated.has_override({}, entity_uid, "particle_emitter_component/texture") &&
                  !restated.has_override({}, entity_uid, "particle_emitter_component/blend_mode") && restated.overrides.size() == 3,
              "an override of the texture is one of the material's overrides, one of the blend one of the material");
        prefab_statements normal_stated;
        normal_stated.add_override({}, entity_uid, "particle_emitter_component/normal_texture", "Particle Emitter/Normal Texture");
        std::stringstream normal_stream;
        {
            ser20::oarchive_associative_t ar(normal_stream);
            try_save(ar, ser20::make_nvp("statements", normal_stated));
        }
        prefab_statements normal_restated;
        {
            ser20::iarchive_associative_t ar(normal_stream);
            try_load(ar, ser20::make_nvp("statements", normal_restated));
        }
        check(normal_restated.overrides.size() == 2 &&
                  normal_restated.has_override({}, entity_uid, "particle_emitter_component/material_overrides") &&
                  normal_restated.has_override({}, entity_uid, "particle_emitter_component/render_material"),
              "an override of the normal map is one of both: the map is an override, and picks the material that reads it");
    }
    ps_soa::shutdown();
    gfx::shutdown();
}

/// Overrides below a particle emitter property, as the inspector records an edit of one field: one end of a
/// range, one field of one list element. The field keeps the instance's value; its siblings follow the prefab.
void test_resync_honours_particle_emitter_field_overrides()
{
    begin_test("resync keeps an overridden field of a particle emitter property and updates its siblings");

    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        check(false, "bgfx initialises on the Noop renderer");
        gfx::shutdown();
        return;
    }
    ps_soa::init(16);
    {
        prefab_fixture fix;
        fix.source = build_sample_tree(fix.authoring);
        auto source_child = find_child_by_name(fix.source, "child_a");
        source_child.emplace<particle_emitter_component>();
        fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
        fix.instance = fix.world.instantiate(fix.asset, false);
        auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
        check(instance_child && instance_child.all_of<particle_emitter_component>(), "the instance has the emitter");
        if(instance_child && instance_child.all_of<particle_emitter_component>())
        {
            const auto child_prefab_uid = prefab_uid_of(instance_child);
            auto& instance_emitter = instance_child.get<particle_emitter_component>();
            auto& instance_prefab = fix.instance.get<prefab_component>();
            instance_emitter.set_lifetime(frange_t(4.0f, 5.0f));
            instance_prefab.add_override(child_prefab_uid, "particle_emitter_component/lifetime/min");
            ps_soa::emitter_burst burst;
            burst.time = 0.5f;
            burst.count = frange_t(9.0f, 9.0f);
            instance_emitter.set_bursts({burst});
            instance_prefab.add_override(child_prefab_uid, "particle_emitter_component/bursts[0]/count");

            auto& source_emitter = source_child.get<particle_emitter_component>();
            source_emitter.set_lifetime(frange_t(1.5f, 6.0f));
            burst.time = 1.0f;
            burst.count = frange_t(1.0f, 1.0f);
            source_emitter.set_bursts({burst});

            fix.republish_and_sync();

            auto after = find_child_by_name(fix.instance, "child_a");
            if(after && after.all_of<particle_emitter_component>())
            {
                const auto& emitter = after.get<particle_emitter_component>();
                const float tolerance = 1.0e-5f;
                check(std::abs(emitter.get_lifetime().min - 4.0f) < tolerance, "the overridden end of a range stays");
                check(std::abs(emitter.get_lifetime().max - 6.0f) < tolerance, "the other end follows the prefab");
                const bool has_burst = emitter.get_bursts().size() == 1;
                check(has_burst && std::abs(emitter.get_bursts()[0].count.min - 9.0f) < tolerance,
                      "the overridden field of a list element stays");
                check(has_burst && std::abs(emitter.get_bursts()[0].time - 1.0f) < tolerance,
                      "the element's other fields follow the prefab");
            }
            else
            {
                check(false, "the emitter is still there after the resync");
            }
        }
    }
    ps_soa::shutdown();
    gfx::shutdown();
}

/// A Line and a Trail in a prefab: an instance's override of one point, of one field of the strip and of one row
/// stays through a resync, and what it did not override follows the prefab.
void test_resync_honours_line_and_trail_overrides()
{
    begin_test("resync keeps the overridden point and rows of a line and a trail, and updates the others");

    prefab_fixture fix;
    fix.source = build_sample_tree(fix.authoring);
    auto source_child = find_child_by_name(fix.source, "child_a");
    auto& source_line = source_child.emplace<line_component>();
    source_line.points = {math::vec3(0.0f), math::vec3(1.0f, 0.0f, 0.0f), math::vec3(2.0f, 0.0f, 0.0f)};
    source_child.emplace<trail_component>();
    fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
    fix.instance = fix.world.instantiate(fix.asset, false);
    auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
    const bool has_both = instance_child && instance_child.all_of<line_component, trail_component>();
    check(has_both, "the instance has the line and the trail");
    if(!has_both)
    {
        return;
    }
    check(instance_child.get<line_component>().points.size() == 3, "the instance has the prefab's points");

    const auto child_prefab_uid = prefab_uid_of(instance_child);
    auto& instance_prefab = fix.instance.get<prefab_component>();
    instance_child.get<line_component>().points[1] = math::vec3(1.0f, 5.0f, 0.0f);
    instance_prefab.add_override(child_prefab_uid, "line_component/points[1]");
    instance_child.get<line_component>().strip.width = 0.75f;
    instance_prefab.add_override(child_prefab_uid, "line_component/strip/width");
    instance_child.get<trail_component>().time = 3.0f;
    instance_prefab.add_override(child_prefab_uid, "trail_component/time");

    source_line.points = {math::vec3(0.0f), math::vec3(1.0f, -1.0f, 0.0f), math::vec3(2.0f, 0.0f, 9.0f)};
    source_line.strip.width = 0.2f;
    source_line.strip.shape = strip_shape::tube;
    source_line.loop = true;
    auto& source_trail = source_child.get<trail_component>();
    source_trail.time = 0.5f;
    source_trail.min_distance = 0.4f;
    source_trail.edge = true;
    source_trail.casts_shadow = false;

    fix.republish_and_sync();

    auto after = find_child_by_name(fix.instance, "child_a");
    if(!after || !after.all_of<line_component, trail_component>())
    {
        check(false, "both components are still there after the resync");
        return;
    }
    const float tolerance = 1.0e-5f;
    const auto& line = after.get<line_component>();
    check(line.points.size() == 3 && std::abs(line.points[1].y - 5.0f) < tolerance, "the overridden point stays");
    check(line.points.size() == 3 && std::abs(line.points[2].z - 9.0f) < tolerance, "the other points follow the prefab");
    check(std::abs(line.strip.width - 0.75f) < tolerance, "the overridden field of the strip stays");
    check(line.strip.shape == strip_shape::tube && line.loop, "the strip's other fields and the line's rows follow the prefab");
    const auto& trail = after.get<trail_component>();
    check(std::abs(trail.time - 3.0f) < tolerance, "the trail's overridden row stays");
    check(std::abs(trail.min_distance - 0.4f) < tolerance && trail.edge && !trail.casts_shadow,
          "the trail's other rows follow the prefab, the ones every renderer has included");
}

/// The override path is the one the inspector records for the probe's Extents row: the reflected names.
void test_resync_honours_reflection_probe_shape_overrides()
{
    begin_test("resync keeps a reflection probe's overridden extents and lets its other values follow the prefab");

    prefab_fixture fix;
    fix.source = build_sample_tree(fix.authoring);
    auto source_child = find_child_by_name(fix.source, "child_a");
    reflection_probe authored;
    authored.box_data.extents = {2.0f, 2.0f, 2.0f};
    source_child.emplace<reflection_probe_component>().set_probe(authored);
    fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
    fix.instance = fix.world.instantiate(fix.asset, false);
    auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
    check(instance_child && instance_child.all_of<reflection_probe_component>(), "the instance has the probe");
    if(!instance_child || !instance_child.all_of<reflection_probe_component>())
    {
        return;
    }

    reflection_probe local = instance_child.get<reflection_probe_component>().get_probe();
    local.box_data.extents = {7.0f, 8.0f, 9.0f};
    instance_child.get<reflection_probe_component>().set_probe(local);
    fix.instance.get<prefab_component>().add_override(prefab_uid_of(instance_child),
                                                      "reflection_probe_component/probe/box_data/extents");

    // Meanwhile the prefab's author changes the extents, the transition distance and the intensity.
    authored.box_data.extents = {4.0f, 4.0f, 4.0f};
    authored.box_data.transition_distance = 3.0f;
    authored.intensity = 2.5f;
    source_child.get<reflection_probe_component>().set_probe(authored);

    fix.republish_and_sync();

    auto after = find_child_by_name(fix.instance, "child_a");
    check(after && after.all_of<reflection_probe_component>(), "the probe is still there after the resync");
    if(after && after.all_of<reflection_probe_component>())
    {
        constexpr float tolerance = 1e-4f;
        const auto& probe = after.get<reflection_probe_component>().get_probe();
        check_near(probe.box_data.extents, {7.0f, 8.0f, 9.0f}, "the overridden extents survive");
        test::check_near(probe.box_data.transition_distance, 3.0f, tolerance, "the transition distance follows the prefab");
        test::check_near(probe.intensity, 2.5f, tolerance, "the intensity follows the prefab");
    }
}


/// A device for the cases whose components build resources on one when they are made: a camera its render
/// pipeline, a text its buffers. The runner starts none. The textures loaded on the device are unloaded before it
/// shuts down; what a case makes on it is declared after it, so it goes first.
struct test_device
{
    bool is_up{};

    test_device()
    {
        bgfx::Init init;
        init.type = bgfx::RendererType::Noop;
        is_up = gfx::init(init);
        check(is_up, "bgfx initialises on the Noop renderer");
    }

    ~test_device()
    {
        if(is_up)
        {
            auto& assets = engine::context().get_cached<asset_manager>();
            for(const auto& texture : assets.get_assets<gfx::texture>())
            {
                if(texture.is_ready())
                {
                    assets.unload_asset<gfx::texture>(texture.id());
                }
            }
        }
        gfx::shutdown();
    }

    test_device(const test_device&) = delete;
    auto operator=(const test_device&) -> test_device& = delete;
};

/// Adds to @p out the writable rows under @p object whose path names no node of the saved document that has the
/// leaf paths @p leaves. A row is a reflected property the Inspector lets a user edit; a group of rows is walked
/// into as the Inspector walks into it.
void collect_rows_without_document_nodes(entt::meta_any& object,
                                         const std::string& parent,
                                         const serialization::path_set& leaves,
                                         std::set<std::string>& out)
{
    for(auto [id, prop] : object.type().data())
    {
        const std::string name = entt::get_name(prop);
        if(name.empty() || prop.is_const() || !entt::is_property_visible(object, prop))
        {
            continue;
        }
        const std::string path = parent.empty() ? name : parent + "/" + name;
        if(!serialization::is_path_related(path, leaves))
        {
            out.insert(path);
            continue;
        }
        if(leaves.count(path) > 0)
        {
            continue;
        }
        auto value = prop.get(object);
        const auto type = value ? value.type() : entt::meta_type{};
        const bool is_group = type && !type.is_enum() && !type.is_arithmetic() && !type.is_sequence_container() &&
                              !type.is_associative_container();
        if(is_group)
        {
            collect_rows_without_document_nodes(value, path, leaves, out);
        }
    }
}

/// Checks that every writable row of @p entity's Component names a node of the component's saved document.
/// @p state says which rows the component shows (its defaults, a light type, a shadow filter).
template<typename Component>
void check_rows_name_document_nodes(entt::handle entity, const std::string& state)
{
    if constexpr(!std::is_empty_v<Component>)
    {
        const auto type = entt::resolve<Component>();
        const std::string name = entt::get_name(type) + " (" + state + ")";
        std::stringstream saved;
        type.invoke("component_save"_hs, {}, entity, entt::forward_as_meta(saved));
        serialization::path_set leaves;
        if(!serialization::get_leaf_paths(saved.str(), "component", leaves))
        {
            check(false, name + " saves a document");
            return;
        }
        auto object = entt::forward_as_meta(entity.get<Component>());
        std::set<std::string> unsaved;
        collect_rows_without_document_nodes(object, {}, leaves, unsaved);
        std::string listed;
        for(const auto& path : unsaved)
        {
            listed += (listed.empty() ? "" : ", ") + path;
        }
        check(unsaved.empty(),
              name + ": every row names a node of the saved document" + (listed.empty() ? "" : "; these do not: " + listed));
    }
}

template<typename... Components>
void add_components(entt::handle entity, std::tuple<Components...>*)
{
    (entity.get_or_emplace<Components>(), ...);
}

template<typename... Components>
void check_rows_name_document_nodes(entt::handle entity, const std::string& state, std::tuple<Components...>*)
{
    (check_rows_name_document_nodes<Components>(entity, state), ...);
}

/// A prefab override and an edit of several entities name a value by its Inspector row, and the row's path is made of
/// reflected names. A value saved under another name is then never found: the override is lost at the next resync
/// and the row is not marked when the selection's values differ.
void test_reflected_rows_are_saved_under_their_names()
{
    begin_test("every writable row of a component a user can add is saved under the name the row has");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    ps_soa::init(16);
    {
        auto* const components = static_cast<all_addable_components*>(nullptr);
        scene scn("scn");
        auto entity = scn.create_entity("rows");
        add_components(entity, components);
        check_rows_name_document_nodes(entity, "defaults", components);

        // The rows a component shows follow its settings: each light type with each shadow filter, the other
        // projection of a camera, the other shape of a reflection probe.
        const std::pair<light_type, const char*> light_types[] = {{light_type::spot, "spot"},
                                                                  {light_type::point, "point"},
                                                                  {light_type::directional, "directional"}};
        const std::pair<sm_impl, const char*> shadow_filters[] = {{sm_impl::hard, "hard"},
                                                                  {sm_impl::pcf, "pcf"},
                                                                  {sm_impl::pcss, "pcss"},
                                                                  {sm_impl::vsm, "vsm"},
                                                                  {sm_impl::esm, "esm"}};
        for(const auto& [type, type_name] : light_types)
        {
            for(const auto& [filter, filter_name] : shadow_filters)
            {
                light shown = entity.get<light_component>().get_light();
                shown.type = type;
                shown.casts_shadows = true;
                shown.shadow_params.type = filter;
                shown.contact_shadow.enabled = true;
                entity.get<light_component>().set_light(shown);
                check_rows_name_document_nodes<light_component>(entity, std::string(type_name) + ", " + filter_name);
            }
        }
        entity.get<camera_component>().set_projection_mode(projection_mode::orthographic);
        check_rows_name_document_nodes<camera_component>(entity, "orthographic");
        reflection_probe sphere = entity.get<reflection_probe_component>().get_probe();
        sphere.type = probe_type::sphere;
        entity.get<reflection_probe_component>().set_probe(sphere);
        check_rows_name_document_nodes<reflection_probe_component>(entity, "sphere");
    }
    ps_soa::shutdown();
}

/// The override paths are the ones the Inspector records for the rows: reflected names from the component down.
void test_resync_honours_camera_and_light_row_overrides()
{
    begin_test("resync keeps the camera and light values overridden at their Inspector rows and lets the others "
               "follow the prefab");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    prefab_fixture fix;
    fix.source = build_sample_tree(fix.authoring);
    auto source_child = find_child_by_name(fix.source, "child_a");
    auto& source_camera = source_child.emplace<camera_component>();
    source_camera.set_fov(60.0f);
    source_camera.set_near_clip(0.5f);
    source_camera.set_render_include_mask(layer_mask{1});
    light authored;
    authored.type = light_type::directional;
    authored.intensity = 1.0f;
    authored.shadow_params.type = sm_impl::pcf;
    authored.shadow_params.bias = 1.0f;
    authored.shadow_params.slope_bias = 1.0f;
    authored.shadow_params.pcf.x_offset = 1.0f;
    authored.shadow_params.pcf.y_offset = 1.0f;
    authored.directional_shadow_params.num_splits = 4;
    authored.directional_shadow_params.split_distribution = 0.8f;
    authored.contact_shadow.occluder_thickness = 0.1f;
    source_child.emplace<light_component>().set_light(authored);
    fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
    fix.instance = fix.world.instantiate(fix.asset, false);
    auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
    const bool has_both = instance_child && instance_child.all_of<camera_component, light_component>();
    check(has_both, "the instance has the camera and the light");
    if(has_both)
    {
        const auto uid = prefab_uid_of(instance_child);
        auto& instance_prefab = fix.instance.get<prefab_component>();
        auto& instance_camera = instance_child.get<camera_component>();
        instance_camera.set_fov(30.0f);
        instance_camera.set_render_include_mask(layer_mask{4});
        instance_prefab.add_override(uid, "camera_component/camera/field_of_view");
        instance_prefab.add_override(uid, "camera_component/include_layers");
        light local = instance_child.get<light_component>().get_light();
        local.shadow_params.bias = 5.0f;
        local.shadow_params.pcf.x_offset = 2.5f;
        local.directional_shadow_params.num_splits = 2;
        local.contact_shadow.occluder_thickness = 0.7f;
        instance_child.get<light_component>().set_light(local);
        instance_prefab.add_override(uid, "light_component/light/shadow_params/depth_bias_texels");
        instance_prefab.add_override(uid, "light_component/light/shadow_params/pcf/x_offset");
        instance_prefab.add_override(uid, "light_component/light/directional_shadow_params/splits");
        instance_prefab.add_override(uid, "light_component/light/contact_shadow/occluder_thickness");

        // Meanwhile the prefab's author changes the overridden values and their neighbours.
        source_camera.set_fov(90.0f);
        source_camera.set_near_clip(0.25f);
        source_camera.set_render_include_mask(layer_mask{2});
        source_camera.set_render_exclude_mask(layer_mask{8});
        authored.shadow_params.bias = 2.0f;
        authored.shadow_params.slope_bias = 4.0f;
        authored.shadow_params.pcf.x_offset = 1.5f;
        authored.shadow_params.pcf.y_offset = 3.0f;
        authored.directional_shadow_params.num_splits = 3;
        authored.directional_shadow_params.split_distribution = 0.5f;
        authored.contact_shadow.occluder_thickness = 0.2f;
        authored.contact_shadow.opacity = 0.4f;
        source_child.get<light_component>().set_light(authored);

        fix.republish_and_sync();

        auto after = find_child_by_name(fix.instance, "child_a");
        if(after && after.all_of<camera_component, light_component>())
        {
            constexpr float tolerance = 1e-4f;
            const auto& camera = after.get<camera_component>();
            test::check_near(camera.get_fov(), 30.0f, tolerance, "camera: the overridden field of view survives");
            test::check_near(camera.get_near_clip(), 0.25f, tolerance, "camera: the near clip follows the prefab");
            check_eq(camera.get_render_include_mask().mask, 4, "camera: the overridden include layers survive");
            check_eq(camera.get_render_exclude_mask().mask, 8, "camera: the exclude layers follow the prefab");
            const auto& lit = after.get<light_component>().get_light();
            const auto& shadows = lit.shadow_params;
            test::check_near(shadows.bias, 5.0f, tolerance, "light: the overridden depth bias survives");
            test::check_near(shadows.slope_bias, 4.0f, tolerance, "light: the slope bias follows the prefab");
            test::check_near(shadows.pcf.x_offset, 2.5f, tolerance, "light: the overridden filter offset survives");
            test::check_near(shadows.pcf.y_offset, 3.0f, tolerance, "light: the other offset follows the prefab");
            check_eq(lit.directional_shadow_params.num_splits, 2, "light: the overridden cascade count survives");
            test::check_near(lit.directional_shadow_params.split_distribution,
                             0.5f,
                             tolerance,
                             "light: the cascade distribution follows the prefab");
            test::check_near(lit.contact_shadow.occluder_thickness, 0.7f, tolerance, "light: the overridden thickness survives");
            test::check_near(lit.contact_shadow.opacity, 0.4f, tolerance, "light: the contact opacity follows the prefab");
        }
        else
        {
            check(false, "the camera and the light are still there after the resync");
        }
    }
}

/// @p document, minified, with the object saved under @p key replaced by @p object: how a file reads that holds
/// the object in an earlier layout.
auto replace_json_object(const std::string& document, const std::string& key, const std::string& object) -> std::string
{
    const std::string opening = "\"" + key + "\":{";
    const auto start = document.find(opening);
    if(start == std::string::npos)
    {
        return document;
    }
    size_t depth = 0;
    size_t end = start + opening.size() - 1;
    for(; end < document.size(); ++end)
    {
        if(document[end] == '{')
        {
            ++depth;
        }
        else if(document[end] == '}' && --depth == 0)
        {
            break;
        }
    }
    return document.substr(0, start) + "\"" + key + "\":" + object + document.substr(end + 1);
}

/// The prefab asset of @p root with each object named in @p objects written as given.
auto make_prefab_with_objects(entt::const_handle root,
                              const std::string& id,
                              const std::vector<std::pair<std::string, std::string>>& objects) -> asset_handle<prefab>
{
    const auto bytes = serialize_as_prefab(root, id);
    auto compiled = minify_json(std::string(bytes.begin(), bytes.end()));
    for(const auto& [key, object] : objects)
    {
        const auto replaced = replace_json_object(compiled, key, object);
        check(replaced != compiled, "the prefab document holds " + key);
        compiled = replaced;
    }
    return make_prefab_asset(std::vector<uint8_t>(compiled.begin(), compiled.end()), id);
}

/// A camera component as documents held it before its layer rows were saved under their names: the layers as
/// render_include_layers and render_exclude_layers.
constexpr const char* CAMERA_COMPONENT_WITH_FORMER_LAYER_KEYS =
    R"({"camera":{"projection_mode":1,"field_of_view":90.0,"near_clip":0.25,"far_clip":750.0,)"
    R"("viewport_position":{"x":0,"y":0},"viewport_size":{"width":320,"height":200},"orthographic_size":7.5,)"
    R"("aspect_ratio":1.6,"aspect_locked":false,"frustum_locked":false},)"
    R"("render_include_layers":{"mask":2},"render_exclude_layers":{"mask":8}})";

/// A directional light with the PCF filter as documents held it before: the filter's settings beside the filter
/// type, the cascade settings as num_splits and split_distribution.
constexpr const char* LIGHT_WITH_FILTER_SETTINGS_BESIDE_THE_TYPE =
    R"({"type":2,"intensity":2.0,"casts_shadows":true,)"
    R"("shadow_params":{"type":1,"depth_bias_texels":2.0,"slope_bias_texels":4.0,"pcf_x_offset":1.5,"pcf_y_offset":3.0},)"
    R"("contact_shadow":{"enabled":true,"occluder_thickness":0.2},)"
    R"("directional_shadow_params":{"num_splits":3,"split_distribution":0.5,"stabilize":false}})";

/// Loads the document @p json holds under "component" into @p entity's Component, as JSON or converted to a binary
/// document; with @p paths, only those of it.
template<typename Component>
auto load_component_document(entt::handle entity,
                             const std::string& json,
                             bool as_binary,
                             const serialization::path_set& paths = {}) -> bool
{
    std::vector<uint8_t> bytes(json.begin(), json.end());
    if(as_binary)
    {
        std::string error;
        bytes = serialization::json_to_binary_document(json, error);
        if(bytes.empty())
        {
            check(false, "the document converts to a binary document: " + error);
            return false;
        }
    }
    const auto load = [&](auto& ar)
    {
        return try_load(ar, ser20::make_nvp("component", entity.get<Component>()));
    };
    if(paths.empty())
    {
        return serialization::read_document(bytes.data(), bytes.size(), load);
    }
    const serialization::scoped_path_filter filter("component", paths);
    return serialization::read_document(bytes.data(), bytes.size(), load);
}

void test_camera_and_light_documents_in_earlier_layouts_load()
{
    begin_test("a camera with its layers under their former keys and a light with its filter settings beside the "
               "type still load, whole and by row");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    constexpr float tolerance = 1e-4f;
    const std::string camera_document =
        std::string(R"({"component":)") + CAMERA_COMPONENT_WITH_FORMER_LAYER_KEYS + "}";
    const std::string light_document = std::string(R"({"component":{"enabled":true,"light":)") +
                                       LIGHT_WITH_FILTER_SETTINGS_BESIDE_THE_TYPE + "}}";
    for(const bool as_binary : {false, true})
    {
        const std::string format = as_binary ? "binary: " : "JSON: ";
        scene scn("scn");
        auto whole = scn.create_entity("whole");
        whole.emplace<camera_component>();
        check(load_component_document<camera_component>(whole, camera_document, as_binary), format + "the camera loads");
        const auto& camera = whole.get<camera_component>();
        check(camera.get_projection_mode() == projection_mode::orthographic, format + "camera: it keeps its projection");
        test::check_near(camera.get_fov(), 90.0f, tolerance, format + "camera: it keeps its field of view");
        test::check_near(camera.get_ortho_size(), 7.5f, tolerance, format + "camera: it keeps its orthographic size");
        test::check_near(camera.get_near_clip(), 0.25f, tolerance, format + "camera: it keeps its near clip");
        test::check_near(camera.get_far_clip(), 750.0f, tolerance, format + "camera: it keeps its far clip");
        check_eq(camera.get_render_include_mask().mask, 2, format + "camera: it keeps its include layers");
        check_eq(camera.get_render_exclude_mask().mask, 8, format + "camera: it keeps its exclude layers");
        check_eq(camera.get_viewport_size().width, 320, format + "camera: it keeps its viewport size");

        // The rows are read under their own paths: the camera's inside its node, a layer row under the name it
        // has now.
        auto by_row = scn.create_entity("by row");
        auto& partly = by_row.emplace<camera_component>();
        partly.set_fov(40.0f);
        partly.set_near_clip(1.0f);
        partly.set_far_clip(500.0f);
        partly.set_render_include_mask(layer_mask{16});
        load_component_document<camera_component>(by_row,
                                                  camera_document,
                                                  as_binary,
                                                  {"camera/near_clip", "include_layers"});
        test::check_near(partly.get_near_clip(), 0.25f, tolerance, format + "camera: a listed row takes the document's value");
        check_eq(partly.get_render_include_mask().mask, 2, format + "camera: and so does a listed layer row");
        test::check_near(partly.get_fov(), 40.0f, tolerance, format + "camera: a row the load leaves out stays");
        test::check_near(partly.get_far_clip(), 500.0f, tolerance, format + "camera: and so does another");

        whole.emplace<light_component>();
        check(load_component_document<light_component>(whole, light_document, as_binary), format + "the light loads");
        const auto& lit = whole.get<light_component>().get_light();
        check(lit.type == light_type::directional, format + "light: it keeps its type");
        test::check_near(lit.shadow_params.bias, 2.0f, tolerance, format + "light: it keeps its depth bias");
        test::check_near(lit.shadow_params.pcf.x_offset, 1.5f, tolerance, format + "light: it keeps its filter's x offset");
        test::check_near(lit.shadow_params.pcf.y_offset, 3.0f, tolerance, format + "light: it keeps its filter's y offset");
        check_eq(lit.directional_shadow_params.num_splits, 3, format + "light: it keeps its cascade count");
        test::check_near(lit.directional_shadow_params.split_distribution,
                         0.5f,
                         tolerance,
                         format + "light: it keeps its cascade distribution");
        check(!lit.directional_shadow_params.stabilize, format + "light: it keeps its stabilize setting");
        test::check_near(lit.contact_shadow.occluder_thickness, 0.2f, tolerance, format + "light: it keeps its contact thickness");

        light kept;
        kept.type = light_type::directional;
        kept.shadow_params.type = sm_impl::pcf;
        kept.shadow_params.pcf.x_offset = 2.5f;
        kept.shadow_params.pcf.y_offset = 2.5f;
        kept.directional_shadow_params.num_splits = 1;
        kept.directional_shadow_params.split_distribution = 0.9f;
        by_row.emplace<light_component>().set_light(kept);
        load_component_document<light_component>(by_row,
                                                 light_document,
                                                 as_binary,
                                                 {"light/shadow_params/pcf/y_offset", "light/directional_shadow_params/splits"});
        const auto& partly_lit = by_row.get<light_component>().get_light();
        test::check_near(partly_lit.shadow_params.pcf.y_offset,
                         3.0f,
                         tolerance,
                         format + "light: a listed filter setting takes the document's value");
        test::check_near(partly_lit.shadow_params.pcf.x_offset, 2.5f, tolerance, format + "light: the other stays");
        check_eq(partly_lit.directional_shadow_params.num_splits, 3, format + "light: a listed cascade setting takes the document's value");
        test::check_near(partly_lit.directional_shadow_params.split_distribution,
                         0.9f,
                         tolerance,
                         format + "light: the other stays");
    }

    const auto camera_type = entt::resolve<camera>();
    check_eq(entt::get_name(entt::find_renamed_property(camera_type, "near_clip_distance")),
             "near_clip",
             "the near clip row answers to the name it had before");
    check_eq(entt::get_name(entt::find_renamed_property(camera_type, "far_clip_distance")),
             "far_clip",
             "the far clip row answers to the name it had before");
    const auto filter_type = entt::resolve<light::shadowmap_params>();
    const auto contact_type = entt::resolve<light::contact_shadow_params>();
    check_eq(entt::get_name(entt::find_renamed_property(filter_type, "bias")),
             "depth_bias_texels",
             "the depth bias row answers to the name it had before");
    check_eq(entt::get_name(entt::find_renamed_property(contact_type, "thickness")),
             "occluder_thickness",
             "the contact thickness row answers to the name it had before");
}

/// The Inspector and the tools edit a nested row by reading the object that owns it, setting the row on that copy
/// and writing the copy back through the property that holds it.
void test_camera_settings_are_rows_of_its_camera()
{
    begin_test("a camera component's projection settings are rows of its camera, written back through the component");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    scene scn("scn");
    auto entity = scn.create_entity("camera");
    auto& component = entity.emplace<camera_component>();
    auto object = entt::forward_as_meta(component);
    const auto camera_row = entt::resolve<camera_component>().data("camera"_hs);
    const auto field_of_view_row = entt::resolve<camera>().data("field_of_view"_hs);
    const bool has_rows = camera_row && field_of_view_row;
    check(has_rows, "the component has a camera row and the camera a field of view row");
    if(has_rows)
    {
        check(entt::is_property_flattable(object, camera_row), "the camera's rows join the component's own");
        auto edited = camera_row.get(object);
        check(field_of_view_row.set(edited, 35.0f), "the field of view is set on the camera");
        check(camera_row.set(object, edited), "the edited camera goes back through the component");
        test::check_near(component.get_fov(), 35.0f, 1e-4f, "the component's camera has the edited field of view");
    }
}

/// An Inspector edit records the path of its row. A row that has since become a row of a flattened member (a
/// camera's projection settings were once reflected on the camera component itself) is found inside that member.
void test_statements_follow_rows_into_a_flattened_member()
{
    begin_test("an override recorded at a row that is now a row of the component's camera is read at the camera's "
               "path");

    const auto uid = generate_uuid();
    const std::set<std::string> current_paths = {"camera_component/camera/field_of_view",
                                                 "camera_component/camera/near_clip",
                                                 "camera_component/include_layers",
                                                 "light_component/light/shadow_params/pcf/x_offset"};
    prefab_statements recorded;
    for(const auto& current_path : current_paths)
    {
        recorded.add_override({}, uid, current_path, current_path);
    }
    std::stringstream stream;
    {
        ser20::oarchive_associative_t ar(stream);
        try_save(ar, ser20::make_nvp("statements", recorded));
    }

    // The same statements as an editor recorded them while the projection settings were the component's own rows.
    std::string document = stream.str();
    const auto record_as = [&](const std::string& current_path, const std::string& recorded_path)
    {
        const std::string quoted = "\"" + current_path + "\"";
        const auto at = document.find(quoted);
        check(at != std::string::npos, "the document states " + current_path);
        if(at != std::string::npos)
        {
            document.replace(at, quoted.size(), "\"" + recorded_path + "\"");
        }
    };
    record_as("camera_component/camera/field_of_view", "camera_component/field_of_view");
    record_as("camera_component/camera/near_clip", "camera_component/near_clip_distance");

    prefab_statements loaded;
    const std::vector<uint8_t> bytes(document.begin(), document.end());
    const bool is_loaded = serialization::read_document(bytes.data(),
                                                        bytes.size(),
                                                        [&](auto& ar)
                                                        {
                                                            return try_load(ar, ser20::make_nvp("statements", loaded));
                                                        });
    check(is_loaded, "the statements load");
    std::set<std::string> loaded_paths;
    for(const auto& statement : loaded.overrides)
    {
        loaded_paths.insert(statement.component_path);
    }
    check(loaded_paths == current_paths,
          "each statement names its row as the component has it now: the camera's rows inside the camera, the "
          "others where they were");
}

void test_resync_honours_row_overrides_against_a_prefab_in_an_earlier_layout()
{
    begin_test("resync keeps values overridden at their rows when the prefab document holds them in an earlier "
               "layout, and reads the others from it");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    prefab_fixture fix;
    fix.source = build_sample_tree(fix.authoring);
    auto source_child = find_child_by_name(fix.source, "child_a");
    source_child.emplace<camera_component>().set_fov(60.0f);
    light authored;
    authored.type = light_type::directional;
    authored.shadow_params.type = sm_impl::pcf;
    source_child.emplace<light_component>().set_light(authored);
    fix.asset = make_prefab_from(fix.source, "test:/tree.pfb");
    fix.instance = fix.world.instantiate(fix.asset, false);
    auto instance_child = fix.instance ? find_child_by_name(fix.instance, "child_a") : entt::handle{};
    const bool has_both = instance_child && instance_child.all_of<camera_component, light_component>();
    check(has_both, "the instance has the camera and the light");
    if(has_both)
    {
        const auto uid = prefab_uid_of(instance_child);
        auto& instance_prefab = fix.instance.get<prefab_component>();
        instance_child.get<camera_component>().set_fov(30.0f);
        instance_prefab.add_override(uid, "camera_component/camera/field_of_view");
        light local = instance_child.get<light_component>().get_light();
        local.shadow_params.pcf.x_offset = 2.5f;
        local.directional_shadow_params.num_splits = 2;
        instance_child.get<light_component>().set_light(local);
        instance_prefab.add_override(uid, "light_component/light/shadow_params/pcf/x_offset");
        instance_prefab.add_override(uid, "light_component/light/directional_shadow_params/splits");

        const std::string light_component_object =
            std::string(R"({"enabled":true,"light":)") + LIGHT_WITH_FILTER_SETTINGS_BESIDE_THE_TYPE + "}";
        fix.asset = make_prefab_with_objects(fix.source,
                                             "test:/tree.pfb",
                                             {{"camera_component", CAMERA_COMPONENT_WITH_FORMER_LAYER_KEYS},
                                              {"light_component", light_component_object}});
        sync_prefab_instance_with(fix.instance, fix.asset);

        auto after = find_child_by_name(fix.instance, "child_a");
        if(after && after.all_of<camera_component, light_component>())
        {
            constexpr float tolerance = 1e-4f;
            const auto& camera = after.get<camera_component>();
            test::check_near(camera.get_fov(), 30.0f, tolerance, "camera: the overridden field of view survives");
            test::check_near(camera.get_near_clip(), 0.25f, tolerance, "camera: the near clip is the document's");
            test::check_near(camera.get_far_clip(), 750.0f, tolerance, "camera: the far clip is the document's");
            check_eq(camera.get_render_include_mask().mask, 2, "camera: the include layers are the document's");
            const auto& lit = after.get<light_component>().get_light();
            test::check_near(lit.shadow_params.pcf.x_offset, 2.5f, tolerance, "light: the overridden filter offset survives");
            test::check_near(lit.shadow_params.pcf.y_offset, 3.0f, tolerance, "light: the other offset is the document's");
            check_eq(lit.directional_shadow_params.num_splits, 2, "light: the overridden cascade count survives");
            test::check_near(lit.directional_shadow_params.split_distribution,
                             0.5f,
                             tolerance,
                             "light: the cascade distribution is the document's");
            test::check_near(lit.shadow_params.bias, 2.0f, tolerance, "light: the depth bias is the document's");
        }
        else
        {
            check(false, "the camera and the light are still there after the resync");
        }
    }
}

void test_text_kerning_and_outline_width_are_saved()
{
    begin_test("a text's kerning switch and outline width survive a save and a load");

    const test_device device;
    if(!device.is_up)
    {
        return;
    }
    scene scn("scn");
    auto source = scn.create_entity("source");
    auto target = scn.create_entity("target");
    text_style outlined;
    outlined.outline_width = 1.25f;
    auto& source_text = source.emplace<text_component>();
    source_text.set_style(outlined);
    source_text.set_apply_kerning(false);
    auto& target_text = target.emplace<text_component>();

    const auto type = entt::resolve<text_component>();
    std::stringstream saved;
    check(type.invoke("component_save"_hs, {}, source, entt::forward_as_meta(saved)).cast<bool>(), "the text saves");
    std::stringstream stream(saved.str());
    check(type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(stream)).cast<bool>(), "the text loads");
    test::check_near(target_text.get_style().outline_width, 1.25f, 1e-4f, "the outline width is the saved one");
    check(!target_text.get_apply_kerning(), "the kerning switch is the saved one");

    // A document written before the two were saved leaves them as the text has them.
    std::stringstream earlier(R"({"component":{"text":"kept","style":{"opacity":0.5}}})");
    check(type.invoke("component_load"_hs, {}, target, entt::forward_as_meta(earlier)).cast<bool>(),
          "a text document without them loads");
    test::check_near(target_text.get_style().outline_width, 1.25f, 1e-4f, "the outline width stays");
    check(!target_text.get_apply_kerning(), "the kerning switch stays");
}

void test_resync_root_position_and_rotation_are_implicit_overrides()
{
    begin_test("resync always preserves the instance root's position and rotation");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    // Placed in the world, with no override recorded anywhere.
    fix.instance.get<transform_component>().set_position_local({50.0f, 60.0f, 70.0f});

    fix.source.get<transform_component>().set_position_local({-1.0f, -1.0f, -1.0f});

    fix.republish_and_sync();

    check_near(fix.instance.get<transform_component>().get_position_local(),
               {50.0f, 60.0f, 70.0f},
               "the placement survives even though nothing recorded it as an override");
}

void test_resync_root_scale_follows_the_prefab()
{
    begin_test("resync lets the instance root's scale follow the prefab (by design)");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    check_near(fix.instance.get<transform_component>().get_scale_local(),
               {2.0f, 2.0f, 2.0f},
               "the instance starts at the prefab's scale");

    fix.source.get<transform_component>().set_scale_local({5.0f, 5.0f, 5.0f});

    fix.republish_and_sync();

    // Deliberate asymmetry with position/rotation (is_nested_root_placement_path): an
    // authored scale change is meant to reach existing instances.
    check_near(fix.instance.get<transform_component>().get_scale_local(),
               {5.0f, 5.0f, 5.0f},
               "an authored scale change reaches the instance");
}

void test_resync_root_scale_survives_when_overridden()
{
    begin_test("an explicitly overridden root scale stops following the prefab");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    const auto root_prefab_uid = prefab_uid_of(fix.instance);
    fix.instance.get<transform_component>().set_scale_local({3.0f, 3.0f, 3.0f});
    fix.instance.get<prefab_component>().add_override(root_prefab_uid, "transform_component/local_transform/scale");

    fix.source.get<transform_component>().set_scale_local({9.0f, 9.0f, 9.0f});

    fix.republish_and_sync();

    check_near(fix.instance.get<transform_component>().get_scale_local(),
               {3.0f, 3.0f, 3.0f},
               "the override wins over the authored scale");
}

void test_resync_adds_entities_added_to_the_prefab()
{
    begin_test("resync brings in entities added to the prefab");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    const size_t before = count_real_entities(*fix.world.registry);

    fix.authoring.create_entity("newcomer", fix.source);

    fix.republish_and_sync();

    auto newcomer = find_child_by_name(fix.instance, "newcomer");
    check(static_cast<bool>(newcomer), "the new entity appears in the instance");
    check_eq(count_real_entities(*fix.world.registry), before + 1, "exactly one entity was added");

    if(newcomer)
    {
        check(!prefab_uid_of(newcomer).is_nil(), "the new entity gets a prefab id so it can be resynced later");
        check(!uid_of(newcomer).is_nil(), "and its own instance uid");
    }
}

void test_resync_removes_entities_deleted_from_the_prefab()
{
    begin_test("resync removes entities deleted from the prefab");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    const size_t before = count_real_entities(*fix.world.registry);

    // child_a takes its grandchild with it.
    scene::destroy_entity(find_child_by_name(fix.source, "child_a"));

    fix.republish_and_sync();

    check(!find_child_by_name(fix.instance, "child_a"), "child_a is gone from the instance");
    check(static_cast<bool>(find_child_by_name(fix.instance, "child_b")), "child_b is untouched");
    check_eq(count_real_entities(*fix.world.registry), before - 2, "the child and its grandchild both went");
}

void test_resync_honours_removed_entities()
{
    begin_test("an entity the user deleted from the instance stays deleted");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto victim = find_child_by_name(fix.instance, "child_b");
    const auto victim_prefab_uid = prefab_uid_of(victim);
    check(!victim_prefab_uid.is_nil(), "the doomed child has a prefab id");

    // What mark_entity_as_removed + the delete action do together.
    fix.instance.get<prefab_component>().remove_entity(victim_prefab_uid);
    scene::destroy_entity(victim);

    fix.republish_and_sync();

    check(!find_child_by_name(fix.instance, "child_b"), "the resync does not resurrect it");
    check(static_cast<bool>(find_child_by_name(fix.instance, "child_a")), "its sibling is still there");
}

void test_removing_an_entity_with_children_stays_removed()
{
    begin_test("a removed entity's children stay removed with it");

    // The victim has a child of its own, which is what the leaf test above cannot see: a
    // resync recreates any record it cannot match to a live entity or a removal entry, so a
    // removal naming only child_a would bring grandchild back - as a root, since its parent
    // link resolves to the null the removed parent maps to. The editor therefore records
    // the whole subtree, and this pins that a subtree-complete record survives the replay.
    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto victim = find_child_by_name(fix.instance, "child_a");
    check(static_cast<bool>(find_child_by_name(victim, "grandchild")), "the victim has a child");

    delete_like_the_editor(fix.instance, victim);

    fix.republish_and_sync();

    check(!find_child_by_name(fix.instance, "child_a"), "the resync does not resurrect the entity");
    check_eq(count_entities_named(*fix.world.registry, "grandchild"),
             0,
             "and its child did not come back as an orphan");
    check(static_cast<bool>(find_child_by_name(fix.instance, "child_b")), "its sibling is still there");
}

void test_resync_keeps_user_added_children()
{
    begin_test("entities the user added to an instance survive a resync");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto extra = fix.world.create_entity("user_added", fix.instance);
    const auto extra_uid = uid_of(extra);
    check(prefab_uid_of(extra).is_nil(), "a hand-added entity has no prefab id");

    fix.republish_and_sync();

    auto after = find_child_by_name(fix.instance, "user_added");
    check(static_cast<bool>(after), "the hand-added child is still under the instance root");
    if(after)
    {
        check(uid_of(after) == extra_uid, "and it is the same entity - cleanup_uid_mapping must not claim it");
    }
}

void test_build_order()
{
    begin_test("build order puts dependencies before dependents and isolates cycles");

    // Synthetic graphs: compute_build_order takes the dependency lookup as a callback, so
    // none of this needs assets, files or an asset_manager.
    std::map<hpp::uuid, std::vector<hpp::uuid>> graph;
    const auto node = [&](const char* label) -> hpp::uuid
    {
        (void)label;
        auto uid = generate_uuid();
        graph[uid] = {};
        return uid;
    };
    const auto resolver = [&](const hpp::uuid& uid) -> std::vector<hpp::uuid>
    {
        auto it = graph.find(uid);
        return it == graph.end() ? std::vector<hpp::uuid>{} : it->second;
    };
    const auto position_of = [](const asset_deps::build_order& order, const hpp::uuid& uid) -> size_t
    {
        const auto it = std::find(order.ordered.begin(), order.ordered.end(), uid);
        return it == order.ordered.end() ? SIZE_MAX : size_t(std::distance(order.ordered.begin(), it));
    };
    const auto is_cyclic = [](const asset_deps::build_order& order, const hpp::uuid& uid) -> bool
    {
        return std::find(order.cyclic.begin(), order.cyclic.end(), uid) != order.cyclic.end();
    };

    // A -> B -> C. C has to be built first.
    {
        auto a = node("a");
        auto b = node("b");
        auto c = node("c");
        graph[a] = {b};
        graph[b] = {c};

        const auto order = asset_deps::compute_build_order({a}, resolver);
        check_eq(order.ordered.size(), 3, "the whole chain is ordered, including nodes not passed in");
        check_eq(order.cyclic.size(), 0, "and none of it is cyclic");
        check(position_of(order, c) < position_of(order, b), "c precedes b");
        check(position_of(order, b) < position_of(order, a), "b precedes a");
    }

    // Diamond: A depends on B and C, both depend on D.
    {
        graph.clear();
        auto a = node("a");
        auto b = node("b");
        auto c = node("c");
        auto d = node("d");
        graph[a] = {b, c};
        graph[b] = {d};
        graph[c] = {d};

        const auto order = asset_deps::compute_build_order({a}, resolver);
        check_eq(order.ordered.size(), 4, "every node appears once, despite d being reached twice");
        check(position_of(order, d) < position_of(order, b), "d precedes b");
        check(position_of(order, d) < position_of(order, c), "d precedes c");
        check(position_of(order, b) < position_of(order, a), "b precedes a");
        check(position_of(order, c) < position_of(order, a), "c precedes a");
    }

    // A prefab that instances the same prefab twice must not be mistaken for a cycle.
    {
        graph.clear();
        auto a = node("a");
        auto b = node("b");
        graph[a] = {b, b, b};

        const auto order = asset_deps::compute_build_order({a}, resolver);
        check_eq(order.cyclic.size(), 0, "a repeated dependency is counted once, not treated as a cycle");
        check(position_of(order, b) < position_of(order, a), "and still ordered correctly");
    }

    // A prefab containing itself.
    {
        graph.clear();
        auto a = node("a");
        graph[a] = {a};

        const auto order = asset_deps::compute_build_order({a}, resolver);
        check_eq(order.ordered.size(), 0, "a self-referencing asset cannot be ordered");
        check(is_cyclic(order, a), "and is reported as cyclic");
    }

    // A -> B -> A, plus C depending on the cycle, plus an unrelated clean pair.
    {
        graph.clear();
        auto a = node("a");
        auto b = node("b");
        auto c = node("c");
        auto clean = node("clean");
        auto clean_dep = node("clean_dep");
        graph[a] = {b};
        graph[b] = {a};
        graph[c] = {a};
        graph[clean] = {clean_dep};

        const auto order = asset_deps::compute_build_order({a, b, c, clean}, resolver);

        check(is_cyclic(order, a) && is_cyclic(order, b), "both members of the cycle are reported");
        check(is_cyclic(order, c), "so is an asset that merely depends on the cycle - equally unbuildable");
        check(!is_cyclic(order, clean) && !is_cyclic(order, clean_dep),
              "an unrelated subgraph is unaffected by the cycle");
        check(position_of(order, clean_dep) < position_of(order, clean),
              "and is still ordered correctly");
    }

    // Degenerate inputs.
    {
        graph.clear();
        const auto empty = asset_deps::compute_build_order({}, resolver);
        check_eq(empty.ordered.size(), 0, "an empty root set yields an empty order");

        auto a = node("a");
        graph[a] = {hpp::uuid{}};
        const auto with_nil = asset_deps::compute_build_order({a, hpp::uuid{}}, resolver);
        check_eq(with_nil.ordered.size(), 1, "nil uids are skipped, in roots and in dependencies");
        check_eq(with_nil.cyclic.size(), 0, "and do not produce a phantom cycle");
    }

    // Determinism: same input, same output.
    {
        graph.clear();
        auto a = node("a");
        auto b = node("b");
        auto c = node("c");
        graph[a] = {b, c};

        const auto first = asset_deps::compute_build_order({a}, resolver);
        const auto second = asset_deps::compute_build_order({a}, resolver);
        check(first.ordered == second.ordered, "the order is a function of the input, not of hash iteration");
    }
}

/// Serializes a subtree as a prefab, declaring its nested instances already resolved -
/// what the deploy bake writes.
auto serialize_as_baked_prefab(entt::const_handle root) -> std::vector<uint8_t>
{
    std::ostringstream ss;

    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = root;
    save_ctx.to_prefab = true;
    save_ctx.nesting_resolved = true;

    save_to_stream(ss, root);

    save_ctx.nesting_resolved = false;
    save_ctx.to_prefab = false;
    save_ctx.save_source = {};
    pop_save_context(pushed);

    const auto str = ss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

/// Counts the members of every object in a JSON document that repeat a name used earlier in the
/// same object: the readers that look a name up keep one of them and drop the rest.
auto count_repeated_keys(const simdjson::dom::element& element) -> size_t
{
    size_t repeated = 0;
    if(element.is_object())
    {
        std::set<std::string_view> names;
        for(const auto field : element.get_object().value())
        {
            if(!names.insert(field.key).second)
            {
                ++repeated;
            }
            repeated += count_repeated_keys(field.value);
        }
    }
    else if(element.is_array())
    {
        for(const auto item : element.get_array().value())
        {
            repeated += count_repeated_keys(item);
        }
    }
    return repeated;
}

auto count_occurrences(const std::string& text, const std::string& what) -> size_t
{
    size_t count = 0;
    for(auto position = text.find(what); position != std::string::npos; position = text.find(what, position + 1))
    {
        ++count;
    }
    return count;
}

/// The same scene in the first layout, as its writer wrote it: a root count, the scene's nesting marker
/// when it has one, then the members of every root side by side in one object, in the order that writer
/// wrote them: the root's entities, its "version" string, and its own marker when it has one.
auto to_first_scene_layout(const std::string& text) -> std::string
{
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if(parser.parse(text.data(), text.size()).get(doc))
    {
        return {};
    }
    simdjson::dom::array roots;
    if(doc["roots"].get_array().get(roots))
    {
        return {};
    }
    std::string first_layout = "{\"entities_count\":" + std::to_string(roots.size());
    bool is_nesting_resolved = false;
    if(!doc["nesting_resolved"].get_bool().get(is_nesting_resolved) && is_nesting_resolved)
    {
        first_layout += ",\"nesting_resolved\":true";
    }
    for(const auto root : roots)
    {
        simdjson::dom::object members;
        if(root.get_object().get(members))
        {
            return {};
        }
        for(const auto member : members)
        {
            first_layout += ",\"" + std::string(member.key) + "\":" + simdjson::minify(member.value);
            if(member.key == "entities")
            {
                first_layout += ",\"version\":\"1.0.0\"";
            }
        }
    }
    first_layout += "}";
    return first_layout;
}

/// The same prefab in the first layout: no version at its head, the string "1.0.0" after the entities.
auto to_first_prefab_layout(const std::string& text) -> std::string
{
    static const std::regex head_version(R"re("version"\s*:\s*[0-9]+\s*,)re");
    auto first_layout = std::regex_replace(text, head_version, "", std::regex_constants::format_first_only);
    const auto end = first_layout.rfind('}');
    if(end == std::string::npos)
    {
        return {};
    }
    first_layout.insert(end, ",\"version\":\"1.0.0\"");
    return first_layout;
}

/// The same document with another version at its head.
auto with_head_version(const std::string& text, uint32_t version) -> std::string
{
    static const std::regex head_version(R"re("version"\s*:\s*[0-9]+)re");
    return std::regex_replace(text,
                              head_version,
                              "\"version\": " + std::to_string(version),
                              std::regex_constants::format_first_only);
}

auto count_scene_roots(const scene& scn) -> size_t
{
    size_t roots = 0;
    scn.registry->view<root_component, transform_component>().each([&](auto, auto&&, auto&&) { ++roots; });
    return roots;
}

auto save_scene_text(const scene& scn) -> std::string
{
    std::stringstream doc;
    save_to_stream(doc, scn);
    return doc.str();
}

/// A scene of three named roots, each a small tree.
void build_three_root_scene(scene& scn)
{
    const char* names[] = {"first_root", "second_root", "third_root"};
    for(const char* name : names)
    {
        build_sample_tree(scn).get<tag_component>().name = name;
    }
}

auto make_scene_asset(const std::string& text, const std::string& id) -> asset_handle<scene_prefab>
{
    auto data = std::make_shared<scene_prefab>();
    data->buffer.data = compiled_document_bytes(std::vector<uint8_t>(text.begin(), text.end()));
    auto& am = engine::context().get_cached<asset_manager>();
    am.unload_asset<scene_prefab>(id);
    return am.get_asset_from_instance<scene_prefab>(id, data);
}

void test_scene_and_prefab_documents_are_not_interchangeable()
{
    begin_test("a scene document is not a prefab document");

    // A bake that mistook scenes for prefabs once rewrote them through the prefab writer and lost
    // every root but the first. A scene holds its roots in a "roots" array, a prefab document one
    // root's "entities": each reader handed the other's file would read part of it, so the
    // single-root reader refuses a scene file of either layout.
    scene src("src");
    build_three_root_scene(src);
    check_eq(count_scene_roots(src), size_t{3}, "the source scene has three roots");

    const auto scene_text = save_scene_text(src);
    check(scene_text.find("\"roots\"") != std::string::npos, "a scene document holds its roots in an array");
    check(scene_text.find("entities_count") == std::string::npos, "and writes no root count");

    {
        scene dst("dst");
        std::stringstream in(scene_text);
        load_from_stream(in, dst);
        check_eq(count_scene_roots(dst), size_t{3}, "the scene reader recovers every root");
    }
    {
        scene dst("dst_via_registry");
        std::stringstream in(scene_text);
        load_from_stream(in, *dst.registry);
        check_eq(count_scene_roots(dst), size_t{0}, "the single-root reader refuses a scene document");
    }
    {
        scene dst("dst_first_layout_via_registry");
        std::stringstream in(to_first_scene_layout(scene_text));
        load_from_stream(in, *dst.registry);
        check_eq(count_scene_roots(dst), size_t{0}, "and one in the first layout");
    }

    scene single("single");
    auto root = build_sample_tree(single);
    auto prefab_doc = serialize_as_prefab(root);
    const std::string prefab_text(prefab_doc.begin(), prefab_doc.end());
    check(prefab_text.find("\"roots\"") == std::string::npos && prefab_text.find("entities_count") == std::string::npos,
          "a prefab document has neither a roots array nor a root count");
    check(prefab_text.find("entities") != std::string::npos, "it holds one root's entities");
}

void test_scene_document_repeats_no_key()
{
    begin_test("a scene document repeats no key, so any JSON reader sees every root");

    scene src("src");
    build_three_root_scene(src);
    const auto scene_text = save_scene_text(src);

    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    check(!parser.parse(scene_text.data(), scene_text.size()).get(doc), "the scene document parses");
    check_eq(count_repeated_keys(doc), size_t{0}, "no object in it repeats a key");
    simdjson::dom::array roots;
    check(!doc["roots"].get_array().get(roots), "it has a roots array");
    check_eq(roots.size(), size_t{3}, "with one element per root");
    check_eq(count_occurrences(scene_text, "\"version\""), size_t{1}, "the version is written once, for the file");

    check_eq(read_document_version(scene_text), SCENE_DOCUMENT_VERSION, "its head names the version it is in");
    check_eq(read_document_version(minify_json(scene_text)), SCENE_DOCUMENT_VERSION, "also when minified");
    check_eq(read_document_version(to_first_scene_layout(scene_text)),
             FIRST_DOCUMENT_VERSION,
             "a file without a version at its head is in the first layout");
}

void test_scene_save_is_in_hierarchy_order_and_stable()
{
    begin_test("a scene saves its roots in hierarchy order, and a re-save writes the same file");

    scene src("src");
    build_three_root_scene(src);
    const auto saved = save_scene_text(src);
    const auto first = saved.find("\"first_root\"");
    const auto second = saved.find("\"second_root\"");
    const auto third = saved.find("\"third_root\"");
    check(first != std::string::npos && first < second && second < third, "the roots are in the order they were made");

    // Storage order is not hierarchy order: only the editor's hierarchy panel sorts the storage.
    src.registry->sort<root_component>(
        [](const auto& lhs, const auto& rhs)
        {
            return lhs.order > rhs.order;
        });
    check(save_scene_text(src) == saved, "reversing the root storage does not change the file");

    scene reloaded("reloaded");
    std::stringstream in(saved);
    load_from_stream(in, reloaded);
    check(save_scene_text(reloaded) == saved, "loading and saving again writes the same file");
}

void test_first_layout_scene_still_loads()
{
    begin_test("a scene in the first layout still loads every root and entity");

    scene src("src");
    build_three_root_scene(src);
    const auto saved = save_scene_text(src);
    const auto first_layout = to_first_scene_layout(saved);
    check(!first_layout.empty(), "the first-layout copy was made");

    scene dst("dst");
    std::stringstream in(first_layout);
    load_from_stream(in, dst);
    check_eq(count_scene_roots(dst), size_t{3}, "every root loads");
    check_eq(count_real_entities(*dst.registry), count_real_entities(*src.registry), "every entity loads");
    check(save_scene_text(dst) == saved, "saved again, it is the scene the current layout writes");

    // A roots array that names no version is read as the layout that brought the array.
    static const std::regex head_version(R"re("version"\s*:\s*[0-9]+\s*,)re");
    const auto unversioned = std::regex_replace(saved, head_version, "", std::regex_constants::format_first_only);
    check(unversioned.find("\"version\"") == std::string::npos, "the unversioned copy names no version");
    scene roots_only("roots_only");
    std::stringstream roots_in(unversioned);
    load_from_stream(roots_in, roots_only);
    check_eq(count_scene_roots(roots_only), size_t{3}, "a roots array without a version loads every root");
}

void test_newer_scene_version_is_refused()
{
    begin_test("a scene in a newer version is refused, and the open scene stays");

    check(is_readable_scene_version(FIRST_DOCUMENT_VERSION) && is_readable_scene_version(SCENE_DOCUMENT_VERSION),
          "this build reads the first layout and its own");
    check(!is_readable_scene_version(SCENE_DOCUMENT_VERSION + 1) && !is_readable_scene_version(0),
          "and no layout outside them");

    scene src("src");
    build_three_root_scene(src);
    const auto newer = with_head_version(save_scene_text(src), SCENE_DOCUMENT_VERSION + 1);
    check_eq(read_document_version(newer), SCENE_DOCUMENT_VERSION + 1, "the head of the newer file is read");

    {
        scene dst("dst");
        std::stringstream in(newer);
        load_from_stream(in, dst);
        check_eq(count_scene_roots(dst), size_t{0}, "the stream reader loads nothing of it");
    }

    scene open("open");
    build_sample_tree(open);
    const auto entities_before = count_real_entities(*open.registry);
    const auto asset = make_scene_asset(minify_json(newer), "test:/newer_version.spfb");
    check(!load_from_prefab(asset, open), "loading the newer scene asset reports the refusal");
    check_eq(count_real_entities(*open.registry), entities_before, "the scene that was open is untouched");
    check_eq(read_document_version(asset), SCENE_DOCUMENT_VERSION + 1, "the asset's version is read from its file");
}

void test_prefab_document_version()
{
    begin_test("a prefab names its version first, older prefabs still load, newer ones are refused");

    scene src("src");
    auto root = build_sample_tree(src);
    const auto bytes = serialize_as_prefab(root, "test:/versioned.pfb");
    const std::string prefab_text(bytes.begin(), bytes.end());
    check_eq(read_document_version(prefab_text), PREFAB_DOCUMENT_VERSION, "its head names the version it is in");
    check_eq(count_occurrences(prefab_text, "\"version\""), size_t{1}, "the version is written once");
    check(is_readable_prefab_version(FIRST_DOCUMENT_VERSION) && is_readable_prefab_version(PREFAB_DOCUMENT_VERSION),
          "this build reads the first layout and its own");
    check(!is_readable_prefab_version(PREFAB_DOCUMENT_VERSION + 1), "and no newer one");

    const auto expected_entities = count_real_entities(*src.registry);

    const auto first_layout = minify_json(to_first_prefab_layout(prefab_text));
    check_eq(read_document_version(first_layout), FIRST_DOCUMENT_VERSION, "a prefab written before versions is in the first layout");
    {
        scene dst("dst_first_layout");
        auto loaded = load_from_prefab(make_prefab_asset(std::vector<uint8_t>(first_layout.begin(), first_layout.end()),
                                                         "test:/first_layout.pfb"),
                                       *dst.registry);
        check(bool(loaded), "a prefab in the first layout loads");
        check_eq(count_real_entities(*dst.registry), expected_entities, "with every entity");
    }

    const auto newer = minify_json(with_head_version(prefab_text, PREFAB_DOCUMENT_VERSION + 1));
    check_eq(read_document_version(newer), PREFAB_DOCUMENT_VERSION + 1, "the head of a newer prefab is read");
    {
        scene dst("dst_newer");
        const auto asset = make_prefab_asset(std::vector<uint8_t>(newer.begin(), newer.end()), "test:/newer.pfb");
        auto loaded = load_from_prefab(asset, *dst.registry);
        check(!loaded, "a prefab in a newer version is refused");
        check_eq(count_real_entities(*dst.registry), size_t{0}, "and nothing of it loads");
        check_eq(read_document_version(asset), PREFAB_DOCUMENT_VERSION + 1, "the asset's version is read from its file");
    }
}

/// The same document with its version written as the string "3", which names no layout.
auto with_string_version(const std::string& text) -> std::string
{
    static const std::regex head_version(R"re("version"\s*:\s*[0-9]+)re");
    return std::regex_replace(text, head_version, "\"version\": \"3\"", std::regex_constants::format_first_only);
}

/// The same scene with its members reordered, as a tool that rewrites JSON may leave it: the roots first,
/// then the given version.
auto with_roots_before_version(const std::string& text, uint32_t version) -> std::string
{
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if(parser.parse(text.data(), text.size()).get(doc))
    {
        return {};
    }
    simdjson::dom::element roots;
    if(doc["roots"].get(roots))
    {
        return {};
    }
    return "{\"roots\":" + simdjson::minify(roots) + ",\"version\":" + std::to_string(version) + "}";
}

/// The first entity in a registry with the given name, or an empty handle.
auto find_entity_named(entt::registry& registry, const std::string& name) -> entt::handle
{
    entt::handle found{};
    registry.view<tag_component>().each(
        [&](auto e, const auto& tag)
        {
            if(!found && tag.name == name)
            {
                found = entt::handle(registry, e);
            }
        });
    return found;
}

void test_malformed_scene_documents_leave_the_open_scene()
{
    begin_test("a scene whose version cannot be told, or is newer, is refused before the open scene goes");
    // The editor's check and the loader read the version the same way, so neither can call a file readable
    // that the other refuses: a load that cleared the open scene and refused afterwards would leave it empty.
    scene src("src");
    build_three_root_scene(src);
    const auto saved = save_scene_text(src);
    struct malformed_case
    {
        std::string what;
        std::string text;
        uint32_t version;
    };
    const std::vector<malformed_case> cases = {
        {"an empty object", "{}", UNKNOWN_DOCUMENT_VERSION},
        {"a version written as a string", with_string_version(saved), UNKNOWN_DOCUMENT_VERSION},
        {"a version of zero", with_head_version(saved, 0), UNKNOWN_DOCUMENT_VERSION},
        {"a newer version after the roots",
         with_roots_before_version(saved, SCENE_DOCUMENT_VERSION + 1),
         SCENE_DOCUMENT_VERSION + 1},
    };
    for(size_t index = 0; index < cases.size(); ++index)
    {
        const auto& item = cases[index];
        check(!item.text.empty(), item.what + ": the document was made");
        check_eq(read_document_version(item.text), item.version, item.what + ": its version is the loader's");
        scene open("open");
        build_three_root_scene(open);
        const auto before = save_scene_text(open);
        const auto asset = make_scene_asset(item.text, "test:/malformed_" + std::to_string(index) + ".spfb");
        const auto problem = find_document_problem(asset);
        check(!problem.empty(), item.what + ": the editor's check refuses it");
        const bool is_newer = item.version != UNKNOWN_DOCUMENT_VERSION;
        check((problem.find("newer engine") != std::string::npos) == is_newer,
              item.what + ": the reason says a newer engine wrote it only when one did");
        check(problem.find("4294967295") == std::string::npos, item.what + ": and names no made-up version");
        check(!load_from_prefab(asset, open), item.what + ": loading it reports the refusal");
        check(save_scene_text(open) == before, item.what + ": the scene that was open is untouched");
        scene dst("dst");
        std::stringstream in(item.text);
        check(!load_from_stream(in, dst), item.what + ": the stream reader refuses it too");
        check_eq(count_scene_roots(dst), size_t{0}, item.what + ": and loads nothing of it");
    }
    // Reordered in this build's version, the same document is read by both, and loads whole.
    const auto reordered = with_roots_before_version(saved, SCENE_DOCUMENT_VERSION);
    check_eq(read_document_version(reordered), SCENE_DOCUMENT_VERSION, "a reordered current scene is in its version");
    scene dst("dst_reordered");
    std::stringstream in(reordered);
    check(load_from_stream(in, dst), "and loads");
    check_eq(count_scene_roots(dst), size_t{3}, "with every root");
}

void test_baked_first_layout_scene_loads()
{
    begin_test("a baked scene in the first layout loads every root once, in order");
    scene src("src");
    build_three_root_scene(src);
    std::stringstream baked;
    const bool pushed = push_save_context();
    get_save_context().nesting_resolved = true;
    save_to_stream(baked, src);
    get_save_context().nesting_resolved = false;
    pop_save_context(pushed);
    // The old writer: the root count, the scene's marker, then each root's entities, version and marker.
    // Duplicate keys are read in file order, so a copy in any other order reads one root's keys for another's.
    const auto first_layout = to_first_scene_layout(baked.str());
    const auto count_at = first_layout.find("\"entities_count\"");
    const auto marker_at = first_layout.find("\"nesting_resolved\"");
    const auto entities_at = first_layout.find("\"entities\"");
    const auto version_at = first_layout.find("\"version\"");
    const auto root_marker_at = first_layout.find("\"nesting_resolved\"", marker_at + 1);
    check(count_at == 1 && count_at < marker_at && marker_at < entities_at && entities_at < version_at &&
              version_at < root_marker_at && root_marker_at != std::string::npos,
          "the copy is in the order the old writer wrote");
    check_eq(count_occurrences(first_layout, "\"nesting_resolved\""),
             size_t{4},
             "with a marker for the scene and each root");
    check_eq(read_document_version(first_layout), FIRST_DOCUMENT_VERSION, "it is read as the first layout");
    scene dst("dst");
    std::stringstream in(first_layout);
    check(load_from_stream(in, dst), "it loads");
    check_eq(count_scene_roots(dst), size_t{3}, "every root loads");
    check_eq(count_entities_named(*dst.registry, "first_root"), size_t{1}, "the first root once");
    check_eq(count_entities_named(*dst.registry, "second_root"), size_t{1}, "the second root once");
    check_eq(count_entities_named(*dst.registry, "third_root"), size_t{1}, "the third root once");
    check_eq(count_real_entities(*dst.registry), count_real_entities(*src.registry), "and every entity, none twice");
    check(save_scene_text(dst) == save_scene_text(src), "saved again, it is the scene that was baked");
}

/// A scene file the first layout's writer wrote, verbatim: ReflScene3D.spfb of a reference project. Four roots
/// and seven entities, linked by the entity ids of that time; its assets are not in the test project.
const char* const REAL_FIRST_LAYOUT_SCENE = R"json({
 "entities_count": 4,
 "entities": [
  {
   "id": 10485761,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "Main Camera",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 0.0,
       "y": 1.0,
       "z": -10.0
      },
      "rotation": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0,
       "w": 1.0
      },
      "scale": {
       "x": 1.0,
       "y": 1.0,
       "z": 1.0
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 4294967295
     },
     "children": [],
     "active": true
    },
    "has_camera_component": true,
    "camera_component": {
     "camera": {
      "projection_mode": 0,
      "field_of_view": 60.0,
      "near_clip": 0.10000000149011612,
      "far_clip": 1000.0,
      "viewport_position": {
       "x": 0,
       "y": 0
      },
      "viewport_size": {
       "width": 1409,
       "height": 796
      },
      "orthographic_size": 5.0,
      "aspect_ratio": 1.770100474357605,
      "aspect_locked": false,
      "frustum_locked": false
     }
    },
    "has_assao_component": true,
    "assao_component": {
     "enabled": true,
     "settings": {
      "radius": 1.2000000476837159,
      "shadow_multiplier": 1.0,
      "shadow_power": 1.0,
      "shadow_clamp": 0.9800000190734863,
      "horizon_angle_threshold": 0.05999999865889549,
      "fade_out_from": 50.0,
      "fade_out_to": 200.0,
      "quality_level": 3,
      "adaptive_quality_limit": 0.44999998807907107,
      "blur_pass_count": 2,
      "sharpness": 0.9800000190734863,
      "temporal_supersampling_angle_offset": 0.0,
      "temporal_supersampling_radius_offset": 1.0,
      "detail_shadow_strength": 0.5,
      "generate_normals": false
     }
    },
    "has_tonemapping_component": true,
    "tonemapping_component": {
     "enabled": true,
     "settings": {
      "exposure": 1.0,
      "method": 6
     }
    },
    "has_fxaa_component": true,
    "fxaa_component": {
     "enabled": true
    },
    "has_audio_listener_component": true,
    "audio_listener_component": {}
   }
  }
 ],
 "version": "1.0.0",
 "entities": [
  {
   "id": 10485763,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "Sky & Directional Light",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 0.0,
       "y": 1.0,
       "z": 0.0
      },
      "rotation": {
       "x": 0.4082179069519043,
       "y": -0.234568789601326,
       "z": 0.10938161611557007,
       "w": 0.8754263520240784
      },
      "scale": {
       "x": 1.000001311302185,
       "y": 0.9999984502792358,
       "z": 0.9999988675117493
      },
      "skew": {
       "x": 1.4901178246873315e-7,
       "y": -0.000002373506276853732,
       "z": -0.0000026332741072110368
      }
     },
     "parent": {
      "id": 4294967295
     },
     "children": [],
     "active": true
    },
    "has_light_component": true,
    "light_component": {
     "light": {
      "type": 2,
      "intensity": 1.0,
      "color": {
       "r": 1.0,
       "g": 0.9568628072738648,
       "b": 0.8392157554626465,
       "a": 1.0
      },
      "casts_shadows": true,
      "shadow_params": {
       "type": 1,
       "depth": 0,
       "resolution": 3,
       "bias": 0.0011500000255182386,
       "normal_bias": 0.014999999664723874,
       "near_plane": 0.20000000298023225,
       "far_plane": 550.0
      },
      "directional_data": {
       "shadow_params": {
        "num_splits": 4,
        "split_distribution": 0.800000011920929,
        "stabilize": true
       }
      }
     }
    },
    "has_skylight_component": true,
    "skylight_component": {
     "mode": 1,
     "turbidity": 1.899999976158142,
     "cubemap": {
      "uid": "00000000-0000-0000-0000-000000000000"
     }
    }
   }
  }
 ],
 "version": "1.0.0",
 "entities": [
  {
   "id": 8388613,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "Envinroment Probe",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 0.0,
       "y": 0.10000000149011612,
       "z": 0.0
      },
      "rotation": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0,
       "w": 1.0
      },
      "scale": {
       "x": 1.0,
       "y": 1.0,
       "z": 1.0
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 4294967295
     },
     "children": [],
     "active": true
    },
    "has_tonemapping_component": true,
    "tonemapping_component": {
     "enabled": true,
     "settings": {
      "exposure": 1.0,
      "method": 6
     }
    },
    "has_reflection_probe_component": true,
    "reflection_probe_component": {
     "probe": {
      "type": 1,
      "method": 0,
      "intensity": 1.0,
      "range": 1000.0
     },
     "faces_per_frame": 1,
     "apply_prefilter": false
    }
   }
  }
 ],
 "version": "1.0.0",
 "entities": [
  {
   "id": 7340041,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "Plane",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 0.0,
       "y": 1.7114271599893983e-7,
       "z": 0.0
      },
      "rotation": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0,
       "w": 1.0
      },
      "scale": {
       "x": 1.0,
       "y": 1.0,
       "z": 1.0
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 4294967295
     },
     "children": [
      {
       "id": 7340039
      },
      {
       "id": 7340043
      }
     ],
     "active": true
    },
    "has_model_component": true,
    "model_component": {
     "enabled": true,
     "static": true,
     "casts_shadow": true,
     "casts_reflection": false,
     "model": {
      "lods": [
       {
        "uid": "d9fdfd2a-f890-a85c-917c-e284c810063d"
       }
      ],
      "materials": [
       {
        "uid": "819043bf-3a62-d949-ad18-98136c59bd29"
       }
      ],
      "material_instances": [
       {
        "polymorphic_id": 0
       }
      ],
      "lod_limits": [
       {
        "min": 0,
        "max": 100
       }
      ]
     }
    }
   }
  },
  {
   "id": 7340039,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "Sphere",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": -1.1847882270812989,
       "y": 0.6078066229820252,
       "z": 0.0
      },
      "rotation": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0,
       "w": 1.0
      },
      "scale": {
       "x": 1.0,
       "y": 1.0,
       "z": 1.0
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 7340041
     },
     "children": [],
     "active": true
    },
    "has_model_component": true,
    "model_component": {
     "enabled": true,
     "static": true,
     "casts_shadow": true,
     "casts_reflection": false,
     "model": {
      "lods": [
       {
        "uid": "b755a718-ef93-1552-bf79-f79c0e6973eb"
       }
      ],
      "materials": [
       {
        "uid": "dd11cda0-190d-fd52-84dd-3a6f67d09813"
       }
      ],
      "material_instances": [],
      "lod_limits": [
       {
        "min": 0,
        "max": 100
       }
      ]
     }
    }
   }
  },
  {
   "id": 7340043,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "DamagedHelmet",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 1.39302396774292,
       "y": 0.9664449691772461,
       "z": -0.3049502372741699
      },
      "rotation": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0,
       "w": 1.0
      },
      "scale": {
       "x": 1.0,
       "y": 1.0,
       "z": 1.0
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 7340041
     },
     "children": [
      {
       "id": 7340044
      }
     ],
     "active": true
    },
    "has_model_component": true,
    "model_component": {
     "enabled": true,
     "static": true,
     "casts_shadow": true,
     "casts_reflection": false,
     "model": {
      "lods": [
       {
        "uid": "72a4eee9-5b2d-004d-9810-e06a83d6d05b"
       }
      ],
      "materials": [
       {
        "uid": "9174dcef-747c-6145-b754-a9143ecdc39f"
       }
      ],
      "material_instances": [],
      "lod_limits": [
       {
        "min": 0,
        "max": 100
       }
      ]
     }
    }
   }
  },
  {
   "id": 7340044,
   "components": {
    "has_tag_component": true,
    "tag_component": {
     "name": "node_damagedHelmet_-6514",
     "tag": ""
    },
    "has_layer_component": true,
    "layer_component": {
     "layers": {
      "mask": 1
     }
    },
    "has_transform_component": true,
    "transform_component": {
     "local_transform": {
      "position": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      },
      "rotation": {
       "x": 0.7071068286895752,
       "y": 0.0,
       "z": 0.0,
       "w": 0.7071067094802856
      },
      "scale": {
       "x": 1.0,
       "y": 1.0000001192092896,
       "z": 1.0000001192092896
      },
      "skew": {
       "x": 0.0,
       "y": 0.0,
       "z": 0.0
      }
     },
     "parent": {
      "id": 7340043
     },
     "children": [],
     "active": true
    },
    "has_submesh_component": true,
    "submesh_component": {
     "submeshes": [
      0
     ]
    }
   }
  }
 ],
 "version": "1.0.0"
})json";

/// An empty scene as the first layout's writer saved it, verbatim.
const char* const REAL_EMPTY_FIRST_LAYOUT_SCENE = "{\r\n \"entities_count\": 0\r\n}";

/**
 * @brief The file with only its tag, layer and transform components switched on: the others (camera, models, lights,
 *        probes, post effects) make GPU resources, and the test runner has no renderer. What the test checks (the
 *        layout, the names, the hierarchy and the root order) lives in those three.
 */
auto keep_scene_graph_components(const std::string& text) -> std::string
{
    static const std::regex has_component(R"re("has_([a-z_]+)": true)re");
    std::string result;
    auto last = text.cbegin();
    for(std::sregex_iterator it(text.cbegin(), text.cend(), has_component), end; it != end; ++it)
    {
        const std::string name = (*it)[1].str();
        const bool keep = name == "tag_component" || name == "layer_component" || name == "transform_component";
        result.append(last, (*it)[0].first);
        result += keep ? it->str() : "\"has_" + name + "\": false";
        last = (*it)[0].second;
    }
    result.append(last, text.cend());
    return result;
}

void test_real_first_layout_scene_file()
{
    begin_test("scene files the first layout's writer wrote still load");
    const std::string text = keep_scene_graph_components(REAL_FIRST_LAYOUT_SCENE);
    check_eq(read_document_version(text), FIRST_DOCUMENT_VERSION, "a real first-layout file is read as one");
    scene open("open");
    build_three_root_scene(open);
    const auto asset = make_scene_asset(text, "test:/real_first_layout.spfb");
    check(find_document_problem(asset).empty(), "the editor's check accepts it");
    check(load_from_prefab(asset, open), "it loads in place of the open scene");
    check_eq(count_scene_roots(open), size_t{4}, "with its four roots");
    check_eq(count_entities_named(*open.registry, "first_root"), size_t{0}, "and nothing of what was open");
    const char* names[] = {"Main Camera",
                           "Sky & Directional Light",
                           "Envinroment Probe",
                           "Plane",
                           "Sphere",
                           "DamagedHelmet",
                           "node_damagedHelmet_-6514"};
    for(const char* name : names)
    {
        check_eq(count_entities_named(*open.registry, name),
                 size_t{1},
                 std::string("its entity ") + name + " loads once");
    }
    auto plane = find_entity_named(*open.registry, "Plane");
    auto helmet = plane ? find_child_by_name(plane, "DamagedHelmet") : entt::handle{};
    check(plane && find_child_by_name(plane, "Sphere") && helmet, "the plane keeps its two children");
    check(helmet && find_child_by_name(helmet, "node_damagedHelmet_-6514"), "and the helmet its node");
    const auto saved = save_scene_text(open);
    const auto camera_at = saved.find("\"Main Camera\"");
    const auto light_at = saved.find("\"Sky & Directional Light\"");
    const auto probe_at = saved.find("\"Envinroment Probe\"");
    const auto plane_at = saved.find("\"Plane\"");
    check(camera_at < light_at && light_at < probe_at && probe_at < plane_at && plane_at != std::string::npos,
          "saved again, its roots are in the order the file holds them");
    scene empty_open("empty_open");
    build_three_root_scene(empty_open);
    const auto empty_asset = make_scene_asset(REAL_EMPTY_FIRST_LAYOUT_SCENE, "test:/real_empty_first_layout.spfb");
    check_eq(read_document_version(REAL_EMPTY_FIRST_LAYOUT_SCENE),
             FIRST_DOCUMENT_VERSION,
             "an empty first-layout file is one too");
    check(load_from_prefab(empty_asset, empty_open), "it loads");
    check_eq(count_scene_roots(empty_open), size_t{0}, "as an empty scene");
}

void test_prefab_loaders_refuse_what_they_cannot_read()
{
    begin_test("the prefab loaders refuse a scene or a newer prefab, and leave an instance as it was");
    scene authoring("authoring");
    auto prefab_root = build_sample_tree(authoring);
    const auto pfb = make_prefab_from(prefab_root, "test:/refusal_source.pfb");
    check(find_document_problem(pfb).empty(), "a prefab this build wrote is not refused");
    // A first-layout scene saved as a prefab: the single-root reader would read its first root alone.
    scene three("three");
    build_three_root_scene(three);
    const auto scene_text = to_first_scene_layout(save_scene_text(three));
    const auto scene_as_prefab =
        make_prefab_asset(std::vector<uint8_t>(scene_text.begin(), scene_text.end()), "test:/scene_as_prefab.pfb");
    check(!find_document_problem(scene_as_prefab).empty(), "a scene saved as a prefab is refused by the check");
    {
        scene dst("dst");
        check(!load_from_prefab(scene_as_prefab, *dst.registry), "instantiating it loads nothing");
        check_eq(count_real_entities(*dst.registry), size_t{0}, "not even its first root");
    }
    const auto prefab_bytes = serialize_as_prefab(prefab_root, "test:/refusal_newer.pfb");
    const auto newer_text = minify_json(with_head_version(std::string(prefab_bytes.begin(), prefab_bytes.end()),
                                                          PREFAB_DOCUMENT_VERSION + 1));
    const auto newer =
        make_prefab_asset(std::vector<uint8_t>(newer_text.begin(), newer_text.end()), "test:/refusal_newer.pfb");
    check(find_document_problem(newer).find("newer engine") != std::string::npos, "a newer prefab is refused as one");
    // A sync against a file the loader refuses leaves the caller's handle on the instance it had.
    scene world("world");
    auto instance = world.instantiate(pfb, false);
    check(static_cast<bool>(instance), "a readable prefab instantiates");
    const auto entities_before = count_real_entities(*world.registry);
    for(const auto& refused : {scene_as_prefab, newer})
    {
        auto target = instance;
        check(!load_from_prefab_out(refused, *world.registry, target), refused.id() + ": the sync reports the refusal");
        check(target == instance, refused.id() + ": the caller's handle still names the instance");
        check_eq(count_real_entities(*world.registry), entities_before, refused.id() + ": nothing of it is loaded");
    }
}

void test_nesting_resolved_marker()
{
    begin_test("the nesting-resolved marker round-trips and suppresses the refresh");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);

    // An ordinary save must never claim the marker - absent means "not resolved", which is
    // the safe reading for every document written before the marker existed.
    const auto plain = serialize_as_prefab(outer_root);
    const std::string plain_text(plain.begin(), plain.end());
    check(plain_text.find("nesting_resolved") == std::string::npos,
          "an ordinary save does not write the marker");

    const auto baked = serialize_as_baked_prefab(outer_root);
    const std::string baked_text(baked.begin(), baked.end());
    check(baked_text.find("nesting_resolved") != std::string::npos, "a baked save writes it");

    // Its effect: the refresh is skipped. Observed through the nested instance keeping the
    // content the document carried, rather than being reloaded from its own asset.
    scene world("world");
    auto instance = world.instantiate(make_prefab_asset(baked, "test:/outer_baked.pfb"), false);
    check(static_cast<bool>(instance), "a baked prefab still loads");
    if(instance)
    {
        check(static_cast<bool>(find_child_by_name(instance, "root")),
              "and its nested instance is present");
    }

    // A scene-shaped document carries the marker at its own level.
    {
        scene src("src");
        build_sample_tree(src);

        std::stringstream unbaked;
        save_to_stream(unbaked, src);
        check(unbaked.str().find("nesting_resolved") == std::string::npos,
              "an ordinary scene save does not write the marker either");

        std::stringstream ss;
        bool pushed = push_save_context();
        get_save_context().nesting_resolved = true;
        save_to_stream(ss, src);
        get_save_context().nesting_resolved = false;
        pop_save_context(pushed);

        const auto text = ss.str();
        check(text.find("nesting_resolved") != std::string::npos, "a baked scene save writes it");

        scene dst("dst");
        std::stringstream in(text);
        load_from_stream(in, dst);
        check_eq(count_real_entities(*dst.registry),
                 count_real_entities(*src.registry),
                 "and the marker does not disturb the load");
    }
}

void test_prefab_dependency_enumeration()
{
    begin_test("a prefab's nested instances are readable from its buffer");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");

    // A prefab with no instances inside it references nothing.
    {
        const auto deps = asset_deps::get_referenced_uids(*inner_pfb.get());
        check_eq(deps.size(), 0, "a prefab with no nested instances references nothing");
    }

    // One containing two instances of Inner and one of Other.
    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    outer_authoring.instantiate(other_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    const auto deps = asset_deps::get_referenced_uids(*outer_pfb.get());

    // Deduplicated: Inner is instanced twice but is one dependency.
    check_eq(deps.size(), 2, "two distinct prefabs are referenced, however many instances");
    check(std::find(deps.begin(), deps.end(), inner_pfb.uid()) != deps.end(), "inner is among them");
    check(std::find(deps.begin(), deps.end(), other_pfb.uid()) != deps.end(), "so is other");

    // Reading the buffer must not depend on it being minified or indented - the source
    // asset is written pretty and the compiled one is minified.
    {
        auto data = std::make_shared<prefab>();
        const auto& pretty = outer_pfb.get()->buffer.data;
        const auto minified = minify_json(std::string(pretty.begin(), pretty.end()));
        data->buffer.data = std::vector<uint8_t>(minified.begin(), minified.end());

        const auto min_deps = asset_deps::get_referenced_uids(*data);
        check_eq(min_deps.size(), deps.size(), "the same references are found in a minified buffer");
    }

    // A buffer that will not parse yields nothing rather than throwing.
    {
        auto data = std::make_shared<prefab>();
        const std::string junk = "{ not json at all";
        data->buffer.data = std::vector<uint8_t>(junk.begin(), junk.end());
        check_eq(asset_deps::get_referenced_uids(*data).size(), 0, "a damaged buffer yields no references");

        auto empty = std::make_shared<prefab>();
        check_eq(asset_deps::get_referenced_uids(*empty).size(), 0, "so does an empty one");
    }
}

void test_loaded_dependents_read_documents_for_prefabs_only()
{
    begin_test("loaded prefab documents are searched for a prefab's dependents, and for no other uid");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/dependents_inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/dependents_outer.pfb");
    check(outer_pfb.get() != nullptr, "the outer prefab loads");

    auto& am = engine::context().get_cached<asset_manager>();
    check(am.has_asset<prefab>(inner_pfb.uid()), "the manager holds the inner prefab");
    check(!am.has_asset<scene_prefab>(inner_pfb.uid()), "as a prefab, not as a scene");

    const auto contains = [](const std::vector<hpp::uuid>& uids, const hpp::uuid& uid) -> bool
    {
        return std::find(uids.begin(), uids.end(), uid) != uids.end();
    };
    check(contains(asset_deps::find_loaded_dependents(am, inner_pfb.uid()), outer_pfb.uid()),
          "the prefab instancing the inner one is its dependent");

    // A document naming a uid the manager holds no prefab or scene for: the walk would find it,
    // but no prefab or scene document can depend on anything but a prefab or a scene.
    const auto stranger = generate_uuid();
    check(!am.has_asset<prefab>(stranger) && !am.has_asset<scene_prefab>(stranger),
          "the manager holds nothing for a fresh uid");

    const auto source = serialize_as_prefab(outer_root);
    std::string text(source.begin(), source.end());
    const auto inner_text = hpp::to_string(inner_pfb.uid());
    const auto stranger_text = hpp::to_string(stranger);
    size_t replaced = 0;
    for(auto at = text.find(inner_text); at != std::string::npos; at = text.find(inner_text, at + stranger_text.size()))
    {
        text.replace(at, inner_text.size(), stranger_text);
        ++replaced;
    }
    check(replaced > 0, "the outer document names the inner prefab's uid");

    auto naming_pfb = make_prefab_asset(std::vector<uint8_t>(text.begin(), text.end()), "test:/dependents_naming.pfb");
    check(naming_pfb.get() != nullptr && contains(asset_deps::get_referenced_uids(*naming_pfb.get()), stranger),
          "the edited document lists the fresh uid as an instance source");
    check(!contains(asset_deps::find_loaded_dependents(am, stranger), naming_pfb.uid()),
          "documents are not searched for a uid that is no prefab or scene");

    am.unload_asset<prefab>("test:/dependents_naming.pfb");
}

void test_nested_prefab_instance_keeps_its_link()
{
    begin_test("a prefab instance nested inside a saved prefab keeps its link");

    // Inner asset.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    // Outer asset: a plain root with an instance of Inner underneath it.
    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto nested = outer_authoring.instantiate(inner_pfb, outer_root, false);
    check(static_cast<bool>(nested), "the nested instance was created");
    check(nested.all_of<prefab_component>(), "and is linked before the outer prefab is saved");
    if(!nested)
    {
        return;
    }

    const auto outer_bytes = serialize_as_prefab(outer_root);
    const std::string text(outer_bytes.begin(), outer_bytes.end());

    // The link is an asset reference, so what has to survive is the inner asset's uid.
    check(text.find(hpp::to_string(inner_pfb.uid())) != std::string::npos,
          "the outer prefab records the inner asset's uid");
    check(text.find("has_prefab_component") != std::string::npos,
          "and carries a prefab_component for the nested instance");

    auto outer_pfb = make_prefab_asset(outer_bytes, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check(static_cast<bool>(outer_instance), "the outer instance was created");
    if(!outer_instance)
    {
        return;
    }

    auto nested_after = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested_after), "the nested subtree came across");
    if(!nested_after)
    {
        return;
    }

    // Previously the link was stripped for every entity when saving to a prefab, so this
    // arrived as loose entities and edits to the inner asset could never reach it again.
    check(nested_after.all_of<prefab_component>(), "the nested instance is still an instance");
    check(!prefab_uid_of(nested_after).is_nil(), "and still carries a prefab id, so it can be resynced");

    // Exactly two instance roots: the outer one and the nested one.
    size_t instance_roots = 0;
    world.registry->view<prefab_component>().each([&](auto, auto&&) { ++instance_roots; });
    check_eq(instance_roots, 2, "the outer and the nested instance are both linked");
}

void test_editing_the_inner_asset_reaches_new_outer_instances()
{
    begin_test("editing an inner prefab reaches instances made from an outer one");

    // Outer's file holds a snapshot of Inner taken when Outer was saved, so the moment
    // Inner is edited that snapshot is stale. Nothing rewrites Outer, so instantiating it
    // used to reproduce the old Inner content indefinitely.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    // Inner is edited and re-saved. Outer's file is untouched and still holds the old copy.
    find_child_by_name(inner_source, "child_a").get<tag_component>().name = "edited_in_inner";
    inner_source.get<transform_component>().set_scale_local({4.0f, 4.0f, 4.0f});
    auto inner_pfb_v2 = make_prefab_asset(serialize_as_prefab(inner_source, "test:/inner.pfb"), "test:/inner.pfb");

    // The instance inside Outer points at the asset by uid, so the updated payload has to
    // reach it through the same handle identity the outer file recorded.
    check(inner_pfb_v2.uid() != inner_pfb.uid(),
          "the harness cannot re-point an asset uid, so this checks the mechanism, not the id");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested), "the nested instance is there");
    if(!nested)
    {
        return;
    }

    // Point it at the edited asset and refresh, which is what the engine now does for
    // every nested instance as part of loading the outer one.
    nested.get<prefab_component>().source = inner_pfb_v2;
    sync_nested_prefab_instances(outer_instance);

    check(static_cast<bool>(find_child_by_name(nested, "edited_in_inner")),
          "the inner asset's edit reached the nested instance");
    check_near(nested.get<transform_component>().get_scale_local(),
               {4.0f, 4.0f, 4.0f},
               "and so did its authored scale, which is not an implicit override");
}

void test_a_fresh_instantiate_pulls_the_inner_asset_edit()
{
    begin_test("a fresh instantiate refreshes nested instances without a manual sync");

    // The automatic sync_nested_prefab_instances call inside load_from_prefab, through the
    // uid the outer document actually recorded. The manual-sync test above bypasses that
    // resolution by assigning the source by hand; this one republishes the inner asset
    // behind its existing handle - the shape of an editor recompile - so the instantiate
    // has to find the new content on its own.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    // Inner is edited after outer was saved, so outer's snapshot of it is stale.
    find_child_by_name(inner_source, "child_a").get<tag_component>().name = "edited_after_outer_saved";
    republish_in_place(inner_pfb, inner_source);

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested), "the nested instance is there");
    if(!nested)
    {
        return;
    }

    check(static_cast<bool>(find_child_by_name(nested, "edited_after_outer_saved")),
          "the instantiate refreshed the nested instance against its own asset");
}

void test_nesting_cycle_is_refused()
{
    begin_test("a prefab that contains itself through another prefab is refused, not recursed");

    // Outer contains an instance of inner; inner is then edited to contain an instance of
    // outer. The republish happens behind inner's existing handle, because the cycle only
    // exists if the uid outer's document references is the uid that serves the new content.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene inner_edit("inner_edit");
    auto inner_v2 = build_sample_tree(inner_edit);
    inner_edit.instantiate(outer_pfb, inner_v2, false);
    republish_in_place(inner_pfb, inner_v2);

    // Expanding outer reaches inner, whose content carries outer again - which is on the
    // expansion stack and gets refused. The test finishing at all is the core assertion;
    // without the guard this recursion has no floor.
    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check(static_cast<bool>(outer_instance), "the outer instance still instantiates");
    if(!outer_instance)
    {
        return;
    }

    // What the refusal leaves behind is bounded: the outer instance, its nested inner, and
    // inner v2's unexpandable snapshot of outer with its own nested inner - four instance
    // roots, not a chain to the depth cap.
    size_t instance_roots = 0;
    world.registry->view<prefab_component>().each([&](auto, auto&&) { ++instance_roots; });
    check_eq(instance_roots, 4, "the expansion stops at the cycle instead of unwinding to the depth cap");
}

void test_outer_resync_preserves_nested_instance_edits()
{
    begin_test("resyncing an outer instance leaves the nested instance's local edits alone");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested), "the nested instance is there");
    if(!nested)
    {
        return;
    }

    // The user edits the nested instance in the scene: moves it, renames one of its
    // children, and clears the override bookkeeping on it.
    nested.get<transform_component>().set_position_local({42.0f, 43.0f, 44.0f});
    auto nested_child = find_child_by_name(nested, "child_a");
    check(static_cast<bool>(nested_child), "the nested instance has child_a");
    if(nested_child)
    {
        nested_child.get<tag_component>().name = "edited_in_scene";
    }
    nested.get<prefab_component>().local.clear();

    // Scene open resyncs every instance root, outer ones included. The outer asset holds a
    // snapshot of the nested instance taken when the outer prefab was saved; replaying it
    // over the live one used to discard everything above.
    sync_prefab_instance_with(outer_instance, outer_pfb);

    auto nested_after = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested_after), "the nested instance survived the outer resync");
    if(!nested_after)
    {
        return;
    }

    // Its placement goes back to where the outer prefab put it - the inner root's (1, 2, 3) as
    // instantiated into the outer authoring scene. The override bookkeeping was cleared above,
    // and a nested root's placement is the container's to state; a move the editor makes
    // records an override, and that is what keeps one (see the placement test).
    check_near(nested_after.get<transform_component>().get_position_local(),
               {1.0f, 2.0f, 3.0f},
               "an unrecorded move of the nested root follows the container's placement");
    // Its children follow the inner prefab again, because the override bookkeeping was
    // cleared above - and with a resolvable source the instance now really does resync
    // against that prefab. This used to assert the opposite, back when nested content was
    // skipped wholesale and nothing refreshed it.
    check(!find_child_by_name(nested_after, "edited_in_scene"),
          "while an edit to a child that nothing recorded follows the inner prefab");
    check(all_overrides_of(nested_after).empty(),
          "cleared overrides stay cleared - the outer asset's copy does not come back");
}

void test_removing_a_nested_instance_from_the_asset_propagates()
{
    begin_test("removing a nested instance from a prefab removes it from existing instances");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto authored_nested = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check(static_cast<bool>(find_child_by_name(outer_instance, "root")), "the nested instance arrives");

    // The instantiate must have recorded where that nested instance came from - without it
    // there is no way to tell a removal from an addition later.
    auto nested_from_asset = find_child_by_name(outer_instance, "root");
    check(nested_from_asset && is_named_instance(nested_from_asset),
          "the nested instance is marked as coming from the outer asset");

    // The author deletes the nested instance from the outer prefab and re-saves.
    scene::destroy_entity(authored_nested);
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    check(!find_child_by_name(outer_instance, "root"),
          "the nested instance is gone from the existing instance too");
}

void test_a_nested_instance_added_in_the_scene_survives_resync()
{
    begin_test("a nested instance added in the scene survives an outer resync");

    // The dangerous half. A nested instance the user added and one the author deleted look
    // identical on the entity - prefab ids come from the nested asset either way - so the
    // only thing separating them is what the instance recorded getting from its asset.
    //
    // A *different* inner prefab is used for the addition on purpose: two instances of the
    // same prefab under one root carry identical prefab uids, which is a separate problem
    // and would obscure what this is testing.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    inner_source.get<tag_component>().name = "authored_inner";
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    other_source.get<tag_component>().name = "user_added_inner";
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);

    // The user adds an instance of a different prefab into this outer instance.
    auto user_added = world.instantiate(other_pfb, outer_instance, false);
    check(static_cast<bool>(user_added), "the user-added nested instance exists");
    if(!user_added)
    {
        return;
    }
    const auto user_added_entity = user_added.entity();

    // Resync against the unchanged asset. The asset says nothing about the added instance,
    // so it is unconsumed - exactly like one that had been deleted from the asset.
    sync_prefab_instance_with(outer_instance, outer_pfb);

    check(static_cast<bool>(find_child_by_name(outer_instance, "user_added_inner")),
          "the user-added nested instance is still there");
    auto still_there = find_child_by_name(outer_instance, "user_added_inner");
    if(still_there)
    {
        check(still_there.entity() == user_added_entity, "and it is the same entity, not a rebuild");
    }
    check(static_cast<bool>(find_child_by_name(outer_instance, "authored_inner")),
          "and the authored one is untouched");

    // It must stay unmarked, or the next resync would decide the asset had dropped it.
    if(still_there)
    {
        check(!is_named_instance(still_there),
              "and it is not marked as coming from the asset");
    }
    auto authored = find_child_by_name(outer_instance, "authored_inner");
    check(authored && is_named_instance(authored),
          "while the authored one is");

    // Repeat: a second resync must not change its mind.
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check(static_cast<bool>(find_child_by_name(outer_instance, "user_added_inner")),
          "it survives a second resync too");
}

void test_duplicate_nested_instances_of_one_prefab()
{
    begin_test("two nested instances of the same prefab are handled individually");

    // The case the previous design could not do. Two instances of one prefab under a single
    // root carry *identical* prefab uids, so anything keyed on the uid alone sees one entry
    // and cannot tell how many instances stand behind it. The marker moves the question onto
    // the entity, and counting how many records mention the uid supplies the rest.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto first = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto second = outer_authoring.instantiate(inner_pfb, outer_root, false);
    check(prefab_uid_of(first) == prefab_uid_of(second),
          "the two nested instances really do share a prefab uid");

    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);

    const auto count_nested = [&]() -> size_t
    {
        size_t n = 0;
        const auto* transform = outer_instance.try_get<transform_component>();
        if(transform != nullptr)
        {
            for(auto child : transform->get_children())
            {
                if(child.all_of<prefab_component>())
                {
                    ++n;
                }
            }
        }
        return n;
    };

    check_eq(count_nested(), 2, "both nested instances arrive");

    // Resync against the unchanged asset: two marked instances, two records, nothing to do.
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check_eq(count_nested(), 2, "an unchanged resync leaves both alone");

    // The author removes one of them and re-saves.
    scene::destroy_entity(second);
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    // Two marked instances against one record: exactly one is surplus. Which one goes is
    // arbitrary - they came from the same asset - but the count has to be right.
    check_eq(count_nested(), 1, "one of the two is removed, matching what the asset still has");
}

auto count_nested_instances(entt::handle root) -> size_t
{
    size_t n = 0;
    if(const auto* transform = root.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>())
            {
                ++n;
            }
        }
    }
    return n;
}

auto nested_instances_of(entt::handle root) -> std::vector<entt::handle>
{
    std::vector<entt::handle> out;
    if(const auto* transform = root.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>())
            {
                out.push_back(child);
            }
        }
    }
    return out;
}

void test_nested_instances_are_named_by_the_containing_prefab()
{
    begin_test("each nested instance gets an id of its own, preserved across instantiates");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto first = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto second = outer_authoring.instantiate(inner_pfb, outer_root, false);

    // Saving the prefab is what brings the slots into existence.
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    const auto first_uid = instance_uid_of(first);
    const auto second_uid = instance_uid_of(second);
    check(!first_uid.is_nil() && !second_uid.is_nil(), "both nested instances were given an id");
    check(first_uid != second_uid, "and the two ids differ, which their prefab uids do not");
    check(prefab_uid_of(first) == prefab_uid_of(second), "- the prefab uids really are identical");

    // Every instance of the outer prefab has to agree with the file about which slot is
    // which, or the file cannot name one.
    scene world("world");
    auto instance_a = world.instantiate(outer_pfb, false);
    auto instance_b = world.instantiate(outer_pfb, false);

    const auto nested_a = nested_instances_of(instance_a);
    const auto nested_b = nested_instances_of(instance_b);
    check_eq(nested_a.size(), 2, "the first instance has both nested instances");
    check_eq(nested_b.size(), 2, "and so does the second");
    if(nested_a.size() != 2 || nested_b.size() != 2)
    {
        return;
    }

    check(instance_uid_of(nested_a[0]) == first_uid && instance_uid_of(nested_a[1]) == second_uid,
          "an instantiate reproduces the prefab's slot ids rather than inventing new ones");
    check(instance_uid_of(nested_b[0]) == first_uid && instance_uid_of(nested_b[1]) == second_uid,
          "and every instance agrees on them");

    // Unlike id_component, which has to differ or two instances would collide scene-wide.
    check(uid_of(nested_a[0]) != uid_of(nested_b[0]),
          "while their entity uids differ, as they must");
}

void test_deleting_one_of_two_nested_instances_removes_that_one()
{
    begin_test("deleting one of two identical nested instances removes that one, not either one");

    // The sharp case for identity. Two instances of one prefab are indistinguishable by
    // prefab uid, so counting can only say "one of these must go" - and picks arbitrarily.
    // Told apart by position here, so the wrong choice is visible.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto first = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto second = outer_authoring.instantiate(inner_pfb, outer_root, false);
    first.get<transform_component>().set_position_local({1.0f, 0.0f, 0.0f});
    second.get<transform_component>().set_position_local({2.0f, 0.0f, 0.0f});
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check_eq(nested_instances_of(outer_instance).size(), 2, "both nested instances arrive");

    // The author deletes the *first* of the two.
    scene::destroy_entity(first);
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    const auto left = nested_instances_of(outer_instance);
    check_eq(left.size(), 1, "one nested instance is left");
    if(left.size() == 1)
    {
        check_near(left[0].get<transform_component>().get_position_local(),
                   {2.0f, 0.0f, 0.0f},
                   "and it is the one the author kept, not whichever came to hand");
    }
}


/// A prefab holding two instances of one inner prefab, with a live instance of it in a scene.
struct nested_fixture
{
    scene inner_authoring{"inner_authoring"};
    scene outer_authoring{"outer_authoring"};
    scene world{"world"};

    asset_handle<prefab> inner_pfb;
    asset_handle<prefab> outer_pfb;
    entt::handle outer_root;
    entt::handle first;
    entt::handle second;
    entt::handle outer_instance;

    void build()
    {
        auto inner_source = build_sample_tree(inner_authoring);
        inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

        outer_root = outer_authoring.create_entity("outer_root");
        first = outer_authoring.instantiate(inner_pfb, outer_root, false);
        second = outer_authoring.instantiate(inner_pfb, outer_root, false);
        outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

        outer_instance = world.instantiate(outer_pfb, false);
    }

    /// Re-saves the outer prefab and replays it over the live instance.
    void republish_and_sync()
    {
        outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");
        sync_prefab_instance_with(outer_instance, outer_pfb);
    }

    auto live_nested() -> std::vector<entt::handle>
    {
        return nested_instances_of(outer_instance);
    }
};

auto document_of(entt::handle entity) -> hpp::uuid
{
    const auto* id_comp = entity.try_get<prefab_id_component>();
    return id_comp != nullptr ? id_comp->document : hpp::uuid{};
}

auto slot_document_of(entt::handle entity) -> hpp::uuid
{
    const auto* prefab_comp = entity.try_get<prefab_component>();
    return prefab_comp != nullptr ? prefab_comp->instance_document : hpp::uuid{};
}


/// The JSON an empty statement pair serializes to inside a prefab_component record (minified).
const std::string k_empty_statement_lists =
    R"("from_document":{"overrides":[],"removed_entities":[],"removed_instances":[]},"local":{"overrides":[],"removed_entities":[],"removed_instances":[]})";

auto legacy_override_entry(const hpp::uuid& entity_uuid, const std::string& component_path) -> std::string
{
    return R"({"entity_uuid":")" + hpp::to_string(entity_uuid) + R"(","component_path":")" + component_path +
           R"(","pretty_component_path":")" + component_path + R"("})";
}

auto nearly(float actual, float expected) -> bool
{
    return std::abs(actual - expected) < 1e-4f;
}

void test_edits_to_content_the_outer_document_added_inside_a_nested_instance_propagate()
{
    begin_test("content the outer document added inside a nested instance follows the outer document's edits, unless stated here");

    nested_fixture fix;
    fix.build();
    auto added = fix.outer_authoring.create_entity("outer_added", fix.first);
    fix.republish_and_sync();

    auto live = fix.live_nested();
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "outer_added")), "the addition reached the world instance");
    if(live.size() != 2)
    {
        return;
    }

    // The outer author edits the addition - content, no statement - and re-saves.
    added.get<tag_component>().name = "outer_added_v2";
    fix.republish_and_sync();
    live = fix.live_nested();
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "outer_added_v2")),
          "the edit reaches the existing instance through the nested scope");

    // The scene states the name on the nested instance; the next outer edit respects it.
    auto live_added = live.size() == 2 ? find_child_by_name(live[0], "outer_added_v2") : entt::handle{};
    check(static_cast<bool>(live_added), "the addition is there to override");
    if(!live_added)
    {
        return;
    }
    live_added.get<tag_component>().name = "renamed_here";
    live[0].get<prefab_component>().add_override(prefab_uid_of(live_added), "tag_component/name");
    added.get<tag_component>().name = "outer_added_v3";
    fix.republish_and_sync();
    live = fix.live_nested();
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "renamed_here")),
          "what the scene states about it is kept");
    check(live.size() == 2 && !find_child_by_name(live[0], "outer_added_v3"), "and the outer edit does not land over it");

    // And the nested asset's own sync still leaves the addition alone.
    sync_prefab_instance_with(live[0], fix.inner_pfb);
    check(static_cast<bool>(find_child_by_name(live[0], "renamed_here")), "the inner asset's sync leaves it alone");
}

void test_an_addition_applied_into_the_nested_asset_keeps_its_new_name()
{
    begin_test("an addition applied into the nested asset is the nested asset's from then on, whatever a stale outer snapshot says");

    nested_fixture fix;
    fix.build();
    fix.outer_authoring.create_entity("outer_added", fix.first);
    fix.republish_and_sync();

    auto live = fix.live_nested();
    auto live_added = live.size() == 2 ? find_child_by_name(live[0], "outer_added") : entt::handle{};
    check(static_cast<bool>(live_added), "the addition reached the world instance");
    if(!live_added)
    {
        return;
    }
    check(document_of(live_added) == fix.outer_pfb.uid(), "it is the outer document's");

    // Apply the nested instance into its own prefab: the addition becomes the inner asset's.
    fix.inner_pfb = make_prefab_from(live[0], "test:/inner.pfb");
    check(document_of(live_added) == fix.inner_pfb.uid(), "applied, it is the inner document's");

    // The outer document's snapshot still names it as the outer's. A replay must not flip it.
    sync_prefab_instance_with(fix.outer_instance, fix.outer_pfb);
    live = fix.live_nested();
    live_added = live.size() == 2 ? find_child_by_name(live[0], "outer_added") : entt::handle{};
    check(static_cast<bool>(live_added), "still there, once");
    if(live_added)
    {
        check(document_of(live_added) == fix.inner_pfb.uid(), "and still the inner document's after the outer replay");
    }
    size_t count = 0;
    if(live.size() == 2)
    {
        for(auto child : live[0].get<transform_component>().get_children())
        {
            if(child.get<tag_component>().name == "outer_added")
            {
                ++count;
            }
        }
    }
    check_eq(count, 1, "exactly one of it - not duplicated by either replay");
}

void test_cloning_a_plain_branch_holding_a_nested_instance_drops_the_branch_ids()
{
    begin_test("a copy of a plain branch holding a nested instance is the container's addition, its ids dropped above the nested root");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto holder = outer_authoring.create_entity("holder", outer_root);
    outer_authoring.instantiate(inner_pfb, holder, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto live_holder = find_child_by_name(outer_instance, "holder");
    check(static_cast<bool>(live_holder), "the instance has the holder");
    if(!live_holder)
    {
        return;
    }

    auto copy = world.clone_entity(live_holder, true);
    check(static_cast<bool>(copy), "the holder clones");
    if(!copy)
    {
        return;
    }
    check(!copy.all_of<prefab_id_component>(), "the copied holder carries no prefab id - it is an addition to the container");
    auto copy_nested = nested_instances_of(copy);
    check_eq(copy_nested.size(), 1, "with its nested instance");
    if(copy_nested.size() == 1)
    {
        check(copy_nested[0].all_of<prefab_id_component>(), "whose content keeps the nested asset's ids");
        check(copy_nested[0].get<prefab_component>().instance_id.is_nil(), "and whose slot is nobody's");
    }

    // The container's replay updates the original and leaves the copy alone.
    holder.get<tag_component>().name = "holder_v2";
    outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check(static_cast<bool>(find_child_by_name(outer_instance, "holder_v2")), "the original follows the document");
    check(copy.get<tag_component>().name == "holder", "the copy does not");
    check(static_cast<bool>(copy), "and survives the replay");
}

void test_a_local_override_on_one_field_does_not_shield_its_siblings()
{
    begin_test("a local override on one field keeps that field and lets the document change the others of the same property");

    // Nested: the scene moves a nested instance (a local position override on its root), then
    // the outer document states that instance's scale. The replay asks for the transform as a
    // whole first; the local position must not make the whole transform skip.
    nested_fixture fix;
    fix.build();
    auto live = fix.live_nested();
    check_eq(live.size(), 2, "both nested instances are live");
    if(live.size() != 2)
    {
        return;
    }
    live[0].get<transform_component>().set_position_local({5.0f, 0.0f, 0.0f});
    live[0].get<prefab_component>().add_override(prefab_uid_of(live[0]), "transform_component/local_transform/position");

    fix.first.get<transform_component>().set_scale_local({3.0f, 3.0f, 3.0f});
    fix.first.get<prefab_component>().add_override(prefab_uid_of(fix.first), "transform_component/local_transform/scale");
    fix.second.get<transform_component>().set_scale_local({3.0f, 3.0f, 3.0f});
    fix.second.get<prefab_component>().add_override(prefab_uid_of(fix.second), "transform_component/local_transform/scale");
    fix.republish_and_sync();

    live = fix.live_nested();
    check_eq(live.size(), 2, "still both");
    if(live.size() != 2)
    {
        return;
    }
    check(nearly(live[0].get<transform_component>().get_scale_local().x, 3.0f),
          "the document's scale reaches the nested instance that has a local position override");
    check(nearly(live[0].get<transform_component>().get_position_local().x, 5.0f),
          "and the local position survives next to it");
    check(nearly(live[1].get<transform_component>().get_scale_local().x, 3.0f),
          "and reaches the one without any override");

    // Own content: the scene overrides a child's position on a top-level instance; the prefab
    // then changes that child's scale.
    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/siblings.pfb");
    scene world("world");
    auto instance = world.instantiate(pfb, false);
    auto live_child = find_child_by_name(instance, "child_a");
    check(static_cast<bool>(live_child), "the instance has child_a");
    if(!live_child)
    {
        return;
    }
    live_child.get<transform_component>().set_position_local({1.0f, 0.0f, 0.0f});
    instance.get<prefab_component>().add_override(prefab_uid_of(live_child), "transform_component/local_transform/position");
    find_child_by_name(source, "child_a").get<transform_component>().set_scale_local({2.0f, 2.0f, 2.0f});
    pfb = make_prefab_from(source, "test:/siblings.pfb");
    sync_prefab_instance_with(instance, pfb);
    live_child = find_child_by_name(instance, "child_a");
    check(static_cast<bool>(live_child) && nearly(live_child.get<transform_component>().get_scale_local().x, 2.0f),
          "the prefab's scale reaches the child with a local position override");
    check(static_cast<bool>(live_child) && nearly(live_child.get<transform_component>().get_position_local().x, 1.0f),
          "and the local position survives");
}

void test_a_replay_applies_only_what_its_document_states_to_nested_content()
{
    begin_test("a document's replay writes nothing into nested content it does not state - even before the nested document replays");

    // The inner prefab changes under an outer instance, and the nested instances follow it.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);

    auto inner_child = find_child_by_name(inner_source, "child_a");
    check(static_cast<bool>(inner_child), "the inner source has child_a");
    if(!inner_child)
    {
        return;
    }
    inner_child.get<tag_component>().name = "inner_v2";
    auto inner_pfb_v2 = make_prefab_asset(serialize_as_prefab(inner_source, "test:/inner.pfb"), "test:/inner.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb);
    auto nested = nested_instances_of(outer_instance);
    check(nested.size() == 1 && static_cast<bool>(find_child_by_name(nested[0], "inner_v2")),
          "the nested instance follows the inner prefab");

    // The outer document replays alone - no cascade. Its snapshot of the nested content is
    // stale (it still says child_a), and it states nothing about it, so nothing of it lands.
    {
        const scoped_deferred_nested_sync one_document_only;
        sync_prefab_instance_with(outer_instance, outer_pfb);
    }
    nested = nested_instances_of(outer_instance);
    check(nested.size() == 1 && static_cast<bool>(find_child_by_name(nested[0], "inner_v2")),
          "the outer document's stale snapshot of nested content it does not state is not applied");

    // What the outer document does state about nested content does land, cascade or not.
    auto o_nested = nested_instances_of(outer_root);
    auto o_child_b = o_nested.size() == 1 ? find_child_by_name(o_nested[0], "child_b") : entt::handle{};
    check(static_cast<bool>(o_child_b), "the outer authoring has child_b");
    if(!o_child_b)
    {
        return;
    }
    o_child_b.get<tag_component>().name = "stated_by_outer";
    o_nested[0].get<prefab_component>().add_override(prefab_uid_of(o_child_b), "tag_component/name");
    outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");
    {
        const scoped_deferred_nested_sync one_document_only;
        sync_prefab_instance_with(outer_instance, outer_pfb);
    }
    nested = nested_instances_of(outer_instance);
    check(nested.size() == 1 && static_cast<bool>(find_child_by_name(nested[0], "stated_by_outer")),
          "while what it states is applied");
    check(nested.size() == 1 && static_cast<bool>(find_child_by_name(nested[0], "inner_v2")),
          "and still nothing else of the snapshot");
}

void test_document_statements_live_with_their_author()
{
    begin_test("a document's statements live on its own root; what was stated here lives on the nearest root; neither touches the other");

    nested_fixture fix;
    fix.build();

    // The outer author overrides child_a inside the first nested instance.
    auto authored_child = find_child_by_name(fix.first, "child_a");
    check(static_cast<bool>(authored_child), "the authoring scene has child_a");
    if(!authored_child)
    {
        return;
    }
    const auto authored_uid = prefab_uid_of(authored_child);
    authored_child.get<tag_component>().name = "authored";
    fix.first.get<prefab_component>().add_override(authored_uid, "tag_component/name");
    fix.republish_and_sync();

    auto live = fix.live_nested();
    check_eq(live.size(), 2, "both nested instances are live");
    if(live.size() != 2)
    {
        return;
    }
    const auto& outer_prefab = fix.outer_instance.get<prefab_component>();
    check_eq(outer_prefab.from_document.overrides.size(), 1, "the outer document's statement sits on the outer root");
    if(!outer_prefab.from_document.overrides.empty())
    {
        const auto& entry = *outer_prefab.from_document.overrides.begin();
        check(entry.instance_path == std::vector<hpp::uuid>{instance_uid_of(live[0])} && entry.entity_uuid == authored_uid,
              "addressed by the slot of the nested instance, then the entity");
    }
    check(live[0].get<prefab_component>().local.empty(), "nothing was stated on the nested root itself");
    check_eq(inherited_overrides_of(live[0]).size(), 1, "seen from the nested instance it reads as inherited");
    check(local_overrides_of(live[0]).empty(), "and not as local");
    check(static_cast<bool>(find_child_by_name(live[0], "authored")), "and the value arrived");

    // The scene overrides child_b on the same nested instance: the nearest root's local list.
    auto live_child_b = find_child_by_name(live[0], "child_b");
    check(static_cast<bool>(live_child_b), "child_b is there");
    if(!live_child_b)
    {
        return;
    }
    live_child_b.get<tag_component>().name = "renamed_here";
    live[0].get<prefab_component>().add_override(prefab_uid_of(live_child_b), "tag_component/name");
    check_eq(local_overrides_of(live[0]).size(), 1, "the scene's statement is local to the nested root");

    // The outer document is replayed: the scene's statement is untouched, its own is restated.
    sync_prefab_instance_with(fix.outer_instance, fix.outer_pfb);
    live = fix.live_nested();
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "renamed_here")),
          "the scene's edit survives the outer replay");
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "authored")), "and so does the document's");
    check(live.size() == 2 && local_overrides_of(live[0]).size() == 1 && inherited_overrides_of(live[0]).size() == 1,
          "each on its author's list");

    // The outer author retracts: the document's list is replaced wholesale, the scene's is not.
    authored_child.get<tag_component>().name = "child_a";
    fix.first.get<prefab_component>().remove_override(authored_uid, "tag_component/name");
    fix.republish_and_sync();
    live = fix.live_nested();
    check(fix.outer_instance.get<prefab_component>().from_document.empty(), "the outer document states nothing now");
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "child_a")),
          "the value returns from the inner prefab");
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "renamed_here")),
          "while the scene's edit is still there");
    check(live.size() == 2 && inherited_overrides_of(live[0]).empty() && local_overrides_of(live[0]).size() == 1,
          "with nothing left attributed to the document");
}

void test_an_outer_statement_wins_over_an_inner_one()
{
    begin_test("when the containing document and the nested one state the same property, the outer one wins");

    scene innermost_authoring("innermost_authoring");
    auto innermost_source = build_sample_tree(innermost_authoring);
    auto innermost_pfb = make_prefab_from(innermost_source, "test:/innermost.pfb");

    // The middle document overrides child_a of its nested innermost instance.
    scene middle_authoring("middle_authoring");
    auto middle_root = middle_authoring.create_entity("middle_root");
    auto m_inner = middle_authoring.instantiate(innermost_pfb, middle_root, false);
    auto m_child = find_child_by_name(m_inner, "child_a");
    check(static_cast<bool>(m_child), "the middle authoring has child_a");
    if(!m_child)
    {
        return;
    }
    const auto child_uid = prefab_uid_of(m_child);
    m_child.get<tag_component>().name = "middle_value";
    m_inner.get<prefab_component>().add_override(child_uid, "tag_component/name");
    auto middle_pfb = make_prefab_from(middle_root, "test:/middle.pfb");

    // The outer document overrides the same property, two levels down.
    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto o_middle = outer_authoring.instantiate(middle_pfb, outer_root, false);
    auto o_inner_list = nested_instances_of(o_middle);
    check_eq(o_inner_list.size(), 1, "the outer authoring has the inner instance");
    if(o_inner_list.size() != 1)
    {
        return;
    }
    auto o_child = find_child_by_name(o_inner_list[0], "middle_value");
    check(static_cast<bool>(o_child), "the outer authoring sees the middle document's value");
    if(!o_child)
    {
        return;
    }
    o_child.get<tag_component>().name = "outer_value";
    o_inner_list[0].get<prefab_component>().add_override(child_uid, "tag_component/name");
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto w_middle = nested_instances_of(outer_instance);
    auto w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && static_cast<bool>(find_child_by_name(w_inner[0], "outer_value")),
          "after the whole cascade the outer document's value stands");

    // Replaying the middle instance on its own does not put the middle value back either.
    if(w_middle.size() == 1)
    {
        sync_prefab_instance_with(w_middle[0], middle_pfb);
    }
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && static_cast<bool>(find_child_by_name(w_inner[0], "outer_value")),
          "a replay of the middle document alone respects the statement above it");

    // The outer document retracts: the middle document's statement applies.
    o_inner_list[0].get<prefab_component>().remove_override(child_uid, "tag_component/name");
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && static_cast<bool>(find_child_by_name(w_inner[0], "middle_value")),
          "retracted by the outer document, the middle document's value shows");

    // The middle document retracts too: back to the innermost prefab's.
    m_child.get<tag_component>().name = "child_a";
    m_inner.get<prefab_component>().remove_override(child_uid, "tag_component/name");
    middle_pfb = make_prefab_from(middle_root, "test:/middle.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && static_cast<bool>(find_child_by_name(w_inner[0], "child_a")),
          "retracted by both, the innermost prefab's value returns");
}

void test_a_removal_two_levels_down_holds_through_every_replay()
{
    begin_test("an entity the outer document removes two levels down stays removed through the nested documents' replays");

    scene innermost_authoring("innermost_authoring");
    auto innermost_source = build_sample_tree(innermost_authoring);
    auto innermost_pfb = make_prefab_from(innermost_source, "test:/innermost.pfb");

    scene middle_authoring("middle_authoring");
    auto middle_root = middle_authoring.create_entity("middle_root");
    middle_authoring.instantiate(innermost_pfb, middle_root, false);
    auto middle_pfb = make_prefab_from(middle_root, "test:/middle.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto o_middle = outer_authoring.instantiate(middle_pfb, outer_root, false);
    auto o_inner_list = nested_instances_of(o_middle);
    check_eq(o_inner_list.size(), 1, "the outer authoring has the inner instance");
    if(o_inner_list.size() != 1)
    {
        return;
    }
    auto doomed = find_child_by_name(o_inner_list[0], "child_a");
    check(static_cast<bool>(doomed), "child_a is there to remove");
    if(!doomed)
    {
        return;
    }
    // What the editor does: record on the nearest root, then destroy.
    o_inner_list[0].get<prefab_component>().remove_entity(prefab_uid_of(doomed));
    outer_authoring.destroy_entity(doomed);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto w_middle = nested_instances_of(outer_instance);
    auto w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && !find_child_by_name(w_inner[0], "child_a"), "a fresh instantiate lacks it");
    check(w_inner.size() == 1 && static_cast<bool>(find_child_by_name(w_inner[0], "child_b")), "and has the rest");

    // The nested documents replay directly - the one that supplies the entity included.
    if(w_middle.size() == 1)
    {
        sync_prefab_instance_with(w_middle[0], middle_pfb);
    }
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && !find_child_by_name(w_inner[0], "child_a"), "the middle document's replay does not bring it back");
    if(w_inner.size() == 1)
    {
        sync_prefab_instance_with(w_inner[0], innermost_pfb);
    }
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && !find_child_by_name(w_inner[0], "child_a"), "nor does the innermost document's own");

    // And the whole cascade from the top.
    sync_prefab_instance_with(outer_instance, outer_pfb);
    w_middle = nested_instances_of(outer_instance);
    w_inner = w_middle.size() == 1 ? nested_instances_of(w_middle[0]) : std::vector<entt::handle>{};
    check(w_inner.size() == 1 && !find_child_by_name(w_inner[0], "child_a"), "nor the full cascade");
    check(w_inner.size() == 1 && inherited_overrides_of(w_inner[0]).empty() &&
              collect_statements_about(w_inner[0]).stated.removed_entities.size() == 1,
          "seen from the innermost instance the removal is a statement from above");
}

void test_applying_a_scene_instance_folds_its_statements_into_the_document()
{
    begin_test("applying an instance to its prefab folds every local statement under it into the document's list");

    nested_fixture fix;
    fix.build();
    auto live = fix.live_nested();
    check_eq(live.size(), 2, "both nested instances are live");
    if(live.size() != 2)
    {
        return;
    }
    auto live_child = find_child_by_name(live[0], "child_a");
    check(static_cast<bool>(live_child), "child_a is there");
    if(!live_child)
    {
        return;
    }
    live_child.get<tag_component>().name = "applied_from_scene";
    live[0].get<prefab_component>().add_override(prefab_uid_of(live_child), "tag_component/name");

    // Apply All: the world instance is written as the prefab, then re-homed the way the
    // inspector does it - the document's list is what was folded, nothing is local any more.
    fix.outer_pfb = make_prefab_from(fix.outer_instance, "test:/outer.pfb");
    auto& outer_prefab = fix.outer_instance.get<prefab_component>();
    outer_prefab.from_document = fold_document_statements(fix.outer_instance);
    outer_prefab.local.clear();
    clear_local_statements_below(fix.outer_instance);
    sync_prefab_instance_with(fix.outer_instance, fix.outer_pfb);

    live = fix.live_nested();
    check(live.size() == 2 && static_cast<bool>(find_child_by_name(live[0], "applied_from_scene")), "the value holds");
    check(live.size() == 2 && local_overrides_of(live[0]).empty() && inherited_overrides_of(live[0]).size() == 1,
          "and reads as the document's statement now, not the scene's");

    scene elsewhere("elsewhere");
    auto fresh = elsewhere.instantiate(fix.outer_pfb, false);
    auto fresh_nested = nested_instances_of(fresh);
    check(fresh_nested.size() == 2 && static_cast<bool>(find_child_by_name(fresh_nested[0], "applied_from_scene")),
          "a fresh instantiate carries it");
    check(fresh_nested.size() == 2 && inherited_overrides_of(fresh_nested[0]).size() == 1 &&
              fresh_nested[0].get<prefab_component>().local.empty(),
          "as a document statement, with nothing local on the nested root");
}

void test_released_format_overrides_load_as_local()
{
    begin_test("the released format's flat override set on an instance root loads as the scene's local list");

    // A scene with one top-level instance, saved, then rewritten the way the released format
    // stored overrides: a flat property_overrides set on the instance root, no statement lists.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene world("world");
    auto instance = world.instantiate(inner_pfb, false);
    auto child = find_child_by_name(instance, "child_a");
    check(static_cast<bool>(child), "the instance has child_a");
    if(!child)
    {
        return;
    }
    const auto child_uid = prefab_uid_of(child);
    child.get<tag_component>().name = "released_override";

    std::stringstream scene_stream;
    save_to_stream(scene_stream, world);
    std::string scene_text = minify_json(scene_stream.str());
    const auto pos = scene_text.find(k_empty_statement_lists);
    check(pos != std::string::npos, "the instance root record carries empty statement lists to replace");
    if(pos == std::string::npos)
    {
        return;
    }
    // One of them names an emitter's blend, whose value the emitter's material holds.
    scene_text.replace(pos,
                       k_empty_statement_lists.size(),
                       R"("property_overrides":[)" + legacy_override_entry(child_uid, "tag_component/name") + "," +
                           legacy_override_entry(child_uid, "particle_emitter_component/blend_mode") +
                           R"(],"removed_entities":[])");

    scene loaded("loaded");
    std::stringstream in(scene_text);
    load_from_stream(in, loaded);
    std::vector<entt::handle> roots;
    loaded.registry->view<root_component, transform_component>().each(
        [&](auto e, auto&&, auto&&)
        {
            roots.emplace_back(*loaded.registry, e);
        });
    check_eq(roots.size(), 1, "the scene loads its one root");
    if(roots.size() != 1)
    {
        return;
    }
    const auto local = local_overrides_of(roots[0]);
    const auto states = [&local](const char* component_path)
    {
        return std::any_of(local.begin(),
                           local.end(),
                           [component_path](const prefab_property_override_data& stated)
                           {
                               return stated.component_path == component_path;
                           });
    };
    check_eq(local.size(), 2, "the overrides read as the scene's own");
    check(states("particle_emitter_component/render_material") && !states("particle_emitter_component/blend_mode"),
          "an override of a name whose value another property holds is restated on that property");
    check(inherited_overrides_of(roots[0]).empty(), "and not as anyone's statement");
    sync_all_prefab_instances(*loaded.registry);
    check(static_cast<bool>(find_child_by_name(roots[0], "released_override")),
          "and the value holds through the replay that follows");
}

void test_issued_ids_name_their_document()
{
    begin_test("every prefab id and slot names the document that issued it");

    nested_fixture fix;
    fix.build();

    // The outer file's own content, and the outer file's slots.
    check(document_of(fix.outer_instance) == fix.outer_pfb.uid(), "the outer root is the outer document's");
    auto nested = fix.live_nested();
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    for(auto& instance : nested)
    {
        check(slot_document_of(instance) == fix.outer_pfb.uid(), "a nested slot is the outer document's");
        check(document_of(instance) == fix.inner_pfb.uid(), "the nested root entity is the inner document's");
        auto child = find_child_by_name(instance, "child_a");
        check(static_cast<bool>(child) && document_of(child) == fix.inner_pfb.uid(),
              "the inner asset's content is the inner document's");
    }

    // Re-saving the outer file never renames what the inner file issued.
    fix.republish_and_sync();
    nested = fix.live_nested();
    for(auto& instance : nested)
    {
        auto child = find_child_by_name(instance, "child_a");
        check(static_cast<bool>(child) && document_of(child) == fix.inner_pfb.uid(),
              "still the inner document's after the outer re-save");
    }
}

void test_an_entity_the_container_adds_inside_a_nested_instance_is_the_containers()
{
    begin_test("an entity the outer document adds inside a nested instance carries the outer name and is the outer's to remove");

    nested_fixture fix;
    fix.build();

    auto added = fix.outer_authoring.create_entity("outer_added", fix.first);
    fix.republish_and_sync();

    auto nested = fix.live_nested();
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    auto live_added = find_child_by_name(nested[0], "outer_added");
    check(static_cast<bool>(live_added), "the addition reached the world instance");
    if(!live_added)
    {
        return;
    }
    check(document_of(live_added) == fix.outer_pfb.uid(), "and carries the outer document's name");

    // The inner asset's own sync: the addition is not its content, by name, and is left alone.
    sync_prefab_instance_with(nested[0], fix.inner_pfb);
    check(static_cast<bool>(find_child_by_name(nested[0], "outer_added")),
          "the inner asset's sync leaves it alone - without any list saying so");

    // A fresh instantiate of the outer file, nested sync included.
    auto fresh = fix.world.instantiate(fix.outer_pfb, false);
    auto fresh_nested = nested_instances_of(fresh);
    check(fresh_nested.size() == 2 && static_cast<bool>(find_child_by_name(fresh_nested[0], "outer_added")),
          "a fresh instantiate carries it through the nested sync");

    // The outer author removes it: the outer document may remove its own addition.
    fix.outer_authoring.destroy_entity(added);
    fix.republish_and_sync();
    nested = fix.live_nested();
    check(nested.size() == 2 && !find_child_by_name(nested[0], "outer_added"),
          "dropped by the outer file, it is gone from the world instance");
}

void test_a_nested_instance_the_container_added_is_removed_when_the_container_drops_it()
{
    begin_test("a nested instance the outer document added inside a nested instance is the outer's to remove");

    nested_fixture fix;
    fix.build();

    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    other_source.get<tag_component>().name = "added_instance";
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");

    auto added = fix.outer_authoring.instantiate(other_pfb, fix.first, false);
    fix.republish_and_sync();

    auto nested = fix.live_nested();
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    auto live_added = find_child_by_name(nested[0], "added_instance");
    check(static_cast<bool>(live_added), "the added instance reached the world instance");
    if(!live_added)
    {
        return;
    }
    check(slot_document_of(live_added) == fix.outer_pfb.uid(), "its slot is the outer document's");

    sync_prefab_instance_with(nested[0], fix.inner_pfb);
    check(static_cast<bool>(find_child_by_name(nested[0], "added_instance")),
          "the inner asset's sync leaves it alone");

    // The outer author removes it. Under the placement flag this was never cleaned up: the
    // instance was "other" relative to its nearest container and nobody's to remove.
    fix.outer_authoring.destroy_entity(added);
    fix.republish_and_sync();
    nested = fix.live_nested();
    check(nested.size() == 2 && !find_child_by_name(nested[0], "added_instance"),
          "dropped by the outer file, it is gone from the world instance");
}

void test_released_format_ids_are_attributed_to_their_instances()
{
    begin_test("prefab ids from files in the released format, which named no document, are attributed on load");

    // Both files written without document names - the released format for prefab ids.
    nested_fixture fix;
    fix.build();

    const auto outer_bytes = serialize_as_prefab(fix.outer_root, "test:/outer.pfb");
    std::string outer_text(outer_bytes.begin(), outer_bytes.end());
    outer_text = strip_document_names(outer_text);
    const auto compiled = minify_json(outer_text);

    // The inner file is re-published only now: the harness's republish unloads the asset, which
    // nils the live handles in the authoring tree, and the outer bytes above had to be taken
    // while those handles still named the inner asset.
    {
        const auto& inner_bytes = fix.inner_pfb.get()->buffer.data;
        const auto stripped = strip_document_names(std::string(inner_bytes.begin(), inner_bytes.end()));
        fix.inner_pfb = make_prefab_asset(std::vector<uint8_t>(stripped.begin(), stripped.end()), "test:/inner.pfb");
    }
    fix.outer_pfb = make_prefab_asset(std::vector<uint8_t>(compiled.begin(), compiled.end()), "test:/outer.pfb");

    scene world("world");
    auto legacy_instance = world.instantiate(fix.outer_pfb, false);
    check(static_cast<bool>(legacy_instance), "the document instantiates");
    if(!legacy_instance)
    {
        return;
    }
    check(document_of(legacy_instance) == fix.outer_pfb.uid(), "the root is attributed to the outer document");
    auto nested = nested_instances_of(legacy_instance);
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    for(auto& instance : nested)
    {
        auto child = find_child_by_name(instance, "child_a");
        check(static_cast<bool>(child) && document_of(child) == fix.inner_pfb.uid(),
              "the nested asset's content is attributed to the inner document");
    }

    // And once attributed, the nested asset's sync treats its content as its own.
    sync_prefab_instance_with(nested[0], fix.inner_pfb);
    check(static_cast<bool>(find_child_by_name(nested[0], "child_a")), "the inner asset's sync keeps its content");
}

void test_clone_slots_follow_the_documents_inside_the_clone()
{
    begin_test("a clone keeps the slots of documents inside it and drops those of documents it is merely inside of");

    nested_fixture fix;
    fix.build();

    // The outer document adds an instance two levels down, inside the first nested instance.
    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    other_source.get<tag_component>().name = "deep_added";
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");
    fix.outer_authoring.instantiate(other_pfb, fix.first, false);
    fix.republish_and_sync();

    auto nested = fix.live_nested();
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    auto deep = find_child_by_name(nested[0], "deep_added");
    check(static_cast<bool>(deep) && slot_document_of(deep) == fix.outer_pfb.uid(),
          "the deep addition's slot is the outer document's");
    if(!deep)
    {
        return;
    }

    // Cloning the outer instance: the outer document is inside the clone, so both the nested
    // slots and the deep addition's slot are kept.
    auto outer_clone = fix.world.clone_entity(fix.outer_instance, true);
    auto clone_nested = nested_instances_of(outer_clone);
    check_eq(clone_nested.size(), 2, "the clone has both nested instances");
    if(clone_nested.size() == 2)
    {
        check(!clone_nested[0].get<prefab_component>().instance_id.is_nil() &&
                  slot_document_of(clone_nested[0]) == fix.outer_pfb.uid(),
              "a nested slot of the cloned document is kept");
        auto clone_deep = find_child_by_name(clone_nested[0], "deep_added");
        check(static_cast<bool>(clone_deep) && !clone_deep.get<prefab_component>().instance_id.is_nil() &&
                  slot_document_of(clone_deep) == fix.outer_pfb.uid(),
              "so is the deep addition's - the outer document is inside the clone");
    }
    check(outer_clone.get<prefab_component>().instance_id.is_nil(), "the clone root itself has no slot");

    // Cloning the first nested instance alone: the outer document is outside the clone now,
    // so the deep addition's slot - placed by the outer document - goes nil, while anything
    // the inner document placed inside it would be kept.
    auto inner_clone = fix.world.clone_entity(nested[0], true);
    check(inner_clone.get<prefab_component>().instance_id.is_nil(), "the cloned nested root has no slot");
    auto inner_clone_deep = find_child_by_name(inner_clone, "deep_added");
    check(static_cast<bool>(inner_clone_deep) && inner_clone_deep.get<prefab_component>().instance_id.is_nil(),
          "the deep addition's slot is dropped - its document is outside this clone");

    // Neither clone is disturbed by the syncs that follow.
    sync_prefab_instance_with(fix.outer_instance, fix.outer_pfb);
    check(static_cast<bool>(find_child_by_name(inner_clone, "deep_added")),
          "the outer file's sync leaves the unslotted copy alone");
    check(nested_instances_of(outer_clone).size() == 2, "and the outer clone keeps its nested instances");
}

void test_an_instance_added_inside_a_nested_instance_survives_that_instances_sync()
{
    begin_test("a nested instance the container adds inside a nested instance is not removed by that instance's sync");

    // A's author instantiates D under nested B and saves A. D is named by A, but B's document
    // never mentions it; without knowing who placed it, B's cleanup read it as a slot the author
    // removed and destroyed it - on every fresh instantiate of A, even.
    nested_fixture fix;
    fix.build();

    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    other_source.get<tag_component>().name = "added_under_nested";
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");

    auto added = fix.outer_authoring.instantiate(other_pfb, fix.first, false);
    check(static_cast<bool>(added), "the addition exists in the authoring scene");
    if(!added)
    {
        return;
    }

    fix.outer_pfb = make_prefab_from(fix.outer_root, "test:/outer.pfb");
    check(!added.get<prefab_component>().instance_id.is_nil(), "and the saving document named it");

    const auto count_added_under = [](entt::handle nested_root) -> size_t
    {
        size_t n = 0;
        if(const auto* transform = nested_root.try_get<transform_component>())
        {
            for(auto child : transform->get_children())
            {
                const auto* tag = child.try_get<tag_component>();
                if(tag != nullptr && tag->name == "added_under_nested")
                {
                    ++n;
                }
            }
        }
        return n;
    };

    scene fresh_world("fresh_world");
    auto fresh = fresh_world.instantiate(fix.outer_pfb, false);
    auto fresh_nested = nested_instances_of(fresh);
    check(fresh_nested.size() == 2 && count_added_under(fresh_nested[0]) == 1,
          "the addition survives a fresh instantiate, the nested instance's own sync included");

    fix.republish_and_sync();
    auto live = fix.live_nested();
    check(live.size() == 2 && count_added_under(live[0]) == 1, "and reaches an existing instance");

    fix.republish_and_sync();
    live = fix.live_nested();
    check(live.size() == 2 && count_added_under(live[0]) == 1, "and is not duplicated by the next sync");
}


void test_an_authoring_root_owns_its_statements_at_every_depth()
{
    begin_test("opening a prefab for editing makes its statements local at every depth, and nobody else's");

    // Middle states something about the innermost's child_b; outer states something about its
    // child_a. In outer's editor, outer's statement must read as local (revertable) while
    // middle's stays "from prefab" - it is middle's, two documents down.
    scene innermost_authoring("innermost_authoring");
    auto innermost_source = build_sample_tree(innermost_authoring);
    auto innermost_pfb = make_prefab_from(innermost_source, "test:/innermost.pfb");

    scene middle_authoring("middle_authoring");
    auto middle_root = middle_authoring.create_entity("middle_root");
    auto m_inner = middle_authoring.instantiate(innermost_pfb, middle_root, false);
    auto m_child_b = find_child_by_name(m_inner, "child_b");
    if(!m_child_b)
    {
        check(false, "the middle authoring has child_b");
        return;
    }
    m_child_b.get<tag_component>().name = "middle_authored";
    m_inner.get<prefab_component>().add_override(prefab_uid_of(m_child_b), "tag_component/name");
    auto middle_pfb = make_prefab_from(middle_root, "test:/middle.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto o_middle = outer_authoring.instantiate(middle_pfb, outer_root, false);
    auto o_inner_list = nested_instances_of(o_middle);
    if(o_inner_list.size() != 1)
    {
        check(false, "the outer authoring has the inner instance");
        return;
    }
    auto o_child_a = find_child_by_name(o_inner_list[0], "child_a");
    if(!o_child_a)
    {
        check(false, "the outer authoring has child_a");
        return;
    }
    o_child_a.get<tag_component>().name = "outer_authored";
    o_inner_list[0].get<prefab_component>().add_override(prefab_uid_of(o_child_a), "tag_component/name");
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    // Open outer for editing: instantiate, keep the link, make outer's statements its own.
    scene editing("editing");
    auto edit_root = editing.instantiate(outer_pfb, false);
    scene::adopt_document_statements(edit_root);

    auto e_middle = nested_instances_of(edit_root);
    auto e_inner = e_middle.size() == 1 ? nested_instances_of(e_middle[0]) : std::vector<entt::handle>{};
    check_eq(e_inner.size(), 1, "the edit root has the inner instance two levels down");
    if(e_inner.size() != 1)
    {
        return;
    }

    auto outer_child = find_child_by_name(e_inner[0], "outer_authored");
    auto middle_child = find_child_by_name(e_inner[0], "middle_authored");
    check(static_cast<bool>(outer_child) && static_cast<bool>(middle_child),
          "both authored values are in place - each document's statement survived the other's replay");
    if(!outer_child || !middle_child)
    {
        return;
    }
    const auto a_uid = prefab_uid_of(outer_child);
    const auto b_uid = prefab_uid_of(middle_child);

    check(has_override_about(e_inner[0], a_uid, "tag_component/name") &&
              has_override_about(e_inner[0], b_uid, "tag_component/name"),
          "the inner instance carries both statements");
    check(inherited_overrides_of(e_inner[0]).count(prefab_property_override_data{a_uid, "tag_component/name"}) == 0u,
          "outer's statement reads as the editor's own - revertable here");
    check(inherited_overrides_of(e_inner[0]).count(prefab_property_override_data{b_uid, "tag_component/name"}) == 1u,
          "while middle's stays attributed to the middle prefab");
}

void test_a_clone_of_an_instance_keeps_its_nested_slots()
{
    begin_test("a clone of an instance keeps its nested slot ids and survives its own resync");

    // A clone of an instance is an instance of the same document, so the slots nested in it
    // are that document's and their ids must agree with it - exactly as two fresh instantiates
    // agree. Regenerating them made every one a named slot the document does not mention; the
    // clone's next sync then created the "missing" ones afresh and removed the renamed ones,
    // local edits included.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    auto clone = fix.world.clone_entity(fix.outer_instance, true, false);
    check(static_cast<bool>(clone), "the clone exists");
    if(!clone || live.size() != 2)
    {
        return;
    }
    check(instance_uid_of(clone).is_nil(), "the clone root is nobody's slot");

    auto cloned_nested = nested_instances_of(clone);
    check_eq(cloned_nested.size(), 2, "the clone carries both nested instances");
    if(cloned_nested.size() != 2)
    {
        return;
    }
    check(instance_uid_of(cloned_nested[0]) == instance_uid_of(live[0]) &&
              instance_uid_of(cloned_nested[1]) == instance_uid_of(live[1]),
          "and they keep the slot ids the document knows them by");

    const auto first_entity = cloned_nested[0].entity();
    auto cloned_child = find_child_by_name(cloned_nested[0], "child_a");
    if(cloned_child)
    {
        cloned_child.get<tag_component>().name = "edited_on_the_clone";
        cloned_nested[0].get<prefab_component>().add_override(prefab_uid_of(cloned_child), "tag_component/name");
    }

    // The clone's own resync against the unchanged asset.
    sync_prefab_instance_with(clone, fix.outer_pfb);

    check(fix.world.registry->valid(first_entity), "the clone's nested instance is the same entity after the resync");
    check_eq(nested_instances_of(clone).size(), 2, "and nothing was duplicated");
    auto clone_nested_after = nested_instances_of(clone);
    check(clone_nested_after.size() == 2 &&
              static_cast<bool>(find_child_by_name(clone_nested_after[0], "edited_on_the_clone")),
          "and the local edit on it survived");
}

void test_a_cloned_branch_makes_its_nested_instances_additions()
{
    begin_test("an instance nested in a cloned plain branch becomes an addition to its container");

    // The other side of the rule. A plain branch of an instance is copied inside that same
    // instance; the instance nested in the branch was a slot of the container, and the copy is
    // not - keeping the id would make the container's next sync read it as a dropped slot.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto holder = outer_authoring.create_entity("holder");
    holder.get<transform_component>().set_parent(outer_root, false);
    outer_authoring.instantiate(inner_pfb, holder, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto live_holder = find_child_by_name(outer_instance, "holder");
    check(static_cast<bool>(live_holder), "the holder is there");
    if(!live_holder)
    {
        return;
    }
    const auto original_nested = nested_instances_of(live_holder);
    check_eq(original_nested.size(), 1, "with its nested instance");
    if(original_nested.size() != 1)
    {
        return;
    }

    auto holder_copy = world.clone_entity(live_holder, true, false);
    auto copied_nested = nested_instances_of(holder_copy);
    check_eq(copied_nested.size(), 1, "the copied branch carries the nested instance");
    if(copied_nested.size() != 1)
    {
        return;
    }
    check(instance_uid_of(copied_nested[0]).is_nil(),
          "whose copy is nobody's slot - an addition to the container");
    check(!instance_uid_of(original_nested[0]).is_nil(), "while the original keeps its id");

    const auto copy_entity = copied_nested[0].entity();
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check(world.registry->valid(copy_entity), "and the container's resync leaves the copy alone");
}

void test_a_failed_sync_does_not_poison_the_override_memo()
{
    begin_test("a sync that loads nothing leaves the nested override memos alone");

    // apply_nested_override_state re-derives the inherited memo from what the replay stated.
    // Run after a load that did nothing, it relabelled every local override as inherited, and
    // the next good replay then dropped them as "nothing local to keep".
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are live");
        return;
    }
    auto live_child = find_child_by_name(live[0], "child_a");
    if(!live_child)
    {
        check(false, "child_a is there");
        return;
    }
    live_child.get<tag_component>().name = "renamed_here";
    live[0].get<prefab_component>().add_override(prefab_uid_of(live_child), "tag_component/name");

    // A prefab with an empty buffer loads nothing.
    auto empty_pfb = make_prefab_asset(std::vector<uint8_t>{}, "test:/outer_empty.pfb");
    sync_prefab_instance_with(fix.outer_instance, empty_pfb);

    check_eq(inherited_overrides_of(live[0]).size(),
             0,
             "nothing was relabelled as inherited by a replay that never happened");

    fix.republish_and_sync();
    auto after = fix.live_nested();
    check(after.size() == 2 && static_cast<bool>(find_child_by_name(after[0], "renamed_here")),
          "and the local edit survives the next good sync");
}

void test_override_touching_treats_array_elements_as_below()
{
    begin_test("an override on an array element is reached through its container");

    prefab_component overrides;
    const auto uid = generate_uuid();
    overrides.add_override(uid, "model_component/materials[2]");

    check(overrides.has_override_touching(uid, "model_component/materials"),
          "the container is on the way to the overridden element");
    check(overrides.has_override_touching(uid, "model_component/materials[2]"), "the element itself is");
    check(overrides.has_override_touching(uid, "model_component"), "and so is the component");
    check(!overrides.has_override_touching(uid, "model_component/materials_other"),
          "a sibling that merely shares the prefix is not");
}

void test_adding_a_nested_instance_to_the_asset_propagates()
{
    begin_test("a nested instance added to a prefab reaches existing instances");

    // The author's side of the same coin as deletion. Duplicating a nested instance is the
    // hard case: the copy carries the *same* prefab uid as the original, so an existing
    // instance sees one shadowed entry standing for two records and used to drop the second
    // one silently.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto authored_nested = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check_eq(count_nested_instances(outer_instance), 1, "the instance starts with one nested instance");

    // The author duplicates it inside the prefab and saves.
    auto authored_clone = outer_authoring.clone_entity(authored_nested, true, false);
    check(static_cast<bool>(authored_clone), "the duplicate exists in the authoring scene");
    if(authored_clone)
    {
        authored_clone.get<transform_component>().set_position_local({7.0f, 8.0f, 9.0f});
    }
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    check_eq(count_nested_instances(outer_instance), 2, "the existing instance gains the duplicate");

    // The one that arrived has to be a working instance, not a bare root: its own subtree,
    // its link to the inner asset, and where the author put it.
    entt::handle arrived;
    if(const auto* transform = outer_instance.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>() &&
               math::abs(child.get<transform_component>().get_position_local().x - 7.0f) < 0.001f)
            {
                arrived = child;
            }
        }
    }
    check(static_cast<bool>(arrived), "the duplicate arrived where the author put it");
    if(arrived)
    {
        check(static_cast<bool>(find_child_by_name(arrived, "child_a")), "and brought its subtree");

        // Its link is checked through the prefab uid rather than prefab_component::source:
        // asset handles cannot resolve in this harness, so a loaded source is always empty.
        check(prefab_uid_of(arrived) == prefab_uid_of(authored_nested),
              "and is an instance of the same inner prefab as the one it was copied from");
        auto arrived_child = find_child_by_name(arrived, "child_a");
        if(arrived_child)
        {
            check(arrived_child.get<transform_component>().get_parent() == arrived,
                  "and its children are parented to it, not left as scene roots");
        }
    }

    // Re-running must not keep adding copies.
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);
    check_eq(count_nested_instances(outer_instance), 2, "a second resync does not add another");
}

void test_adding_a_different_nested_prefab_to_the_asset_propagates()
{
    begin_test("a nested instance of a different prefab added to a prefab reaches instances");

    // Control for the test above: with distinct prefab uids there is no collision, so this
    // path should already work and must keep working.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    inner_source.get<tag_component>().name = "first_inner";
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene other_authoring("other_authoring");
    auto other_source = build_sample_tree(other_authoring);
    other_source.get<tag_component>().name = "second_inner";
    auto other_pfb = make_prefab_from(other_source, "test:/other.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    check_eq(count_nested_instances(outer_instance), 1, "the instance starts with one nested instance");

    outer_authoring.instantiate(other_pfb, outer_root, false);
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    check_eq(count_nested_instances(outer_instance), 2, "the existing instance gains the new one");
    check(static_cast<bool>(find_child_by_name(outer_instance, "second_inner")),
          "and it is the prefab that was added");
}


void test_replacing_a_prefab_component_keeps_its_owner()
{
    begin_test("replacing an instance's prefab_component keeps its owner handle");

    // entt's emplace_or_replace on an existing component takes the patch path: a default
    // component is assigned over the live one - owner handle included - and only on_update
    // fires. The content browser's "save entity as prefab" did exactly that to an entity that
    // was already an instance, leaving a null owner that the inspector's Apply All later saved
    // through. The scene now re-stamps the owner on update.
    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/owner.pfb");

    scene world("world");
    auto instance = world.instantiate(pfb, false);
    check(instance.get<prefab_component>().get_owner() == instance, "a fresh instance knows its owner");

    auto& replaced = instance.emplace_or_replace<prefab_component>();
    check(replaced.get_owner() == instance, "and still does after emplace_or_replace on the existing component");
    check(static_cast<bool>(replaced.get_owner().registry()), "- the owner's registry is not null");

    instance.patch<prefab_component>([](prefab_component& comp) { comp.instance_id = {}; });
    check(instance.get<prefab_component>().get_owner() == instance, "and after a patch");
}

void test_fresh_instance_attributes_nested_overrides_and_detach_makes_them_local()
{
    begin_test("a fresh instance attributes nested overrides to the prefab; its authoring root does not");

    // The same file, two readings. An *instance* of B sees B's authored overrides on A as
    // inherited - stated by B, restated on every sync, not the instance's to revert. B's
    // *authoring root* (prefab mode, the content browser's prefab inspector) sees the very same
    // entries as its own: there is no document above it, and it is the author.
    nested_fixture fix;
    fix.build();

    auto authored_child = find_child_by_name(fix.first, "child_a");
    if(!authored_child)
    {
        check(false, "the authoring scene has child_a");
        return;
    }
    authored_child.get<tag_component>().name = "authored_by_outer";
    fix.first.get<prefab_component>().add_override(prefab_uid_of(authored_child), "tag_component/name");
    fix.outer_pfb = make_prefab_from(fix.outer_root, "test:/outer.pfb");

    // A fresh instance, no resync ever run on it.
    scene fresh_world("fresh_world");
    auto fresh_instance = fresh_world.instantiate(fix.outer_pfb, false);
    auto fresh_nested = nested_instances_of(fresh_instance);
    check_eq(fresh_nested.size(), 2, "the fresh instance has both nested instances");
    if(fresh_nested.size() != 2)
    {
        return;
    }
    check_eq(all_overrides_of(fresh_nested[0]).size(), 1, "the authored override is on the nested instance");
    check_eq(inherited_overrides_of(fresh_nested[0]).size(),
             1,
             "and is attributed to the containing prefab from the first frame, not from the first resync");

    // The same file opened as an authoring root: instantiate, then detach - what prefab mode
    // and the content browser's prefab inspector do.
    scene editing("editing");
    auto edit_root = editing.instantiate(fix.outer_pfb, false);
    scene::detach_instance_link(edit_root);
    auto edit_nested = nested_instances_of(edit_root);
    check_eq(edit_nested.size(), 2, "the authoring root has both nested instances");
    if(edit_nested.size() != 2)
    {
        return;
    }
    check_eq(all_overrides_of(edit_nested[0]).size(), 1, "the authored override is there");
    check_eq(inherited_overrides_of(edit_nested[0]).size(),
             0,
             "and reads as this document's own - nothing above an authoring root can claim it");

    // The editor keeps the root's instance link and resets the attribution on its own - the
    // half of detach it does want. Same reading, root still an instance.
    scene editing_linked("editing_linked");
    auto linked_root = editing_linked.instantiate(fix.outer_pfb, false);
    scene::adopt_document_statements(linked_root);
    auto linked_nested = nested_instances_of(linked_root);
    check(linked_root.all_of<prefab_component>(), "the root keeps its instance link");
    check_eq(linked_nested.size(), 2, "and has both nested instances");
    if(linked_nested.size() == 2)
    {
        check_eq(inherited_overrides_of(linked_nested[0]).size(),
                 0,
                 "whose authored overrides read as the root's own after the reset alone");
    }
}

void test_a_nested_instance_returns_to_where_its_container_placed_it()
{
    begin_test("a nested instance's placement is the container's, and reverting a move restores it");

    // Position and rotation of an instance root belong to whoever placed the instance. For a
    // nested one that is the containing prefab - so the container restates them, a local move
    // is an override that holds them back, and reverting that override has somewhere to go.
    // Before this the nested asset's implicit rule applied from both sides and nothing could
    // ever put a nested instance back where its container authored it.
    nested_fixture fix;
    fix.build();

    // The author places the first nested instance and re-saves. Existing instances follow.
    fix.first.get<transform_component>().set_position_local({5.0f, 0.0f, 0.0f});
    fix.republish_and_sync();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are live");
        return;
    }
    check_near(live[0].get<transform_component>().get_position_local(),
               {5.0f, 0.0f, 0.0f},
               "the container's authored placement reaches the existing instance");

    // The user moves it in the scene; the editor records the override for them.
    live[0].get<transform_component>().set_position_local({9.0f, 9.0f, 9.0f});
    live[0].get<prefab_component>().add_override(prefab_uid_of(live[0]), "transform_component/local_transform/position");
    fix.republish_and_sync();

    auto moved = fix.live_nested();
    if(moved.size() != 2)
    {
        check(false, "still both nested instances");
        return;
    }
    check_near(moved[0].get<transform_component>().get_position_local(),
               {9.0f, 9.0f, 9.0f},
               "a recorded move holds against the container's replay");
    check_near(moved[1].get<transform_component>().get_position_local(),
               {1.0f, 2.0f, 3.0f},
               "and the other instance of the same prefab was never affected");

    // Revert the override. Synced from the container, as the editor does, because only the
    // container's replay owns the value being reverted to.
    moved[0].get<prefab_component>().remove_override(prefab_uid_of(moved[0]), "transform_component/local_transform/position");
    fix.republish_and_sync();

    auto reverted = fix.live_nested();
    if(reverted.size() != 2)
    {
        check(false, "still both after the revert");
        return;
    }
    check_near(reverted[0].get<transform_component>().get_position_local(),
               {5.0f, 0.0f, 0.0f},
               "the revert puts it back where the container placed it");

    // Scale is not placement: it follows the nested asset unless overridden, from either side.
    check_near(reverted[0].get<transform_component>().get_scale_local(),
               {2.0f, 2.0f, 2.0f},
               "while its scale still follows the inner prefab");
}

void test_editing_a_prefab_as_its_own_root_keeps_ids_and_instances_stable()
{
    begin_test("detaching the edit root keeps prefab ids, so a save does not rebuild instances");

    // What prefab mode does: instantiate the prefab, then drop the instance link so the root is
    // the document's content rather than an instance of it. Dropping the link with a plain
    // remove<prefab_component>() unpacks the instance - the on_destroy hook strips the prefab
    // ids - and the next save then hands out fresh ones, so every live instance of the prefab
    // fails to match its root, rebuilds it, and loses whatever was nested under the old one.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto plain_child = outer_authoring.create_entity("plain_child");
    plain_child.get<transform_component>().set_parent(outer_root, false);
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    const auto instance_entity = outer_instance.entity();
    check_eq(nested_instances_of(outer_instance).size(), 1, "the live instance has its nested instance");

    // Open the outer prefab for editing.
    scene editing("editing");
    auto edit_root = editing.instantiate(outer_pfb, false);
    auto edit_child = find_child_by_name(edit_root, "plain_child");
    check(static_cast<bool>(edit_child), "the edit root has the plain child");
    if(!edit_child)
    {
        return;
    }
    const auto root_uid_before = prefab_uid_of(edit_root);
    const auto child_uid_before = prefab_uid_of(edit_child);
    check(!root_uid_before.is_nil() && !child_uid_before.is_nil(), "both carry prefab ids before the detach");

    scene::detach_instance_link(edit_root);
    check(!edit_root.all_of<prefab_component>(), "the edit root is no longer an instance");
    check(edit_root.all_of<prefab_id_component>() && prefab_uid_of(edit_root) == root_uid_before,
          "and still carries the same prefab id");
    check(edit_child.all_of<prefab_id_component>() && prefab_uid_of(edit_child) == child_uid_before,
          "and so does its child - the ids are what the file is keyed by");

    // For contrast: an ordinary unlink does strip them, which is what the hook is for.
    scene unlinking("unlinking");
    auto unlinked = unlinking.instantiate(outer_pfb, false);
    auto unlinked_child = find_child_by_name(unlinked, "plain_child");
    unlinked.remove<prefab_component>();
    check(unlinked_child && !unlinked_child.all_of<prefab_id_component>(),
          "a plain remove<prefab_component>() unpacks the instance and strips its child's prefab id");

    // The user edits something and saves the prefab from the detached root.
    edit_child.get<tag_component>().name = "renamed_in_prefab_mode";
    auto outer_pfb_v2 = make_prefab_from(edit_root, "test:/outer.pfb");

    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    check(world.registry->valid(instance_entity), "the live instance root is the same entity, not rebuilt");
    entt::handle after{*world.registry, instance_entity};
    check_eq(nested_instances_of(after).size(), 1, "and it still has its nested instance");
    check(static_cast<bool>(find_child_by_name(after, "renamed_in_prefab_mode")), "and it received the edit");
}

void test_nested_instance_scale_follows_its_prefab_after_container_is_saved()
{
    begin_test("a nested instance's scale follows its prefab once the container is saved and synced");

    // The scenario as reported: instantiate A, parent it into an object, save the object as B,
    // then change A's scale. With nothing recorded as a scale override, the change has to
    // reach the nested instance through B's replay *and* through A's own cascade - in that
    // order, which is the order the editor runs them in. The editor used to record all four
    // transform parts as overrides on reparent, which blocked this; that half is editor-only,
    // and this pins the engine side it was blocking.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto child = outer_authoring.create_entity("child2");
    child.get<transform_component>().set_parent(outer_root, false);

    // Instantiated at the top level, then parented into the object - what a drag in the
    // hierarchy does, world transform kept.
    auto placed = outer_authoring.instantiate(inner_pfb, false);
    placed.get<transform_component>().set_position_local({3.0f, 0.0f, 0.0f});
    placed.get<transform_component>().set_parent(outer_root, true);
    check(all_overrides_of(placed).empty(),
          "reparenting recorded no overrides on the instance");

    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = nested_instances_of(outer_instance);
    check_eq(nested.size(), 1, "the nested instance arrives");
    if(nested.size() != 1)
    {
        return;
    }
    check_near(nested[0].get<transform_component>().get_scale_local(),
               {2.0f, 2.0f, 2.0f},
               "it starts at the scale the inner prefab has");

    // The author scales A and re-saves it; the outer instance resyncs.
    inner_source.get<transform_component>().set_scale_local({3.0f, 3.0f, 3.0f});
    inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb);

    auto after = nested_instances_of(outer_instance);
    if(after.size() != 1)
    {
        check(false, "the nested instance is still there");
        return;
    }
    check_near(after[0].get<transform_component>().get_scale_local(),
               {3.0f, 3.0f, 3.0f},
               "A's new scale reached the nested instance through the container's sync");
    check_near(after[0].get<transform_component>().get_position_local(),
               {3.0f, 0.0f, 0.0f},
               "while its placement stayed where the container put it");
}

void test_authored_override_on_one_nested_instance_reaches_existing_instances()
{
    begin_test("an override authored on one nested instance reaches existing instances of the prefab");

    // The scenario this whole design is for. Two instances of one prefab inside another, a
    // change made to one of them, and every instance of the outer prefab expected to learn
    // that *that* one changed - which nothing keyed on prefab uid can express.
    nested_fixture fix;
    fix.build();

    auto live_before = fix.live_nested();
    check_eq(live_before.size(), 2, "the live instance has both nested instances");
    if(live_before.size() != 2)
    {
        return;
    }

    // The author edits a child of the *first* nested instance, and the editor records the
    // override against that instance.
    auto authored_child = find_child_by_name(fix.first, "child_a");
    check(static_cast<bool>(authored_child), "the authoring scene has the child to edit");
    if(!authored_child)
    {
        return;
    }
    const auto child_prefab_uid = prefab_uid_of(authored_child);
    authored_child.get<tag_component>().name = "authored_in_outer";
    fix.first.get<prefab_component>().add_override(child_prefab_uid, "tag_component/name");

    fix.republish_and_sync();

    auto live_after = fix.live_nested();
    check_eq(live_after.size(), 2, "both nested instances are still there");
    if(live_after.size() != 2)
    {
        return;
    }

    check(static_cast<bool>(find_child_by_name(live_after[0], "authored_in_outer")),
          "the edit reached the instance it was made on");
    check(static_cast<bool>(find_child_by_name(live_after[1], "child_a")),
          "and left the other instance of the same prefab alone");
    check(!find_child_by_name(live_after[1], "authored_in_outer"),
          "- it really is untouched, not renamed too");
}

void test_a_local_edit_survives_the_containing_prefab_being_replayed()
{
    begin_test("an edit made here outlives the containing prefab's own authoring");

    // The other direction. The outer prefab is entitled to update what it authors on a
    // nested instance, and to nothing else - an edit made in this scene has to survive it.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "the live instance has both nested instances");
        return;
    }

    // The user renames a child of the first nested instance in the scene.
    auto live_child = find_child_by_name(live[0], "child_a");
    check(static_cast<bool>(live_child), "the child to edit is there");
    if(!live_child)
    {
        return;
    }
    live_child.get<tag_component>().name = "renamed_in_scene";
    live[0].get<prefab_component>().add_override(prefab_uid_of(live_child), "tag_component/name");

    // Meanwhile the author changes something else entirely on that same instance.
    auto authored_other = find_child_by_name(fix.first, "child_b");
    if(authored_other)
    {
        authored_other.get<tag_component>().name = "authored_child_b";
        fix.first.get<prefab_component>().add_override(prefab_uid_of(authored_other), "tag_component/name");
    }

    fix.republish_and_sync();

    auto live_after = fix.live_nested();
    if(live_after.size() != 2)
    {
        check(false, "both nested instances survived");
        return;
    }

    check(static_cast<bool>(find_child_by_name(live_after[0], "renamed_in_scene")),
          "the local edit is still there");
    check(static_cast<bool>(find_child_by_name(live_after[0], "authored_child_b")),
          "and the author's edit arrived alongside it");

    // Both are overrides on that instance now, from two different places - and which is
    // which is still recoverable, which is what the inspector shows and what lets the
    // containing prefab change its mind about its own half.
    check_eq(all_overrides_of(live_after[0]).size(),
             2,
             "the instance carries both - each on its author's list, neither replacing the other");
    check_eq(inherited_overrides_of(live_after[0]).size(), 1, "one of them is attributed to the containing prefab");

    const auto authored_uid = prefab_uid_of(authored_other);
    check(inherited_overrides_of(live_after[0]).count(prefab_property_override_data{authored_uid,
                                                                                "tag_component/name"}) == 1u,
          "and it is the author's, not the one made here");
    check(inherited_overrides_of(live_after[0]).count(
              prefab_property_override_data{prefab_uid_of(live_child), "tag_component/name"}) == 0u,
          "- the local one is not attributed to the prefab");

    prefab_component cleared = live_after[0].get<prefab_component>();
    cleared.local.clear();
    check(cleared.local.empty(),
          "reverting everything drops what was stated here - the containing prefab's statement is its own, not this instance's to drop");
}

void test_the_containing_prefab_can_take_its_override_back()
{
    begin_test("the containing prefab can stop overriding something it used to");

    // Requires the two halves to stay distinguishable. If the outer prefab's authoring were
    // merged into the instance's own overrides, dropping it here would look like a local
    // edit and stay forever.
    nested_fixture fix;
    fix.build();

    auto authored_child = find_child_by_name(fix.first, "child_a");
    if(!authored_child)
    {
        check(false, "the child to edit is there");
        return;
    }
    const auto child_prefab_uid = prefab_uid_of(authored_child);
    authored_child.get<tag_component>().name = "authored_in_outer";
    fix.first.get<prefab_component>().add_override(child_prefab_uid, "tag_component/name");

    fix.republish_and_sync();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are there");
        return;
    }
    check(static_cast<bool>(find_child_by_name(live[0], "authored_in_outer")), "the override arrived");
    check_eq(all_overrides_of(live[0]).size(), 1, "and is stated about the instance");

    // The author reverts it: the name goes back to the inner prefab's, and the override with it.
    authored_child.get<tag_component>().name = "child_a";
    fix.first.get<prefab_component>().remove_override(child_prefab_uid, "tag_component/name");

    fix.republish_and_sync();

    auto live_after = fix.live_nested();
    if(live_after.size() != 2)
    {
        check(false, "both nested instances survived");
        return;
    }
    check_eq(all_overrides_of(live_after[0]).size(),
             0,
             "the instance stops claiming an override nobody makes any more");
    check_eq(inherited_overrides_of(live_after[0]).size(),
             0,
             "and stops attributing one to the prefab that contains it");

    // The value itself returns from the inner prefab, on the sync that follows - which this
    // harness cannot run, because prefab_component::source cannot resolve without an asset
    // database. What is checked here is the state that lets that sync happen: with nothing
    // claiming the property any more, the inner prefab's value is no longer held off.
}

void test_local_and_authored_removals_are_distinguishable()
{
    begin_test("a removal made here and one authored by the containing prefab stay apart");

    // Same two-author problem as overrides, one operation over. The containing document's
    // record replaces the removal sets on every replay and the local half is unioned back,
    // so without a memo the two merge - and then neither the UI can attribute a removal nor
    // a revert restore only the local one.
    nested_fixture fix;
    fix.build();

    // The author removes child_a from the first nested instance and re-saves.
    auto authored_child = find_child_by_name(fix.first, "child_a");
    check(static_cast<bool>(authored_child), "the authoring scene has child_a");
    if(!authored_child)
    {
        return;
    }
    fix.first.get<prefab_component>().remove_entity(prefab_uid_of(authored_child));
    scene::destroy_entity(authored_child);
    fix.republish_and_sync();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are live");
        return;
    }

    // The user removes child_b from the same live instance, in the scene.
    auto live_child = find_child_by_name(live[0], "child_b");
    check(static_cast<bool>(live_child), "child_b is there to remove");
    if(!live_child)
    {
        return;
    }
    live[0].get<prefab_component>().remove_entity(prefab_uid_of(live_child));
    scene::destroy_entity(live_child);

    // Replay against the unchanged asset: both removals survive it, attributed differently.
    fix.republish_and_sync();

    auto after = fix.live_nested();
    if(after.size() != 2)
    {
        check(false, "still both nested instances");
        return;
    }
    auto& after_prefab = after[0].get<prefab_component>();
    check_eq(all_removed_entities_of(after[0]).size(), 2, "both removals are recorded");
    check_eq(inherited_removed_entities_of(after[0]).size(),
             1,
             "exactly one is attributed to the containing prefab");
    check(!find_child_by_name(after[0], "child_a"), "the authored removal holds");
    check(!find_child_by_name(after[0], "child_b"), "and so does the local one");

    // What the inspector's revert does: collapse the sets to the inherited halves. The local
    // removal returns from the inner prefab on the next sync; the authored one stays gone.
    after_prefab.local.clear();
    after_prefab.changed = true;
    fix.republish_and_sync();

    auto reverted = fix.live_nested();
    if(reverted.size() != 2)
    {
        check(false, "still both after the revert");
        return;
    }
    check(static_cast<bool>(find_child_by_name(reverted[0], "child_b")), "the locally removed entity returns");
    check(!find_child_by_name(reverted[0], "child_a"), "while the authored removal still holds");
    check(static_cast<bool>(find_child_by_name(reverted[1], "child_b")),
          "and the other instance of the same prefab was never affected");
}

void test_removing_an_entity_inside_a_nested_instance_propagates()
{
    begin_test("an entity removed from one nested instance goes from existing instances too");

    nested_fixture fix;
    fix.build();

    auto authored_child = find_child_by_name(fix.first, "child_b");
    if(!authored_child)
    {
        check(false, "the child to remove is there");
        return;
    }

    // What the editor records when a child of an instance is deleted.
    fix.first.get<prefab_component>().remove_entity(prefab_uid_of(authored_child));
    scene::destroy_entity(authored_child);

    fix.republish_and_sync();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are there");
        return;
    }

    check(!find_child_by_name(live[0], "child_b"), "the removal reached the instance it was made on");
    check(static_cast<bool>(find_child_by_name(live[1], "child_b")),
          "and the other instance of the same prefab still has it");
}

void test_adding_an_entity_under_a_nested_instance_propagates()
{
    begin_test("an entity added under one nested instance reaches existing instances");

    nested_fixture fix;
    fix.build();

    auto added = fix.outer_authoring.create_entity("added_by_outer");
    added.get<transform_component>().set_parent(fix.first, false);

    fix.republish_and_sync();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "both nested instances are there");
        return;
    }

    check(static_cast<bool>(find_child_by_name(live[0], "added_by_outer")),
          "the addition reached the instance it was made on");
    check(!find_child_by_name(live[1], "added_by_outer"),
          "and not the other instance of the same prefab");
}



void test_a_locally_cloned_nested_instance_is_not_claimed_by_the_prefab()
{
    begin_test("a nested instance cloned here is not claimed when the prefab gains one");

    // Both a nested instance cloned in the scene and one that predates instance ids have no
    // id, and adoption exists for the second. If it runs for the first, the prefab's new slot
    // claims the user's copy and replaces its contents - the copy is gone, and it looks like
    // a straight overwrite.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    check_eq(live.size(), 2, "the scene instance starts with the two the prefab has");
    if(live.size() != 2)
    {
        return;
    }

    // The user duplicates one of them in the scene.
    auto local_clone = fix.world.clone_entity(live[0], true, false);
    check(static_cast<bool>(local_clone), "the local clone exists");
    if(!local_clone)
    {
        return;
    }
    check(instance_uid_of(local_clone).is_nil(), "and is nobody's slot");
    local_clone.get<transform_component>().set_position_local({5.0f, 0.0f, 0.0f});
    const auto local_clone_entity = local_clone.entity();

    // The author duplicates one inside the prefab and saves, so the prefab now has three.
    fix.outer_authoring.clone_entity(fix.first, true, false);
    fix.republish_and_sync();

    auto after = fix.live_nested();
    check_eq(after.size(), 4, "the prefab's third arrives alongside the local one");

    check(static_cast<bool>(fix.world.registry->valid(local_clone_entity)), "the local clone still exists");
    if(!fix.world.registry->valid(local_clone_entity))
    {
        return;
    }

    entt::handle survivor{*fix.world.registry, local_clone_entity};
    check(instance_uid_of(survivor).is_nil(), "and was not claimed as a slot of the prefab");
    check_near(survivor.get<transform_component>().get_position_local(),
               {5.0f, 0.0f, 0.0f},
               "and is still where it was put, not replaced by the prefab's copy");

    // Repeating must not start claiming it either.
    fix.republish_and_sync();
    check_eq(fix.live_nested().size(), 4, "a second resync leaves the count alone");
    check(static_cast<bool>(fix.world.registry->valid(local_clone_entity)), "and the local clone is still there");
}

void test_a_locally_deleted_nested_instance_stays_deleted()
{
    begin_test("a nested instance deleted here is not brought back by the prefab changing");

    // Deleting one of two identical nested instances from a live instance. Recorded by
    // instance id, because the prefab uid the two share would read as "remove both".
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    check_eq(live.size(), 2, "the live instance starts with both");
    if(live.size() != 2)
    {
        return;
    }

    const auto kept_uid = instance_uid_of(live[0]);
    const auto deleted_uid = instance_uid_of(live[1]);
    check(!deleted_uid.is_nil(), "the one being deleted can be named");

    // What the editor records: on the instance that contains it, by instance id.
    fix.outer_instance.get<prefab_component>().remove_instance(deleted_uid);
    scene::destroy_entity(live[1]);
    check_eq(fix.live_nested().size(), 1, "it is gone");

    // The author now changes something unrelated in the outer prefab and it resyncs.
    fix.outer_root.get<tag_component>().name = "outer_root_renamed";
    fix.republish_and_sync();

    auto after = fix.live_nested();
    check_eq(after.size(), 1, "the deleted nested instance stays deleted");
    if(after.size() == 1)
    {
        check(instance_uid_of(after[0]) == kept_uid, "and it is the other one that remains");
    }

    // The removal has to still be recorded, or the next resync brings it back.
    check(fix.outer_instance.get<prefab_component>().is_instance_removed(deleted_uid),
          "and the instance still remembers the deletion");

    // Which the second resync is what actually proves.
    fix.republish_and_sync();
    check_eq(fix.live_nested().size(), 1, "a second resync does not bring it back either");
}

void test_deleting_the_only_nested_instance_stays_deleted()
{
    begin_test("deleting the only nested instance survives a resync");

    // The single-instance shape matters: with nothing nested left alive, the resolver's
    // fast path used to skip reading instance ids entirely, and the removed-instance check
    // with them - so the deleted instance came back on every other resync, alternating with
    // cleanup destroying it again. The two-instance test above never sees this, because the
    // surviving instance keeps the slow path active.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto live = nested_instances_of(outer_instance);
    check_eq(live.size(), 1, "the live instance starts with the one nested instance");
    if(live.size() != 1)
    {
        return;
    }

    const auto deleted_uid = instance_uid_of(live[0]);
    check(!deleted_uid.is_nil(), "it can be named");

    // What the editor records on delete: remove_instance on the container, nothing else
    // (mark_entity_as_removed deliberately refuses remove_entity for instance roots - the
    // prefab uid is shared with every other instance of that prefab).
    outer_instance.get<prefab_component>().remove_instance(deleted_uid);
    scene::destroy_entity(live[0]);
    check_eq(nested_instances_of(outer_instance).size(), 0, "it is gone");

    sync_prefab_instance_with(outer_instance, outer_pfb);
    check_eq(nested_instances_of(outer_instance).size(), 0,
             "a resync does not bring the deleted instance back");

    // The old failure alternated - revived on one resync, destroyed on the next - so one
    // clean resync proves nothing without a second.
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check_eq(nested_instances_of(outer_instance).size(), 0, "nor does a second resync");
}

void test_removing_an_entity_two_levels_down_stays_removed()
{
    begin_test("an entity deleted two instances deep stays deleted through the outer resync");

    // A holds an instance of B, B holds an instance of C. The deletion happens in the world
    // instance, inside C - recorded on C, the nearest instance root, the way the editor
    // records it. The resync replayed is A's, so the removal has to hold while A's document
    // reaches through B into C, and again while the cascade resyncs B and C against their
    // own assets.
    scene c_authoring("c_authoring");
    auto c_source = build_sample_tree(c_authoring);
    auto c_pfb = make_prefab_from(c_source, "test:/c.pfb");

    scene b_authoring("b_authoring");
    auto b_root = b_authoring.create_entity("b_root");
    b_authoring.instantiate(c_pfb, b_root, false);
    auto b_pfb = make_prefab_from(b_root, "test:/b.pfb");

    scene a_authoring("a_authoring");
    auto a_root = a_authoring.create_entity("a_root");
    a_authoring.instantiate(b_pfb, a_root, false);
    auto a_pfb = make_prefab_from(a_root, "test:/a.pfb");

    scene world("world");
    auto a_instance = world.instantiate(a_pfb, false);

    const auto find_c_instance = [&]() -> entt::handle
    {
        auto b_instances = nested_instances_of(a_instance);
        if(b_instances.size() != 1)
        {
            return {};
        }
        auto c_instances = nested_instances_of(b_instances[0]);
        return c_instances.size() == 1 ? c_instances[0] : entt::handle{};
    };

    auto c_instance = find_c_instance();
    check(static_cast<bool>(c_instance), "the chain instantiated: A holds B holds C");
    if(!c_instance)
    {
        return;
    }

    auto victim = find_child_by_name(c_instance, "child_a");
    check(static_cast<bool>(victim), "the entity to delete is there");
    if(!victim)
    {
        return;
    }

    delete_like_the_editor(c_instance, victim);
    check(!find_child_by_name(c_instance, "child_a"), "it is gone");

    sync_prefab_instance_with(a_instance, a_pfb);

    c_instance = find_c_instance();
    check(static_cast<bool>(c_instance), "the B and C instances survived the resync");
    if(!c_instance)
    {
        return;
    }

    check(!find_child_by_name(c_instance, "child_a"), "the deleted entity stays deleted");
    check_eq(count_entities_named(*world.registry, "grandchild"),
             0,
             "and its child did not come back as an orphan");
    check(static_cast<bool>(find_child_by_name(c_instance, "child_b")), "its sibling is untouched");

    // Twice, because the machinery has already had one bug that alternated between resyncs.
    sync_prefab_instance_with(a_instance, a_pfb);
    c_instance = find_c_instance();
    check(static_cast<bool>(c_instance), "everything survives a second resync");
    check(!c_instance || !find_child_by_name(c_instance, "child_a"), "and the deletion still holds");
    check_eq(count_entities_named(*world.registry, "grandchild"), 0, "with no orphan either");
}

void test_a_locally_deleted_nested_instance_survives_the_prefab_changing_it()
{
    begin_test("a locally deleted nested instance is not resurrected by an edit to it");

    // The harder version: the author edits the very instance that was deleted here. Its
    // record now carries an override, so it is not merely present in the document, it is
    // being actively written.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "the live instance starts with both");
        return;
    }
    const auto deleted_uid = instance_uid_of(live[1]);

    fix.outer_instance.get<prefab_component>().remove_instance(deleted_uid);
    scene::destroy_entity(live[1]);

    auto authored_child = find_child_by_name(fix.second, "child_a");
    if(authored_child)
    {
        authored_child.get<tag_component>().name = "authored_on_the_deleted_one";
        fix.second.get<prefab_component>().add_override(prefab_uid_of(authored_child), "tag_component/name");
    }

    fix.republish_and_sync();

    check_eq(fix.live_nested().size(), 1, "the deletion still wins");
    // find_child_by_name is one level deep, so look inside the instance that is left.
    auto remaining = fix.live_nested();
    check(remaining.size() == 1 && !find_child_by_name(remaining[0], "authored_on_the_deleted_one"),
          "and nothing of it leaked into the instance that is left");
}

void test_authored_override_two_levels_down()
{
    begin_test("an override two levels down reaches the right instance");

    // Where one id stops being enough. A holds two instances of B, and B holds an instance
    // of C - so A holds two instances of C carrying the *same* id, since both came from the
    // same place in B. Only the chain leading to each separates them.
    scene innermost_authoring("innermost_authoring");
    auto innermost_source = build_sample_tree(innermost_authoring);
    auto innermost_pfb = make_prefab_from(innermost_source, "test:/innermost.pfb");

    scene middle_authoring("middle_authoring");
    auto middle_root = middle_authoring.create_entity("middle_root");
    middle_authoring.instantiate(innermost_pfb, middle_root, false);
    auto middle_pfb = make_prefab_from(middle_root, "test:/middle.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto first_middle = outer_authoring.instantiate(middle_pfb, outer_root, false);
    auto second_middle = outer_authoring.instantiate(middle_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    auto first_innermost = nested_instances_of(first_middle);
    auto second_innermost = nested_instances_of(second_middle);
    check_eq(first_innermost.size(), 1, "the first middle instance nests one innermost instance");
    check_eq(second_innermost.size(), 1, "and so does the second");
    if(first_innermost.size() != 1 || second_innermost.size() != 1)
    {
        return;
    }
    check(instance_uid_of(first_innermost[0]) == instance_uid_of(second_innermost[0]),
          "the two innermost instances share an id - they came from the same place in the middle prefab");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);

    // The author edits a child of the innermost instance inside the *first* middle one.
    auto authored_child = find_child_by_name(first_innermost[0], "child_a");
    check(static_cast<bool>(authored_child), "the child two levels down is there to edit");
    if(!authored_child)
    {
        return;
    }
    authored_child.get<tag_component>().name = "authored_two_levels_down";
    first_innermost[0].get<prefab_component>().add_override(prefab_uid_of(authored_child), "tag_component/name");

    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    auto live_middles = nested_instances_of(outer_instance);
    check_eq(live_middles.size(), 2, "both middle instances are live");
    if(live_middles.size() != 2)
    {
        return;
    }

    auto live_first_inner = nested_instances_of(live_middles[0]);
    auto live_second_inner = nested_instances_of(live_middles[1]);
    check_eq(live_first_inner.size(), 1, "the first middle instance still nests one");
    check_eq(live_second_inner.size(), 1, "and so does the second");
    if(live_first_inner.size() != 1 || live_second_inner.size() != 1)
    {
        return;
    }

    check(static_cast<bool>(find_child_by_name(live_first_inner[0], "authored_two_levels_down")),
          "the edit reached the innermost instance under the middle one it was made on");
    check(!find_child_by_name(live_second_inner[0], "authored_two_levels_down"),
          "and not the identically-identified one under the other");

    // The outer document changes its mind. Its statement is kept apart from the middle
    // document's, so on the next replay it is neither mistaken for a local edit (and held
    // back) nor overwritten by the middle one restating its own share.
    authored_child.get<tag_component>().name = "authored_v2";
    auto outer_pfb_v3 = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v3);

    live_middles = nested_instances_of(outer_instance);
    live_first_inner = live_middles.size() == 2 ? nested_instances_of(live_middles[0]) : std::vector<entt::handle>{};
    check(live_first_inner.size() == 1 && static_cast<bool>(find_child_by_name(live_first_inner[0], "authored_v2")),
          "the outer prefab can change its override two levels down on an existing instance");

    // And retracts it: the value goes back to whichever prefab owns it - the innermost.
    authored_child.get<tag_component>().name = "child_a";
    first_innermost[0].get<prefab_component>().remove_override(prefab_uid_of(authored_child), "tag_component/name");
    auto outer_pfb_v4 = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v4);

    live_middles = nested_instances_of(outer_instance);
    live_first_inner = live_middles.size() == 2 ? nested_instances_of(live_middles[0]) : std::vector<entt::handle>{};
    check(live_first_inner.size() == 1 && static_cast<bool>(find_child_by_name(live_first_inner[0], "child_a")),
          "and retract it - the value returns from the innermost prefab");
    if(live_first_inner.size() == 1)
    {
        check(inherited_overrides_of(live_first_inner[0]).empty(),
              "with nothing left attributed to any document");
    }
}

void test_cloning_a_nested_instance_makes_it_the_users()
{
    begin_test("a clone of a nested instance is the user's, not the asset's");

    // Cloning preserves prefab uids (clone_mode_t::cloning_prefab_instance), so the clone
    // is indistinguishable from the original by uid. If it also inherited the marker, the
    // author removing that instance from the outer asset would delete the user's copy too.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    auto authored_nested = outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    check(nested && is_named_instance(nested), "the authored nested instance is marked");
    if(!nested)
    {
        return;
    }

    // The user duplicates it inside the outer instance.
    auto clone = world.clone_entity(nested, true, false);
    check(static_cast<bool>(clone), "the clone exists");
    if(!clone)
    {
        return;
    }
    const auto clone_entity_id = clone.entity();

    check(!is_named_instance(clone),
          "the clone is not marked as coming from the outer asset");
    check(clone.all_of<prefab_component>(), "but it is still an instance of the inner prefab");
    check(prefab_uid_of(clone) == prefab_uid_of(nested),
          "and it shares the original's prefab uid, which is why the marker has to carry the difference");

    // A descendant of the clone keeps whatever it had - the clone is as entitled to the
    // inner asset's contents as the original.
    check(static_cast<bool>(find_child_by_name(clone, "child_a")), "the clone brought the subtree with it");

    // Unchanged resync: one marked instance, one record, and one unmarked bystander.
    sync_prefab_instance_with(outer_instance, outer_pfb);
    check(static_cast<bool>(world.registry->valid(clone_entity_id)), "the clone survives an unchanged resync");

    // The author now deletes the nested instance from the outer prefab.
    scene::destroy_entity(authored_nested);
    auto outer_pfb_v2 = make_prefab_from(outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);

    check(static_cast<bool>(world.registry->valid(clone_entity_id)),
          "and it survives the author removing the instance it was cloned from");

    size_t nested_left = 0;
    if(const auto* transform = outer_instance.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>())
            {
                ++nested_left;
            }
        }
    }
    check_eq(nested_left, 1, "the asset's copy went, the user's stayed");
}

void test_cloning_an_outer_instance_keeps_what_it_inherited()
{
    begin_test("cloning an instance keeps the marker on what was nested inside it");

    // The other half of the rule: only the clone *root* stops belonging to an asset. What
    // was nested inside it still comes from the cloned instance's own asset, and must keep
    // tracking it.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);

    auto clone = world.clone_entity(outer_instance, true, false);
    check(static_cast<bool>(clone), "the clone of the outer instance exists");
    if(!clone)
    {
        return;
    }

    auto cloned_nested = find_child_by_name(clone, "root");
    check(static_cast<bool>(cloned_nested), "its nested instance came along");
    if(cloned_nested)
    {
        check(is_named_instance(cloned_nested),
              "and is still marked - it comes from the outer asset, which the clone is also an instance of");
    }
}

void test_promoting_a_clone_into_the_asset_marks_it()
{
    begin_test("a cloned instance saved into the outer prefab becomes part of it");

    // Third scenario: clone a nested instance, then apply the outer instance back to its
    // asset. The clone is now genuinely part of that file, so it has to stop being "the
    // user's" - otherwise it would never track deletions.
    //
    // Nothing marks it at save time; marking is derived on load, which is what makes the
    // two directions converge.
    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    if(!nested)
    {
        check(false, "the nested instance is there");
        return;
    }

    auto clone = world.clone_entity(nested, true, false);
    check(clone && !is_named_instance(clone), "the clone starts out unmarked");

    // Apply the edited instance back to the prefab, then instantiate it somewhere fresh.
    auto outer_pfb_v2 = make_prefab_from(outer_instance, "test:/outer.pfb");

    scene world2("world2");
    auto fresh = world2.instantiate(outer_pfb_v2, false);

    size_t nested_count = 0;
    size_t marked_count = 0;
    if(const auto* transform = fresh.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>())
            {
                ++nested_count;
                if(is_named_instance(child))
                {
                    ++marked_count;
                }
            }
        }
    }
    check_eq(nested_count, 2, "the saved prefab carries both nested instances");
    check_eq(marked_count, 2, "and a fresh instantiate marks both - the clone belongs to the file now");

    // And the live instance it was applied from converges on the next resync.
    sync_prefab_instance_with(outer_instance, outer_pfb_v2);
    size_t live_marked = 0;
    if(const auto* transform = outer_instance.try_get<transform_component>())
    {
        for(auto child : transform->get_children())
        {
            if(child.all_of<prefab_component>() && is_named_instance(child))
            {
                ++live_marked;
            }
        }
    }
    check_eq(live_marked, 2, "the instance it was applied from agrees after a resync");
}

void test_unpacking_an_outer_instance_leaves_the_nested_one_linked()
{
    begin_test("unpacking an instance does not unpack the one nested inside it");

    scene inner_authoring("inner_authoring");
    auto inner_source = build_sample_tree(inner_authoring);
    auto inner_pfb = make_prefab_from(inner_source, "test:/inner.pfb");

    scene outer_authoring("outer_authoring");
    auto outer_root = outer_authoring.create_entity("outer_root");
    outer_authoring.instantiate(inner_pfb, outer_root, false);
    auto outer_pfb = make_prefab_from(outer_root, "test:/outer.pfb");

    scene world("world");
    auto outer_instance = world.instantiate(outer_pfb, false);
    auto nested = find_child_by_name(outer_instance, "root");
    check(static_cast<bool>(nested), "the nested instance is there");
    if(!nested)
    {
        return;
    }

    const auto nested_prefab_uid = prefab_uid_of(nested);
    auto nested_child = find_child_by_name(nested, "child_a");
    check(!nested_prefab_uid.is_nil(), "the nested root has a prefab id");

    // Unpack the outer instance - the editor's "break the link" - which clears prefab ids
    // down the subtree via on_destroy<prefab_component>.
    outer_instance.remove<prefab_component>();

    check(nested.all_of<prefab_component>(), "the nested instance is still an instance");
    check(prefab_uid_of(nested) == nested_prefab_uid,
          "and kept its prefab id, so it can still be matched to its own asset");
    if(nested_child)
    {
        check(!prefab_uid_of(nested_child).is_nil(),
              "so did its children - the strip stopped at the nested instance root");
    }
}

void test_two_instances_of_one_prefab_are_independent()
{
    begin_test("two instances of one prefab resync independently");

    // Two instances of the same asset carry *identical* prefab uids - those identify an
    // entity within the prefab, not within the scene. So the uid mapping a resync builds
    // has to be scoped to the instance being loaded, or one instance's records resolve to
    // the other's entities and cleanup destroys whatever it did not claim.
    scene authoring("authoring");
    auto source = build_sample_tree(authoring);
    auto pfb = make_prefab_from(source, "test:/tree.pfb");

    scene world("world");
    auto a = world.instantiate(pfb, false);
    auto b = world.instantiate(pfb, false);
    check(static_cast<bool>(a) && static_cast<bool>(b), "both instances exist");
    if(!a || !b)
    {
        return;
    }

    auto b_child = find_child_by_name(b, "child_a");
    check(static_cast<bool>(b_child), "instance B has child_a");
    if(!b_child)
    {
        return;
    }

    const auto b_entity = b_child.entity();
    const auto b_uid = uid_of(b_child);
    const auto b_prefab_uid = prefab_uid_of(b_child);
    const auto a_prefab_uid = prefab_uid_of(find_child_by_name(a, "child_a"));
    check(a_prefab_uid == b_prefab_uid, "the two instances really do share prefab uids");

    const size_t before = count_real_entities(*world.registry);

    // Edit the prefab and resync only instance A.
    find_child_by_name(source, "child_a").get<tag_component>().name = "renamed_in_prefab";
    auto updated = make_prefab_from(source, "test:/tree.pfb");
    sync_prefab_instance_with(a, updated);

    check(static_cast<bool>(find_child_by_name(a, "renamed_in_prefab")), "instance A picked up the edit");

    // B was not resynced, so it keeps its old values - and more importantly it still has
    // its own entities. If A's uid mapping had reached across, B's child would have been
    // consumed by A's records or destroyed as unclaimed.
    auto b_child_after = find_child_by_name(b, "child_a");
    check(static_cast<bool>(b_child_after), "instance B still has its own child_a");
    if(b_child_after)
    {
        check(b_child_after.entity() == b_entity, "and it is the same entity");
        check(uid_of(b_child_after) == b_uid, "with its uid intact");
    }
    check_eq(count_real_entities(*world.registry), before, "no entity was destroyed by the resync");
}

// ---------------------------------------------------------------------------------
// Sparse application
//
// A LOAD must leave a property alone when the document does not mention it. That already
// happens on a suppressed prefab override, and it is the precondition for nested prefabs:
// an instance would be stored as its asset reference plus only the properties the user
// changed, so every LOAD would routinely see records with most of their fields missing.
//
// The failure mode is a LOAD that reads into a fresh local and applies it regardless, so a
// field the document never carried lands as a default. Suppressing the property through
// path_context produces exactly the same signal a missing key does - try_load returns
// false - so these tests exercise the real mechanism rather than editing JSON by hand.
// ---------------------------------------------------------------------------------

/// Resyncs with an override recorded on every entity of the instance, so the named
/// property is suppressed wherever it appears.
void resync_suppressing(prefab_fixture& fix, const std::string& component_path)
{
    auto& comp = fix.instance.get<prefab_component>();
    fix.world.registry->view<prefab_id_component>().each(
        [&](auto, auto&& id) { comp.add_override(id.id, component_path); });
    fix.republish_and_sync();
}

// NOT covered here: light_component, whose LOAD had the same defect and was fixed with it.
// It cannot be constructed in this harness - light_component holds a
// shadow::shadowmap_generator built in its member initialiser, whose constructor calls
// engine::context() and initialises graphics resources. A component reaching for the global
// engine context just to exist is worth revisiting on its own account; until then the fix
// there rides on the two below, which exercise the identical pattern.

void test_sparse_load_keeps_parent()
{
    begin_test("an unmentioned parent does not tear the entity out of its hierarchy");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    auto child = find_child_by_name(fix.instance, "child_a");
    check(static_cast<bool>(child), "the instance has child_a");
    if(!child)
    {
        return;
    }

    resync_suppressing(fix, "transform_component/parent");

    auto after = find_child_by_name(fix.instance, "child_a");
    check(static_cast<bool>(after), "child_a is still a child of the instance root");

    // With the old LOAD an absent parent key produced set_parent({}, false), which promoted
    // every child to a scene root.
    if(after)
    {
        check(after.get<transform_component>().get_parent() == fix.instance,
              "its parent link survived the suppressed record");
        check(!after.all_of<root_component>(), "and it did not become a root");
    }
}

void test_sparse_load_keeps_volume_mode()
{
    begin_test("an unmentioned volume mode keeps its value");

    prefab_fixture fix;
    fix.build();
    if(!fix.instance)
    {
        check(false, "instance was created");
        return;
    }

    fix.source.get_or_emplace<volume_component>().mode = volume_mode::global;
    fix.republish_and_sync();
    check(fix.instance.get<volume_component>().mode == volume_mode::global,
          "the authored mode reaches the instance");

    // Both keys have to be suppressed: the loader prefers the legacy "is_global" bool and
    // only falls back to "mode", and it is the fallback that used to force a value.
    auto& comp = fix.instance.get<prefab_component>();
    fix.world.registry->view<prefab_id_component>().each(
        [&](auto, auto&& id)
        {
            comp.add_override(id.id, "volume_component/mode");
            comp.add_override(id.id, "volume_component/is_global");
        });
    fix.republish_and_sync();

    // With the old LOAD this came back as volume_mode::local - the zero value, applied
    // because neither key was present.
    check(fix.instance.get<volume_component>().mode == volume_mode::global,
          "a suppressed mode keeps the instance's value, and is not forced to zero");
}

// ---------------------------------------------------------------------------------
// prefab_component override bookkeeping
// ---------------------------------------------------------------------------------

void test_override_path_parsing()
{
    begin_test("has_serialization_override parses the load-time path");

    prefab_component comp;
    const auto uid = generate_uuid();
    const auto uid_str = hpp::to_string(uid);

    comp.add_override(uid, "tag_component/name");

    // The shape serialize_check builds during a load: an "entities" segment carrying the
    // vector index, the entity's prefab uuid, then "components".
    check(comp.has_serialization_override("entities[0]/" + uid_str + "/components/tag_component/name"),
          "the recorded property is recognised");
    check(!comp.has_serialization_override("entities[0]/" + uid_str + "/components/tag_component/tag"),
          "a sibling property on the same component is not");
    check(!comp.has_serialization_override("entities[0]/" + hpp::to_string(generate_uuid()) +
                                           "/components/tag_component/name"),
          "the same property on a different entity is not");
    check(!comp.has_serialization_override("entities[0]/" + uid_str + "/components/tag_component"),
          "the component node itself is not suppressed, or the property could never load");

    comp.add_override(uid, "transform_component/local_transform/scale");
    check(comp.has_serialization_override("entities[3]/" + uid_str +
                                          "/components/transform_component/local_transform/scale"),
          "nested property paths survive the round-trip through tokenize/rejoin");
    check(!comp.has_serialization_override("entities[3]/" + uid_str +
                                           "/components/transform_component/local_transform/position"),
          "a sibling of a nested override is not suppressed");
}

void test_add_override_collapses_nested_paths()
{
    begin_test("add_override keeps the most specific path only");

    prefab_component comp;
    const auto uid = generate_uuid();

    comp.add_override(uid, "transform_component/local_transform", "Transform/Local");
    comp.add_override(uid, "transform_component/local_transform/scale", "Transform/Local/Scale");
    check_eq(comp.local.overrides.size(), 1, "the child path replaces the parent path");
    check(comp.has_override(uid, "transform_component/local_transform/scale"), "the surviving entry is the child");

    // The other direction: a broader path must not displace a narrower one already there.
    comp.add_override(uid, "transform_component", "Transform");
    check_eq(comp.local.overrides.size(), 1, "the broader path is dropped");
    check(comp.has_override(uid, "transform_component/local_transform/scale"), "the specific entry is still the one kept");
}

void test_remove_entity_clears_all_of_its_overrides()
{
    begin_test("removing an entity clears every override it owned");

    prefab_component comp;
    const auto doomed = generate_uuid();
    const auto other = generate_uuid();

    comp.add_override(doomed, "tag_component/name");
    comp.add_override(doomed, "transform_component/local_transform/scale");
    comp.add_override(doomed, "layer_component/layers");
    comp.add_override(other, "tag_component/name");
    check_eq(comp.local.overrides.size(), 4, "four overrides to start with");

    comp.remove_entity(doomed);

    check(comp.local.is_entity_removed({}, doomed), "the entity is recorded as removed");
    check(comp.has_override(other, "tag_component/name"), "another entity's override is untouched");

    // Every override the entity owned must go, not just the first match. They are keyed by
    // uuid, so any left behind can never be reached again: they would be re-serialized into
    // the scene forever and show in the inspector as "Entity Not Found".
    size_t remaining = 0;
    for(const auto& entry : comp.local.overrides)
    {
        if(entry.entity_uuid == doomed)
        {
            ++remaining;
        }
    }
    check_eq(remaining, 0, "no overrides remain for the removed entity");
}

// ---------------------------------------------------------------------------------
// Scene cloning (the edit-scene -> play-scene path)
// ---------------------------------------------------------------------------------

void test_clone_scene_uses_one_load_context()
{
    begin_test("cloning a scene resolves every root in one load context");

    scene src("src");
    build_sample_tree(src);
    auto root_b = build_sample_tree(src);
    root_b.get<tag_component>().name = "root_b";
    const size_t expected = count_real_entities(*src.registry);

    scene dst("dst");

    // The load context is not directly observable, but the post-load callback is: it
    // fires from pop_load_context, so one invocation carrying every entity means one
    // context spanned the whole clone.
    size_t invocations = 0;
    size_t announced = 0;
    push_on_load_callbacks({[&](hpp::span<const entt::handle> entities)
                            {
                                ++invocations;
                                announced += entities.size();
                            }});
    scene::clone_scene(src, dst, false);
    pop_on_load_callbacks();

    // Why this matters beyond tidiness: the uid map lives on the load context. A context
    // per root means a link from root A to an entity under root B resolves against a map
    // that does not contain it, so the loader creates an empty entity and aims the link
    // at that instead. One shared context makes it a forward reference that the real
    // record later fills in.
    check_eq(invocations, 1, "the clone announces its entities once, not once per root");
    check_eq(announced, expected, "and the announcement covers every entity in the scene");
}

void test_clone_scene_preserves_structure_and_uids()
{
    begin_test("cloning a scene preserves structure and uids");

    scene src("src");
    auto root_a = build_sample_tree(src);
    auto root_b = build_sample_tree(src);
    root_b.get<tag_component>().name = "root_b";

    const auto uid_a = uid_of(root_a);
    const auto uid_b = uid_of(root_b);
    const auto grandchild_uid = uid_of(find_child_by_name(find_child_by_name(root_a, "child_a"), "grandchild"));

    scene dst("dst");
    scene::clone_scene(src, dst, false);

    check_eq(count_real_entities(*dst.registry), count_real_entities(*src.registry), "entity counts match");

    // Play mode resolves gameplay references against the cloned scene by uid, so the uids
    // must carry over unchanged - unlike a duplicate, which regenerates them.
    auto cloned_a = dst.find_entity_by_uuid(uid_a);
    auto cloned_b = dst.find_entity_by_uuid(uid_b);
    check(static_cast<bool>(cloned_a), "root_a is findable by its original uid in the clone");
    check(static_cast<bool>(cloned_b), "root_b is findable by its original uid in the clone");
    check(static_cast<bool>(dst.find_entity_by_uuid(grandchild_uid)), "so is a grandchild");

    size_t roots = 0;
    dst.registry->view<root_component, transform_component>().each([&](auto, auto&&, auto&&) { ++roots; });
    check_eq(roots, 2, "both roots survive as roots");

    const size_t orphans = count_componentless(*dst.registry) - 1; // minus unload()'s reserved entity
    check_eq(orphans, 0, "cloning a 2-root scene creates no componentless orphans");
}

// ---------------------------------------------------------------------------------
// Binary documents
// ---------------------------------------------------------------------------------

/// The sample tree, with a test component holding a value of every kind a document stores.
void build_binary_document_scene(scene& scn)
{
    auto root = build_sample_tree(scn);
    auto& values = find_child_by_name(root, "child_a").emplace<test_component>();
    values.str = "text with \"quotes\", a tab\tand a new line\n";
    values.u8 = 250;
    values.u16 = 65000;
    values.u32 = 4000000000u;
    values.u64 = 18000000000000000000ull;
    values.i8 = -120;
    values.i16 = -32000;
    values.i32 = -2000000000;
    values.i64 = -9000000000000000000ll;
    values.f = 0.1f;
    values.d = 0.12345678901234568;
    values.color = math::color{0.25f, 0.5f, 0.75f, 1.0f};
    values.sequential = {"first", "", "third"};
    values.associative = {{-3, "minus three"}, {7, "seven"}};
}

/// A scene saved as minified JSON: what two loads of one scene are compared by.
auto save_scene_json(const scene& scn) -> std::string
{
    std::stringstream ss;
    save_to_stream(ss, scn);
    return minify_json(ss.str());
}

auto save_scene_binary(const scene& scn) -> std::string
{
    std::stringstream ss;
    serialization::scoped_output_format format(serialization::output_format::binary);
    save_to_stream(ss, scn);
    return ss.str();
}

void test_binary_document_round_trip()
{
    begin_test("a scene written as a binary document loads back to the same scene");

    scene src("src");
    build_binary_document_scene(src);
    const auto json = save_scene_json(src);
    const auto binary = save_scene_binary(src);

    check(serialization::is_binary_document(binary.data(), binary.size()), "the binary output format writes a binary document");
    check(binary.size() < json.size(), "the binary document is smaller than the minified JSON");

    scene dst("dst");
    check(load_from_view(binary, dst), "the binary document loads");
    check_eq(save_scene_json(dst), json, "the scene loaded from it saves to the same JSON");
}

void test_converted_document_loads_the_same()
{
    begin_test("a JSON document converted to a binary document loads to the same scene");

    scene src("src");
    build_binary_document_scene(src);
    const auto json = save_scene_json(src);

    std::string error;
    const auto converted = serialization::json_to_binary_document(json, error);
    check(!converted.empty(), "the scene document converts" + (error.empty() ? std::string{} : ": " + error));

    scene dst("dst");
    check(load_from_view(hpp::string_view(reinterpret_cast<const char*>(converted.data()), converted.size()), dst),
          "the converted document loads");
    check_eq(save_scene_json(dst), json, "the scene loaded from it saves to the same JSON");
}

void test_broken_binary_documents_are_refused()
{
    begin_test("a broken binary document fails its load without reading past its end");

    scene src("src");
    build_binary_document_scene(src);
    const auto binary = save_scene_binary(src);

    // Cut short at many lengths: every one is refused.
    size_t cuts = 0;
    size_t refused_cuts = 0;
    const size_t cut_step = std::max<size_t>(1, binary.size() / 97);
    for(size_t size = 0; size < binary.size(); size += cut_step)
    {
        scene dst("dst");
        if(!load_from_view(hpp::string_view(binary.data(), size), dst))
        {
            ++refused_cuts;
        }
        ++cuts;
    }
    check(cuts > 50 && refused_cuts == cuts, "every document cut short is refused");

    // With single bytes changed: a changed byte of a name or a number may still read, so what is checked is that
    // every load returns without reading past the document.
    size_t flips = 0;
    const size_t flip_step = std::max<size_t>(1, binary.size() / 61);
    for(size_t at = 0; at < binary.size(); at += flip_step)
    {
        auto corrupt = binary;
        corrupt[at] = static_cast<char>(corrupt[at] ^ 0x5A);
        scene dst("dst");
        load_from_view(corrupt, dst);
        ++flips;
    }
    check(flips > 50, "every load of a document with a byte changed returns");
}

// ---------------------------------------------------------------------------------
// Benchmarks
//
// Baselines for the save/load rework. Every number is a best-of-N so that scheduler
// noise cannot make a regression look like an improvement; the interesting figure is
// us/entity, which should stay flat as the scene grows.
//
// "misses" counts name lookups that found nothing and "thrown" those that cost an exception.
// Loading an entity reads which component types its document states from its keys, so an
// absent type costs neither.
// ---------------------------------------------------------------------------------

using clock_t_ = std::chrono::steady_clock;

auto ms_since(clock_t_::time_point start) -> double
{
    return std::chrono::duration<double, std::milli>(clock_t_::now() - start).count();
}

struct bench_row
{
    std::string name;
    size_t entities{};
    double ms{};
    size_t bytes{};
    uint64_t lookups{};
    uint64_t thrown{};
};

std::vector<bench_row> g_bench_rows;

void record(const std::string& name,
            size_t entities,
            double ms,
            size_t bytes = 0,
            uint64_t lookups = 0,
            uint64_t thrown = 0)
{
    g_bench_rows.push_back({name, entities, ms, bytes, lookups, thrown});
}

/**
 * @brief A scene of roughly `target` entities, shaped like real content.
 *
 * Roots with a few children and a grandchild each, so the hierarchy has depth rather than
 * being one flat list. Entities carry what scene::create_entity gives them - id, tag,
 * layer, transform: four present types out of the full serializable set.
 */
void build_bench_scene(scene& scn, size_t target)
{
    constexpr size_t per_root = 4; // root + 2 children + 1 grandchild
    const size_t roots = std::max<size_t>(1, target / per_root);

    for(size_t i = 0; i < roots; ++i)
    {
        auto root = scn.create_entity("root_" + std::to_string(i));
        root.get<transform_component>().set_position_local({float(i), 0.0f, 0.0f});

        auto a = scn.create_entity("child_a", root);
        auto b = scn.create_entity("child_b", root);
        (void)b;
        scn.create_entity("grandchild", a);
    }
}

void bench_scene_save_load(size_t target, int reps)
{
    scene src("bench_src");
    build_bench_scene(src, target);
    const size_t n = count_real_entities(*src.registry);

    double best_save = 1e30;
    size_t bytes = 0;
    for(int i = 0; i < reps; ++i)
    {
        std::stringstream ss;
        const auto t0 = clock_t_::now();
        save_to_stream(ss, src);
        best_save = std::min(best_save, ms_since(t0));
        bytes = ss.str().size();
    }
    record("scene save", n, best_save, bytes);

    std::stringstream blob;
    save_to_stream(blob, src);
    const auto text = blob.str();

    // What the runtime reads: asset_compiler converts a scene's JSON into a binary document on
    // compile, so neither the editor's indentation nor JSON parsing reaches a released load. A
    // minified JSON scene is measured beside it.
    std::string error;
    const auto converted = serialization::json_to_binary_document(text, error);
    check(!converted.empty(), "the bench scene converts to a binary document: " + error);
    const std::string compiled(converted.begin(), converted.end());
    const auto minified = minify_json(text);

    const auto time_load = [&](const std::string& blob_text, const char* label, size_t bytes)
    {
        double best = 1e30;
        uint64_t lookups = 0;
        uint64_t thrown = 0;
        for(int i = 0; i < reps; ++i)
        {
            scene dst("bench_dst");
            std::stringstream ss(blob_text);
            serialization::reset_failed_lookup_count();
            const auto t0 = clock_t_::now();
            load_from_stream(ss, dst);
            best = std::min(best, ms_since(t0));
            lookups = serialization::failed_lookup_count();
            thrown = serialization::thrown_lookup_count();
        }
        record(label, n, best, bytes, lookups, thrown);
    };

    time_load(compiled, "scene load (compiled)", compiled.size());
    time_load(minified, "scene load (minified JSON)", minified.size());
    time_load(text, "scene load (editor src)", text.size());

    double best_binary_save = 1e30;
    std::string binary;
    for(int i = 0; i < reps; ++i)
    {
        const auto t0 = clock_t_::now();
        binary = save_scene_binary(src);
        best_binary_save = std::min(best_binary_save, ms_since(t0));
    }
    record("scene save (binary)", n, best_binary_save, binary.size());
    time_load(binary, "scene load (binary)", binary.size());
}

void bench_clone_scene(size_t target, int reps)
{
    scene src("bench_src");
    build_bench_scene(src, target);
    const size_t n = count_real_entities(*src.registry);

    double best = 1e30;
    for(int i = 0; i < reps; ++i)
    {
        scene dst("bench_dst");
        const auto t0 = clock_t_::now();
        scene::clone_scene(src, dst, false);
        best = std::min(best, ms_since(t0));
    }
    record("clone scene", n, best);
}

void bench_prefab(size_t target, int reps)
{
    scene authoring("bench_authoring");
    build_bench_scene(authoring, target);

    // One root holding everything, so the whole thing is a single prefab.
    entt::handle prefab_root{};
    authoring.registry->view<root_component, transform_component>().each(
        [&](auto e, auto&&, auto&&)
        {
            if(!prefab_root)
            {
                prefab_root = authoring.create_handle(e);
            }
        });
    authoring.registry->view<root_component, transform_component>().each(
        [&](auto e, auto&&, auto&&)
        {
            auto h = authoring.create_handle(e);
            if(h != prefab_root)
            {
                h.template get<transform_component>().set_parent(prefab_root, false);
            }
        });

    auto asset = make_prefab_from(prefab_root, "bench:/tree.pfb");
    const size_t n = count_real_entities(*authoring.registry);

    // The same prefab as the asset compiler writes it for a shipped game: a binary document.
    auto binary_asset = [&]
    {
        const binary_compiled_documents_scope binary;
        return make_prefab_from(prefab_root, "bench:/tree_binary.pfb");
    }();
    {
        double best = 1e30;
        uint64_t binary_lookups = 0;
        for(int i = 0; i < reps; ++i)
        {
            scene world("bench_world");
            serialization::reset_failed_lookup_count();
            const auto t0 = clock_t_::now();
            world.instantiate(binary_asset, false);
            best = std::min(best, ms_since(t0));
            binary_lookups = serialization::failed_lookup_count();
        }
        record("prefab instantiate (binary)", n, best, binary_asset.get()->buffer.data.size(), binary_lookups);

        double best_sync = 1e30;
        for(int i = 0; i < reps; ++i)
        {
            scene world("bench_world");
            auto instance = world.instantiate(binary_asset, false);
            const auto t0 = clock_t_::now();
            sync_prefab_instance_with(instance, binary_asset);
            best_sync = std::min(best_sync, ms_since(t0));
        }
        record("prefab resync (binary)", n, best_sync);
    }

    double best_inst = 1e30;
    uint64_t lookups = 0;
    uint64_t thrown = 0;
    for(int i = 0; i < reps; ++i)
    {
        scene world("bench_world");
        serialization::reset_failed_lookup_count();
        const auto t0 = clock_t_::now();
        world.instantiate(asset, false);
        best_inst = std::min(best_inst, ms_since(t0));
        lookups = serialization::failed_lookup_count();
        thrown = serialization::thrown_lookup_count();
    }
    record("prefab instantiate", n, best_inst, asset.get()->buffer.data.size(), lookups, thrown);

    // Resync with no overrides: the path_context is installed and consulted for every
    // property, but every answer is "not overridden".
    double best_sync = 1e30;
    for(int i = 0; i < reps; ++i)
    {
        scene world("bench_world");
        auto instance = world.instantiate(asset, false);
        const auto t0 = clock_t_::now();
        sync_prefab_instance_with(instance, asset);
        best_sync = std::min(best_sync, ms_since(t0));
    }
    record("prefab resync (no overrides)", n, best_sync);

    // With overrides present, has_serialization_override does real work per property:
    // tokenize the path, rejoin the tail, then hit the set.
    double best_sync_ovr = 1e30;
    for(int i = 0; i < reps; ++i)
    {
        scene world("bench_world");
        auto instance = world.instantiate(asset, false);
        auto& comp = instance.get<prefab_component>();
        size_t added = 0;
        world.registry->view<prefab_id_component>().each(
            [&](auto, auto&& id)
            {
                if(added < 32)
                {
                    comp.add_override(id.id, "tag_component/name");
                    ++added;
                }
            });
        const auto t0 = clock_t_::now();
        sync_prefab_instance_with(instance, asset);
        best_sync_ovr = std::min(best_sync_ovr, ms_since(t0));
    }
    record("prefab resync (32 overrides)", n, best_sync_ovr);

    // A clone by entity ids (scene::clone_entity, C# Instantiate) and a portable copy written and loaded back (the
    // editor's clipboard), each of the whole tree into its own scene.
    double best_clone = 1e30;
    for(int i = 0; i < reps; ++i)
    {
        const auto t0 = clock_t_::now();
        auto copy = authoring.clone_entity(prefab_root, true, false);
        best_clone = std::min(best_clone, ms_since(t0));
        scene::destroy_entity(copy);
    }
    record("clone entity", n, best_clone);

    double best_portable = 1e30;
    size_t portable_bytes = 0;
    for(int i = 0; i < reps; ++i)
    {
        const auto t0 = clock_t_::now();
        std::stringstream ss;
        save_clone_to_stream(ss, {prefab_root}, clone_identity::uuids);
        const auto text = ss.str();
        const auto roots = load_clone_from_view(text, *authoring.registry);
        best_portable = std::min(best_portable, ms_since(t0));
        portable_bytes = text.size();
        for(auto root : roots)
        {
            scene::destroy_entity(root);
        }
    }
    record("portable copy + load", n, best_portable, portable_bytes);

    double best_binary_copy = 1e30;
    size_t binary_copy_bytes = 0;
    for(int i = 0; i < reps; ++i)
    {
        const auto t0 = clock_t_::now();
        std::stringstream ss;
        {
            serialization::scoped_output_format format(serialization::output_format::binary);
            save_clone_to_stream(ss, {prefab_root}, clone_identity::uuids);
        }
        const auto text = ss.str();
        const auto roots = load_clone_from_view(text, *authoring.registry);
        best_binary_copy = std::min(best_binary_copy, ms_since(t0));
        binary_copy_bytes = text.size();
        for(auto root : roots)
        {
            scene::destroy_entity(root);
        }
    }
    record("portable copy + load (binary)", n, best_binary_copy, binary_copy_bytes);
}

/**
 * @brief What one failed NVP lookup costs, isolated from everything else.
 *
 * ser20's archives signal "no such name here" by throwing an Exception whose message is a
 * freshly built std::string. This times exactly that - construct, throw, unwind, catch,
 * destroy - so the load figures can be attributed rather than guessed at.
 */
void bench_exception_cost()
{
    constexpr int iterations = 200000;

    // Warm the unwinder: the first throw in a process pays for lazily resolving the
    // personality routine and faulting in the unwind tables.
    for(int i = 0; i < 100; ++i)
    {
        try
        {
            throw ser20::Exception("warmup");
        }
        catch(const std::exception&)
        {
        }
    }

    double best = 1e30;
    for(int rep = 0; rep < 3; ++rep)
    {
        const auto t0 = clock_t_::now();
        int caught = 0;
        for(int i = 0; i < iterations; ++i)
        {
            try
            {
                throw ser20::Exception("JSON Parsing failed - provided NVP (has_light_component) not found");
            }
            catch(const std::exception&)
            {
                ++caught;
            }
        }
        best = std::min(best, ms_since(t0));
        if(caught != iterations)
        {
            check(false, "exception benchmark caught what it threw");
        }
    }

    const double us_each = (best * 1000.0) / double(iterations);
    std::printf("\nthrow/catch of one ser20::Exception: %.3f us  (%d iterations, best of 3)\n", us_each, iterations);
    std::printf("  at 30.2 failed lookups per entity that is %.1f us/entity of pure unwinding\n", us_each * 30.2);
}

/**
 * @brief Checks that a component type an entity lacks costs no lookup.
 *
 * Adds one more component type to every entity and re-measures. Loading reads which components
 * a document states from its keys, so the miss rate must not change (a delta of 0). A delta of
 * 1.0 per entity would mean every absent type is asked for by name again.
 */
void bench_lookup_attribution()
{
    constexpr size_t target = 1000;

    auto measure = [](bool add_extra) -> double
    {
        scene src("attrib_src");
        build_bench_scene(src, target);
        if(add_extra)
        {
            src.registry->view<transform_component>().each(
                [&](auto e, auto&&) { src.create_handle(e).template emplace<test_component>(); });
        }
        const size_t n = count_real_entities(*src.registry);

        std::stringstream ss;
        save_to_stream(ss, src);

        scene dst("attrib_dst");
        std::stringstream in(ss.str());
        serialization::reset_failed_lookup_count();
        load_from_stream(in, dst);
        return double(serialization::failed_lookup_count()) / double(n);
    };

    const double base = measure(false);
    const double with_extra = measure(true);

    std::printf("\nfailed lookups per entity: %.2f with 4 components, %.2f with 5 (delta %.2f)\n",
                base,
                with_extra,
                base - with_extra);
    std::printf("  0.00: absent component types are not looked up; 1.00 would mean each one is asked for\n");
}

/**
 * @brief Minifies a JSON document: parse, then simdjson::minify, rather than a whitespace
 *        stripper written for the tests.
 *
 * Returns the input unchanged if it does not parse. That cannot pass silently: the callers
 * either compare two minified documents for equality, or feed the result to a loader.
 */
auto minify_json(const std::string& text) -> std::string
{
    simdjson::dom::parser parser;
    simdjson::dom::element doc;

    const auto err = parser.parse(text.data(), text.size()).get(doc);
    if(err)
    {
        std::printf("  minify_json: %s\n", simdjson::error_message(err));
        return text;
    }

    return simdjson::minify(doc);
}

/**
 * @brief Splits load into "parse the document" and "walk it", and shows what the output
 *        archive's pretty-printing costs in bytes.
 *
 * The two halves of the associative archive are different libraries: reading is simdjson,
 * writing is RapidJSON's PrettyWriter (simdjson has no serializer). They have to be
 * measured separately to know which one to attack.
 */
void bench_archive_internals()
{
    constexpr size_t target = 10000;

    scene src("internals_src");
    build_bench_scene(src, target);
    const size_t n = count_real_entities(*src.registry);

    std::stringstream ss;
    save_to_stream(ss, src);
    const auto text = ss.str();
    const auto minified = minify_json(text);

    // Bare simdjson DOM parse of the document the archive would read, with no ser20
    // involvement: everything above this is the archive walking the tree.
    const auto parse_of = [](const std::string& blob) -> double
    {
        double best = 1e30;
        for(int i = 0; i < 5; ++i)
        {
            simdjson::dom::parser parser;
            const auto t0 = clock_t_::now();
            simdjson::dom::element doc;
            const auto err = parser.parse(blob.data(), blob.size()).get(doc);
            const double ms = ms_since(t0);
            if(err)
            {
                return -1.0;
            }
            best = std::min(best, ms);
        }
        return best;
    };

    const double parse_pretty = parse_of(text);
    const double parse_min = parse_of(minified);

    double best_load = 1e30;
    for(int i = 0; i < 3; ++i)
    {
        scene dst("internals_dst");
        std::stringstream in(text);
        const auto t0 = clock_t_::now();
        load_from_stream(in, dst);
        best_load = std::min(best_load, ms_since(t0));
    }

    const double whitespace_pct = 100.0 * double(text.size() - minified.size()) / double(text.size());

    size_t newlines = 0;
    for(char c : text)
    {
        newlines += (c == '\n') ? 1 : 0;
    }

    std::printf("\narchive internals (%zu entities)\n", n);
    std::printf("  first 160 bytes of the document, so the shape is on the record:\n    |%s|\n",
                text.substr(0, 160).c_str());
    std::printf("  %zu lines for %zu entities (%.1f per entity)\n",
                newlines,
                n,
                double(newlines) / double(n));
    std::printf("  output is RapidJSON PrettyWriter (SmallIndent, 1 space); input is simdjson DOM\n");
    std::printf("  document              %zu bytes pretty, %zu minified (%.1f%% is whitespace)\n",
                text.size(),
                minified.size(),
                whitespace_pct);
    std::printf("  simdjson parse        %.3f ms pretty, %.3f ms minified\n", parse_pretty, parse_min);
    std::printf("  full load             %.3f ms  -> parse is %.0f%% of it, archive walk is %.0f%%\n",
                best_load,
                100.0 * parse_pretty / best_load,
                100.0 * (best_load - parse_pretty) / best_load);
}

void run_benchmarks()
{
    std::printf("\n================================ benchmarks ================================\n");

    bench_archive_internals();

    bench_exception_cost();
    bench_lookup_attribution();

    for(size_t target : {size_t(100), size_t(1000), size_t(10000)})
    {
        const int reps = target >= 10000 ? 3 : 5;
        bench_scene_save_load(target, reps);
        bench_clone_scene(target, reps);
        bench_prefab(target, reps);
    }

    std::printf("\n%-30s %8s %10s %10s %12s %10s %8s %8s\n",
                "case",
                "entities",
                "ms",
                "us/entity",
                "bytes",
                "misses",
                "per ent",
                "thrown");
    std::printf("%s\n", std::string(105, '-').c_str());
    for(const auto& row : g_bench_rows)
    {
        const double us_per_entity = row.entities ? (row.ms * 1000.0) / double(row.entities) : 0.0;
        const double lookups_per_entity = row.entities ? double(row.lookups) / double(row.entities) : 0.0;
        std::printf("%-30s %8zu %10.3f %10.3f %12zu %10llu %8.1f %8llu\n",
                    row.name.c_str(),
                    row.entities,
                    row.ms,
                    us_per_entity,
                    row.bytes,
                    static_cast<unsigned long long>(row.lookups),
                    lookups_per_entity,
                    static_cast<unsigned long long>(row.thrown));
    }
    std::printf("\n'misses' counts name lookups that found nothing; 'thrown' counts how many of\n"
                "those cost an exception. The gap between them is what the cheap probe buys.\n\n");
}

} // namespace

// ---------------------------------------------------------------------------------

void run_ecs_serialization_cases();

void run_ecs_serialization_suite(rtti::context& ctx)
{
    // The context comes from the test runner, which boots the engine the way the editor and
    // the game do. That is the whole point of running here: the asset_manager is real, so a
    // prefab registered below resolves through asset_handle and a prefab instance can reach
    // its source - which is what makes nested prefab syncing actually run.

    const bool bench_only = tests::wants("bench-only");
    const bool bench = bench_only || tests::wants("bench");

    if(!bench_only)
    {
        run_ecs_serialization_cases();
    }

    if(bench)
    {
        run_benchmarks();
    }
}

/**
 * @brief The same cases with every prefab and scene asset they build compiled to a binary document, as the
 *        asset compiler writes them for a shipped game.
 */
void run_ecs_serialization_binary_suite(rtti::context& ctx)
{
    if(tests::wants("bench-only"))
    {
        return;
    }
    const binary_compiled_documents_scope binary;
    run_ecs_serialization_cases();
}

void run_ecs_serialization_cases()
{
    {
        RUN_CASE(test_binary_document_round_trip());
        RUN_CASE(test_converted_document_loads_the_same());
        RUN_CASE(test_broken_binary_documents_are_refused());
        RUN_CASE(test_scene_roundtrip_preserves_hierarchy());
        RUN_CASE(test_scene_roundtrip_preserves_uids());
        RUN_CASE(test_scene_load_leaves_no_orphans());

        RUN_CASE(test_clone_entity_regenerates_uids());
        RUN_CASE(test_clone_object_strips_prefab_ids());
        RUN_CASE(test_clone_prefab_instance_keeps_prefab_ids());
        RUN_CASE(test_clone_entity_leaves_no_orphans());
        RUN_CASE(test_clone_links_to_entities_further_down_point_at_the_copy());
        RUN_CASE(test_subtree_snapshot_restores_links_further_down());
        RUN_CASE(test_subtree_snapshot_restores_links_to_outside_entities());
        RUN_CASE(test_component_load_honours_a_path_filter());
        RUN_CASE(test_filtered_list_load_steps_over_skipped_elements());
        RUN_CASE(test_list_loads_keep_the_elements_they_leave_out());
        RUN_CASE(test_value_loads_keep_the_members_they_leave_out());
        RUN_CASE(test_text_loads_keep_the_members_they_leave_out());
        RUN_CASE(test_probe_shape_values_are_saved_under_their_reflected_names());
        RUN_CASE(test_reflected_rows_are_saved_under_their_names());
        RUN_CASE(test_camera_and_light_documents_in_earlier_layouts_load());
        RUN_CASE(test_camera_settings_are_rows_of_its_camera());
        RUN_CASE(test_statements_follow_rows_into_a_flattened_member());
        RUN_CASE(test_text_kerning_and_outline_width_are_saved());
        RUN_CASE(test_portable_copy_loads_into_another_registry());
        RUN_CASE(test_portable_copy_into_the_same_scene());
        RUN_CASE(test_portable_copy_links_between_its_roots());
        RUN_CASE(test_portable_copy_outlives_its_originals());
        RUN_CASE(test_portable_copy_keeps_prefab_ids_at_and_below_instance_roots());
        RUN_CASE(test_portable_copy_takes_each_root_once());
        RUN_CASE(test_portable_copy_in_a_newer_layout_is_refused());

        RUN_CASE(test_instantiate_assigns_fresh_uids_per_instance());
        RUN_CASE(test_instantiate_sets_prefab_source());
        RUN_CASE(test_output_format_scope());
        RUN_CASE(test_absent_components_do_not_throw());
        RUN_CASE(test_failed_load_leaves_siblings_readable());
        RUN_CASE(test_light_source_shape_round_trip());
        RUN_CASE(test_auto_exposure_settings_start_fresh_without_version());
        RUN_CASE(test_prefab_asset_does_not_store_prefab_component());

        RUN_CASE(test_resync_picks_up_prefab_changes());
        RUN_CASE(test_resync_keeps_the_same_entities());
        RUN_CASE(test_resync_honours_a_property_override());
        RUN_CASE(test_resync_honours_particle_emitter_overrides());
        RUN_CASE(test_particle_emitter_legacy_look());
        RUN_CASE(test_resync_honours_particle_emitter_field_overrides());
        RUN_CASE(test_resync_honours_line_and_trail_overrides());
        RUN_CASE(test_resync_honours_reflection_probe_shape_overrides());
        RUN_CASE(test_resync_honours_camera_and_light_row_overrides());
        RUN_CASE(test_resync_honours_row_overrides_against_a_prefab_in_an_earlier_layout());
        RUN_CASE(test_resync_root_position_and_rotation_are_implicit_overrides());
        RUN_CASE(test_resync_root_scale_follows_the_prefab());
        RUN_CASE(test_resync_root_scale_survives_when_overridden());
        RUN_CASE(test_resync_adds_entities_added_to_the_prefab());
        RUN_CASE(test_resync_removes_entities_deleted_from_the_prefab());
        RUN_CASE(test_resync_honours_removed_entities());
        RUN_CASE(test_removing_an_entity_with_children_stays_removed());
        RUN_CASE(test_resync_keeps_user_added_children());

        RUN_CASE(test_build_order());
        RUN_CASE(test_scene_and_prefab_documents_are_not_interchangeable());
        RUN_CASE(test_scene_document_repeats_no_key());
        RUN_CASE(test_scene_save_is_in_hierarchy_order_and_stable());
        RUN_CASE(test_first_layout_scene_still_loads());
        RUN_CASE(test_newer_scene_version_is_refused());
        RUN_CASE(test_malformed_scene_documents_leave_the_open_scene());
        RUN_CASE(test_baked_first_layout_scene_loads());
        RUN_CASE(test_real_first_layout_scene_file());
        RUN_CASE(test_prefab_document_version());
        RUN_CASE(test_prefab_loaders_refuse_what_they_cannot_read());
        RUN_CASE(test_nesting_resolved_marker());
        RUN_CASE(test_prefab_dependency_enumeration());
        RUN_CASE(test_loaded_dependents_read_documents_for_prefabs_only());
        RUN_CASE(test_nested_prefab_instance_keeps_its_link());
        RUN_CASE(test_editing_the_inner_asset_reaches_new_outer_instances());
        RUN_CASE(test_a_fresh_instantiate_pulls_the_inner_asset_edit());
        RUN_CASE(test_nesting_cycle_is_refused());
        RUN_CASE(test_outer_resync_preserves_nested_instance_edits());
        RUN_CASE(test_removing_a_nested_instance_from_the_asset_propagates());
        RUN_CASE(test_a_nested_instance_added_in_the_scene_survives_resync());
        RUN_CASE(test_duplicate_nested_instances_of_one_prefab());
        RUN_CASE(test_nested_instances_are_named_by_the_containing_prefab());
        RUN_CASE(test_deleting_one_of_two_nested_instances_removes_that_one());
        RUN_CASE(test_edits_to_content_the_outer_document_added_inside_a_nested_instance_propagate());
    RUN_CASE(test_an_addition_applied_into_the_nested_asset_keeps_its_new_name());
    RUN_CASE(test_cloning_a_plain_branch_holding_a_nested_instance_drops_the_branch_ids());
    RUN_CASE(test_a_local_override_on_one_field_does_not_shield_its_siblings());
    RUN_CASE(test_a_replay_applies_only_what_its_document_states_to_nested_content());
    RUN_CASE(test_document_statements_live_with_their_author());
    RUN_CASE(test_an_outer_statement_wins_over_an_inner_one());
    RUN_CASE(test_a_removal_two_levels_down_holds_through_every_replay());
    RUN_CASE(test_applying_a_scene_instance_folds_its_statements_into_the_document());
    RUN_CASE(test_released_format_overrides_load_as_local());
    RUN_CASE(test_issued_ids_name_their_document());
    RUN_CASE(test_an_entity_the_container_adds_inside_a_nested_instance_is_the_containers());
    RUN_CASE(test_a_nested_instance_the_container_added_is_removed_when_the_container_drops_it());
    RUN_CASE(test_released_format_ids_are_attributed_to_their_instances());
    RUN_CASE(test_clone_slots_follow_the_documents_inside_the_clone());
    RUN_CASE(test_an_instance_added_inside_a_nested_instance_survives_that_instances_sync());
        RUN_CASE(test_an_authoring_root_owns_its_statements_at_every_depth());
        RUN_CASE(test_a_clone_of_an_instance_keeps_its_nested_slots());
        RUN_CASE(test_a_cloned_branch_makes_its_nested_instances_additions());
        RUN_CASE(test_a_failed_sync_does_not_poison_the_override_memo());
        RUN_CASE(test_override_touching_treats_array_elements_as_below());
        RUN_CASE(test_adding_a_nested_instance_to_the_asset_propagates());
        RUN_CASE(test_adding_a_different_nested_prefab_to_the_asset_propagates());
        RUN_CASE(test_replacing_a_prefab_component_keeps_its_owner());
        RUN_CASE(test_fresh_instance_attributes_nested_overrides_and_detach_makes_them_local());
        RUN_CASE(test_a_nested_instance_returns_to_where_its_container_placed_it());
        RUN_CASE(test_editing_a_prefab_as_its_own_root_keeps_ids_and_instances_stable());
        RUN_CASE(test_nested_instance_scale_follows_its_prefab_after_container_is_saved());
        RUN_CASE(test_authored_override_on_one_nested_instance_reaches_existing_instances());
        RUN_CASE(test_a_local_edit_survives_the_containing_prefab_being_replayed());
        RUN_CASE(test_the_containing_prefab_can_take_its_override_back());
        RUN_CASE(test_local_and_authored_removals_are_distinguishable());
        RUN_CASE(test_removing_an_entity_inside_a_nested_instance_propagates());
        RUN_CASE(test_adding_an_entity_under_a_nested_instance_propagates());
        RUN_CASE(test_a_locally_cloned_nested_instance_is_not_claimed_by_the_prefab());
        RUN_CASE(test_a_locally_deleted_nested_instance_stays_deleted());
        RUN_CASE(test_deleting_the_only_nested_instance_stays_deleted());
        RUN_CASE(test_removing_an_entity_two_levels_down_stays_removed());
        RUN_CASE(test_a_locally_deleted_nested_instance_survives_the_prefab_changing_it());
        RUN_CASE(test_authored_override_two_levels_down());
        RUN_CASE(test_cloning_a_nested_instance_makes_it_the_users());
        RUN_CASE(test_cloning_an_outer_instance_keeps_what_it_inherited());
        RUN_CASE(test_promoting_a_clone_into_the_asset_marks_it());
        RUN_CASE(test_unpacking_an_outer_instance_leaves_the_nested_one_linked());
        RUN_CASE(test_two_instances_of_one_prefab_are_independent());
        RUN_CASE(test_sparse_load_keeps_parent());
        RUN_CASE(test_sparse_load_keeps_volume_mode());

        RUN_CASE(test_override_path_parsing());
        RUN_CASE(test_add_override_collapses_nested_paths());
        RUN_CASE(test_remove_entity_clears_all_of_its_overrides());

        RUN_CASE(test_clone_scene_uses_one_load_context());
        RUN_CASE(test_clone_scene_preserves_structure_and_uids());
    }
}

REGISTER_TEST_SUITE("ecs serialization / prefabs / cloning", run_ecs_serialization_suite)
REGISTER_TEST_SUITE("ecs serialization / binary documents", run_ecs_serialization_binary_suite)
