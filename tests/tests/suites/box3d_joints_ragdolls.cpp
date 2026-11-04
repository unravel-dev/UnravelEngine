/*
 * Joints, ragdolls and the body properties added with them, on the Box3D backend.
 *
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite joints
 *
 * Like the backend smoke suite, every test builds a small scene on the shared game registry and drives the backend
 * the way physics_system does; the ragdoll tests also run the ragdoll pose capture and write around each step.
 */

#include "../tests.h"

#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/scene.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/backend/physics_backend.h>
#include <engine/physics/ecs/components/joint_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/components/ragdoll_component.h>
#include <engine/physics/ecs/systems/ragdoll_poses.h>
#include <engine/physics/ragdoll_builder.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <base/basetypes.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace unravel;

// Named, not anonymous: test sources may be joined into one unit.
namespace joints_suite
{
constexpr float step_seconds = 1.0f / 60.0f;
constexpr int one_second = 60;
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
    if(!(std::abs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %f, expected %f +/- %f)\n", what.c_str(), actual, expected, tolerance);
    }
}

auto is_finite(const math::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct sim_fixture
{
    explicit sim_fixture(rtti::context& c)
        : ctx(c)
        , registry(*c.get_cached<ecs>().get_scene().registry)
    {
        backend = create_physics_backend(physics_backend_type::box3d);
        backend->init();
    }

    ~sim_fixture()
    {
        end();
        for(auto it = entities.rbegin(); it != entities.rend(); ++it)
        {
            if(*it && it->valid())
            {
                scene::destroy_entity(*it);
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

    /// One frame of one fixed step, ragdoll poses included.
    void step(int count = 1)
    {
        const delta_t dt(step_seconds);
        for(int i = 0; i < count; ++i)
        {
            if(animate)
            {
                animate();
            }
            capture_ragdoll_animations(registry, step_seconds);
            backend->sync_to_physics(ctx, dt);
            backend->simulate(dt);
            backend->sync_from_physics(ctx);
            backend->dispatch_contacts(ctx);
            show_ragdolls(registry, 1.0f, step_seconds);
        }
    }

    /// One paused frame: the animation still poses the bones, nothing steps, the ragdolls pose them again.
    void hold(int count = 1)
    {
        for(int i = 0; i < count; ++i)
        {
            if(animate)
            {
                animate();
            }
            capture_ragdoll_animations(registry, 0.0f);
            show_ragdolls(registry, 1.0f, 0.0f);
        }
    }

    auto spawn(const std::string& name, const math::vec3& position, entt::handle parent = {}) -> entt::handle
    {
        auto e = scene::create_entity(registry, name, parent);
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

    /// A joint entity at @p position whose X axis turns by @p axis_rotation (Euler degrees).
    auto spawn_joint(const std::string& name,
                     const math::vec3& position,
                     joint_type type,
                     entt::handle body_a,
                     entt::handle body_b,
                     const math::vec3& axis_rotation = math::vec3{0.0f}) -> joint_component&
    {
        auto e = spawn(name, position);
        auto& joint = e.emplace<joint_component>();
        joint.set_type(type);
        joint.set_body_a(body_a);
        joint.set_body_b(body_b);
        joint.set_axis_rotation(axis_rotation);
        return joint;
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
    /// Runs at the start of every frame, before the ragdolls read the pose: what the animation system does.
    std::function<void()> animate;
};

// --- body properties ------------------------------------------------------------------------

void test_damping_and_gravity_scale(rtti::context& ctx)
{
    std::printf("test_damping_and_gravity_scale\n");
    sim_fixture fx(ctx);
    auto damped = fx.spawn_box("damped", {0.0f, 10.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto& damped_comp = damped.get<physics_component>();
    damped_comp.set_is_using_gravity(false);
    damped_comp.set_linear_damping(1.0f);
    damped_comp.set_velocity({10.0f, 0.0f, 0.0f});

    auto half = fx.spawn_box("half", {0.0f, 10.0f, 5.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    half.get<physics_component>().set_gravity_scale(0.5f);

    fx.begin();
    fx.step(one_second);

    // Box3D damps by 1 / (1 + h c) per sub-step: about e^-1 after a second.
    check_near(damped_comp.get_velocity().x, 10.0f * std::exp(-1.0f), 0.4f, "linear damping 1 loses about 63% a second");
    check_near(10.0f - fx.position_of(half).y, 0.5f * 0.5f * 9.8f, 0.2f, "half gravity falls half as far");
}

void test_rotated_shapes(rtti::context& ctx)
{
    std::printf("test_rotated_shapes\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    // A long thin box stood on end by its shape rotation rests on its short side, its center one meter up.
    auto post = fx.spawn("post", {0.0f, 1.5f, 0.0f});
    physics_box_shape box;
    box.extends = {2.0f, 0.4f, 0.4f};
    box.rotation = {0.0f, 0.0f, 90.0f};
    auto& post_comp = fx.add_body(post, rigidbody_type::dynamic, box);
    post_comp.set_freeze_rotation({true, true, true});

    auto lying = fx.spawn("lying capsule", {4.0f, 1.0f, 0.0f});
    physics_capsule_shape capsule;
    capsule.radius = 0.25f;
    capsule.length = 1.5f;
    capsule.rotation = {90.0f, 0.0f, 0.0f};
    fx.add_body(lying, rigidbody_type::dynamic, capsule).set_freeze_rotation({true, true, true});

    fx.begin();
    fx.step(3 * one_second);

    check_near(fx.position_of(post).y, 1.0f, 0.03f, "a box turned on end rests on its short side");
    check_near(fx.position_of(lying).y, 0.25f, 0.03f, "a capsule turned onto its side rests on its radius");

    auto hit = fx.backend->shape_cast(post_comp.get_shapes()[0], {0.0f, 5.0f, 0.0f}, math::identity<math::quat>(),
                                      {0.0f, -1.0f, 0.0f}, 10.0f, everything, false);
    check(hit.has_value() && hit->entity == post.entity(), "a box cast down finds the post first");
    auto overlaps = fx.backend->shape_overlap(post_comp.get_shapes()[0], {4.0f, 0.25f, 0.0f},
                                              math::identity<math::quat>(), everything, false);
    check(std::find(overlaps.begin(), overlaps.end(), lying.entity()) != overlaps.end(),
          "a box overlap finds the capsule it covers");
}

void test_sleep_and_forces_at_a_point(rtti::context& ctx)
{
    std::printf("test_sleep_and_forces_at_a_point\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto sleeper = fx.spawn_box("sleeper", {0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto insomniac = fx.spawn_box("insomniac", {3.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    insomniac.get<physics_component>().set_can_sleep(false);
    auto spinner = fx.spawn_box("spinner", {0.0f, 10.0f, 5.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto& spinner_comp = spinner.get<physics_component>();
    spinner_comp.set_is_using_gravity(false);

    fx.begin();
    fx.step(4 * one_second);
    check(sleeper.get<physics_component>().is_sleeping(), "a resting body falls asleep");
    check(!insomniac.get<physics_component>().is_sleeping(), "a body that may not sleep stays awake");

    fx.backend->set_body_awake(sleeper.get<physics_component>(), true);
    fx.step(1);
    check(!sleeper.get<physics_component>().is_sleeping(), "waking a body wakes it");

    fx.backend->apply_force_at_position(spinner_comp, {0.0f, 0.0f, 5.0f}, {0.5f, 10.0f, 5.0f}, force_mode::impulse);
    check(math::length(spinner_comp.get_angular_velocity()) > 1.0f, "an impulse off the center spins the body");
    check_near(spinner_comp.get_velocity().z, 5.0f, 0.01f, "an impulse at a point still moves the body by impulse / mass");
}

void test_kinematic_release_lets_bodies_move(rtti::context& ctx)
{
    std::printf("test_kinematic_release_lets_bodies_move\n");
    sim_fixture fx(ctx);
    // A slope a little steeper than the box's grip (tan 30 degrees is 0.58, the grip 0.5): released, the box gathers
    // speed slowly, so it is still slow enough to sleep for a few steps. A kinematic stop holds it on the slope.
    const float angle = math::radians(30.0f);
    const math::vec3 down_slope{0.0f, -std::sin(angle), -std::cos(angle)};
    const math::vec3 normal{0.0f, std::cos(angle), -std::sin(angle)};
    auto slope = fx.spawn("slope", {0.0f, 2.0f, 0.0f});
    slope.get<transform_component>().set_rotation_euler_global({-30.0f, 0.0f, 0.0f});
    physics_box_shape slope_box;
    slope_box.extends = {2.0f, 0.2f, 6.0f};
    fx.add_body(slope, rigidbody_type::static_body, slope_box);
    const math::vec3 box_start = math::vec3{0.0f, 2.0f, 0.0f} + normal * 0.6f;
    auto box = fx.spawn("box", box_start);
    box.get<transform_component>().set_rotation_euler_global({-30.0f, 0.0f, 0.0f});
    physics_box_shape box_shape;
    box_shape.extends = {1.0f, 1.0f, 1.0f};
    fx.add_body(box, rigidbody_type::dynamic, box_shape);
    const math::vec3 stop_start = box_start + down_slope * 0.62f - normal * 0.25f;
    auto stop = fx.spawn("stop", stop_start);
    stop.get<transform_component>().set_rotation_euler_global({-30.0f, 0.0f, 0.0f});
    physics_box_shape stop_shape;
    stop_shape.extends = {2.0f, 0.6f, 0.2f};
    fx.add_body(stop, rigidbody_type::kinematic, stop_shape);
    fx.begin();
    fx.step(2 * one_second);
    const math::vec3 held = fx.position_of(box);
    check(math::distance(held, box_start) < 0.1f, "the kinematic stop holds the box on the slope");
    // The stop sinks into the slope, as a gate opening does.
    for(int i = 0; i < 10; ++i)
    {
        stop.get<transform_component>().set_position_global(stop_start - normal * (0.08f * float(i + 1)));
        fx.step(1);
    }
    fx.step(one_second + one_second / 2);
    check(math::dot(fx.position_of(box) - held, down_slope) > 0.5f,
          "a box that rested against a kinematic stop slides once the stop moves away");
}

void test_gravity_and_explosions(rtti::context& ctx)
{
    std::printf("test_gravity_and_explosions\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto left = fx.spawn_box("left", {-1.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    auto right = fx.spawn_box("right", {1.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic);
    fx.begin();
    fx.backend->set_gravity({0.0f, -3.0f, 0.0f});
    check_near(fx.backend->get_gravity().y, -3.0f, 1e-5f, "gravity changes while playing");
    fx.step(10);
    fx.backend->explode({0.0f, 0.5f, 0.0f}, 3.0f, 1.0f, 20.0f, everything);
    fx.step(1);
    check(left.get<physics_component>().get_velocity().x < -0.5f, "an explosion pushes a body on its left away");
    check(right.get<physics_component>().get_velocity().x > 0.5f, "an explosion pushes a body on its right away");
}

// --- joints -----------------------------------------------------------------------------------

void test_hinge_pendulum_and_limits(rtti::context& ctx)
{
    std::printf("test_hinge_pendulum_and_limits\n");
    sim_fixture fx(ctx);
    // Hinges about world Z (the frame X axis turned by -90 degrees about Y).
    const math::vec3 about_z{0.0f, -90.0f, 0.0f};
    auto free_bob = fx.spawn_box("free bob", {1.0f, 3.0f, 0.0f}, {0.2f, 0.2f, 0.2f}, rigidbody_type::dynamic);
    auto& free_joint = fx.spawn_joint("free hinge", {0.0f, 3.0f, 0.0f}, joint_type::hinge, {}, free_bob, about_z);
    // Angular damping would barely slow the swing: the bob's own spin carries almost none of its energy.
    free_bob.get<physics_component>().set_linear_damping(0.8f);

    auto limited_bob = fx.spawn_box("limited bob", {1.0f, 3.0f, 4.0f}, {0.2f, 0.2f, 0.2f}, rigidbody_type::dynamic);
    auto& limited = fx.spawn_joint("limited hinge", {0.0f, 3.0f, 4.0f}, joint_type::hinge, {}, limited_bob, about_z);
    limited.set_limits({true, -30.0f, 30.0f, 45.0f});

    fx.begin();
    float worst_stretch = 0.0f;
    for(int i = 0; i < 6 * one_second; ++i)
    {
        fx.step(1);
        const float radius = math::distance(fx.position_of(free_bob), math::vec3{0.0f, 3.0f, 0.0f});
        worst_stretch = std::max(worst_stretch, std::abs(radius - 1.0f));
    }
    check(free_joint.get_state().is_active, "a hinge on the world is active");
    check(worst_stretch < 0.02f, "a swinging hinge keeps its bob at its distance");
    check_near(fx.position_of(free_bob).y, 2.0f, 0.1f, "a damped pendulum comes to hang below its hinge");
    check(std::abs(limited.get_state().angle) <= 31.0f, "a limited hinge stays inside its limits");
    check_near(fx.position_of(limited_bob).y, 3.0f - std::sin(math::radians(30.0f)), 0.05f,
               "a limited hinge hangs at its limit");
}

void test_hinge_motor(rtti::context& ctx)
{
    std::printf("test_hinge_motor\n");
    sim_fixture fx(ctx);
    auto wheel = fx.spawn_box("wheel", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 0.2f}, rigidbody_type::dynamic);
    wheel.get<physics_component>().set_is_using_gravity(false);
    auto& hinge = fx.spawn_joint("axle", {0.0f, 3.0f, 0.0f}, joint_type::hinge, {}, wheel, {0.0f, -90.0f, 0.0f});
    hinge.set_motor({true, 90.0f, 1000.0f});
    fx.begin();
    fx.step(one_second);
    check_near(hinge.get_state().speed, 90.0f, 3.0f, "a hinge motor turns at its speed");

    hinge.set_motor({false, 0.0f, 0.0f});
    hinge.set_friction(1000.0f);
    fx.step(one_second / 2);
    check(std::abs(hinge.get_state().speed) < 2.0f, "joint friction stops a hinge whose motor is off");
}

void test_slider_and_rope(rtti::context& ctx)
{
    std::printf("test_slider_and_rope\n");
    sim_fixture fx(ctx);
    // A slider pointing down (X turned by -90 degrees about Z) stops a falling body at its upper limit.
    auto rider = fx.spawn_box("rider", {0.0f, 5.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, rigidbody_type::dynamic);
    auto& slider = fx.spawn_joint("slider", {0.0f, 5.0f, 0.0f}, joint_type::slider, {}, rider, {0.0f, 0.0f, -90.0f});
    slider.set_limits({true, 0.0f, 1.0f, 0.0f});

    auto ball = fx.spawn("ball", {3.0f, 4.0f, 0.0f});
    physics_sphere_shape sphere;
    sphere.radius = 0.2f;
    fx.add_body(ball, rigidbody_type::dynamic, sphere);
    auto& rope = fx.spawn_joint("rope", {3.0f, 5.0f, 0.0f}, joint_type::distance, {}, ball);
    rope.set_limits({true, 0.0f, 2.0f, 0.0f});

    fx.begin();
    fx.step(2 * one_second);
    check_near(fx.position_of(rider).y, 4.0f, 0.02f, "a slider stops at its limit");
    check_near(slider.get_state().position, 1.0f, 0.02f, "a slider reports its position");
    check_near(fx.position_of(ball).y, 3.0f, 0.03f, "a rope goes slack up to its length and holds there");
    check_near(rope.get_state().position, 2.0f, 0.03f, "a rope reports its length");
}

struct break_listener
{
    int breaks{0};
};

void test_breaking(rtti::context& ctx)
{
    std::printf("test_breaking\n");
    sim_fixture fx(ctx);
    auto heavy = fx.spawn_box("heavy", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic, 10.0f);
    auto& weak = fx.spawn_joint("weak weld", {0.0f, 3.5f, 0.0f}, joint_type::fixed, {}, heavy);
    weak.set_break_force(50.0f);

    auto light = fx.spawn_box("light", {3.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, rigidbody_type::dynamic, 1.0f);
    auto& strong = fx.spawn_joint("strong weld", {3.0f, 3.5f, 0.0f}, joint_type::fixed, {}, light);
    strong.set_break_force(50.0f);

    fx.begin();
    fx.step(one_second);
    check(weak.get_state().is_broken, "a weld holding more than its break force breaks");
    check(fx.position_of(heavy).y < 2.0f, "a body falls once its joint broke");
    check(!strong.get_state().is_broken, "a weld holding less than its break force holds");
    check_near(fx.position_of(light).y, 3.0f, 0.02f, "a holding weld keeps its body in place");

    strong.break_joint();
    fx.step(one_second / 2);
    check(strong.get_state().is_broken, "a script can break a joint");
    check(fx.position_of(light).y < 2.5f, "a joint broken by a script lets go");

    weak.repair();
    const float y_before = fx.position_of(heavy).y;
    heavy.get<physics_component>().set_is_using_gravity(false);
    heavy.get<physics_component>().set_velocity({0.0f, 0.0f, 0.0f});
    fx.step(one_second / 2);
    check(!weak.get_state().is_broken && weak.get_state().is_active, "a repaired joint holds again");
    check_near(fx.position_of(heavy).y, y_before, 0.05f, "a repaired joint holds the body where it was");
}

void test_ball_cone(rtti::context& ctx)
{
    std::printf("test_ball_cone\n");
    sim_fixture fx(ctx);
    // A rod hanging from a ball joint whose axis points down, pushed sideways, swings no further than its cone.
    auto rod = fx.spawn_box("rod", {0.0f, 4.0f, 0.0f}, {0.1f, 2.0f, 0.1f}, rigidbody_type::dynamic);
    auto& ball = fx.spawn_joint("ball", {0.0f, 5.0f, 0.0f}, joint_type::ball, {}, rod, {0.0f, 0.0f, -90.0f});
    ball.set_limits({true, -20.0f, 20.0f, 25.0f});
    fx.begin();
    fx.backend->apply_force_at_position(rod.get<physics_component>(), {8.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f},
                                        force_mode::impulse);
    float widest = 0.0f;
    for(int i = 0; i < 2 * one_second; ++i)
    {
        fx.step(1);
        const math::vec3 down = math::normalize(fx.position_of(rod) - math::vec3{0.0f, 5.0f, 0.0f});
        widest = std::max(widest, math::degrees(std::acos(math::clamp(-down.y, -1.0f, 1.0f))));
    }
    check(widest > 15.0f, "the push swings the rod");
    check(widest < 27.0f, "a ball joint keeps the rod inside its cone");
}

void test_wheel(rtti::context& ctx)
{
    std::printf("test_wheel\n");
    sim_fixture fx(ctx);
    auto tire = fx.spawn("tire", {0.0f, 2.0f, 0.0f});
    physics_cylinder_shape cylinder;
    cylinder.radius = 0.4f;
    cylinder.length = 0.2f;
    cylinder.rotation = {0.0f, 0.0f, 90.0f};
    fx.add_body(tire, rigidbody_type::dynamic, cylinder, 5.0f);
    auto& wheel = fx.spawn_joint("wheel", {0.0f, 2.0f, 0.0f}, joint_type::wheel, {}, tire);
    wheel.set_spring({true, 4.0f, 0.7f, 0.0f, math::vec3{0.0f}});
    wheel.set_limits({true, -0.3f, 0.3f, 0.0f});
    wheel.set_motor({true, 360.0f, 200.0f});
    fx.begin();
    fx.step(2 * one_second);
    check_near(wheel.get_state().speed, 360.0f, 10.0f, "a wheel spins at its motor speed");
    const float sag = 2.0f - fx.position_of(tire).y;
    check(sag > 0.0f && sag <= 0.31f, "a wheel hangs on its suspension inside its limits");
}

// --- ragdolls -----------------------------------------------------------------------------------

struct test_humanoid
{
    entt::handle root;
    entt::handle model;
    std::vector<entt::handle> bones;

    auto bone(const std::string& name) const -> entt::handle
    {
        for(const auto& b : bones)
        {
            if(b.get<tag_component>().name == name)
            {
                return b;
            }
        }
        return {};
    }
};

/**
 * @brief A mannequin-shaped skeleton in a T-pose, Unreal bone names, facing +Z with its left at +X, standing at
 *        @p position. No mesh: the ragdoll setup sizes the bodies from the bone lengths.
 */
auto make_humanoid(sim_fixture& fx, const math::vec3& position) -> test_humanoid
{
    test_humanoid result;
    result.model = fx.spawn("mannequin", position);
    struct bone_def
    {
        const char* name;
        const char* parent;
        math::vec3 position;
    };
    const bone_def defs[] = {
        {"root", nullptr, {0.0f, 0.0f, 0.0f}},
        {"pelvis", "root", {0.0f, 0.95f, 0.0f}},
        {"spine_01", "pelvis", {0.0f, 1.05f, 0.0f}},
        {"spine_02", "spine_01", {0.0f, 1.18f, 0.0f}},
        {"spine_03", "spine_02", {0.0f, 1.32f, 0.0f}},
        {"neck_01", "spine_03", {0.0f, 1.50f, 0.0f}},
        {"head", "neck_01", {0.0f, 1.60f, 0.02f}},
        {"clavicle_l", "spine_03", {0.04f, 1.45f, 0.0f}},
        {"upperarm_l", "clavicle_l", {0.18f, 1.45f, 0.0f}},
        {"lowerarm_l", "upperarm_l", {0.46f, 1.45f, 0.0f}},
        {"hand_l", "lowerarm_l", {0.72f, 1.45f, 0.0f}},
        {"clavicle_r", "spine_03", {-0.04f, 1.45f, 0.0f}},
        {"upperarm_r", "clavicle_r", {-0.18f, 1.45f, 0.0f}},
        {"lowerarm_r", "upperarm_r", {-0.46f, 1.45f, 0.0f}},
        {"hand_r", "lowerarm_r", {-0.72f, 1.45f, 0.0f}},
        {"thigh_l", "pelvis", {0.10f, 0.92f, 0.0f}},
        {"calf_l", "thigh_l", {0.10f, 0.50f, 0.0f}},
        {"foot_l", "calf_l", {0.10f, 0.08f, 0.0f}},
        {"ball_l", "foot_l", {0.10f, 0.02f, 0.13f}},
        {"thigh_r", "pelvis", {-0.10f, 0.92f, 0.0f}},
        {"calf_r", "thigh_r", {-0.10f, 0.50f, 0.0f}},
        {"foot_r", "calf_r", {-0.10f, 0.08f, 0.0f}},
        {"ball_r", "foot_r", {-0.10f, 0.02f, 0.13f}},
    };
    for(const auto& def : defs)
    {
        entt::handle parent = def.parent ? result.bone(def.parent) : result.model;
        auto bone = fx.spawn(def.name, position + def.position, parent);
        result.bones.push_back(bone);
    }
    result.root = result.bone("root");
    auto& model = result.model.emplace<model_component>();
    model.set_armature_entities(result.bones);
    return result;
}

auto add_ragdoll(test_humanoid& humanoid) -> ragdoll_component&
{
    auto& ragdoll = humanoid.model.emplace<ragdoll_component>();
    ragdoll_build_options options;
    options.fit_to_skin = false;
    const auto result = setup_ragdoll(ragdoll, options);
    check(result.ok, "a humanoid skeleton gets a ragdoll: " + result.error);
    return ragdoll;
}

auto body_position(const ragdoll_component& ragdoll, const std::string& bone) -> math::vec3
{
    const int index = ragdoll.find_body(bone);
    return index >= 0 ? ragdoll.get_runtime().bodies[size_t(index)].world.get_position() : math::vec3{0.0f};
}

void test_ragdoll_setup(rtti::context& ctx)
{
    std::printf("test_ragdoll_setup\n");
    sim_fixture fx(ctx);
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    const auto& bodies = ragdoll.get_bodies();
    check(bodies.size() == 18, "a mannequin gets hips, three spine bodies, neck, head, arms, hands, legs and feet");
    float mass = 0.0f;
    for(const auto& body : bodies)
    {
        mass += body.mass;
    }
    check_near(mass, 70.0f, 0.01f, "the masses add up to the total mass");
    const int knee = ragdoll.find_body("calf_l");
    const int elbow = ragdoll.find_body("lowerarm_r");
    check(knee >= 0 && bodies[size_t(knee)].joint == ragdoll_joint_type::hinge, "the knee is a hinge");
    check(elbow >= 0 && bodies[size_t(elbow)].joint == ragdoll_joint_type::hinge, "the elbow is a hinge");
    check(ragdoll.find_body("clavicle_l") < 0, "the clavicles ride on the chest without bodies");
    bool thighs_apart = false;
    for(const auto& pair : ragdoll.get_disabled_collisions())
    {
        thighs_apart |= (pair.bone_a == "thigh_l" && pair.bone_b == "thigh_r") ||
                        (pair.bone_a == "thigh_r" && pair.bone_b == "thigh_l");
    }
    check(thighs_apart, "the thighs never collide with each other");
}

void test_ragdoll_animated_follows_bones(rtti::context& ctx)
{
    std::printf("test_ragdoll_animated_follows_bones\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    fx.begin();
    fx.step(10);
    check(ragdoll.get_runtime().is_built, "the ragdoll is built at play start");
    check(ragdoll.get_state() == ragdoll_state::animated, "a ragdoll starts animated");
    check_near(humanoid.bone("pelvis").get<transform_component>().get_position_global().y, 0.95f, 1e-4f,
               "an animated ragdoll leaves the bones where the animation put them");
    // The character walks: kinematic bodies follow the bones.
    humanoid.model.get<transform_component>().set_position_global({2.0f, 0.0f, 0.0f});
    fx.step(2);
    check_near(body_position(ragdoll, "pelvis").x, 2.0f, 0.05f, "kinematic bodies follow the moving character");
    auto hit = fx.backend->ray_cast({2.0f, 1.0f, 2.0f}, {0.0f, 0.0f, -1.0f}, 5.0f, everything, false);
    check(hit.has_value() && hit->entity != entt::null &&
              ragdoll.find_body(fx.registry.valid(hit->entity) ? entt::handle(fx.registry, hit->entity) : entt::handle{}) >= 0,
          "a ray hits a ragdoll body and reports its bone");
}

void test_ragdoll_falls_and_holds_together(rtti::context& ctx)
{
    std::printf("test_ragdoll_falls_and_holds_together\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    fx.begin();
    fx.step(2);
    ragdoll.set_state(ragdoll_state::simulated);
    const int chest = ragdoll.find_body("spine_03");
    ragdoll.add_impulse(chest, {0.0f, 0.0f, -40.0f});
    fx.step(5 * one_second);

    const auto& runtime = ragdoll.get_runtime();
    bool finite = true;
    float worst_gap = 0.0f;
    for(size_t i = 0; i < runtime.bodies.size(); ++i)
    {
        const auto& body = runtime.bodies[i];
        finite &= is_finite(body.world.get_position());
        if(body.parent < 0)
        {
            continue;
        }
        // The joint sits at this bone's origin: where the parent body carries it.
        const auto& parent = runtime.bodies[size_t(body.parent)];
        const math::vec3 rest = glm::inverse(parent.bind_model.get_rotation()) *
                                (body.bind_model.get_position() - parent.bind_model.get_position());
        const math::vec3 expected = parent.world.get_position() + parent.world.get_rotation() * rest;
        worst_gap = std::max(worst_gap, math::distance(expected, body.world.get_position()));
    }
    check(finite, "every body stays finite");
    check(worst_gap < 0.03f, "the joints hold the bodies together");
    const float pelvis_height = humanoid.bone("pelvis").get<transform_component>().get_position_global().y;
    check(pelvis_height < 0.4f, "a simulated ragdoll falls to the ground");
    check(pelvis_height > 0.0f, "a simulated ragdoll rests on the ground, not through it");
    check(body_position(ragdoll, "head").y > -0.05f, "the head rests on the ground");
    check(ragdoll.get_rest_time() > 0.5f, "a fallen ragdoll comes to rest");
    const math::vec3 drawn = humanoid.bone("head").get<transform_component>().get_position_global();
    check(math::distance(drawn, body_position(ragdoll, "head")) < 0.05f, "the head bone is drawn where its body lies");
}

void test_ragdoll_draws_physics_under_animation(rtti::context& ctx)
{
    std::printf("test_ragdoll_draws_physics_under_animation\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    // The animation poses every bone every frame, as a playing clip does.
    std::vector<std::pair<entt::handle, math::transform>> pose;
    for(const auto& bone : humanoid.bones)
    {
        pose.emplace_back(bone, bone.get<transform_component>().get_transform_local());
    }
    fx.animate = [&pose]()
    {
        for(const auto& [bone, local] : pose)
        {
            bone.get<transform_component>().set_transform_local(local);
        }
    };
    fx.begin();
    fx.step(2);
    ragdoll.set_state(ragdoll_state::simulated);
    ragdoll.add_impulse(ragdoll.find_body("spine_03"), {0.0f, 0.0f, -60.0f});
    fx.step(3 * one_second);
    const auto check_drawn = [&](const std::string& when)
    {
        for(const char* bone : {"head", "hand_l", "foot_r", "spine_03"})
        {
            const math::vec3 drawn = humanoid.bone(bone).get<transform_component>().get_position_global();
            check(math::distance(drawn, body_position(ragdoll, bone)) < 0.05f,
                  std::string("a simulated ragdoll draws ") + bone + " where its body lies " + when);
        }
    };
    check_drawn("though the animation poses it");
    fx.hold(3);
    check_drawn("while the game is paused");
}

void test_ragdoll_turns_as_one(rtti::context& ctx)
{
    std::printf("test_ragdoll_turns_as_one\n");
    sim_fixture fx(ctx);
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    fx.begin();
    fx.backend->set_gravity({0.0f, 0.0f, 0.0f});
    fx.step(2);
    ragdoll.set_state(ragdoll_state::simulated);
    fx.step(1);
    const math::vec3 pelvis = body_position(ragdoll, "pelvis");
    // A quarter turn about the spine in a second: the arm stretched along +X swings towards -Z.
    ragdoll.add_angular_velocity({0.0f, math::half_pi<float>(), 0.0f}, pelvis);
    fx.step(one_second / 2);
    const math::vec3 hand = body_position(ragdoll, "hand_l") - body_position(ragdoll, "pelvis");
    check(hand.z < -0.25f && hand.x > 0.25f, "the bodies turn about the pivot together");
    check_near(math::distance(body_position(ragdoll, "pelvis"), pelvis), 0.0f, 0.05f, "a body on the pivot stays where it is");
}

void test_ragdoll_blends_back(rtti::context& ctx)
{
    std::printf("test_ragdoll_blends_back\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    fx.begin();
    fx.step(2);
    ragdoll.set_state(ragdoll_state::simulated);
    fx.step(3 * one_second);
    const float fallen = humanoid.bone("pelvis").get<transform_component>().get_position_global().y;
    ragdoll.set_state(ragdoll_state::animated, 0.25f);
    fx.step(3);
    const float blending = humanoid.bone("pelvis").get<transform_component>().get_position_global().y;
    check(blending > fallen && blending < 0.95f, "the pose blends from where the ragdoll lies");
    fx.step(one_second / 2);
    check_near(humanoid.bone("pelvis").get<transform_component>().get_position_global().y, 0.95f, 1e-3f,
               "after the blend the bones are back on the animation");
    check_near(humanoid.bone("hand_l").get<transform_component>().get_position_global().x, 0.72f, 1e-3f,
               "every bone is back on the animation");
}

void test_ragdoll_powered_stands(rtti::context& ctx)
{
    std::printf("test_ragdoll_powered_stands\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    auto drive = ragdoll.get_drive();
    drive.world_frequency = 4.0f;
    ragdoll.set_drive(drive);
    fx.begin();
    fx.step(2);
    ragdoll.set_state(ragdoll_state::powered);
    ragdoll.add_impulse(ragdoll.find_body("head"), {0.0f, 0.0f, -10.0f});
    fx.step(3 * one_second);
    check_near(body_position(ragdoll, "pelvis").y, 0.95f, 0.08f, "a powered ragdoll stays on its feet");
    check_near(body_position(ragdoll, "head").z, 0.02f, 0.1f, "a powered ragdoll finds its way back to the pose");

    ragdoll.set_drive_strength(0.0f);
    fx.step(3 * one_second);
    check(body_position(ragdoll, "pelvis").y < 0.5f, "a powered ragdoll without strength falls");
}

void test_ragdoll_partial(rtti::context& ctx)
{
    std::printf("test_ragdoll_partial\n");
    sim_fixture fx(ctx);
    fx.spawn_ground();
    auto humanoid = make_humanoid(fx, {0.0f, 0.0f, 0.0f});
    auto& ragdoll = add_ragdoll(humanoid);
    auto drive = ragdoll.get_drive();
    drive.local_frequency = 0.0f;
    ragdoll.set_drive(drive);
    fx.begin();
    fx.step(2);
    check(ragdoll.set_bone_simulated("upperarm_l", true, true), "an arm can simulate on its own");
    fx.step(2 * one_second);
    const float hand = humanoid.bone("hand_l").get<transform_component>().get_position_global().y;
    check(hand < 1.2f, "a limp arm hangs down");
    check_near(humanoid.bone("pelvis").get<transform_component>().get_position_global().y, 0.95f, 1e-3f,
               "the rest of the character stays on the animation");
    check(!ragdoll.is_body_simulated(ragdoll.find_body("upperarm_r")), "the other arm stays animated");
}
} // namespace joints_suite

auto run_joints_suite(rtti::context& ctx) -> int
{
    using namespace joints_suite;
    test_damping_and_gravity_scale(ctx);
    test_rotated_shapes(ctx);
    test_sleep_and_forces_at_a_point(ctx);
    test_kinematic_release_lets_bodies_move(ctx);
    test_gravity_and_explosions(ctx);
    test_hinge_pendulum_and_limits(ctx);
    test_hinge_motor(ctx);
    test_slider_and_rope(ctx);
    test_breaking(ctx);
    test_ball_cone(ctx);
    test_wheel(ctx);
    test_ragdoll_setup(ctx);
    test_ragdoll_animated_follows_bones(ctx);
    test_ragdoll_falls_and_holds_together(ctx);
    test_ragdoll_draws_physics_under_animation(ctx);
    test_ragdoll_turns_as_one(ctx);
    test_ragdoll_blends_back(ctx);
    test_ragdoll_powered_stands(ctx);
    test_ragdoll_partial(ctx);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("box3d joints and ragdolls", run_joints_suite)
