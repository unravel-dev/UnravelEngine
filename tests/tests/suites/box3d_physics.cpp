/*
 * Smoke suite for the Box3D physics backend.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite box3d
 *
 * The runner is headless but owns the real ecs scene, so the backend is driven exactly the
 * way physics_system drives it: on_play_begin, fixed steps of sync / simulate / sync /
 * dispatch, on_play_end. Every test builds a small scene, plays it and tears it down, and
 * only looks at what reaches the ECS: transforms, cached velocities, query results and the
 * character controller state.
 */

#include "../tests.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/scene.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/backend/physics_backend.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/physics_types.h>

#include <base/basetypes.hpp>
#include <logging/logging.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace unravel;

namespace
{
constexpr float step_seconds = 1.0f / 60.0f;
/// Four seconds: enough for anything dropped from three meters to land and go quiet.
constexpr int settle_steps = 240;
constexpr float settle_tolerance = 0.05f;
constexpr int everything = static_cast<int>(layer_reserved::everything_layer);

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

void check_near(float actual, float expected, float tolerance, const std::string& what)
{
    ++g_checks;
    if(std::abs(actual - expected) > tolerance)
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %f, expected %f +/- %f)\n", what.c_str(), actual, expected, tolerance);
    }
}

/**
 * @brief A played scene on the shared game registry.
 *
 * The runner never initialises physics_system, so its component hooks find no backend;
 * the fixture forwards creation to the backend itself when a body is added mid-play.
 */
struct sim_fixture
{
    explicit sim_fixture(rtti::context& c) : ctx(c), registry(*c.get_cached<ecs>().get_scene().registry)
    {
        backend = create_physics_backend(physics_backend_type::box3d);
        backend->init();
    }

    ~sim_fixture()
    {
        end();
        for(auto& e : entities)
        {
            if(e)
            {
                scene::destroy_entity(e);
            }
        }
        backend->deinit();
    }

    void begin()
    {
        backend->on_play_begin(ctx);
        playing = true;
    }

    void end()
    {
        if(playing)
        {
            backend->on_play_end(ctx);
            playing = false;
        }
    }

    void step(int count = 1)
    {
        const delta_t dt(step_seconds);
        for(int i = 0; i < count; ++i)
        {
            backend->sync_to_physics(ctx, dt);
            backend->simulate(dt);
            backend->sync_from_physics(ctx);
            backend->dispatch_contacts(ctx);
        }
    }

    auto spawn(const std::string& name, const math::vec3& position) -> entt::handle
    {
        auto e = scene::create_entity(registry, name);
        e.get<transform_component>().set_position_global(position);
        (void)e.get_or_emplace<active_component>();
        entities.push_back(e);
        return e;
    }

    auto add_body(entt::handle e, rigidbody_type type, const physics_compound_shape::shape_t& shape, float mass = 1.0f)
        -> physics_component&
    {
        auto& comp = e.emplace<physics_component>();
        comp.set_body_type(type);
        comp.set_mass(mass);
        comp.set_is_using_gravity(true);
        physics_compound_shape compound;
        compound.shape = shape;
        comp.set_shapes({compound});
        if(playing)
        {
            backend->on_create_component(registry, e.entity());
        }
        return comp;
    }

    auto spawn_box(const std::string& name,
                   const math::vec3& position,
                   const math::vec3& extends,
                   rigidbody_type type,
                   float mass = 1.0f) -> entt::handle
    {
        auto e = spawn(name, position);
        physics_box_shape box;
        box.extends = extends;
        add_body(e, type, box, mass);
        return e;
    }

    auto spawn_ground() -> entt::handle
    {
        return spawn_box("ground", {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f}, rigidbody_type::static_body);
    }

    auto spawn_character(const std::string& name, const math::vec3& position) -> entt::handle
    {
        auto e = spawn(name, position);
        auto& cc = e.emplace<character_controller_component>();
        cc.set_radius(0.5f);
        cc.set_height(2.0f);
        if(playing)
        {
            backend->on_create_cc_component(registry, e.entity());
        }
        return e;
    }

    void forget(entt::handle e)
    {
        for(auto& entry : entities)
        {
            if(entry == e)
            {
                entry = {};
            }
        }
    }

    auto position_of(entt::handle e) -> math::vec3
    {
        return e.get<transform_component>().get_position_global();
    }

    rtti::context& ctx;
    entt::registry& registry;
    std::unique_ptr<physics_backend> backend;
    std::vector<entt::handle> entities;
    bool playing{false};
};

void test_dynamic_bodies_settle(rtti::context& ctx)
{
    std::printf("test_dynamic_bodies_settle\n");
    sim_fixture fx(ctx);
    auto ground = fx.spawn_ground();
    auto box = fx.spawn_box("box", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);

    auto sphere = fx.spawn("sphere", {3.0f, 3.0f, 0.0f});
    physics_sphere_shape sphere_shape;
    sphere_shape.radius = 0.5f;
    fx.add_body(sphere, rigidbody_type::dynamic, sphere_shape);

    auto capsule = fx.spawn("capsule", {6.0f, 3.0f, 0.0f});
    physics_capsule_shape capsule_shape;
    capsule_shape.radius = 0.25f;
    capsule_shape.length = 0.5f;
    fx.add_body(capsule, rigidbody_type::dynamic, capsule_shape);

    auto cylinder = fx.spawn("cylinder", {9.0f, 3.0f, 0.0f});
    physics_cylinder_shape cylinder_shape;
    cylinder_shape.radius = 0.5f;
    cylinder_shape.length = 1.0f;
    fx.add_body(cylinder, rigidbody_type::dynamic, cylinder_shape);

    fx.begin();
    fx.step(settle_steps);

    check_near(fx.position_of(box).y, 0.5f, settle_tolerance, "a box comes to rest on the ground");
    check_near(fx.position_of(sphere).y, 0.5f, settle_tolerance, "a sphere comes to rest on the ground");
    check_near(fx.position_of(capsule).y, 0.5f, settle_tolerance, "a capsule comes to rest on the ground");
    check_near(fx.position_of(cylinder).y, 0.5f, settle_tolerance, "a cylinder comes to rest on the ground");
    check_near(fx.position_of(ground).y, -0.5f, 1e-4f, "the static ground does not move");

    const auto& box_comp = box.get<physics_component>();
    check(math::length(box_comp.get_velocity()) < 0.05f, "the cached velocity of a resting box is quiet");
}

void test_gravity_and_constraints(rtti::context& ctx)
{
    std::printf("test_gravity_and_constraints\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();

    auto floating = fx.spawn_box("floating", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    floating.get<physics_component>().set_is_using_gravity(false);

    auto frozen = fx.spawn_box("frozen", {3.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    frozen.get<physics_component>().set_freeze_position(math::bvec3{false, true, false});

    fx.begin();
    fx.step(120);

    check_near(fx.position_of(floating).y, 3.0f, 0.01f, "a body without gravity stays put");
    check_near(fx.position_of(frozen).y, 3.0f, 0.01f, "a frozen y axis holds against gravity");

    // Toggling gravity at runtime goes through the dirty flags like any inspector edit.
    floating.get<physics_component>().set_is_using_gravity(true);
    fx.step(settle_steps);
    check_near(fx.position_of(floating).y, 0.5f, settle_tolerance, "enabling gravity mid-play drops the body");
}

void test_scale_is_baked_into_shapes(rtti::context& ctx)
{
    std::printf("test_scale_is_baked_into_shapes\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto big = fx.spawn_box("big", {0.0f, 4.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    big.get<transform_component>().set_scale_global(math::vec3{2.0f, 2.0f, 2.0f});

    fx.begin();
    fx.step(settle_steps);
    check_near(fx.position_of(big).y, 1.0f, settle_tolerance, "entity scale is applied to the collision shape");

    // Rescaling mid-play rebuilds the shapes.
    big.get<transform_component>().set_scale_global(math::vec3{1.0f, 1.0f, 1.0f});
    fx.step(settle_steps);
    check_near(fx.position_of(big).y, 0.5f, settle_tolerance, "a scale change mid-play rebuilds the shape");
}

void test_sensors_have_no_response(rtti::context& ctx)
{
    std::printf("test_sensors_have_no_response\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto sensor = fx.spawn_box("sensor", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    sensor.get<physics_component>().set_is_sensor(true);

    fx.begin();
    fx.step(settle_steps);
    check(fx.position_of(sensor).y < -2.0f, "a sensor body falls straight through the ground");
}

void test_queries(rtti::context& ctx)
{
    std::printf("test_queries\n");
    sim_fixture fx(ctx);
    auto ground = fx.spawn_ground();
    auto box = fx.spawn_box("box", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto trigger = fx.spawn_box("trigger", {5.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::static_body);
    trigger.get<physics_component>().set_is_sensor(true);

    fx.begin();
    fx.step(settle_steps);

    const math::vec3 down{0.0f, -1.0f, 0.0f};

    auto hit = fx.backend->ray_cast({0.0f, 5.0f, 0.0f}, down, 10.0f, everything, false);
    check(static_cast<bool>(hit), "a ray straight down hits something");
    if(hit)
    {
        check(hit->entity == box.entity(), "the closest hit is the box, not the ground under it");
        check_near(hit->point.y, 1.0f, 0.02f, "the hit point is on the top face of the box");
        check_near(hit->distance, 4.0f, 0.02f, "the hit distance is measured from the ray origin");
        check(hit->normal.y > 0.99f, "the hit normal points up");
    }

    auto all = fx.backend->ray_cast_all({0.0f, 5.0f, 0.0f}, down, 10.0f, everything, false);
    check(all.size() == 2, "ray_cast_all sees the box and the ground");
    if(all.size() == 2)
    {
        check(all[0].entity == box.entity() && all[1].entity == ground.entity(), "ray_cast_all is sorted near to far");
    }

    auto miss = fx.backend->ray_cast({0.0f, 5.0f, 0.0f}, down, 10.0f, layer_reserved::static_static, false);
    check(!miss, "a ray restricted to an unused layer hits nothing");

    auto solid_only = fx.backend->ray_cast({5.0f, 5.0f, 0.0f}, down, 10.0f, everything, false);
    check(solid_only && solid_only->entity == ground.entity(), "rays skip sensors unless asked");

    auto with_sensors = fx.backend->ray_cast({5.0f, 5.0f, 0.0f}, down, 10.0f, everything, true);
    check(with_sensors && with_sensors->entity == trigger.entity(), "rays hit sensors when asked");

    auto sweep = fx.backend->sphere_cast({0.0f, 5.0f, 0.0f}, down, 0.25f, 10.0f, everything, false);
    check(static_cast<bool>(sweep), "a sphere cast straight down hits something");
    if(sweep)
    {
        check(sweep->entity == box.entity(), "the sphere cast hits the box first");
        check_near(sweep->point.y, 1.25f, 0.03f, "the sphere cast reports the sphere center at impact");
        check_near(sweep->distance, 3.75f, 0.03f, "the sphere cast distance is along the cast");
    }

    auto sweeps = fx.backend->sphere_cast_all({0.0f, 5.0f, 0.0f}, down, 0.25f, 10.0f, everything, false);
    check(sweeps.size() >= 1 && sweeps[0].entity == box.entity(), "sphere_cast_all reports the box first");

    auto overlap = fx.backend->sphere_overlap({0.0f, 0.5f, 0.0f}, 0.2f, everything, false);
    check(overlap.size() == 1 && overlap[0] == box.entity(), "a sphere inside the box overlaps only the box");

    auto overlap_both = fx.backend->sphere_overlap({0.0f, 0.1f, 0.0f}, 0.2f, everything, false);
    check(overlap_both.size() == 2, "a sphere straddling the box and the ground overlaps both");

    auto overlap_sensor = fx.backend->sphere_overlap({5.0f, 0.5f, 0.0f}, 0.2f, everything, false);
    check(overlap_sensor.empty(), "overlaps skip sensors unless asked");

    auto overlap_with_sensor = fx.backend->sphere_overlap({5.0f, 0.5f, 0.0f}, 0.2f, everything, true);
    check(overlap_with_sensor.size() == 1 && overlap_with_sensor[0] == trigger.entity(),
          "overlaps report sensors when asked");
}

void test_forces(rtti::context& ctx)
{
    std::printf("test_forces\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto box = fx.spawn_box("box", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto spinner = fx.spawn_box("spinner", {3.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto blasted = fx.spawn_box("blasted", {6.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);

    fx.begin();
    fx.step(settle_steps);

    auto& box_comp = box.get<physics_component>();
    fx.backend->apply_force(box_comp, {0.0f, 5.0f, 0.0f}, force_mode::impulse);
    check(box_comp.get_velocity().y > 4.0f, "an impulse immediately shows in the cached velocity");
    fx.step(10);
    check(fx.position_of(box).y > 0.8f, "an upward impulse lifts the box");

    auto& spinner_comp = spinner.get<physics_component>();
    fx.backend->apply_torque(spinner_comp, {0.0f, 0.0f, 2.0f}, force_mode::impulse);
    check(std::abs(spinner_comp.get_angular_velocity().z) > 0.1f,
          "an angular impulse immediately shows in the cached angular velocity");

    auto& blasted_comp = blasted.get<physics_component>();
    fx.backend->apply_explosion_force(blasted_comp, 8.0f, {6.0f, -1.0f, 0.0f}, 5.0f, 0.0f, force_mode::impulse);
    fx.step(10);
    check(fx.position_of(blasted).y > 0.8f, "an explosion below the box throws it upwards");

    // Sustained force: acceleration mode is mass independent.
    auto heavy = fx.spawn_box("heavy", {9.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic, 50.0f);
    fx.step(settle_steps);
    auto& heavy_comp = heavy.get<physics_component>();
    for(int i = 0; i < 30; ++i)
    {
        fx.backend->apply_force(heavy_comp, {0.0f, 20.0f, 0.0f}, force_mode::acceleration);
        fx.step(1);
    }
    check(fx.position_of(heavy).y > 0.8f, "acceleration mode overcomes gravity regardless of mass");
}

void test_kinematic_pushes_dynamic(rtti::context& ctx)
{
    std::printf("test_kinematic_pushes_dynamic\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto pusher = fx.spawn_box("pusher", {-3.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::kinematic);
    auto target = fx.spawn_box("target", {-1.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);

    fx.begin();
    fx.step(30);

    const float per_step = 0.03f;
    const int steps = 90;
    for(int i = 0; i < steps; ++i)
    {
        auto& transform = pusher.get<transform_component>();
        transform.set_position_global(transform.get_position_global() + math::vec3{per_step, 0.0f, 0.0f});
        fx.step(1);
    }

    check_near(fx.position_of(pusher).x, -3.0f + per_step * steps, 1e-3f, "the ECS drives the kinematic body");
    check(fx.position_of(target).x > 0.4f, "a kinematic body pushes the dynamic box ahead of it");
    check_near(fx.position_of(target).y, 0.5f, settle_tolerance, "the pushed box stays on the ground");

    // Once the ECS stops moving it, the kinematic body must not coast on. The ECS never
    // reads kinematic poses back, so the proof is in the world: a ray where the body
    // parked still finds it, and the box only slides out its own momentum instead of
    // being carried a full second further.
    const float parked_x = fx.position_of(pusher).x;
    const float target_x = fx.position_of(target).x;
    fx.step(60);
    auto parked = fx.backend->ray_cast({parked_x, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, everything, false);
    check(parked && parked->entity == pusher.entity(), "a parked kinematic body stays where the ECS left it");
    if(parked)
    {
        check_near(parked->point.y, 1.0f, 0.02f, "the parked kinematic body is still on the ground");
    }
    const float coasted = fx.position_of(target).x - target_x;
    check(coasted < 0.5f * per_step * 60.0f, "the box is no longer being pushed");
}

void test_bodies_come_and_go_during_play(rtti::context& ctx)
{
    std::printf("test_bodies_come_and_go_during_play\n");
    sim_fixture fx(ctx);
    auto ground = fx.spawn_ground();
    auto box = fx.spawn_box("box", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);

    fx.begin();
    fx.step(60);

    fx.forget(box);
    scene::destroy_entity(box);
    fx.step(10);

    const math::vec3 down{0.0f, -1.0f, 0.0f};
    auto hit = fx.backend->ray_cast({0.0f, 5.0f, 0.0f}, down, 10.0f, everything, false);
    check(hit && hit->entity == ground.entity(), "a destroyed body leaves the world");

    auto late = fx.spawn_box("late", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    fx.step(settle_steps);
    check_near(fx.position_of(late).y, 0.5f, settle_tolerance, "a body added mid-play simulates");

    // Deactivation pulls the body out; reactivation puts it back where the ECS says.
    late.get<transform_component>().set_position_global({0.0f, 3.0f, 0.0f});
    late.remove<active_component>();
    fx.step(60);
    check_near(fx.position_of(late).y, 3.0f, 1e-3f, "an inactive body does not simulate");
    late.emplace<active_component>();
    fx.step(settle_steps);
    check_near(fx.position_of(late).y, 0.5f, settle_tolerance, "a reactivated body simulates again");

    // Switching body type at runtime.
    late.get<physics_component>().set_body_type(rigidbody_type::static_body);
    fx.step(30);
    auto dropped = fx.spawn_box("dropped", {0.0f, 4.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    fx.step(settle_steps);
    check_near(fx.position_of(dropped).y, 1.5f, settle_tolerance, "a box turned static supports a dropped box");
}

void test_character_controller(rtti::context& ctx)
{
    std::printf("test_character_controller\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    fx.spawn_box("wall", {12.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 4.0f}, rigidbody_type::static_body);
    fx.spawn_box("step", {2.0f, 0.1f, 5.0f}, {1.0f, 0.2f, 2.0f}, rigidbody_type::static_body);
    auto hero = fx.spawn_character("hero", {8.0f, 3.0f, 0.0f});
    auto& cc = hero.get<character_controller_component>();

    fx.begin();
    fx.step(settle_steps);

    check(cc.is_grounded(), "the character lands and reports grounded");
    check_near(fx.position_of(hero).y, 1.0f, settle_tolerance, "the capsule rests on the ground");

    for(int i = 0; i < 60; ++i)
    {
        fx.backend->move_character(cc, {0.02f, 0.0f, 0.0f});
        fx.step(1);
    }
    check_near(fx.position_of(hero).x, 9.2f, 0.1f, "move() displaces the character");
    check_near(fx.position_of(hero).y, 1.0f, settle_tolerance, "the character stays on the ground while walking");
    check(cc.is_grounded(), "walking keeps the character grounded");

    for(int i = 0; i < 200; ++i)
    {
        fx.backend->move_character(cc, {0.05f, 0.0f, 0.0f});
        fx.step(1);
    }
    check_near(fx.position_of(hero).x, 11.0f, settle_tolerance, "a wall stops the character");
    check_near(fx.position_of(hero).y, 1.0f, settle_tolerance, "pushing into a wall does not lift the character");

    fx.backend->jump_character(cc, {0.0f, 5.0f, 0.0f});
    fx.step(5);
    check(!cc.is_grounded(), "the character leaves the ground on jump");
    check(fx.position_of(hero).y > 1.1f, "the jump gains height");
    fx.step(settle_steps);
    check(cc.is_grounded(), "the character lands after the jump");
    check_near(fx.position_of(hero).y, 1.0f, settle_tolerance, "the character is back on the ground");

    fx.backend->warp_character(cc, {0.0f, 1.0f, 5.0f});
    fx.step(1);
    check_near(fx.position_of(hero).x, 0.0f, 1e-3f, "warp moves the character instantly");
    check_near(fx.position_of(hero).z, 5.0f, 1e-3f, "warp moves the character instantly (z)");

    // A 0.2 m ledge is under the default 0.3 m step height: walk onto it, then off it.
    fx.step(30);
    for(int i = 0; i < 66; ++i)
    {
        fx.backend->move_character(cc, {0.03f, 0.0f, 0.0f});
        fx.step(1);
    }
    check_near(fx.position_of(hero).x, 2.0f, 0.1f, "the character walks onto the ledge");
    check_near(fx.position_of(hero).y, 1.2f, settle_tolerance, "the character steps up onto the ledge");
    check(cc.is_grounded(), "the character is grounded on the ledge");

    for(int i = 0; i < 40; ++i)
    {
        fx.backend->move_character(cc, {0.03f, 0.0f, 0.0f});
        fx.step(1);
    }
    check_near(fx.position_of(hero).y, 1.0f, settle_tolerance, "the character steps down off the ledge");
    check(cc.is_grounded(), "the character stays grounded stepping down");

    fx.backend->apply_impulse_character(cc, {0.0f, 4.0f, 0.0f});
    fx.step(5);
    check(fx.position_of(hero).y > 1.1f, "an upward impulse lifts the character");
    fx.step(settle_steps);
    check(cc.is_grounded(), "the character lands after the impulse");
}

} // namespace

auto run_box3d_suite(rtti::context& ctx) -> int
{
    test_dynamic_bodies_settle(ctx);
    test_gravity_and_constraints(ctx);
    test_scale_is_baked_into_shapes(ctx);
    test_sensors_have_no_response(ctx);
    test_queries(ctx);
    test_forces(ctx);
    test_kinematic_pushes_dynamic(ctx);
    test_bodies_come_and_go_during_play(ctx);
    test_character_controller(ctx);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("box3d backend smoke", run_box3d_suite)
