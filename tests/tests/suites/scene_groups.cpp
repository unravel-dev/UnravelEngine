/*
 * Validation suite for scene groups: additive loads, group unloads and persistent roots.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "scene groups"
 *
 * Every loaded scene shares the one registry. A root carries the group of the scene it came
 * from, and a child belongs to whatever group its root has. These pin the rules that follow:
 *   - roots created during an additive load join its group, every other new root the main one;
 *   - a subtree taken out from under its root stays in that root's group;
 *   - an additive load gives its entities fresh ids, as a prefab instance does;
 *   - unloading a group, or loading a scene while persistent roots exist, destroys root by
 *     root through the announced path, so survivors hear about what goes; a load without
 *     persistent roots stays the bulk unload it always was;
 *   - the main scene's active skylight wins over the skylights of other groups.
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/ecs/scene.h>
#include <engine/engine.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/systems/skylight_system.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

void check_eq(size_t actual, size_t expected, const std::string& what)
{
    ++g_checks;
    if(actual != expected)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %zu, expected %zu)\n", what.c_str(), actual, expected);
    }
}

/// Publishes a scene's document as a scene asset, so handles to it resolve.
auto make_scene_asset(const scene& source, const std::string& id) -> asset_handle<scene_prefab>
{
    std::stringstream stream;
    save_to_stream(stream, source);
    const std::string text = stream.str();
    auto data = std::make_shared<scene_prefab>();
    data->buffer.data.assign(text.begin(), text.end());
    auto& am = engine::context().get_cached<asset_manager>();
    am.unload_asset<scene_prefab>(id);
    return am.get_asset_from_instance<scene_prefab>(id, data);
}

/// A scene file holding one root named `root_name` with one child under it.
auto make_scene_file(const std::string& id, const std::string& root_name) -> asset_handle<scene_prefab>
{
    scene source("scene_groups_source");
    auto root = source.create_entity(root_name);
    source.create_entity(root_name + " child", root);
    return make_scene_asset(source, id);
}

auto find_all_named(scene& scn, const std::string& name) -> std::vector<entt::handle>
{
    std::vector<entt::handle> found;
    for(auto e : scn.registry->view<tag_component>())
    {
        if(scn.registry->get<tag_component>(e).name == name)
        {
            found.push_back(scn.create_handle(e));
        }
    }
    return found;
}

auto find_named(scene& scn, const std::string& name) -> entt::handle
{
    auto found = find_all_named(scn, name);
    return found.empty() ? entt::handle{} : found.front();
}

/// Records every entity destroy_entity announces before tearing it down.
struct announcement_recorder
{
    explicit announcement_recorder(entt::registry& registry) : registry_(registry)
    {
        on_pre_destroy(registry_).connect<&announcement_recorder::on_announce>(*this);
    }

    ~announcement_recorder()
    {
        on_pre_destroy(registry_).disconnect<&announcement_recorder::on_announce>(*this);
    }

    announcement_recorder(const announcement_recorder&) = delete;
    auto operator=(const announcement_recorder&) -> announcement_recorder& = delete;

    void on_announce(entt::registry& /*registry*/, entt::entity e)
    {
        announced.push_back(e);
    }

    auto was_announced(entt::entity e) const -> bool
    {
        return std::find(announced.begin(), announced.end(), e) != announced.end();
    }

    std::vector<entt::entity> announced;

private:
    entt::registry& registry_;
};

void test_new_roots_join_the_main_group()
{
    std::printf("[new roots join the main group]\n");
    scene world("scene_groups");
    auto root = world.create_entity("root");
    auto child = world.create_entity("child", root);
    check(root.get<root_component>().group == MAIN_SCENE_GROUP, "a new root is in the main group");
    check(!child.all_of<root_component>(), "a child has no root component");
    check(find_scene_group(child) == MAIN_SCENE_GROUP, "a child belongs to its root's group");
}

void test_additive_load_tags_its_roots()
{
    std::printf("[an additive load tags its roots]\n");
    auto main_file = make_scene_file("test:/groups_main.spfb", "main");
    auto level_file = make_scene_file("test:/groups_level.spfb", "level");
    scene world("scene_groups");
    world.load_from(main_file, false);
    const scene_group_id group = world.reserve_scene_group();
    check(group >= FIRST_ADDITIVE_SCENE_GROUP, "a reserved group is an additive one");
    check(world.load_additive(level_file, group, false), "the additive load succeeds");
    auto main_root = find_named(world, "main");
    auto level_root = find_named(world, "level");
    auto level_child = find_named(world, "level child");
    check(bool(main_root), "the main scene is still there");
    check(bool(level_root) && bool(level_child), "the additive scene is there");
    check(find_scene_group(main_root) == MAIN_SCENE_GROUP, "the main root stays in the main group");
    check(level_root && level_root.get<root_component>().group == group, "the additive root is in its group");
    check(find_scene_group(level_child) == group, "the additive child follows its root");
    check(level_child && level_child.get<transform_component>().get_parent() == level_root,
          "the additive hierarchy is intact");
    auto spawned = world.create_entity("spawned");
    check(find_scene_group(spawned) == MAIN_SCENE_GROUP, "a root created after the load is in the main group");
    check(world.reserve_scene_group() > group, "group ids are not reused");
}

void test_additive_load_gives_fresh_ids()
{
    std::printf("[an additive load gives fresh ids]\n");
    auto level_file = make_scene_file("test:/groups_ids.spfb", "level");
    scene world("scene_groups");
    world.load_from(level_file, false);
    world.load_additive(level_file, world.reserve_scene_group(), false);
    world.load_additive(level_file, world.reserve_scene_group(), false);
    auto copies = find_all_named(world, "level");
    check_eq(copies.size(), 3, "three copies of the same scene");
    if(copies.size() != 3)
    {
        return;
    }
    const auto& id0 = copies[0].get<id_component>().id;
    const auto& id1 = copies[1].get<id_component>().id;
    const auto& id2 = copies[2].get<id_component>().id;
    check(id0 != id1 && id0 != id2 && id1 != id2, "every copy has its own id");
}

void test_unload_group_removes_only_that_group()
{
    std::printf("[unloading a group removes only that group]\n");
    auto main_file = make_scene_file("test:/groups_unload_main.spfb", "main");
    auto level_file = make_scene_file("test:/groups_unload_level.spfb", "level");
    scene world("scene_groups");
    world.load_from(main_file, false);
    const scene_group_id first = world.reserve_scene_group();
    const scene_group_id second = world.reserve_scene_group();
    world.load_additive(level_file, first, false);
    world.load_additive(level_file, second, false);
    auto main_root = find_named(world, "main");
    auto adopted = world.create_entity("adopted");
    auto first_root = entt::handle{};
    for(auto root : find_all_named(world, "level"))
    {
        if(root.get<root_component>().group == first)
        {
            first_root = root;
        }
    }
    adopted.get<transform_component>().set_parent(first_root, false);
    announcement_recorder recorder(*world.registry);
    world.unload_group(first);
    check_eq(find_all_named(world, "level").size(), 1, "one copy of the level is left");
    check_eq(find_all_named(world, "level child").size(), 1, "its child went with it");
    check(!adopted.valid(), "an entity parented under the unloaded group went with it");
    check(main_root.valid(), "the main scene stays");
    check(recorder.was_announced(first_root.entity()), "the unloaded root was announced before teardown");
    check(!recorder.was_announced(main_root.entity()), "the main root was not announced");
}

void test_detached_child_keeps_its_group()
{
    std::printf("[a detached child keeps its group]\n");
    auto level_file = make_scene_file("test:/groups_detach.spfb", "level");
    scene world("scene_groups");
    const scene_group_id group = world.reserve_scene_group();
    world.load_additive(level_file, group, false);
    auto child = find_named(world, "level child");
    child.get<transform_component>().set_parent({}, true);
    check(child.all_of<root_component>(), "the detached child is a root now");
    check(child.get<root_component>().group == group, "it stays in the scene it came from");
    auto main_root = world.create_entity("main");
    check(scene::make_persistent(main_root), "a root can be made persistent");
    auto carried = world.create_entity("carried");
    carried.get<transform_component>().set_parent(main_root, false);
    check(find_scene_group(carried) == PERSISTENT_SCENE_GROUP, "an entity parented under a persistent root is persistent");
}

void test_make_persistent_needs_a_root()
{
    std::printf("[only a root can be made persistent]\n");
    scene world("scene_groups");
    auto root = world.create_entity("root");
    auto child = world.create_entity("child", root);
    check(!scene::make_persistent(child), "a child cannot be made persistent");
    check(find_scene_group(child) == MAIN_SCENE_GROUP, "the refused child keeps its group");
    check(!scene::make_persistent(entt::handle{}), "an invalid entity cannot be made persistent");
}

void test_persistent_roots_survive_load_and_reload()
{
    std::printf("[persistent roots survive load and reload]\n");
    auto first_file = make_scene_file("test:/groups_first.spfb", "first");
    auto second_file = make_scene_file("test:/groups_second.spfb", "second");
    auto level_file = make_scene_file("test:/groups_persist_level.spfb", "level");
    scene world("scene_groups");
    world.load_from(first_file, false);
    world.load_additive(level_file, world.reserve_scene_group(), false);
    auto keeper = world.create_entity("keeper");
    auto keeper_child = world.create_entity("keeper child", keeper);
    check(scene::make_persistent(keeper), "the keeper is persistent");
    auto level_root = find_named(world, "level");
    check(scene::make_persistent(find_named(world, "level child")) == false, "a child of the level is refused");
    announcement_recorder recorder(*world.registry);
    world.load_from(second_file, false);
    check(keeper.valid() && keeper_child.valid(), "the keeper and its child survive the load");
    check(find_all_named(world, "first").empty(), "the previous scene is gone");
    check(find_all_named(world, "level").empty(), "the additive scene is gone");
    check_eq(find_all_named(world, "second").size(), 1, "the new scene is loaded once");
    check(recorder.was_announced(level_root.entity()), "the teardown with survivors was announced");
    world.reload(false);
    check(keeper.valid() && keeper_child.valid(), "the keeper survives a reload");
    check_eq(find_all_named(world, "second").size(), 1, "the reloaded scene is there once");
    check(world.source == second_file, "the scene still knows its source");
    world.unload();
    check(!keeper.valid(), "a full unload removes persistent roots too");
}

void test_load_without_persistent_roots_is_a_bulk_unload()
{
    std::printf("[a load without persistent roots is a bulk unload]\n");
    auto first_file = make_scene_file("test:/groups_bulk_first.spfb", "first");
    auto second_file = make_scene_file("test:/groups_bulk_second.spfb", "second");
    scene world("scene_groups");
    world.load_from(first_file, false);
    auto first_root = find_named(world, "first");
    announcement_recorder recorder(*world.registry);
    world.load_from(second_file, false);
    check(!first_root.valid(), "the previous scene is gone");
    check(recorder.announced.empty(), "nothing was announced");
}

/// Adds an active skylight of the given irradiance intensity to a new root.
auto make_sky(scene& world, const std::string& name, float intensity) -> entt::handle
{
    auto sky = world.create_entity(name);
    sky.emplace<skylight_component>().set_irradiance_intensity(intensity);
    (void)sky.get_or_emplace<active_component>();
    return sky;
}

void test_main_scene_skylight_wins()
{
    std::printf("[the main scene's skylight wins]\n");
    scene world("scene_groups");
    auto main_sky = make_sky(world, "main sky", 1.0f);
    auto level_sky = make_sky(world, "level sky", 5.0f);
    auto kept_sky = make_sky(world, "kept sky", 3.0f);
    level_sky.get<root_component>().group = FIRST_ADDITIVE_SCENE_GROUP;
    scene::make_persistent(kept_sky);
    check(find_skylight_in_effect(*world.registry) == main_sky, "the main sky wins over brighter ones elsewhere");
    auto second_main_sky = make_sky(world, "second main sky", 2.0f);
    check(find_skylight_in_effect(*world.registry) == second_main_sky, "among main skies the strongest wins");
    main_sky.remove<active_component>();
    second_main_sky.remove<active_component>();
    check(find_skylight_in_effect(*world.registry) == level_sky,
          "without an active main sky the strongest of the others wins");
    level_sky.remove<active_component>();
    kept_sky.remove<active_component>();
    check(!find_skylight_in_effect(*world.registry), "no active skylight, no sky");
}

/// Creates a root from a pre-destroy slot, as a script's OnDestroy spawning loot does.
struct teardown_spawner
{
    explicit teardown_spawner(entt::registry& registry, std::string trigger, bool every_time = false)
        : registry_(registry)
        , trigger_(std::move(trigger))
        , every_time_(every_time)
    {
        on_pre_destroy(registry_).connect<&teardown_spawner::on_announce>(*this);
    }

    ~teardown_spawner()
    {
        on_pre_destroy(registry_).disconnect<&teardown_spawner::on_announce>(*this);
    }

    teardown_spawner(const teardown_spawner&) = delete;
    auto operator=(const teardown_spawner&) -> teardown_spawner& = delete;

    void on_announce(entt::registry& registry, entt::entity e)
    {
        const auto* tag = registry.try_get<tag_component>(e);
        if(tag != nullptr && (every_time_ || tag->name == trigger_))
        {
            scene::create_entity(registry, "spawned");
        }
    }

private:
    entt::registry& registry_;
    std::string trigger_;
    bool every_time_{};
};

void test_teardown_takes_what_it_spawns()
{
    std::printf("[a partial teardown takes what scripts spawn meanwhile]\n");
    auto level_file = make_scene_file("test:/groups_spawn_level.spfb", "level");
    auto next_file = make_scene_file("test:/groups_spawn_next.spfb", "next");
    scene world("scene_groups");
    world.load_from(next_file, false);
    const scene_group_id group = world.reserve_scene_group();
    world.load_additive(level_file, group, false);
    {
        teardown_spawner spawner(*world.registry, "level");
        world.unload_group(group);
    }
    check(find_all_named(world, "spawned").empty(), "loot spawned while a group unloads goes with it");
    check_eq(find_all_named(world, "next").size(), 1, "the main scene stays");
    auto keeper = world.create_entity("keeper");
    scene::make_persistent(keeper);
    {
        teardown_spawner spawner(*world.registry, "next");
        world.load_from(next_file, false);
    }
    check(find_all_named(world, "spawned").empty(), "loot spawned during a switch does not reach the next scene");
    check(keeper.valid(), "the persistent root survives");
}

void test_switch_takes_entities_outside_any_hierarchy()
{
    std::printf("[a switch with persistent roots takes entities outside any hierarchy]\n");
    auto next_file = make_scene_file("test:/groups_bare_next.spfb", "next");
    scene world("scene_groups");
    scene::make_persistent(world.create_entity("keeper"));
    entt::handle bare(*world.registry, world.registry->create());
    bare.emplace<tag_component>().name = "bare";
    world.load_from(next_file, false);
    check(!bare.valid(), "an entity without a transform holding a component is removed");
    check(!find_all_named(world, "keeper").empty(), "the persistent root survives");
}

void test_endless_spawning_teardown_ends()
{
    std::printf("[a teardown whose scripts keep spawning ends]\n");
    auto next_file = make_scene_file("test:/groups_endless_next.spfb", "next");
    scene world("scene_groups");
    scene::make_persistent(world.create_entity("keeper"));
    world.create_entity("level");
    {
        teardown_spawner spawner(*world.registry, {}, true);
        world.load_from(next_file, false);
    }
    check(find_all_named(world, "spawned").size() <= 1, "the teardown stopped after its pass limit");
}

} // namespace

auto run_scene_groups_suite(rtti::context& /*ctx*/) -> int
{
    test_new_roots_join_the_main_group();
    test_additive_load_tags_its_roots();
    test_additive_load_gives_fresh_ids();
    test_unload_group_removes_only_that_group();
    test_detached_child_keeps_its_group();
    test_make_persistent_needs_a_root();
    test_persistent_roots_survive_load_and_reload();
    test_load_without_persistent_roots_is_a_bulk_unload();
    test_main_scene_skylight_wins();
    test_teardown_takes_what_it_spawns();
    test_switch_takes_entities_outside_any_hierarchy();
    test_endless_spawning_teardown_ends();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("scene groups", run_scene_groups_suite)
