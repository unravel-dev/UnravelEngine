/*
 * Behaviour harness for entity serialization, prefabs, cloning and prefab overrides.
 *
 * Not part of the default build. Run it explicitly:
 *   cmake --build <build-dir> --target ecs_serialization_tests
 *   <build-dir>/bin/ecs_serialization_tests           # correctness
 *   <build-dir>/bin/ecs_serialization_tests --bench   # correctness + timings
 *   <build-dir>/bin/ecs_serialization_tests --bench-only
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
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/test_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/threading/threader.h>
#include <engine/meta/ecs/entity.hpp>

#include <logging/logging.h>
#include <serialization/serialization.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <threadpp/thread_pool.h>
#include <uuid/uuid.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
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
auto serialize_as_prefab(entt::const_handle root) -> std::vector<uint8_t>
{
    std::ostringstream ss;

    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = root;
    save_ctx.to_prefab = true;

    save_to_stream(ss, root);

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
auto make_prefab_asset(std::vector<uint8_t> bytes, const std::string& id) -> asset_handle<prefab>
{
    auto data = std::make_shared<prefab>();
    data->buffer.data = std::move(bytes);

    asset_handle<prefab> handle;
    handle.set_internal_ids(generate_uuid(), id);

    tpp::job_future<std::shared_ptr<prefab>> job(tpp::make_ready_future(std::shared_ptr<prefab>(data)));
    handle.set_internal_job(job.share());

    return handle;
}

auto make_prefab_from(entt::const_handle root, const std::string& id) -> asset_handle<prefab>
{
    return make_prefab_asset(serialize_as_prefab(root), id);
}

/**
 * @brief Resyncs a prefab instance against its source, as the editor does.
 *
 * Deliberately duplicates editing_manager::sync_prefab_entity
 * (editor/editor/editing/editing_manager.cpp) rather than calling it: the editor is not
 * linkable from an engine test, and what needs pinning is the engine-side contract that
 * the editor relies on. If the editor's version changes, this must change with it.
 *
 * The position/rotation snapshot is intentional and is not an oversight - see the header
 * comment and tasks/lessons.md.
 */
void sync_prefab_instance(entt::handle& instance, const asset_handle<prefab>& pfb)
{
    auto* trans_comp = instance.try_get<transform_component>();
    if(!trans_comp)
    {
        return;
    }

    auto parent = trans_comp->get_parent();
    auto pos = trans_comp->get_position_local();
    auto rot = trans_comp->get_rotation_local();

    auto& prefab_comp = instance.get<prefab_component>();

    // Mirrors the editor: with no overrides recorded, the callback could only ever answer
    // "not overridden", which is what happens with no context installed. Skipping it
    // avoids building a path per property to be told so.
    const bool needs_override_tracking = !prefab_comp.get_all_overrides().empty();

    serialization::path_context path_ctx;
    serialization::path_context* old_ctx = serialization::get_path_context();

    if(needs_override_tracking)
    {
        path_ctx.should_serialize_property_callback = [&](const std::string& property_path) -> bool
        {
            return !prefab_comp.has_serialization_override(property_path);
        };
        path_ctx.enable_recording();
        serialization::set_path_context(&path_ctx);
    }

    if(scene::instantiate_out(*instance.registry(), pfb, instance, false))
    {
        auto& new_trans = instance.get<transform_component>();
        new_trans.set_position_local(pos);
        new_trans.set_rotation_local(rot);
        new_trans.set_parent(parent, false);
    }

    if(needs_override_tracking)
    {
        serialization::set_path_context(old_ctx);
    }
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

// ---------------------------------------------------------------------------------
// Scene round-trip
// ---------------------------------------------------------------------------------

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
        check(prefab_comp->property_overrides.empty(), "a fresh instance has no overrides");
        check(prefab_comp->removed_entities.empty(), "a fresh instance has no removed entities");
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
        sync_prefab_instance(instance, asset);
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
    check_eq(comp.get_all_overrides().size(), 1, "the child path replaces the parent path");
    check(comp.has_override(uid, "transform_component/local_transform/scale"), "the surviving entry is the child");

    // The other direction: a broader path must not displace a narrower one already there.
    comp.add_override(uid, "transform_component", "Transform");
    check_eq(comp.get_all_overrides().size(), 1, "the broader path is dropped");
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
    check_eq(comp.get_all_overrides().size(), 4, "four overrides to start with");

    comp.remove_entity(doomed);

    check(comp.removed_entities.contains(doomed), "the entity is recorded as removed");
    check(comp.has_override(other, "tag_component/name"), "another entity's override is untouched");

    // Every override the entity owned must go, not just the first match. They are keyed by
    // uuid, so any left behind can never be reached again: they would be re-serialized into
    // the scene forever and show in the inspector as "Entity Not Found".
    size_t remaining = 0;
    for(const auto& entry : comp.get_all_overrides())
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

    double best_load = 1e30;
    uint64_t lookups = 0;
    uint64_t thrown = 0;
    for(int i = 0; i < reps; ++i)
    {
        scene dst("bench_dst");
        std::stringstream ss(text);
        serialization::reset_failed_lookup_count();
        const auto t0 = clock_t_::now();
        load_from_stream(ss, dst);
        best_load = std::min(best_load, ms_since(t0));
        lookups = serialization::failed_lookup_count();
        thrown = serialization::thrown_lookup_count();
    }
    record("scene load", n, best_load, text.size(), lookups, thrown);
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
                h.get<transform_component>().set_parent(prefab_root, false);
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
        sync_prefab_instance(instance, asset);
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
        sync_prefab_instance(instance, asset);
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
                [&](auto e, auto&&) { src.create_handle(e).emplace<test_component>(); });
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

void run_benchmarks()
{
    std::printf("\n================================ benchmarks ================================\n");

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

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if(!spdlog::get(APPLOG))
    {
        spdlog::create<spdlog::sinks::stdout_sink_mt>(APPLOG);
    }

    // Installs the vector element callbacks that produce "[i]" path segments; the prefab
    // override paths are built out of them.
    serialization::init();

    // LOAD(asset_handle<T>) resolves a uid through engine::context()'s asset_manager, so
    // anything that loads an entity carrying an asset reference needs an ambient context.
    // asset_manager binds a reference to threader's pool in its constructor, so the
    // threader has to come first (its constructor is what calls tpp::init and builds the
    // pool - threader::init is a no-op).
    //
    // No asset database is registered behind the manager, so a handle that is loaded back
    // comes out empty. Asset resolution is not what this harness pins; entity identity is.
    // The uid that gets written is asserted directly instead.
    rtti::context ctx;
    ctx.add<threader>();
    ctx.add<asset_manager>(ctx);
    engine::set_context(&ctx);

    bool bench = false;
    bool bench_only = false;
    bool binary_probe = false;
    for(int i = 1; i < argc; ++i)
    {
        if(std::strcmp(argv[i], "--bench") == 0)
        {
            bench = true;
        }
        else if(std::strcmp(argv[i], "--bench-only") == 0)
        {
            bench = true;
            bench_only = true;
        }
        else if(std::strcmp(argv[i], "--binary-probe") == 0)
        {
            binary_probe = true;
        }
    }

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
        test_resync_keeps_user_added_children();

        test_override_path_parsing();
        test_add_override_collapses_nested_paths();
        test_remove_entity_clears_all_of_its_overrides();

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

    const int result = g_failures == 0 ? 0 : 1;

    // Join the worker threads before the context unwinds; tpp::shutdown outlives the
    // pool's own destructor.
    engine::set_context(nullptr);
    ctx.get<threader>().deinit(ctx);

    return result;
}
