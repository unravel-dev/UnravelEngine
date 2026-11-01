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
#include <engine/events.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/backend/physics_backend.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/physics/physics_types.h>

#include <base/basetypes.hpp>
#include <logging/logging.h>

#include <algorithm>
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
    explicit sim_fixture(rtti::context& c, physics_backend_type type = physics_backend_type::box3d)
        : ctx(c)
        , registry(*c.get_cached<ecs>().get_scene().registry)
    {
        backend = create_physics_backend(type);
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

    // A character that turns in the same frame as it warps (a walking hero that teleports) dirties its transform;
    // the warp must survive the transform-to-physics sync of the next step.
    fx.backend->warp_character(cc, {3.0f, 1.0f, 5.0f});
    hero.get<transform_component>().set_rotation_global(math::angleAxis(math::radians(90.0f), math::vec3{0.0f, 1.0f, 0.0f}));
    fx.step(1);
    check_near(fx.position_of(hero).x, 3.0f, 1e-3f, "a warp survives a rotation set in the same frame");
    fx.backend->warp_character(cc, {0.0f, 1.0f, 5.0f});
    fx.step(1);

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

// --- characters shown between fixed steps -------------------------------------------------

constexpr float walk_speed = 3.6f;
constexpr int max_fixed_steps = 3;
/// A pair of a frame rate and a fixed step that beat against each other, or do not.
struct frame_timing
{
    float frames_per_second;
    float step_seconds;
};
constexpr frame_timing beating_timings[] = {{60.0f, 0.02f},
                                            {60.0f, 1.0f / 60.0f},
                                            {144.0f, 0.02f},
                                            {120.0f, 1.0f / 60.0f},
                                            {75.0f, 0.02f},
                                            {45.0f, 0.02f},
                                            {30.0f, 0.02f}};
/// Frame time wobbles this much, as a vsynced frame's does.
constexpr float frame_jitter = 0.03f;
/// Time a character gets to lock on to the steps before its travel is judged.
constexpr float lock_seconds = 0.5f;

auto get_jittered_frame_seconds(float frames_per_second, int frame) -> float
{
    return (1.0f + frame_jitter * std::sin(float(frame) * 1.7f)) / frames_per_second;
}

/**
 * @brief The frame loop physics_system runs: game time goes to the step runner, then a script moves the character.
 */
struct character_frames
{
    character_frames(sim_fixture& f, entt::handle h, float step)
        : fx(f)
        , hero(h)
        , cc(h.get<character_controller_component>())
        , transform(h.get<transform_component>())
        , step_seconds(step)
    {
    }

    void advance(float frame_seconds)
    {
        runner.advance(fx.ctx, *fx.backend, frame_seconds, step_seconds, max_fixed_steps);
    }

    void script_moves(const math::vec3& displacement)
    {
        cc.get_motion().queued_move += displacement;
    }

    void settle()
    {
        for(int i = 0; i < settle_steps; ++i)
        {
            advance(1.0f / 60.0f);
        }
    }

    sim_fixture& fx;
    entt::handle hero;
    character_controller_component& cc;
    transform_component& transform;
    float step_seconds;
    fixed_step_runner runner;
};

struct walk_result
{
    /// Travel of a frame over what its game time pays for, the extremes after the character locked on.
    float slowest{1.0f};
    float fastest{1.0f};
    float travel{};
    float expected_travel{};
};

auto walk_along_x(character_frames& frames, const frame_timing& timing, float seconds, bool turns_every_frame = false)
    -> walk_result
{
    walk_result result;
    const math::vec3 start = frames.transform.get_position_global();
    math::vec3 previous = start;
    float time = 0.0f;
    float frame_seconds = 1.0f / timing.frames_per_second;
    for(int frame = 0; time < seconds; ++frame)
    {
        frame_seconds = get_jittered_frame_seconds(timing.frames_per_second, frame);
        time += frame_seconds;
        frames.advance(frame_seconds);
        const math::vec3 position = frames.transform.get_position_global();
        if(time > lock_seconds)
        {
            const float ratio = (position.x - previous.x) / (walk_speed * frame_seconds);
            result.slowest = std::min(result.slowest, ratio);
            result.fastest = std::max(result.fastest, ratio);
        }
        previous = position;
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
        if(turns_every_frame)
        {
            frames.transform.set_rotation_global(
                math::angleAxis(math::radians(float(frame % 360)), math::vec3{0.0f, 1.0f, 0.0f}));
        }
        result.expected_travel += walk_speed * frame_seconds;
    }
    result.travel = previous.x - start.x;
    return result;
}

void test_character_travel_is_even(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_character_travel_is_even\n");
    for(const auto& timing : beating_timings)
    {
        sim_fixture fx(ctx, backend);
        fx.spawn_ground();
        auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
        fx.begin();
        character_frames frames(fx, hero, timing.step_seconds);
        frames.settle();

        const auto walk = walk_along_x(frames, timing, 3.0f);
        const std::string name = std::to_string(int(timing.frames_per_second)) + " fps, step " +
                                 std::to_string(int(timing.step_seconds * 1000.0f)) + " ms";
        check(walk.slowest > 0.85f, name + ": no frame falls behind (slowest " + std::to_string(walk.slowest) + ")");
        check(walk.fastest < 1.15f, name + ": no frame jumps ahead (fastest " + std::to_string(walk.fastest) + ")");
        check_near(walk.travel, walk.expected_travel, walk_speed * 0.1f, name + ": the walk covers its distance");
    }
}

/// What the test above guards against: the same walk with raw steps stalls and takes double steps.
void test_raw_steps_stall(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_raw_steps_stall\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    auto& cc = hero.get<character_controller_component>();
    cc.set_interpolate(false);
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    float slowest = 1.0f;
    float fastest = 1.0f;
    float previous = frames.transform.get_position_global().x;
    for(int frame = 0; frame < 180; ++frame)
    {
        const float frame_seconds = get_jittered_frame_seconds(60.0f, frame);
        frames.advance(frame_seconds);
        const float position = frames.transform.get_position_global().x;
        if(frame > 30)
        {
            const float ratio = (position - previous) / (walk_speed * frame_seconds);
            slowest = std::min(slowest, ratio);
            fastest = std::max(fastest, ratio);
        }
        previous = position;
        fx.backend->move_character(cc, {walk_speed * frame_seconds, 0.0f, 0.0f});
    }
    check(slowest < 0.2f, "raw steps leave frames without travel");
    check(fastest > 1.5f, "raw steps put two frames of travel in one");
}

/**
 * A script that turns a character every frame dirties its transform, and the backend then places the body where the
 * transform is. An interpolated position must not get that far: it would pull the body back on every step.
 */
void test_turning_does_not_drag_the_body_back(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_turning_does_not_drag_the_body_back\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    const frame_timing timing{60.0f, 0.02f};
    const auto walk = walk_along_x(frames, timing, 3.0f, true);
    check_near(walk.travel, walk.expected_travel, walk_speed * 0.1f, "a turning character covers the same distance");
    check(walk.slowest > 0.85f && walk.fastest < 1.15f, "a turning character travels evenly");
}

void test_transform_trails_the_physics_position(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_transform_trails_the_physics_position\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    const float step_seconds = 0.02f;
    character_frames frames(fx, hero, step_seconds);
    frames.settle();

    const float frame_seconds = 1.0f / 60.0f;
    float widest_gap = 0.0f;
    bool ever_ahead = false;
    for(int frame = 0; frame < 120; ++frame)
    {
        frames.advance(frame_seconds);
        const float gap = frames.cc.get_position().x - frames.transform.get_position_global().x;
        widest_gap = std::max(widest_gap, gap);
        ever_ahead = ever_ahead || gap < -1e-4f;
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
    }
    check(widest_gap > 0.0f, "the transform trails the physics position while walking");
    check(widest_gap <= walk_speed * step_seconds * 1.05f, "by at most one fixed step");
    check(!ever_ahead, "and never runs ahead of it");
    for(int frame = 0; frame < 30; ++frame)
    {
        frames.advance(frame_seconds);
    }
    check_near(frames.cc.get_position().x,
               frames.transform.get_position_global().x,
               1e-4f,
               "a character that stopped shows where it stands");
}

/// A fixed update reads where the character really is, and a move made there belongs to its step.
struct fixed_update_probe
{
    void on_fixed_update(rtti::context& /*ctx*/, delta_t /*step*/)
    {
        ++updates;
        const float gap = std::abs(frames->transform.get_position_global().x - frames->cc.get_motion().step_end.x);
        widest_gap = std::max(widest_gap, gap);
    }

    character_frames* frames{};
    int updates{};
    float widest_gap{};
};

void test_a_fixed_update_reads_the_physics_position(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_a_fixed_update_reads_the_physics_position\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    fixed_update_probe probe;
    probe.frames = &frames;
    auto sentinel = std::make_shared<int>(0);
    ctx.get_cached<events>().on_frame_fixed_update.connect(sentinel, &probe, &fixed_update_probe::on_fixed_update);
    const float frame_seconds = 1.0f / 60.0f;
    for(int frame = 0; frame < 60; ++frame)
    {
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
        frames.advance(frame_seconds);
    }
    sentinel.reset();
    check(probe.updates > 40, "the fixed update runs");
    check(probe.widest_gap < 1e-4f, "a fixed update reads the position the last step ended at");
}

void test_walls_stop_the_shown_position(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_walls_stop_the_shown_position\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    fx.spawn_box("wall", {4.0f, 2.0f, 0.0f}, {1.0f, 4.0f, 4.0f}, rigidbody_type::static_body);
    auto hero = fx.spawn_character("hero", {0.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    const float wall_contact_x = 3.0f;
    const float frame_seconds = 1.0f / 60.0f;
    float furthest = -100.0f;
    float last = 0.0f;
    float widest_late_change = 0.0f;
    for(int frame = 0; frame < 180; ++frame)
    {
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
        frames.advance(frame_seconds);
        const float x = frames.transform.get_position_global().x;
        furthest = std::max(furthest, x);
        if(frame > 120)
        {
            widest_late_change = std::max(widest_late_change, std::abs(x - last));
        }
        last = x;
    }
    check_near(furthest, wall_contact_x, settle_tolerance, "the shown position stops at the wall");
    check(widest_late_change < 0.01f, "and rests there without shaking");
}

void test_teleports_do_not_smear(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_teleports_do_not_smear\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    const float frame_seconds = 1.0f / 60.0f;
    const float frame_travel = walk_speed * frame_seconds;
    const auto walk_frames = [&](int count)
    {
        for(int i = 0; i < count; ++i)
        {
            frames.script_moves({frame_travel, 0.0f, 0.0f});
            frames.advance(frame_seconds);
        }
    };
    /// Walks on after a jump to `target`: every frame must start from there, none lies on the way there.
    const auto check_lands_at = [&](const math::vec3& target, const std::string& what)
    {
        float furthest_step = 0.0f;
        float previous = target.x;
        for(int i = 0; i < 30; ++i)
        {
            frames.script_moves({frame_travel, 0.0f, 0.0f});
            frames.advance(frame_seconds);
            const float x = frames.transform.get_position_global().x;
            furthest_step = std::max(furthest_step, std::abs(x - previous));
            previous = x;
        }
        check(furthest_step < frame_travel * 2.5f, what + ": no frame lies on the way there");
        check_near(frames.cc.get_position().z, target.z, 1e-3f, what + ": the body went there too");
        check(previous > target.x, what + ": the walk goes on from there");
    };

    walk_frames(40);
    const math::vec3 written{-6.0f, 1.0f, 3.0f};
    frames.transform.set_position_global(written);
    check_near(frames.cc.get_position().x, written.x, 1e-3f, "a written position counts at once");
    check_lands_at(written, "a written position");

    walk_frames(10);
    const math::vec3 warped{-4.0f, 1.0f, -3.0f};
    fx.backend->warp_character(frames.cc, warped);
    check_near(frames.transform.get_position_global().x, warped.x, 1e-3f, "a warp shows at once");
    check_near(frames.cc.get_position().x, warped.x, 1e-3f, "a warp counts at once");
    check_lands_at(warped, "a warp");

    // Written on a frame where a script also turns: the sync must carry the teleport, not a stale position.
    walk_frames(10);
    const math::vec3 turned{2.0f, 1.0f, 5.0f};
    frames.transform.set_position_global(turned);
    frames.transform.set_rotation_global(math::angleAxis(math::radians(30.0f), math::vec3{0.0f, 1.0f, 0.0f}));
    check_lands_at(turned, "a position written with a turn");
}

/// A parent that moves carries the character: the character starts over wherever that puts it.
void test_a_moving_parent_carries_the_character(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_a_moving_parent_carries_the_character\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto platform = fx.spawn("platform", {0.0f, 0.0f, 0.0f});
    auto hero = fx.spawn_character("hero", {0.0f, 1.2f, 0.0f});
    hero.get<transform_component>().set_parent(platform, true);
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    const float frame_seconds = 1.0f / 60.0f;
    const float start_x = frames.transform.get_position_global().x;
    platform.get<transform_component>().set_position_global({2.0f, 0.0f, 0.0f});
    for(int i = 0; i < 10; ++i)
    {
        frames.advance(frame_seconds);
    }
    check_near(frames.transform.get_position_global().x, start_x + 2.0f, 1e-3f, "the character goes with its parent");
    check_near(frames.cc.get_position().x, start_x + 2.0f, 1e-3f, "and its body goes along");
}

void test_a_jump_arc_is_even(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_a_jump_arc_is_even\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {0.0f, 1.2f, 0.0f});
    fx.begin();
    const float step_seconds = 0.02f;
    character_frames frames(fx, hero, step_seconds);
    frames.settle();

    const float jump_speed = 5.0f;
    const float frame_seconds = 1.0f / 60.0f;
    fx.backend->jump_character(frames.cc, {0.0f, jump_speed, 0.0f});
    float previous_y = frames.transform.get_position_global().y;
    float previous_rise = 0.0f;
    float widest_change = 0.0f;
    // Well inside the arc, away from the take-off and the landing.
    for(int frame = 0; frame < 40; ++frame)
    {
        frames.advance(frame_seconds);
        const float y = frames.transform.get_position_global().y;
        const float rise = y - previous_y;
        if(frame > 4)
        {
            widest_change = std::max(widest_change, std::abs(rise - previous_rise));
        }
        previous_y = y;
        previous_rise = rise;
    }
    // Gravity alone changes a frame's rise by 3 mm. Raw steps would change it by a whole frame's rise.
    check(widest_change < 0.008f, "a frame's rise changes no faster than gravity allows");
}

void test_a_standing_character_stays_clean(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_a_standing_character_stays_clean\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {0.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    frames.transform.set_dirty(dirty_ids::render_change, false);
    for(int frame = 0; frame < 60; ++frame)
    {
        frames.advance(1.0f / 60.0f);
    }
    check(!frames.transform.is_dirty(dirty_ids::render_change), "standing still touches no transform");
}

void test_a_slow_frame_rate_builds_no_backlog(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_a_slow_frame_rate_builds_no_backlog\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    // Ten frames a second: more game time per frame than the steps a frame may run catch up.
    const float frame_seconds = 0.1f;
    const float start_x = frames.transform.get_position_global().x;
    float expected = 0.0f;
    for(int frame = 0; frame < 30; ++frame)
    {
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
        expected += walk_speed * frame_seconds;
        frames.advance(frame_seconds);
    }
    check_near(frames.transform.get_position_global().x - start_x,
               expected,
               walk_speed * frame_seconds * 2.0f,
               "the walk keeps up at a slow frame rate");
}

void test_interpolation_turns_off_cleanly(rtti::context& ctx, physics_backend_type backend)
{
    std::printf("test_interpolation_turns_off_cleanly\n");
    sim_fixture fx(ctx, backend);
    fx.spawn_ground();
    auto hero = fx.spawn_character("hero", {-12.0f, 1.2f, 0.0f});
    fx.begin();
    character_frames frames(fx, hero, 0.02f);
    frames.settle();

    const float frame_seconds = 1.0f / 60.0f;
    for(int frame = 0; frame < 30; ++frame)
    {
        frames.script_moves({walk_speed * frame_seconds, 0.0f, 0.0f});
        frames.advance(frame_seconds);
    }
    const float truth = frames.cc.get_position().x;
    frames.cc.set_interpolate(false);
    frames.advance(frame_seconds);
    check(frames.transform.get_position_global().x >= truth - 1e-4f, "the transform goes back to the physics position");
    check_near(frames.cc.get_position().x,
               frames.transform.get_position_global().x,
               1e-4f,
               "and stays there");
    check(frames.cc.get_motion().queued_move == math::vec3{0.0f}, "no move is left behind");
}

void test_moves_are_shared_by_time()
{
    std::printf("test_moves_are_shared_by_time\n");
    character_motion motion;
    motion.queued_move = {1.0f, 0.0f, 0.0f};
    motion.add_queued_time(0.05f, 1.0f);
    const math::vec3 first = motion.take_step_move(0.02f);
    check_near(first.x, 0.4f, 1e-5f, "a step takes its share of the time the moves span");
    const math::vec3 second = motion.take_step_move(0.02f);
    check_near(second.x, 0.4f, 1e-5f, "the next step takes the same");
    const math::vec3 last = motion.take_step_move(0.02f);
    check_near(last.x, 0.2f, 1e-5f, "the last one takes what is left");
    check(motion.queued_move == math::vec3{0.0f} && motion.queued_time == 0.0f, "nothing waits any more");

    motion.add_queued_time(0.05f, 1.0f);
    check(motion.queued_time == 0.0f, "time only counts for moves that wait");

    motion.queued_move = {1.0f, 0.0f, 0.0f};
    motion.add_queued_time(5.0f, 0.08f);
    check_near(motion.queued_time, 0.08f, 1e-6f, "the time moves span has a limit");
}

void run_character_interpolation_tests(rtti::context& ctx, physics_backend_type backend)
{
    test_character_travel_is_even(ctx, backend);
    test_raw_steps_stall(ctx, backend);
    test_turning_does_not_drag_the_body_back(ctx, backend);
    test_transform_trails_the_physics_position(ctx, backend);
    test_a_fixed_update_reads_the_physics_position(ctx, backend);
    test_walls_stop_the_shown_position(ctx, backend);
    test_teleports_do_not_smear(ctx, backend);
    test_a_moving_parent_carries_the_character(ctx, backend);
    test_a_jump_arc_is_even(ctx, backend);
    test_a_standing_character_stays_clean(ctx, backend);
    test_a_slow_frame_rate_builds_no_backlog(ctx, backend);
    test_interpolation_turns_off_cleanly(ctx, backend);
}

/**
 * Box3D recomputes the mass from the shape densities when the motion locks turn a fixed
 * rotation on or off. A character-like body (light capsule, rotation frozen) then weighed
 * two tons, and a dash impulse changed its velocity by millimeters per second.
 */
void test_frozen_rotation_keeps_the_authored_mass(rtti::context& ctx)
{
    std::printf("test_frozen_rotation_keeps_the_authored_mass\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    physics_capsule_shape capsule;
    capsule.center = {0.0f, 1.5f, 0.0f};
    capsule.radius = 0.5f;
    capsule.length = 2.0f;
    const float impulse = 8.0f;

    // Frozen before play: the locks are applied while the body is made.
    auto runner = fx.spawn("runner", {0.0f, 0.0f, 0.0f});
    auto& runner_comp = fx.add_body(runner, rigidbody_type::dynamic, capsule, 1.0f);
    runner_comp.set_freeze_rotation({true, true, true});
    // Frozen during play: the locks change on a body that already has its mass.
    auto late = fx.spawn("late", {5.0f, 0.0f, 0.0f});
    auto& late_comp = fx.add_body(late, rigidbody_type::dynamic, capsule, 2.0f);

    fx.begin();
    fx.step(settle_steps);
    const float runner_before = runner_comp.get_velocity().x;
    fx.backend->apply_force(runner_comp, {impulse, 0.0f, 0.0f}, force_mode::impulse);
    check_near(runner_comp.get_velocity().x - runner_before,
               impulse / 1.0f,
               0.01f,
               "an impulse on a rotation-frozen body is divided by the authored mass");

    late_comp.set_freeze_rotation({true, true, true});
    fx.step(1);
    const float late_before = late_comp.get_velocity().x;
    fx.backend->apply_force(late_comp, {impulse, 0.0f, 0.0f}, force_mode::impulse);
    check_near(late_comp.get_velocity().x - late_before,
               impulse / 2.0f,
               0.01f,
               "freezing the rotation during play keeps the authored mass");

    late_comp.set_freeze_rotation({false, false, false});
    fx.step(1);
    const float unfrozen_before = late_comp.get_velocity().x;
    fx.backend->apply_force(late_comp, {impulse, 0.0f, 0.0f}, force_mode::impulse);
    check_near(late_comp.get_velocity().x - unfrozen_before,
               impulse / 2.0f,
               0.01f,
               "releasing the rotation during play keeps the authored mass");
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
    test_frozen_rotation_keeps_the_authored_mass(ctx);
    test_kinematic_pushes_dynamic(ctx);
    test_bodies_come_and_go_during_play(ctx);
    test_character_controller(ctx);
    test_moves_are_shared_by_time();
    std::printf("character interpolation: Box3D\n");
    run_character_interpolation_tests(ctx, physics_backend_type::box3d);
    std::printf("character interpolation: Bullet\n");
    run_character_interpolation_tests(ctx, physics_backend_type::bullet);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("box3d backend smoke", run_box3d_suite)
