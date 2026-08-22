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
 * SHOULD read and marked with check_xfail(), naming the entry in
 * tasks/serialization_prefab_audit.md. Those report as XFAIL today and turn into a loud
 * XPASS the moment a fix lands, at which point they should be promoted to check().
 *
 * Two behaviours that look asymmetric are deliberate and are pinned as such
 * (see tasks/lessons.md):
 *   - a prefab instance root's local position and rotation are implicit overrides and
 *     always survive a resync;
 *   - its local scale is NOT, and keeps following the prefab until explicitly overridden.
 */

#include <engine/assets/asset_handle.h>
#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>

#include "../tests.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/test_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/threading/threader.h>
#include <engine/meta/ecs/entity.hpp>

#include <logging/logging.h>
#include <serialization/associative_archive.h>
#include <serialization/serialization.h>
#include <threadpp/thread_pool.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

// ---------------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------------

int g_checks = 0;
int g_failures = 0;
int g_xfail = 0;
int g_xpass = 0;
std::vector<std::string> g_xpass_list;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void check_eq(size_t actual, size_t expected, const std::string& what)
{
    ++g_checks;
    if(actual != expected)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %zu, expected %zu)\n", what.c_str(), actual, expected);
    }
}

void check_eq_str(const std::string& actual, const std::string& expected, const std::string& what)
{
    ++g_checks;
    if(actual != expected)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got \"%s\", expected \"%s\")\n", what.c_str(), actual.c_str(), expected.c_str());
    }
}

/// Vectors compared with a tolerance; transforms round-trip through decimal text.
void check_near(const math::vec3& actual, const math::vec3& expected, const std::string& what)
{
    ++g_checks;
    constexpr float epsilon = 1e-4f;
    const bool ok = std::abs(actual.x - expected.x) < epsilon && std::abs(actual.y - expected.y) < epsilon &&
                    std::abs(actual.z - expected.z) < epsilon;
    if(!ok)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got [%f %f %f], expected [%f %f %f])\n",
                    what.c_str(),
                    actual.x,
                    actual.y,
                    actual.z,
                    expected.x,
                    expected.y,
                    expected.z);
    }
}

/**
 * @brief An expectation that the current implementation is known not to meet.
 *
 * Reports XFAIL (not a failure) while the defect stands, and XPASS once it stops -
 * which is the signal to promote the call to check(). Never fails the suite in either
 * direction: a green run means "nothing regressed", and the summary carries the news
 * that something improved.
 */
void check_xfail(bool condition, const std::string& what, const char* audit_id)
{
    ++g_checks;
    if(condition)
    {
        ++g_xpass;
        g_xpass_list.push_back(std::string(audit_id) + ": " + what);
        std::printf("  XPASS [%s]: %s\n", audit_id, what.c_str());
    }
    else
    {
        ++g_xfail;
        std::printf("  xfail [%s]: %s\n", audit_id, what.c_str());
    }
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

/**
 * @brief Wraps a ready prefab in an asset_handle without an asset_manager.
 *
 * asset_handle only ever reads its payload through a tpp future, so a future that is
 * already satisfied is enough. job_future_storage defaults to submitted_ == true, so
 * get() never tries to reach a thread pool that is not there.
 */
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
    data->buffer.data = std::move(bytes);

    auto& am = engine::context().get_cached<asset_manager>();

    // Drop whatever is registered under this key first: get_asset_from_instance returns the
    // existing entry if there is one, so republishing would otherwise hand back the old
    // content under a new name.
    am.unload_asset<prefab>(id);

    return am.get_asset_from_instance<prefab>(id, data);
}

auto minify_json(const std::string& text) -> std::string;

/**
 * @brief Builds the prefab asset the way the pipeline does.
 *
 * asset_compiler's compile<prefab> minifies the source before it ever becomes a loadable
 * asset, so an asset built here has to be minified too or every prefab measurement is
 * taken against a document the runtime never sees.
 */
/**
 * @brief Builds a prefab the way one written before instance ids existed would look.
 *
 * The ids are blanked in the serialized text rather than withheld at save time, because a
 * prefab save is exactly what allocates them. Same length, so the document stays valid.
 */
auto make_prefab_from_without_allocating_ids(entt::const_handle root, const std::string& id)
    -> asset_handle<prefab>
{
    const auto bytes = serialize_as_prefab(root, id);
    std::string text(bytes.begin(), bytes.end());

    // Scanned rather than regex-replaced: "$1" followed by a digit reads as group 10, which
    // silently produced malformed JSON and a test that passed by never loading anything.
    static constexpr size_t uuid_length = 36;
    const std::string nil_uuid(uuid_length, '0');
    const auto blank_ids = [&text, &nil_uuid](const std::string& key)
    {
        size_t pos = 0;
        while((pos = text.find(key, pos)) != std::string::npos)
        {
            const auto quote = text.find('"', pos + key.size());
            if(quote == std::string::npos || quote + 1 + uuid_length > text.size())
            {
                return;
            }

            text.replace(quote + 1, uuid_length, nil_uuid);
            pos = quote + 1 + uuid_length;
        }
    };
    blank_ids("\"instance_uid\"");
    blank_ids("\"instance_id\"");

    const auto compiled = minify_json(text);
    return make_prefab_asset(std::vector<uint8_t>(compiled.begin(), compiled.end()), id);
}

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
    pfb.get()->buffer.data = std::vector<uint8_t>(compiled.begin(), compiled.end());
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

    check_eq_str(name_of(loaded_root), "root", "the root keeps its name");
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

    check_eq_str(name_of(clone), "root", "the clone keeps the source's name");
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

/**
 * @brief Demonstrates that entity data cannot survive the binary archive.
 *
 * NOT run by the default suite: the load does not fail, it hangs. Run it deliberately
 * with --binary-probe, and expect to kill the process.
 *
 * The binary archive stores no names and no field boundaries - BinaryInputArchive has no
 * setNextName or getNodeName at all, and its only throw is a short read at EOF. So it
 * cannot express "this optional field is absent": a reader expecting a field the writer
 * never emitted silently consumes the next field's bytes.
 *
 * SAVE(entity_components) writes "has_<component>" only for components that exist, while
 * LOAD(entity_components) reads one bool per type in all_serializeable_components. For an
 * entity with 4 of 32 components that is 4 bools written against 32 read. The stream
 * desynchronises on the first entity, and shortly afterwards a std::string or std::vector
 * length gets read out of misaligned bytes - which is why this hangs or exhausts memory
 * rather than throwing.
 *
 * The save side is asserted below because it is safe; the load is left commented so that
 * running this cannot wedge a machine by accident.
 */
void test_binary_scene_roundtrip_is_broken()
{
    begin_test("scene round-trip through the binary archive (opt-in)");

    scene src("src");
    build_sample_tree(src);

    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    save_to_stream_bin(ss, src);
    const size_t written = ss.str().size();
    check(written > 0, "the binary save produces bytes");

    std::printf("  wrote %zu bytes for %zu entities\n", written, count_real_entities(*src.registry));
    std::printf("  the matching load is left commented out: it does not throw, it hangs.\n");
    std::printf("  see the comment on this function, and B16 in tasks/serialization_prefab_audit.md\n");

    // Deliberately not executed. Uncomment only under a debugger:
    //
    // scene dst("dst");
    // std::stringstream in(ss.str(), std::ios::in | std::ios::binary);
    // load_from_stream_bin(in, dst);
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
    check_eq_str(minify_json(compact), minify_json(readable), "both carry identical data");

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

    // Deliberate asymmetry with position/rotation - see tasks/lessons.md. An authored
    // scale change is meant to reach existing instances.
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

void test_scene_and_prefab_documents_are_not_interchangeable()
{
    begin_test("a scene document is not a prefab document");

    // A bake that mistook scenes for prefabs rewrote them through the prefab writer and
    // lost every root but the first. The two formats look similar enough that nothing
    // complained: a scene is `entities_count` followed by one `entities` array **per
    // root**, all at the same level, so reading one as a prefab silently takes the first
    // array and stops.
    scene src("src");
    build_sample_tree(src);
    auto second = build_sample_tree(src);
    second.get<tag_component>().name = "second_root";
    build_sample_tree(src);

    const size_t expected_roots = 3;
    size_t roots = 0;
    src.registry->view<root_component, transform_component>().each([&](auto, auto&&, auto&&) { ++roots; });
    check_eq(roots, expected_roots, "the source scene has three roots");

    std::stringstream scene_doc;
    save_to_stream(scene_doc, src);
    const auto scene_text = scene_doc.str();

    check(scene_text.find("entities_count") != std::string::npos,
          "a scene document declares how many roots it holds");

    const auto count_roots_in = [](scene& scn) -> size_t
    {
        size_t n = 0;
        scn.registry->view<root_component, transform_component>().each([&](auto, auto&&, auto&&) { ++n; });
        return n;
    };

    // Read through the scene overload: all three roots.
    {
        scene dst("dst");
        std::stringstream in(scene_text);
        load_from_stream(in, dst);
        check_eq(count_roots_in(dst), expected_roots, "the scene reader recovers every root");
    }

    // Read through the *registry* overload: one root, silently.
    //
    // These two overloads are one argument apart - `scene&` versus `entt::registry&`, and a
    // scene is barely more than a registry - but they read different formats.
    // load_from_stream(stream, scene&) reads a scene document; load_from_stream(stream,
    // registry&) reads a single-root one, which is what clone_scene_from_stream wants
    // because it serializes root by root. Handing a scene document to the second silently
    // keeps only the first root, because a scene repeats "entities" once per root and the
    // single-root reader takes the first array it finds.
    //
    // That is the shape of the bug that rewrote scenes as prefabs during a bake. Pinned so
    // the asymmetry is on the record: code choosing a reader must key on the asset type,
    // never guess.
    {
        scene dst("dst_via_registry");
        std::stringstream in(scene_text);
        load_from_stream(in, *dst.registry);
        check_eq(count_roots_in(dst), 1, "the single-root reader takes only the first root");
    }

    // A prefab document has no root count at all, which is what made the mix-up silent.
    auto prefab_doc = serialize_as_prefab(second);
    const std::string prefab_text(prefab_doc.begin(), prefab_doc.end());
    check(prefab_text.find("entities_count") == std::string::npos,
          "a prefab document carries no root count");
    check(prefab_text.find("entities") != std::string::npos, "though both use an 'entities' array");
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

void test_legacy_override_records_convert_on_load()
{
    begin_test("overrides stored on nested roots by files and scenes from before convert to their authors' lists");

    // A prefab document written before statements lived with their author: the nested root's
    // record carried the containing document's override in property_overrides, and the
    // snapshot carried the authored value.
    nested_fixture fix;
    fix.build();
    auto authored_child = find_child_by_name(fix.first, "child_a");
    check(static_cast<bool>(authored_child), "the authoring scene has child_a");
    if(!authored_child)
    {
        return;
    }
    const auto child_uid = prefab_uid_of(authored_child);
    authored_child.get<tag_component>().name = "legacy_authored";
    fix.outer_pfb = make_prefab_from(fix.outer_root, "test:/outer.pfb");

    {
        const auto& bytes = fix.outer_pfb.get()->buffer.data;
        std::string text(bytes.begin(), bytes.end());
        const auto pos = text.find(k_empty_statement_lists);
        check(pos != std::string::npos, "the first nested root's record carries empty statement lists to replace");
        if(pos == std::string::npos)
        {
            return;
        }
        text.replace(pos,
                     k_empty_statement_lists.size(),
                     R"("property_overrides":[)" + legacy_override_entry(child_uid, "tag_component/name") + "]");
        fix.outer_pfb = make_prefab_asset(std::vector<uint8_t>(text.begin(), text.end()), "test:/outer.pfb");
    }

    scene world("world");
    auto legacy_instance = world.instantiate(fix.outer_pfb, false);
    auto nested = nested_instances_of(legacy_instance);
    check_eq(nested.size(), 2, "two nested instances");
    if(nested.size() != 2)
    {
        return;
    }
    check(static_cast<bool>(find_child_by_name(nested[0], "legacy_authored")),
          "the authored value holds through the nested document's replay");
    check(static_cast<bool>(find_child_by_name(nested[1], "child_a")), "and the other instance is untouched");
    check_eq(inherited_overrides_of(nested[0]).size(), 1, "the override reads as the outer document's statement");
    check(nested[0].get<prefab_component>().local.empty(), "not as something stated here");
    check_eq(legacy_instance.get<prefab_component>().from_document.overrides.size(), 1,
             "it was converted onto the outer root");

    // A scene written before: the nested root's record merged the scene's and the document's
    // halves in property_overrides, with the document's half keyed in stated_overrides.
    std::stringstream scene_stream;
    save_to_stream(scene_stream, world);
    std::string scene_text = minify_json(scene_stream.str());
    const auto entry = legacy_override_entry(child_uid, "tag_component/name");
    const std::string scene_empty_lists = k_empty_statement_lists;
    const auto scene_pos = scene_text.find(scene_empty_lists);
    check(scene_pos != std::string::npos, "the scene's first nested root record carries empty statement lists");
    if(scene_pos == std::string::npos)
    {
        return;
    }
    scene_text.replace(scene_pos,
                       scene_empty_lists.size(),
                       R"("property_overrides":[)" + entry + R"(],"stated_overrides":[{"key":")" +
                           hpp::to_string(fix.outer_pfb.uid()) + R"(","value":[)" + entry + "]}]");

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
    auto loaded_nested = nested_instances_of(roots[0]);
    check_eq(loaded_nested.size(), 2, "with both nested instances");
    if(loaded_nested.size() != 2)
    {
        return;
    }
    check_eq(inherited_overrides_of(loaded_nested[0]).size(), 1,
             "the document's half was handed to the document's instance above, seen as inherited from here");
    check(loaded_nested[0].get<prefab_component>().local.empty(), "and nothing of it stayed local");
    sync_all_prefab_instances(*loaded.registry);
    loaded_nested = nested_instances_of(roots[0]);
    check(loaded_nested.size() == 2 && static_cast<bool>(find_child_by_name(loaded_nested[0], "legacy_authored")),
          "and the value holds through the replays that follow");
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

void test_legacy_unnamed_ids_are_attributed_to_their_containers()
{
    begin_test("ids from files written before they named their document are attributed on load");

    // Both files written without document names, the outer one with an old-style foreign list
    // for an entity the outer author added under a nested instance.
    nested_fixture fix;
    fix.build();
    auto added = fix.outer_authoring.create_entity("outer_added", fix.first);

    const auto outer_bytes = serialize_as_prefab(fix.outer_root, "test:/outer.pfb");
    std::string outer_text(outer_bytes.begin(), outer_bytes.end());
    const auto added_uid = added.get<prefab_id_component>().id;
    const std::string foreign_list = "\"foreign_entities\": [\"" + hpp::to_string(added_uid) + "\"], \"instance_id\":";
    outer_text = std::regex_replace(outer_text, std::regex(R"("instance_id"\s*:)"), foreign_list);
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

    auto legacy_instance = fix.world.instantiate(fix.outer_pfb, false);
    check(static_cast<bool>(legacy_instance), "the legacy document instantiates");
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
        check(slot_document_of(instance) == fix.outer_pfb.uid(), "a nested slot is attributed to the outer document");
        auto child = find_child_by_name(instance, "child_a");
        check(static_cast<bool>(child) && document_of(child) == fix.inner_pfb.uid(),
              "the nested asset's content is attributed to the inner document");
    }
    auto live_added = find_child_by_name(nested[0], "outer_added");
    check(static_cast<bool>(live_added), "the listed addition is there");
    if(live_added)
    {
        check(document_of(live_added) == fix.outer_pfb.uid(),
              "and is attributed to the outer document, as the old list said");
    }

    // And once attributed, the nested asset's sync tells its content from the addition.
    sync_prefab_instance_with(nested[0], fix.inner_pfb);
    check(static_cast<bool>(find_child_by_name(nested[0], "outer_added")),
          "the inner asset's sync leaves the attributed addition alone");
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
    check(added.get<prefab_component>().placed_by == instance_placement::other,
          "a hand-placed instance is not its container's");

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

void test_a_slot_of_a_deeper_document_is_named_by_that_document_only()
{
    begin_test("saving the outer prefab does not name slots that belong to a nested prefab's file");

    // Z inside M inside O, where M's file predates instance ids. Saving O used to name Z's slot
    // from O; when M was later re-saved it named the same slot differently, and the two never
    // reconciled - Z was recreated and destroyed on every sync, and O's overrides on it were
    // orphaned. A slot is named by the document that placed it, and Z was placed by M.
    scene z_authoring("z_authoring");
    auto z_source = build_sample_tree(z_authoring);
    z_source.get<tag_component>().name = "z_root";
    auto z_pfb = make_prefab_from(z_source, "test:/z.pfb");

    scene m_authoring("m_authoring");
    auto m_root = m_authoring.create_entity("m_root");
    m_authoring.instantiate(z_pfb, m_root, false);
    auto m_pfb_old = make_prefab_from_without_allocating_ids(m_root, "test:/m.pfb");

    scene o_authoring("o_authoring");
    auto o_root = o_authoring.create_entity("o_root");
    auto m_inst = o_authoring.instantiate(m_pfb_old, o_root, false);
    auto z_in_o = nested_instances_of(m_inst);
    check_eq(z_in_o.size(), 1, "the middle instance carries the inner one");
    if(z_in_o.size() != 1)
    {
        return;
    }
    check(instance_uid_of(z_in_o[0]).is_nil(), "the inner slot is unnamed - the middle file predates ids");
    check(z_in_o[0].get<prefab_component>().placed_by == instance_placement::container,
          "but it is known to be the middle instance's own");

    auto o_pfb = make_prefab_from(o_root, "test:/o.pfb");
    check(instance_uid_of(z_in_o[0]).is_nil(),
          "saving the outer prefab leaves it unnamed - it is not the outer file's slot to name");
    check(!instance_uid_of(m_inst).is_nil(), "while the middle instance, directly in the outer file, is named");

    scene world("world");
    auto o_inst = world.instantiate(o_pfb, false);
    auto live_m = nested_instances_of(o_inst);
    check(live_m.size() == 1 && nested_instances_of(live_m[0]).size() == 1,
          "a world instance has exactly one inner instance");

    // The middle prefab is re-saved by a build that names slots.
    auto m_pfb_new = make_prefab_from(m_root, "test:/m.pfb");
    auto z_in_m = nested_instances_of(m_root);
    check(z_in_m.size() == 1 && !instance_uid_of(z_in_m[0]).is_nil(), "the middle file now names its slot");
    if(z_in_m.size() != 1)
    {
        return;
    }

    sync_prefab_instance_with(o_inst, o_pfb);
    sync_prefab_instance_with(o_inst, o_pfb);

    live_m = nested_instances_of(o_inst);
    auto live_z = live_m.size() == 1 ? nested_instances_of(live_m[0]) : std::vector<entt::handle>{};
    check_eq(live_z.size(), 1, "and the world instance still has exactly one inner instance after two syncs");
    if(live_z.size() == 1)
    {
        check(instance_uid_of(live_z[0]) == instance_uid_of(z_in_m[0]),
              "which was adopted into the name the middle file gave it");
    }
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

void test_an_unnamed_live_instance_is_adopted_not_duplicated()
{
    begin_test("a prefab that gains instance ids adopts the instances it already has");

    // The transition. Instances created before the prefab had instance ids carry none; the
    // prefab is then re-saved by a build that does. Both sides mean the same slot, and
    // matching by id alone finds nothing and creates a second one - so the nesting doubles,
    // and doubles again on every load after that.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    check_eq(live.size(), 2, "the live instance starts with two nested instances");
    if(live.size() != 2)
    {
        return;
    }

    // Wind the live side back to before instance ids existed.
    live[0].get<prefab_component>().instance_id = {};
    live[1].get<prefab_component>().instance_id = {};

    // Something recognisable on each, so adoption can be shown to pick a live instance rather
    // than build a new one.
    live[0].get<transform_component>().set_position_local({11.0f, 0.0f, 0.0f});
    live[1].get<transform_component>().set_position_local({22.0f, 0.0f, 0.0f});
    const auto first_entity = live[0].entity();
    const auto second_entity = live[1].entity();

    fix.republish_and_sync();

    auto after = fix.live_nested();
    check_eq(after.size(), 2, "the nested instances are matched, not duplicated");
    if(after.size() != 2)
    {
        return;
    }

    check(after[0].entity() == first_entity && after[1].entity() == second_entity,
          "and they are the same entities, adopted rather than rebuilt");
    check(!instance_uid_of(after[0]).is_nil() && !instance_uid_of(after[1]).is_nil(),
          "each has been given the id the prefab knows it by");
    check(instance_uid_of(after[0]) != instance_uid_of(after[1]),
          "and the two ids differ - one instance cannot answer for both");

    // Adoption must survive: a second resync now matches by id and must not double either.
    fix.republish_and_sync();
    check_eq(fix.live_nested().size(), 2, "a second resync still does not duplicate them");
}

void test_a_named_live_instance_survives_an_unnamed_prefab()
{
    begin_test("instances keep working against a prefab that has no instance ids");

    // The reverse mismatch, which doubles just as readily: the live side is named and the
    // document is not, so its records miss the scopes entirely.
    nested_fixture fix;
    fix.build();

    auto live = fix.live_nested();
    if(live.size() != 2)
    {
        check(false, "the live instance starts with two nested instances");
        return;
    }
    const auto first_entity = live[0].entity();

    // Strip the ids from the *authoring* side and re-save, so the document is the old kind.
    fix.first.get<prefab_component>().instance_id = {};
    fix.second.get<prefab_component>().instance_id = {};
    auto unnamed_pfb = make_prefab_from_without_allocating_ids(fix.outer_root, "test:/outer.pfb");
    sync_prefab_instance_with(fix.outer_instance, unnamed_pfb);

    auto after = fix.live_nested();
    check_eq(after.size(), 2, "the nested instances are left alone, not duplicated");
    if(after.size() == 2)
    {
        check(after[0].entity() == first_entity, "and are the same entities");
    }
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
// changed, so every LOAD would routinely see records with most of their fields missing
// (tasks/nested_prefabs_design.md, stage 0).
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
// Benchmarks
//
// Baselines for the save/load rework. Every number is a best-of-N so that scheduler
// noise cannot make a regression look like an improvement; the interesting figure is
// us/entity, which should stay flat as the scene grows.
//
// "lookups" counts NVP probes that missed and threw. Loading an entity probes every type
// in all_serializeable_components by name, so an entity with 4 components produces one
// throw per absent type. That number is the size of the prize.
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
 * layer, transform - which is also the density that makes the component probe loop hurt
 * most: four present types out of the full serializable set.
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

    // What the runtime actually parses: asset_compiler minifies prefabs and scenes on
    // compile (write_minified_file -> simdjson::minify), so the indentation the editor
    // writes never reaches a released load.
    const auto compiled = minify_json(text);

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
    time_load(text, "scene load (editor src)", text.size());
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
 * @brief Confirms the failed lookups really are the absent-component probes.
 *
 * Adds one more component type to every entity and re-measures. If the probe loop is the
 * source, the rate must fall by exactly 1.0 per entity: one type flips from miss to hit.
 * Anything else means the model is wrong and the attribution needs redoing.
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
    std::printf("  a delta of 1.00 confirms the absent-component probe loop is the source\n");
}

/**
 * @brief Minifies a document the way the asset pipeline does.
 *
 * Deliberately the same two calls as asset_compiler's write_minified_file - parse, then
 * simdjson::minify - rather than a whitespace stripper written for the tests. Anything
 * measured against a document the compiler would not have produced is measuring the wrong
 * document, which is the mistake these benchmarks were already making once.
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

auto run_ecs_serialization_suite(rtti::context& ctx) -> int
{
    // The context comes from the test runner, which boots the engine the way the editor and
    // the game do. That is the whole point of running here: the asset_manager is real, so a
    // prefab registered below resolves through asset_handle and a prefab instance can reach
    // its source - which is what makes nested prefab syncing actually run.

    const bool bench_only = tests::wants("bench-only");
    const bool bench = bench_only || tests::wants("bench");
    const bool binary_probe = tests::wants("binary-probe");

    if(!bench_only)
    {
        test_scene_roundtrip_preserves_hierarchy();
        test_scene_roundtrip_preserves_uids();
        test_scene_load_leaves_no_orphans();

        test_clone_entity_regenerates_uids();
        test_clone_object_strips_prefab_ids();
        test_clone_prefab_instance_keeps_prefab_ids();
        test_clone_entity_leaves_no_orphans();

        test_instantiate_assigns_fresh_uids_per_instance();
        test_instantiate_sets_prefab_source();
        test_output_format_scope();
        test_absent_components_do_not_throw();
        test_prefab_asset_does_not_store_prefab_component();

        test_resync_picks_up_prefab_changes();
        test_resync_keeps_the_same_entities();
        test_resync_honours_a_property_override();
        test_resync_root_position_and_rotation_are_implicit_overrides();
        test_resync_root_scale_follows_the_prefab();
        test_resync_root_scale_survives_when_overridden();
        test_resync_adds_entities_added_to_the_prefab();
        test_resync_removes_entities_deleted_from_the_prefab();
        test_resync_honours_removed_entities();
        test_removing_an_entity_with_children_stays_removed();
        test_resync_keeps_user_added_children();

        test_build_order();
        test_scene_and_prefab_documents_are_not_interchangeable();
        test_nesting_resolved_marker();
        test_prefab_dependency_enumeration();
        test_nested_prefab_instance_keeps_its_link();
        test_editing_the_inner_asset_reaches_new_outer_instances();
        test_a_fresh_instantiate_pulls_the_inner_asset_edit();
        test_nesting_cycle_is_refused();
        test_outer_resync_preserves_nested_instance_edits();
        test_removing_a_nested_instance_from_the_asset_propagates();
        test_a_nested_instance_added_in_the_scene_survives_resync();
        test_duplicate_nested_instances_of_one_prefab();
        test_nested_instances_are_named_by_the_containing_prefab();
        test_deleting_one_of_two_nested_instances_removes_that_one();
        test_a_local_override_on_one_field_does_not_shield_its_siblings();
    test_a_replay_applies_only_what_its_document_states_to_nested_content();
    test_document_statements_live_with_their_author();
    test_an_outer_statement_wins_over_an_inner_one();
    test_a_removal_two_levels_down_holds_through_every_replay();
    test_applying_a_scene_instance_folds_its_statements_into_the_document();
    test_legacy_override_records_convert_on_load();
    test_issued_ids_name_their_document();
    test_an_entity_the_container_adds_inside_a_nested_instance_is_the_containers();
    test_a_nested_instance_the_container_added_is_removed_when_the_container_drops_it();
    test_legacy_unnamed_ids_are_attributed_to_their_containers();
    test_clone_slots_follow_the_documents_inside_the_clone();
    test_an_instance_added_inside_a_nested_instance_survives_that_instances_sync();
        test_a_slot_of_a_deeper_document_is_named_by_that_document_only();
        test_an_authoring_root_owns_its_statements_at_every_depth();
        test_a_clone_of_an_instance_keeps_its_nested_slots();
        test_a_cloned_branch_makes_its_nested_instances_additions();
        test_a_failed_sync_does_not_poison_the_override_memo();
        test_override_touching_treats_array_elements_as_below();
        test_adding_a_nested_instance_to_the_asset_propagates();
        test_adding_a_different_nested_prefab_to_the_asset_propagates();
        test_replacing_a_prefab_component_keeps_its_owner();
        test_fresh_instance_attributes_nested_overrides_and_detach_makes_them_local();
        test_a_nested_instance_returns_to_where_its_container_placed_it();
        test_editing_a_prefab_as_its_own_root_keeps_ids_and_instances_stable();
        test_nested_instance_scale_follows_its_prefab_after_container_is_saved();
        test_authored_override_on_one_nested_instance_reaches_existing_instances();
        test_a_local_edit_survives_the_containing_prefab_being_replayed();
        test_the_containing_prefab_can_take_its_override_back();
        test_local_and_authored_removals_are_distinguishable();
        test_removing_an_entity_inside_a_nested_instance_propagates();
        test_adding_an_entity_under_a_nested_instance_propagates();
        test_an_unnamed_live_instance_is_adopted_not_duplicated();
        test_a_named_live_instance_survives_an_unnamed_prefab();
        test_a_locally_cloned_nested_instance_is_not_claimed_by_the_prefab();
        test_a_locally_deleted_nested_instance_stays_deleted();
        test_deleting_the_only_nested_instance_stays_deleted();
        test_removing_an_entity_two_levels_down_stays_removed();
        test_a_locally_deleted_nested_instance_survives_the_prefab_changing_it();
        test_authored_override_two_levels_down();
        test_cloning_a_nested_instance_makes_it_the_users();
        test_cloning_an_outer_instance_keeps_what_it_inherited();
        test_promoting_a_clone_into_the_asset_marks_it();
        test_unpacking_an_outer_instance_leaves_the_nested_one_linked();
        test_two_instances_of_one_prefab_are_independent();
        test_sparse_load_keeps_parent();
        test_sparse_load_keeps_volume_mode();

        test_override_path_parsing();
        test_add_override_collapses_nested_paths();
        test_remove_entity_clears_all_of_its_overrides();

        test_clone_scene_uses_one_load_context();
        test_clone_scene_preserves_structure_and_uids();
    }

    if(binary_probe)
    {
        test_binary_scene_roundtrip_is_broken();
    }

    if(bench)
    {
        run_benchmarks();
    }

    std::printf("\n%d checks, %d failures, %d known failures", g_checks, g_failures, g_xfail);
    if(g_xpass > 0)
    {
        std::printf(", %d UNEXPECTED PASSES", g_xpass);
    }
    std::printf("\n");

    if(g_xpass > 0)
    {
        std::printf("\nThese expectations now hold - promote them from check_xfail to check:\n");
        for(const auto& entry : g_xpass_list)
        {
            std::printf("  %s\n", entry.c_str());
        }
    }

    // The runner owns the context and tears it down; this only reports.
    return g_failures;
}

REGISTER_TEST_SUITE("ecs serialization / prefabs / cloning", run_ecs_serialization_suite)
