/*
 * Validation harness for contact tracking and the entity destroy funnel.
 *
 * Not part of the default build. Run it explicitly:
 *   cmake --build <build-dir> --target physics_tests
 *   <build-dir>/bin/physics_tests
 *
 * These pin the 2026-08 contact-exit-on-destroy work (tasks/contact_exit_on_destroy_plan.md):
 *   - a body destroyed inside a sensor produced no OnSensorExit, because exit was
 *     inferred a step later from a vanished manifold and then dropped when both
 *     entt::handles turned out to be dead;
 *   - entt::registry::destroy removes components in reverse pool-registration order
 *     and forbids mutating the entity from inside a destroy hook, so the notification
 *     has to happen before registry::destroy is entered;
 *   - the sorted-vector contact map had no per-body index, making "does destroying
 *     this body owe anyone an event" an O(n) scan on a path taken by every death.
 *
 * The contact graph is covered directly rather than through a live btDynamicsWorld:
 * the intrusive link surgery is the part most likely to be wrong, and it is pure
 * data-structure code with no Bullet dependency.
 */

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/physics/backend/contact_graph.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/physics/physics_types.h>

#include <logging/logging.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <algorithm>
#include <cstdio>
#include <memory>
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

// ---------------------------------------------------------------------------------
// contact_graph fixture
// ---------------------------------------------------------------------------------

/// Stands in for bullet::rigidbody, which is where the real link header lives.
struct test_body
{
    contact_links links{};
};

auto resolve_test_links(entt::handle entity) -> contact_links*
{
    if(!entity)
    {
        return nullptr;
    }
    if(auto* body = entity.try_get<test_body>())
    {
        return &body->links;
    }
    return nullptr;
}

struct test_payload
{
    int tag{};
};

using test_graph = contact_graph<test_payload>;

/// A registry of bodies, heap-allocated so entt::handle's registry pointer stays put.
struct graph_fixture
{
    graph_fixture() : registry(std::make_unique<entt::registry>())
    {
    }

    auto spawn_body() -> entt::handle
    {
        entt::handle e(*registry, registry->create());
        e.emplace<test_body>();
        return e;
    }

    /// An entity the graph cannot resolve a link header for.
    auto spawn_bodyless() -> entt::handle
    {
        return entt::handle(*registry, registry->create());
    }

    auto links_of(entt::handle e) -> const contact_links&
    {
        return e.get<test_body>().links;
    }

    /// Every slot reachable from one participant's list, in walk order.
    auto walk(test_graph& graph, entt::handle owner) -> std::vector<uint32_t>
    {
        std::vector<uint32_t> out;
        graph.visit(owner,
                    [&](uint32_t id, const test_graph::slot&)
                    {
                        out.push_back(id);
                        return false;
                    });
        return out;
    }

    /**
     * @brief Full structural check of one participant's list.
     *
     * A forward walk alone is far too weak: dropping the successor's back-pointer on
     * unlink leaves the forward chain intact and only corrupts the list two erases
     * later. Every invariant the link surgery is supposed to maintain gets asserted
     * here, and every test calls this after each mutation.
     */
    void check_links(test_graph& graph, entt::handle owner, const std::string& what)
    {
        const auto& links = links_of(owner);
        const auto chain = walk(graph, owner);

        check_eq(chain.size(), links.tracked, what + ": walk length matches the tracked counter");

        size_t owed = 0;
        for(size_t i = 0; i < chain.size(); ++i)
        {
            const auto& slot = graph.get(chain[i]);
            check(slot.in_use, what + ": every slot in the list is in use");
            check(slot.a == owner || slot.b == owner, what + ": every slot in the list actually involves the owner");

            if(slot.flush_on_destroy)
            {
                ++owed;
            }

            // The back-pointer of each node must name its predecessor, and the first
            // node must have none.
            const bool as_a = (slot.a == owner);
            const uint32_t prev = as_a ? slot.prev_a : slot.prev_b;
            if(i == 0)
            {
                check(prev == test_graph::npos, what + ": the head has no predecessor");
            }
            else
            {
                check(prev == chain[i - 1], what + ": each node's back-pointer names its predecessor");
            }
        }

        check_eq(owed, links.flush_pending, what + ": owed pairs match the flush_pending counter");
    }

    /// The same check for every participant of every live pair.
    void check_all_links(test_graph& graph, const std::string& what)
    {
        std::vector<entt::entity> seen;
        const auto once = [&](entt::handle e)
        {
            if(!e || std::find(seen.begin(), seen.end(), e.entity()) != seen.end())
            {
                return;
            }
            seen.push_back(e.entity());
            check_links(graph, e, what);
        };

        for(const uint32_t id : graph.live())
        {
            once(graph.get(id).a);
            once(graph.get(id).b);
        }
    }

    std::unique_ptr<entt::registry> registry;
};

// ---------------------------------------------------------------------------------
// contact_graph
// ---------------------------------------------------------------------------------

void test_graph_insert_and_erase()
{
    std::printf("test_graph_insert_and_erase\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto a = fx.spawn_body();
    auto b = fx.spawn_body();

    const uint32_t id = graph.insert(a, b, true);
    check(id != test_graph::npos, "insert returns a slot");
    check_eq(fx.links_of(a).tracked, 1, "a tracks one pair");
    check_eq(fx.links_of(b).tracked, 1, "b tracks one pair");
    check_eq(fx.links_of(a).flush_pending, 1, "a owes one exit");
    check_eq(fx.links_of(b).flush_pending, 1, "b owes one exit");
    check_eq(graph.live().size(), 1, "one live slot");

    graph.erase(id);
    check_eq(fx.links_of(a).tracked, 0, "a tracks nothing after erase");
    check_eq(fx.links_of(b).tracked, 0, "b tracks nothing after erase");
    check_eq(fx.links_of(a).flush_pending, 0, "a owes nothing after erase");
    check_eq(fx.links_of(b).flush_pending, 0, "b owes nothing after erase");
    check(fx.links_of(a).head == test_graph::npos, "a's list is empty after erase");
    check_eq(graph.live().size(), 0, "no live slots after erase");

    // Erasing twice must be a no-op, not a double unlink.
    graph.erase(id);
    check_eq(fx.links_of(a).tracked, 0, "double erase leaves counters at zero");
}

void test_graph_list_integrity()
{
    std::printf("test_graph_list_integrity\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    // One hub touching several partners: the sensor-with-many-occupants topology.
    auto hub = fx.spawn_body();
    std::vector<entt::handle> partners;
    std::vector<uint32_t> ids;
    for(int i = 0; i < 5; ++i)
    {
        partners.push_back(fx.spawn_body());
        ids.push_back(graph.insert(hub, partners.back(), false));
    }

    check_eq(fx.links_of(hub).tracked, 5, "hub tracks every partner");
    check_eq(fx.walk(graph, hub).size(), 5, "walking the hub reaches every pair");
    fx.check_links(graph, hub, "after five inserts");

    // Erase from the middle, then the head of the list, then the remaining tail.
    // The list is [4,3,2,1,0] - most recent insert first.
    graph.erase(ids[2]);
    auto remaining = fx.walk(graph, hub);
    check_eq(remaining.size(), 4, "middle erase leaves the rest reachable");
    check(std::find(remaining.begin(), remaining.end(), ids[2]) == remaining.end(),
          "the erased middle pair is gone");
    fx.check_links(graph, hub, "after a middle erase");

    // Erasing the erased node's SUCCESSOR is the case a forward walk cannot see:
    // if unlink left ids[1].prev naming the freed ids[2], this repairs the wrong
    // node and leaves ids[3] pointing into the free pool.
    graph.erase(ids[1]);
    fx.check_links(graph, hub, "after erasing the successor of an erased node");
    check_eq(fx.walk(graph, hub).size(), 3, "erasing a successor leaves exactly the rest");

    // ids[4] was inserted last, so it is the head.
    graph.erase(ids[4]);
    remaining = fx.walk(graph, hub);
    check_eq(remaining.size(), 2, "head erase leaves the rest reachable");
    check(std::find(remaining.begin(), remaining.end(), ids[4]) == remaining.end(), "the erased head is gone");
    fx.check_links(graph, hub, "after a head erase");

    graph.erase(ids[0]);
    remaining = fx.walk(graph, hub);
    check_eq(remaining.size(), 1, "tail erase leaves the rest reachable");
    fx.check_links(graph, hub, "after a tail erase");

    // Each partner's own side must have been unlinked too.
    check_eq(fx.walk(graph, partners[3]).size(), 1, "partner 3 still sees its pair");
    check_eq(fx.walk(graph, partners[2]).size(), 0, "partner 2's pair was erased from its side too");
    check_eq(fx.walk(graph, partners[1]).size(), 0, "partner 1's pair was erased from its side too");
    fx.check_all_links(graph, "after the full erase sequence");
}

void test_graph_body_on_both_sides()
{
    std::printf("test_graph_body_on_both_sides\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    // The reported bug's topology: the dying body is the `b` side of a sensor pair
    // while also being the `a` side of others. Both must be reachable from it.
    auto victim = fx.spawn_body();
    auto sensor_one = fx.spawn_body();
    auto sensor_two = fx.spawn_body();
    auto touched = fx.spawn_body();

    graph.insert(sensor_one, victim, true);
    graph.insert(sensor_two, victim, true);
    graph.insert(victim, touched, true);

    check_eq(fx.links_of(victim).tracked, 3, "the victim tracks pairs from both sides");
    check_eq(fx.walk(graph, victim).size(), 3, "walking the victim crosses a/b sides");
    check_eq(fx.links_of(sensor_one).tracked, 1, "sensor one tracks the victim");
    fx.check_all_links(graph, "with the victim on both sides");

    // Releasing the victim must unlink from every partner, not just its own list.
    graph.visit(victim,
                [&](uint32_t id, const test_graph::slot&)
                {
                    graph.erase(id);
                    return false;
                });

    check_eq(fx.links_of(victim).tracked, 0, "the victim tracks nothing after release");
    check_eq(fx.links_of(sensor_one).tracked, 0, "sensor one lost its pair too");
    check_eq(fx.links_of(sensor_two).tracked, 0, "sensor two lost its pair too");
    check_eq(fx.links_of(touched).tracked, 0, "the touched body lost its pair too");
    check_eq(graph.live().size(), 0, "no slots survive the release");
}

void test_graph_flush_counters()
{
    std::printf("test_graph_flush_counters\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto body = fx.spawn_body();
    auto p0 = fx.spawn_body();
    auto p1 = fx.spawn_body();
    auto p2 = fx.spawn_body();

    // The projectile case: many tracked pairs, none of them owed on destroy.
    graph.insert(body, p0, false);
    graph.insert(body, p1, false);
    const uint32_t owed = graph.insert(body, p2, true);

    check_eq(fx.links_of(body).tracked, 3, "all three pairs are tracked");
    check_eq(fx.links_of(body).flush_pending, 1, "only the opted-in pair is owed");
    fx.check_all_links(graph, "with a mixed removal policy");

    // Flipping the policy has to move both participants' counters.
    graph.set_flush_on_destroy(owed, false);
    check_eq(fx.links_of(body).flush_pending, 0, "clearing the policy clears the owner's counter");
    check_eq(fx.links_of(p2).flush_pending, 0, "clearing the policy clears the partner's counter");

    graph.set_flush_on_destroy(owed, true);
    check_eq(fx.links_of(body).flush_pending, 1, "setting the policy restores the owner's counter");
    check_eq(fx.links_of(p2).flush_pending, 1, "setting the policy restores the partner's counter");

    // A redundant set must not double-count.
    graph.set_flush_on_destroy(owed, true);
    check_eq(fx.links_of(body).flush_pending, 1, "setting an already-set policy is a no-op");

    graph.erase(owed);
    check_eq(fx.links_of(body).flush_pending, 0, "erasing the owed pair clears the counter");
    check_eq(fx.links_of(body).tracked, 2, "the other pairs survive");
    fx.check_all_links(graph, "after erasing the owed pair");
}

void test_graph_erase_during_visit()
{
    std::printf("test_graph_erase_during_visit\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    // This is exactly how release_contacts_for drains a body: erase the slot it was
    // just handed, and keep walking.
    auto hub = fx.spawn_body();
    for(int i = 0; i < 6; ++i)
    {
        graph.insert(hub, fx.spawn_body(), true);
    }

    size_t visited = 0;
    graph.visit(hub,
                [&](uint32_t id, const test_graph::slot&)
                {
                    ++visited;
                    graph.erase(id);
                    return false;
                });

    check_eq(visited, 6, "erasing during the walk still visits every pair exactly once");
    check_eq(fx.links_of(hub).tracked, 0, "the hub is empty afterwards");
    check_eq(graph.live().size(), 0, "no slots leak");
}

void test_graph_generation_guard()
{
    std::printf("test_graph_generation_guard\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto a = fx.spawn_body();
    auto b = fx.spawn_body();
    auto c = fx.spawn_body();

    const uint32_t id = graph.insert(a, b, true);
    const uint32_t generation = graph.get(id).generation;
    check(graph.is_live(id, generation), "a fresh slot is live");

    graph.erase(id);
    check(!graph.is_live(id, generation), "an erased slot is not live");

    // The pool recycles ids, so a stale queued reference must not resolve to the
    // pair that took its place.
    const uint32_t recycled = graph.insert(a, c, true);
    check_eq(recycled, id, "the pool recycles the freed slot id");
    check(!graph.is_live(id, generation), "the stale generation does not match the recycled slot");
    check(graph.is_live(recycled, graph.get(recycled).generation), "the recycled slot is live under its own generation");
}

void test_graph_live_list()
{
    std::printf("test_graph_live_list\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto hub = fx.spawn_body();
    std::vector<uint32_t> ids;
    for(int i = 0; i < 8; ++i)
    {
        ids.push_back(graph.insert(hub, fx.spawn_body(), false));
    }

    // Interleaved erases exercise the swap-and-pop bookkeeping.
    graph.erase(ids[0]);
    graph.erase(ids[7]);
    graph.erase(ids[3]);

    check_eq(graph.live().size(), 5, "the live list shrinks by exactly the erased count");
    fx.check_all_links(graph, "after interleaved erases");

    for(const uint32_t id : graph.live())
    {
        check(graph.get(id).in_use, "every id in the live list is in use");
        check(graph.get(id).live_index < graph.live().size(), "every slot's live index is in range");
        check(graph.live()[graph.get(id).live_index] == id, "live index round-trips");
    }

    // The Phase 2 sweep pattern: walk the live list backwards, erasing as it goes.
    const auto& live = graph.live();
    size_t swept = 0;
    for(size_t i = live.size(); i != 0; --i)
    {
        ++swept;
        graph.erase(live[i - 1]);
    }
    check_eq(swept, 5, "the backwards sweep visits every live slot once");
    check_eq(graph.live().size(), 0, "the backwards sweep drains the live list");
}

void test_graph_step_stamp()
{
    std::printf("test_graph_step_stamp\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto a = fx.spawn_body();
    auto b = fx.spawn_body();
    auto c = fx.spawn_body();

    const uint32_t first = graph.advance_stamp();
    check(first != 0, "a step stamp is never zero, so a value-initialised slot cannot look seen");
    check_eq(graph.stamp(), first, "stamp() reports the open step");

    const uint32_t id = graph.insert(a, b, false);
    check_eq(graph.get(id).seen_stamp, first, "a pair inserted during a step counts as seen in it");

    // Opening the next step is what makes last step's pairs stale - no pass over the
    // pool, just a counter bump.
    const uint32_t second = graph.advance_stamp();
    check(second != first, "each step gets a distinct stamp");
    check(graph.get(id).seen_stamp != second, "a pair not seen again is stale in the new step");

    graph.get(id).seen_stamp = graph.stamp();
    check_eq(graph.get(id).seen_stamp, second, "refreshing a pair marks it seen in the current step");

    // A recycled slot must not inherit a stale stamp from its previous life.
    graph.erase(id);
    graph.advance_stamp();
    const uint32_t recycled = graph.insert(a, c, false);
    check_eq(recycled, id, "the pool recycles the freed slot");
    check_eq(graph.get(recycled).seen_stamp, graph.stamp(), "a recycled slot is stamped for the current step");
}

void test_graph_clear_and_unresolvable()
{
    std::printf("test_graph_clear_and_unresolvable\n");

    graph_fixture fx;
    test_graph graph(&resolve_test_links);

    auto a = fx.spawn_body();
    auto b = fx.spawn_body();
    graph.insert(a, b, true);
    graph.insert(a, fx.spawn_body(), true);

    graph.clear();
    check_eq(graph.live().size(), 0, "clear drains the live list");
    check_eq(fx.links_of(a).tracked, 0, "clear resets the participants' counters");
    check_eq(fx.links_of(a).flush_pending, 0, "clear resets the owed counters");
    check(fx.links_of(a).head == test_graph::npos, "clear resets the participants' heads");

    // An object with no body component (a query proxy, a stale user index) must be
    // refused rather than silently half-linked.
    auto bodyless = fx.spawn_bodyless();
    check(graph.insert(a, bodyless, true) == test_graph::npos, "insert refuses an unresolvable participant");
    check_eq(fx.links_of(a).tracked, 0, "the refused insert left no partial link");
    check_eq(graph.live().size(), 0, "the refused insert allocated no slot");
}

// ---------------------------------------------------------------------------------
// contact policy
// ---------------------------------------------------------------------------------

void test_contact_event_flag_defaults()
{
    std::printf("test_contact_event_flag_defaults\n");

    entt::registry registry;
    entt::handle e(registry, registry.create());
    auto& comp = e.emplace<physics_component>();

    check(comp.is_sensor_events_enabled(), "sensor events are on by default");
    check(comp.is_collision_events_enabled(), "collision events are on by default");
    check(comp.is_sensor_exit_on_destroy(),
          "sensor exit on destroy is on by default - this is the reported bug's case");
    check(!comp.is_collision_exit_on_destroy(),
          "collision exit on destroy is off by default - a body in permanent contact must not pay for it");

    check(has_any(contact_event_flags_default, contact_event_flags::sensor_exit_on_destroy),
          "the default flag set agrees with the accessors");
    check(!has_any(contact_event_flags_default, contact_event_flags::collision_exit_on_destroy),
          "the default flag set excludes collision exit on destroy");
}

void test_contact_event_flag_roundtrip()
{
    std::printf("test_contact_event_flag_roundtrip\n");

    entt::registry registry;
    entt::handle e(registry, registry.create());
    auto& comp = e.emplace<physics_component>();

    comp.set_dirty(0, false);
    check(!comp.is_property_dirty(physics_property::contact_events), "clearing dirty clears the contact policy flag");

    // A no-op write must not dirty the component: the dirty flag drives an O(k) walk
    // of the body's contacts.
    comp.set_sensor_events_enabled(true);
    check(!comp.is_property_dirty(physics_property::contact_events), "setting an already-set flag does not dirty");

    comp.set_collision_exit_on_destroy(true);
    check(comp.is_property_dirty(physics_property::contact_events), "a real change dirties the contact policy");
    check(comp.is_collision_exit_on_destroy(), "collision exit on destroy round-trips");
    check(comp.is_sensor_exit_on_destroy(), "enabling one flag leaves the others alone");

    comp.set_sensor_events_enabled(false);
    check(!comp.is_sensor_events_enabled(), "sensor events can be turned off");
    check(comp.is_collision_events_enabled(), "turning off sensor events leaves collision events alone");

    comp.set_contact_event_flags(contact_event_flags::none);
    check(!comp.is_sensor_events_enabled() && !comp.is_collision_events_enabled() &&
              !comp.is_sensor_exit_on_destroy() && !comp.is_collision_exit_on_destroy(),
          "none clears every flag");

    comp.set_contact_event_flags(contact_event_flags::sensor_events | contact_event_flags::collision_exit_on_destroy);
    check(comp.is_sensor_events_enabled() && comp.is_collision_exit_on_destroy() &&
              !comp.is_collision_events_enabled() && !comp.is_sensor_exit_on_destroy(),
          "an explicit flag set round-trips exactly");
}

void test_contact_end_reason_mirror()
{
    std::printf("test_contact_end_reason_mirror\n");

    check(mirror_contact_end_reason(contact_end_reason::other_destroyed) == contact_end_reason::self_destroyed,
          "the survivor's 'other destroyed' is the victim's 'self destroyed'");
    check(mirror_contact_end_reason(contact_end_reason::self_destroyed) == contact_end_reason::other_destroyed,
          "mirroring is symmetric for destruction");
    check(mirror_contact_end_reason(contact_end_reason::other_disabled) == contact_end_reason::self_disabled,
          "mirroring maps disabled to disabled");
    check(mirror_contact_end_reason(contact_end_reason::separated) == contact_end_reason::separated,
          "separation reads the same from both sides");

    // Mirroring twice has to be the identity, or a collision exit delivered to both
    // sides would disagree about what happened.
    const contact_end_reason all[] = {contact_end_reason::separated,
                                      contact_end_reason::other_destroyed,
                                      contact_end_reason::other_disabled,
                                      contact_end_reason::self_destroyed,
                                      contact_end_reason::self_disabled};
    for(const auto reason : all)
    {
        check(mirror_contact_end_reason(mirror_contact_end_reason(reason)) == reason,
              "mirroring is an involution");
    }
}

// ---------------------------------------------------------------------------------
// EnTT behaviour this design depends on
//
// These are not tests of our code. They pin the entt guarantees that decide why the
// pre-destroy phase has to live in scene::destroy_entity rather than in a hook or a
// marker component. If a future entt changes any of them, the right response is to
// revisit tasks/contact_exit_on_destroy_plan.md section 2, not to edit these.
// ---------------------------------------------------------------------------------

std::vector<int> g_destroy_order;

struct order_a
{
};
struct order_b
{
};
struct order_marker
{
};

void record_a(entt::registry&, entt::entity)
{
    g_destroy_order.push_back(0);
}
void record_b(entt::registry&, entt::entity)
{
    g_destroy_order.push_back(1);
}
void record_marker(entt::registry&, entt::entity)
{
    g_destroy_order.push_back(2);
}
void record_entity(entt::registry&, entt::entity)
{
    g_destroy_order.push_back(3);
}

void test_entt_destroys_in_reverse_pool_order()
{
    std::printf("test_entt_destroys_in_reverse_pool_order\n");

    // Destroy walks pools in reverse assurance order, so the LAST pool to come into
    // existence is destroyed FIRST. That is the entire basis of the "marker component
    // destroyed first" idea - and of why it cannot be relied on.
    {
        entt::registry r;
        g_destroy_order.clear();
        r.on_destroy<order_a>().connect<&record_a>();
        r.on_destroy<order_b>().connect<&record_b>();
        r.on_destroy<order_marker>().connect<&record_marker>();

        auto e = r.create();
        r.emplace<order_a>(e);
        r.emplace<order_b>(e);
        r.emplace<order_marker>(e);
        r.destroy(e);

        check(g_destroy_order == std::vector<int>({2, 1, 0}),
              "destroy order is the reverse of pool assurance order");
    }

    // Emplace order is irrelevant - only the order the pools were assured in.
    {
        entt::registry r;
        g_destroy_order.clear();
        r.on_destroy<order_a>().connect<&record_a>();
        r.on_destroy<order_b>().connect<&record_b>();
        r.on_destroy<order_marker>().connect<&record_marker>();

        auto e = r.create();
        r.emplace<order_marker>(e);
        r.emplace<order_a>(e);
        r.emplace<order_b>(e);
        r.destroy(e);

        check(g_destroy_order == std::vector<int>({2, 1, 0}),
              "emplace order does not affect destroy order");
    }

    // THE reason a marker component cannot be trusted to run first: any pool assured
    // after it is destroyed before it. In this engine bullet::rigidbody's storage is
    // assured at play_begin, prefab_id_component on first instantiation - all long
    // after anything a scene constructor could register.
    {
        entt::registry r;
        g_destroy_order.clear();
        r.on_destroy<order_marker>().connect<&record_marker>(); // "registered at scene setup"
        r.on_destroy<order_a>().connect<&record_a>();           // pool created later, at play begin
        r.on_destroy<order_b>().connect<&record_b>();

        auto e = r.create();
        r.emplace<order_marker>(e);
        r.emplace<order_a>(e);
        r.emplace<order_b>(e);
        r.destroy(e);

        check(g_destroy_order == std::vector<int>({1, 0, 2}),
              "a marker registered before a later pool is destroyed AFTER that pool");
        check(g_destroy_order.back() == 2, "the marker ends up last, not first - exactly backwards");
    }

    // And the position cannot be repaired: re-assuring a pool does not move it.
    {
        entt::registry r;
        g_destroy_order.clear();
        r.on_destroy<order_marker>().connect<&record_marker>();
        r.on_destroy<order_a>().connect<&record_a>();

        (void)r.storage<order_marker>(); // try to make the marker "newest" again

        auto e = r.create();
        r.emplace<order_marker>(e);
        r.emplace<order_a>(e);
        r.destroy(e);

        check(g_destroy_order == std::vector<int>({0, 2}),
              "re-assuring a pool does not move it to the front of the destroy order");
    }
}

void test_entt_entity_signal_fires_last()
{
    std::printf("test_entt_entity_signal_fires_last\n");

    entt::registry r;
    g_destroy_order.clear();
    r.on_destroy<order_a>().connect<&record_a>();
    r.on_destroy<order_b>().connect<&record_b>();
    r.on_destroy<entt::entity>().connect<&record_entity>();

    auto e = r.create();
    r.emplace<order_a>(e);
    r.emplace<order_b>(e);
    r.destroy(e);

    // on_destroy<entt::entity> is documented to fire after every component has been
    // removed, which makes it useless as a pre-destroy hook: by then the entity has
    // nothing left to hand to gameplay.
    check(!g_destroy_order.empty() && g_destroy_order.back() == 3,
          "on_destroy<entt::entity> fires after every component hook, not before");
    check_eq(g_destroy_order.size(), 3, "every hook ran exactly once");
}

void test_entt_emplace_from_destroy_hook_leaks()
{
    std::printf("test_entt_emplace_from_destroy_hook_leaks\n");

    entt::registry r;

    // entt documents adding components to an entity being destroyed as UB. It is not
    // merely theoretical: if the target pool was already passed in the reverse walk,
    // the component is stranded on a released id and the next entity to recycle that
    // id is born carrying it.
    r.on_destroy<order_a>()
        .connect<+[](entt::registry& reg, entt::entity ent)
                 {
                     reg.emplace<order_marker>(ent);
                 }>();

    auto e = r.create();
    r.emplace<order_marker>(e); // assure the marker pool BEFORE order_a's
    r.emplace<order_a>(e);
    r.destroy(e);

    check(r.storage<order_marker>().contains(e),
          "a component emplaced from a destroy hook is stranded on a released id - "
          "this is why pre-destroy work cannot tag the entity from inside a hook");

    r.storage<order_marker>().clear();
}

/**
 * @brief Position of a component's pool in the registry's assurance order.
 * @return npos-ish (-1) when the pool does not exist yet.
 */
template<typename T>
auto pool_position(entt::registry& r) -> int
{
    const auto target = entt::type_hash<T>::value();
    int index = 0;
    for(auto&& [id, storage] : r.storage())
    {
        if(id == target)
        {
            return index;
        }
        ++index;
    }
    return -1;
}

void test_script_component_is_torn_down_last()
{
    std::printf("test_script_component_is_torn_down_last\n");

    scene scn("test");
    auto& registry = *scn.registry;

    // Destroy walks pools in reverse assurance order, so the pool assured FIRST is torn
    // down LAST. scene's constructor calls unload() before it connects any hook, and
    // unload's explicit clears assure physics, character controller and script ahead of
    // everything else - so script_component outlives transform, model and animation.
    //
    // The consequence is worth knowing and is not specific to physics: by the time a
    // script's OnDestroy runs, the entity has already lost its transform. It cannot
    // read its own final position.
    //
    // Fixing that would mean giving scripts a notification ahead of registry::destroy,
    // the same way physics gets one - which is why scene::destroy_entity detaching
    // components early is the shape to build on, not a physics-only workaround.
    //
    // Checked by pool position rather than by destroying a real script_component, which
    // would need a live .NET runtime.
    const int physics_pos = pool_position<physics_component>(registry);
    const int script_pos = pool_position<script_component>(registry);

    const int transform_pos = pool_position<transform_component>(registry);

    check(physics_pos >= 0, "the physics_component pool exists after scene construction");
    check(script_pos >= 0, "the script_component pool exists after scene construction");
    check(transform_pos >= 0, "the transform_component pool exists after scene construction");

    check(script_pos < transform_pos,
          "script_component's pool is assured before transform_component's, so it is torn down "
          "AFTER it - a script's OnDestroy cannot read its own transform");
}

// ---------------------------------------------------------------------------------
// scene::destroy_entity
// ---------------------------------------------------------------------------------

/// Records what a pre-destroy detach hook can see. physics_component's real hook runs
/// at exactly this point.
/**
 * @brief A registry wired with just enough of a scene to exercise destroy_entity.
 *
 * Not a real scene: several of the hooks scene's constructor connects reach into
 * engine::context(), which only exists inside a booted engine. The one that matters
 * here is transform_component's teardown, which cascades the destroy to children.
 */
struct destroy_fixture
{
    destroy_fixture() : registry(std::make_unique<entt::registry>())
    {
        registry->on_construct<transform_component>().connect<&transform_component::on_create_component>();
        registry->on_destroy<transform_component>().connect<&transform_component::on_destroy_component>();
    }

    std::unique_ptr<entt::registry> registry;
};

std::vector<entt::entity> g_announced;
std::vector<bool> g_whole_at_announce;
std::vector<bool> g_suppressed_at_announce;
std::vector<entt::entity> g_destroyed;
entt::handle g_reentrant_target;
entt::handle g_suicidal;

void record_announce(entt::registry& r, entt::entity e)
{
    g_announced.push_back(e);
    // Everything still present is the whole point of announcing ahead of
    // registry::destroy rather than from inside it.
    g_whole_at_announce.push_back(r.all_of<transform_component>(e) && r.all_of<tag_component>(e) &&
                                  r.all_of<id_component>(e));
    g_suppressed_at_announce.push_back(scene::is_destroy_suppressed());
}

void record_destroyed(entt::registry&, entt::entity e)
{
    g_destroyed.push_back(e);
}

void announce_and_destroy_bystander(entt::registry& r, entt::entity e)
{
    record_announce(r, e);

    // A slot destroying another entity is legal and must not corrupt the walk already
    // in flight.
    if(g_reentrant_target && g_reentrant_target.entity() != e)
    {
        auto target = g_reentrant_target;
        g_reentrant_target = {};
        scene::destroy_entity(target);
    }
}

void announce_and_destroy_self(entt::registry& r, entt::entity e)
{
    record_announce(r, e);

    // What a C# Destroy(entity) from inside the callback maps to. Announcing happens
    // before any component removal starts, so this cannot re-enter a removal that is
    // still in flight - the failure mode the detach-based variant had.
    if(g_suicidal && g_suicidal.entity() == e)
    {
        auto self = g_suicidal;
        g_suicidal = {};
        scene::destroy_entity(self);
    }
}

void reset_records()
{
    g_announced.clear();
    g_whole_at_announce.clear();
    g_suppressed_at_announce.clear();
    g_destroyed.clear();
}

auto all_true(const std::vector<bool>& v) -> bool
{
    return std::all_of(v.begin(),
                       v.end(),
                       [](bool b)
                       {
                           return b;
                       });
}

auto count_of(const std::vector<entt::entity>& v, entt::entity e) -> size_t
{
    return static_cast<size_t>(std::count(v.begin(), v.end(), e));
}

void test_destroy_announces_before_teardown()
{
    std::printf("test_destroy_announces_before_teardown\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&record_announce>();

    auto e = scene::create_entity(registry, "actor");
    const auto id = e.entity();

    reset_records();
    scene::destroy_entity(e);

    check_eq(g_announced.size(), 1, "the entity is announced exactly once");
    check(all_true(g_whole_at_announce),
          "the entity is whole when announced - what the backend needs in order to report contact "
          "exits with both sides valid");
    check(!g_suppressed_at_announce.empty() && !g_suppressed_at_announce.front(),
          "the announcement is not suppressed, so subscribers know to report");
    check(!registry.valid(id), "the entity is destroyed afterwards");

    on_pre_destroy(registry).disconnect<&record_announce>();
}

void test_destroy_announces_whole_subtree_first()
{
    std::printf("test_destroy_announces_whole_subtree_first\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&record_announce>();
    registry.on_destroy<transform_component>().connect<&record_destroyed>();

    auto root = scene::create_entity(registry, "root");
    auto child_a = scene::create_entity(registry, "child_a", root);
    auto child_b = scene::create_entity(registry, "child_b", root);
    auto grandchild = scene::create_entity(registry, "grandchild", child_a);

    const auto root_id = root.entity();
    const auto child_a_id = child_a.entity();
    const auto child_b_id = child_b.entity();
    const auto grandchild_id = grandchild.entity();

    reset_records();
    scene::destroy_entity(root);

    check_eq(g_announced.size(), 4, "every entity in the subtree is announced exactly once");
    check(all_true(g_whole_at_announce), "every entity is whole when announced");

    const auto index_of = [](entt::entity id) -> size_t
    {
        const auto it = std::find(g_announced.begin(), g_announced.end(), id);
        return static_cast<size_t>(std::distance(g_announced.begin(), it));
    };

    check(index_of(grandchild_id) < index_of(child_a_id), "a grandchild is announced before its parent");
    check(index_of(child_a_id) < index_of(root_id), "a child is announced before the root");
    check(index_of(child_b_id) < index_of(root_id), "every child is announced before the root");

    // THE guarantee the detach-based variant could not give: nothing is torn down until
    // every slot has run, so no callback ever sees a half-dismantled relative.
    check_eq(g_announced.size(), 4, "all four announcements happened");
    check_eq(g_destroyed.size(), 4, "all four entities were destroyed");
    check(!registry.valid(root_id), "the root is destroyed");
    check(!registry.valid(child_a_id), "children are destroyed with the root");
    check(!registry.valid(grandchild_id), "grandchildren are destroyed with the root");

    registry.on_destroy<transform_component>().disconnect<&record_destroyed>();
    on_pre_destroy(registry).disconnect<&record_announce>();
}

void test_destroy_suppression()
{
    std::printf("test_destroy_suppression\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&record_announce>();

    auto root = scene::create_entity(registry, "root");
    scene::create_entity(registry, "child", root);
    const auto root_id = root.entity();

    check(!scene::is_destroy_suppressed(), "suppression is off by default");

    reset_records();
    {
        scene::scoped_destroy_suppression guard;
        check(scene::is_destroy_suppressed(), "the guard suppresses");
        {
            scene::scoped_destroy_suppression nested;
            check(scene::is_destroy_suppressed(), "nesting keeps suppression on");
        }
        check(scene::is_destroy_suppressed(), "leaving a nested guard keeps the outer one");

        scene::destroy_entity(root);
    }

    check(!scene::is_destroy_suppressed(), "suppression is lifted with the guard");
    check_eq(g_announced.size(), 0, "a suppressed destroy announces nothing");
    check(!registry.valid(root_id), "a suppressed destroy still destroys");

    on_pre_destroy(registry).disconnect<&record_announce>();
}

void test_destroy_suppressed_during_teardown()
{
    std::printf("test_destroy_suppressed_during_teardown\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    registry.on_destroy<transform_component>().connect<&record_announce>();

    auto root = scene::create_entity(registry, "root");
    scene::create_entity(registry, "child", root);

    reset_records();
    scene::destroy_entity(root);

    // Destroy hooks run inside registry::destroy. Suppression is how a subsystem tells
    // "this entity is being torn down, its announcement already happened" from "this
    // component is being taken off an entity that lives on".
    check_eq(g_suppressed_at_announce.size(), 2, "both destroy hooks ran");
    check(all_true(g_suppressed_at_announce),
          "destroy hooks observe suppression, so they never start new announcement work");

    registry.on_destroy<transform_component>().disconnect<&record_announce>();
}

void test_destroy_reentrant()
{
    std::printf("test_destroy_reentrant\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&announce_and_destroy_bystander>();

    auto root = scene::create_entity(registry, "root");
    auto child = scene::create_entity(registry, "child", root);
    auto bystander = scene::create_entity(registry, "bystander");

    const auto root_id = root.entity();
    const auto child_id = child.entity();
    const auto bystander_id = bystander.entity();

    reset_records();
    g_reentrant_target = bystander;
    scene::destroy_entity(root);
    g_reentrant_target = {};

    check(!registry.valid(root_id), "the root is destroyed");
    check(!registry.valid(child_id), "the child is destroyed");
    check(!registry.valid(bystander_id), "the entity destroyed from inside a slot is destroyed too");

    check_eq(count_of(g_announced, root_id), 1, "the root is announced exactly once");
    check_eq(count_of(g_announced, child_id), 1, "the child is announced exactly once");
    check_eq(count_of(g_announced, bystander_id), 1, "the re-entrant target is announced exactly once");

    on_pre_destroy(registry).disconnect<&announce_and_destroy_bystander>();
}

void test_destroy_self_from_slot()
{
    std::printf("test_destroy_self_from_slot\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&announce_and_destroy_self>();

    auto root = scene::create_entity(registry, "root");
    auto child = scene::create_entity(registry, "child", root);
    const auto root_id = root.entity();
    const auto child_id = child.entity();

    reset_records();
    g_suicidal = root;
    scene::destroy_entity(root);
    g_suicidal = {};

    // A slot destroying the entity currently being announced re-enters destroy_entity.
    // Announcing runs before any component removal, so this is a second pass rather
    // than a corrupted one - it costs a duplicate announcement, not a crash, which is
    // exactly why subscribers are required to be idempotent.
    check(!registry.valid(root_id), "the root is destroyed exactly once, with no double destroy");
    check(!registry.valid(child_id), "the child goes with it");
    check(count_of(g_announced, root_id) >= 1, "the root was announced");

    on_pre_destroy(registry).disconnect<&announce_and_destroy_self>();
}

entt::handle g_reparent_child;
entt::handle g_reparent_to;

void announce_and_reparent(entt::registry& r, entt::entity e)
{
    record_announce(r, e);

    // "Drop what I am carrying before I die." A slot may move a child out of the
    // subtree, and that child must then survive the teardown.
    if(g_reparent_child && g_reparent_to && g_reparent_child.entity() != e)
    {
        auto child = g_reparent_child;
        auto target = g_reparent_to;
        g_reparent_child = {};
        child.get<transform_component>().set_parent(target, false);
    }
}

void test_destroy_respects_reparent_from_slot()
{
    std::printf("test_destroy_respects_reparent_from_slot\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&announce_and_reparent>();

    auto keeper = scene::create_entity(registry, "keeper");
    auto doomed = scene::create_entity(registry, "doomed");
    auto carried = scene::create_entity(registry, "carried", doomed);
    auto bystander = scene::create_entity(registry, "bystander", doomed);

    const auto doomed_id = doomed.entity();
    const auto carried_id = carried.entity();
    const auto bystander_id = bystander.entity();

    reset_records();
    g_reparent_child = carried;
    g_reparent_to = keeper;
    scene::destroy_entity(doomed);
    g_reparent_child = {};
    g_reparent_to = {};

    // This is why the cascade lives in transform_component rather than being driven
    // from the announcement snapshot: the snapshot still contains the reparented child,
    // but the live children_ list no longer does.
    check(!registry.valid(doomed_id), "the entity is destroyed");
    check(!registry.valid(bystander_id), "a child left in place goes with it");
    check(registry.valid(carried_id), "a child reparented out by a slot survives");
    check(carried.get<transform_component>().get_parent() == keeper,
          "the reparented child keeps its new parent");
}

entt::handle g_adopt_under;
entt::entity g_adopted = entt::null;

void announce_and_adopt(entt::registry& r, entt::entity e)
{
    record_announce(r, e);

    // "Spawn my death effect as a child of me." The entity is created AFTER the
    // subtree snapshot was taken, so nothing in that snapshot knows it exists.
    if(g_adopt_under && g_adopt_under.entity() == e && g_adopted == entt::null)
    {
        auto effect = scene::create_entity(r, "death_effect", g_adopt_under);
        g_adopted = effect.entity();
    }
}

void test_destroy_takes_children_added_during_announce()
{
    std::printf("test_destroy_takes_children_added_during_announce\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;
    on_pre_destroy(registry).connect<&announce_and_adopt>();

    auto doomed = scene::create_entity(registry, "doomed");
    const auto doomed_id = doomed.entity();

    reset_records();
    g_adopt_under = doomed;
    g_adopted = entt::null;
    scene::destroy_entity(doomed);
    g_adopt_under = {};

    // The cascade reads the live children_ list at teardown, so it sees an entity the
    // snapshot could not possibly contain. Driving destruction from the snapshot
    // instead would leave this one alive holding a dangling parent handle.
    check(g_adopted != entt::null, "the slot did parent a new entity under the dying one");
    check(!registry.valid(doomed_id), "the announced entity is destroyed");
    check(!registry.valid(g_adopted), "a child created during the announcement is destroyed too");

    on_pre_destroy(registry).disconnect<&announce_and_adopt>();
}

void test_destroy_cascade_consumes_all_children()
{
    std::printf("test_destroy_cascade_consumes_all_children\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;

    // The cascade consumes children_ rather than iterating a copy of it, so it has to
    // both terminate and leave nothing behind.
    auto root = scene::create_entity(registry, "root");
    for(int i = 0; i < 4; ++i)
    {
        scene::create_entity(registry, "child", root);
    }

    // A child destroyed out of band unlinks itself, which is the invariant the loop
    // relies on to make progress.
    auto victim = root.get<transform_component>().get_children().front();
    registry.destroy(victim.entity());
    check_eq(root.get<transform_component>().get_children().size(),
             3,
             "a child destroyed out of band removes itself from its parent's list");

    const auto root_id = root.entity();
    scene::destroy_entity(root);

    size_t alive = 0;
    for(auto e : registry.view<transform_component>())
    {
        (void)e;
        ++alive;
    }

    check(!registry.valid(root_id), "the cascade terminates and destroys the root");
    check_eq(alive, 0, "every remaining child is consumed");
}

void test_raw_destroy_still_takes_the_subtree()
{
    std::printf("test_raw_destroy_still_takes_the_subtree\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;

    auto root = scene::create_entity(registry, "root");
    auto child = scene::create_entity(registry, "child", root);
    auto grandchild = scene::create_entity(registry, "grandchild", child);

    const auto child_id = child.entity();
    const auto grandchild_id = grandchild.entity();

    // Bypass the funnel entirely. Editor scratch stubs and prefab load cleanup do this,
    // and handle.destroy() is an easy thing to reach for anywhere else.
    root.destroy();

    // This is the cascade's remaining job. Drive destruction from a list built in
    // scene::destroy_entity instead and these two are orphaned - still alive, holding a
    // parent handle that no longer resolves.
    check(!registry.valid(child_id), "a raw destroy still takes its children with it");
    check(!registry.valid(grandchild_id), "and their children too");
}

void test_destroy_without_subscribers()
{
    std::printf("test_destroy_without_subscribers\n");

    destroy_fixture fx;
    auto& registry = *fx.registry;

    // The edit-mode path: no physics world, nobody subscribed, so destroy must not even
    // walk the subtree.
    auto root = scene::create_entity(registry, "root");
    auto child = scene::create_entity(registry, "child", root);
    const auto root_id = root.entity();
    const auto child_id = child.entity();

    scene::destroy_entity(root);

    check(!registry.valid(root_id), "destroy works with no subscribers");
    check(!registry.valid(child_id), "children still go with the root");

    scene::destroy_entity(root);
    check(true, "destroying an already-destroyed handle is a no-op");

    scene::destroy_entity({});
    check(true, "destroying a null handle is a no-op");
}

} // namespace

int main()
{
    // Unbuffered stdout: a mid-suite crash must not swallow the progress output that
    // tells us which test it happened in.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // APPLOG_* expands to spdlog::get("Log")->log(...) - register a real sink so engine
    // code that logs does not null-dereference mid-suite.
    if(!spdlog::get(APPLOG))
    {
        spdlog::create<spdlog::sinks::stdout_sink_mt>(APPLOG);
    }

    test_graph_insert_and_erase();
    test_graph_list_integrity();
    test_graph_body_on_both_sides();
    test_graph_flush_counters();
    test_graph_erase_during_visit();
    test_graph_generation_guard();
    test_graph_live_list();
    test_graph_step_stamp();
    test_graph_clear_and_unresolvable();

    test_entt_destroys_in_reverse_pool_order();
    test_entt_entity_signal_fires_last();
    test_entt_emplace_from_destroy_hook_leaks();

    test_contact_event_flag_defaults();
    test_contact_event_flag_roundtrip();
    test_contact_end_reason_mirror();

    test_script_component_is_torn_down_last();
    test_destroy_announces_before_teardown();
    test_destroy_announces_whole_subtree_first();
    test_destroy_suppression();
    test_destroy_suppressed_during_teardown();
    test_destroy_reentrant();
    test_destroy_self_from_slot();
    test_destroy_respects_reparent_from_slot();
    test_destroy_takes_children_added_during_announce();
    test_destroy_cascade_consumes_all_children();
    test_raw_destroy_still_takes_the_subtree();
    test_destroy_without_subscribers();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
