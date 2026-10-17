/*
 * Validation suite for the analytic particle model (engine/engine/rendering/particles/ps).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "particle motion"
 *
 * Pins the closed-form motion against a numeric integration, the velocity it reports against the
 * derivative of its position, the exact conversion of scenes saved with the old motion, the slot
 * allocator and death queue, per-emitter seeds, emission duration, sub-frame birth ages, the catch-up
 * after a culled freeze, the capacity cap, that the analytic bounds hold every particle, and the
 * per-axis size, spin, intensity and two-gradient curves.
 */

#include "../tests.h"

#include <engine/meta/ecs/components/particle_emitter_component.hpp>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/particles/ps/emitter_desc.h>
#include <engine/rendering/particles/ps/particle_cutout.h>
#include <engine/rendering/particles/ps/particle_eval.h>
#include <engine/rendering/particles/ps/particle_luts.h>
#include <engine/rendering/particles/ps/particle_noise.h>
#include <engine/rendering/particles/ps/particle_slots.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/graphics.h>
#include <serialization/associative_archive.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
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

constexpr float k_frame_seconds = 1.0f / 60.0f;
constexpr float k_position_tolerance = 2.0e-3f;
constexpr int k_integration_steps = 20000;
constexpr float k_standard_gravity = 9.81f;

auto is_near(const math::vec3& a, const math::vec3& b, float tolerance) -> bool
{
    return math::length(a - b) <= tolerance;
}

/// A record launched at @p velocity from @p start, born at tick 0, living @p lifespan seconds.
auto make_record(const math::vec3& start, const math::vec3& velocity, float lifespan) -> ps_soa::particle_record
{
    ps_soa::particle_record record;
    record.set_start(start);
    record.set_velocity(velocity);
    record.birth = 0;
    record.lifespan = uint32_t(ps_soa::seconds_to_ticks(lifespan));
    record.size_picks = ps_soa::pack_size_picks(0.5f, 0.5f);
    ps_soa::pack_tint(math::vec4(1.0f), record);
    return record;
}

auto make_params(const ps_soa::emitter_desc& desc, float age) -> ps_soa::emitter_eval_params
{
    ps_soa::emitter_eval_params params;
    params.flags = static_cast<uint32_t>(desc.bake_features());
    params.acceleration = math::vec3(0.0f, -k_standard_gravity * desc.motion.gravity_scale, 0.0f) +
                          desc.motion.acceleration;
    params.drag = desc.motion.drag;
    params.now = uint32_t(ps_soa::seconds_to_ticks(age));
    return params;
}

/// Position after @p age seconds by explicit integration of dv/dt = a - drag v (midpoint steps, in double).
auto integrate(const math::vec3& start, const math::vec3& velocity, const math::vec3& acceleration, float drag, float age)
    -> math::vec3
{
    math::dvec3 position(start);
    math::dvec3 current(velocity);
    const math::dvec3 pull(acceleration);
    const double step = double(age) / double(k_integration_steps);
    for(int i = 0; i < k_integration_steps; ++i)
    {
        const math::dvec3 half = current + (pull - current * double(drag)) * (step * 0.5);
        position += half * step;
        current += (pull - half * double(drag)) * step;
    }
    return math::vec3(position);
}

void test_closed_form_matches_integration()
{
    struct motion_case
    {
        float gravity_scale;
        float drag;
        math::vec3 force;
        math::vec3 velocity;
    };
    const motion_case cases[] = {
        {0.0f, 0.0f, math::vec3(0.0f), math::vec3(1.0f, 2.0f, 0.5f)},
        // Small drag: the exponential forms cancel badly just above the series' range.
        {1.0f, 0.003f, math::vec3(0.0f), math::vec3(1.0f, 3.0f, 0.0f)},
        {1.0f, 0.05f, math::vec3(0.0f), math::vec3(1.0f, 3.0f, 0.0f)},
        {1.0f, 0.0f, math::vec3(0.0f), math::vec3(0.0f, 8.0f, 0.0f)},
        {1.0f, 1.5f, math::vec3(0.0f), math::vec3(3.0f, 6.0f, 0.0f)},
        {0.3f, 0.0001f, math::vec3(2.0f, 0.0f, -1.0f), math::vec3(-2.0f, 1.0f, 4.0f)},
        {-0.5f, 4.0f, math::vec3(0.0f, 0.0f, 3.0f), math::vec3(10.0f, 0.0f, 0.0f)},
    };
    bool matches = true;
    bool velocity_matches = true;
    for(const auto& motion : cases)
    {
        ps_soa::emitter_desc desc;
        desc.reset();
        desc.motion.gravity_scale = motion.gravity_scale;
        desc.motion.drag = motion.drag;
        desc.motion.acceleration = motion.force;
        ps_soa::lut_set luts;
        ps_soa::bake_lut_set(desc, desc.bake_features(), luts);
        const auto record = make_record(math::vec3(1.0f, 2.0f, 3.0f), motion.velocity, 5.0f);
        for(float age : {0.0f, 0.25f, 1.0f, 2.5f, 4.9f})
        {
            const auto params = make_params(desc, age);
            const auto state = ps_soa::evaluate_particle(record, params, luts);
            const math::vec3 expected = integrate(record.get_start(), motion.velocity, params.acceleration, motion.drag, state.age);
            if(!is_near(state.position, expected, k_position_tolerance))
            {
                std::printf("    motion g %.2f drag %.4f age %.3f: %.4f %.4f %.4f vs %.4f %.4f %.4f\n",
                            motion.gravity_scale,
                            motion.drag,
                            state.age,
                            state.position.x,
                            state.position.y,
                            state.position.z,
                            expected.x,
                            expected.y,
                            expected.z);
            }
            matches = matches && is_near(state.position, expected, k_position_tolerance);
            // The reported velocity is the derivative of the position.
            constexpr float k_derivative_step = 1.0f / 512.0f;
            auto later = make_params(desc, state.age + k_derivative_step);
            const auto ahead = ps_soa::evaluate_particle(record, later, luts);
            const math::vec3 slope = (ahead.position - state.position) / (ahead.age - state.age);
            velocity_matches = velocity_matches && is_near(slope, state.velocity, 0.05f * (1.0f + math::length(state.velocity)));
        }
    }
    check(matches, "closed-form motion (gravity, force, drag) matches a numeric integration");
    check(velocity_matches, "the reported velocity is the derivative of the position (size / color by speed read it)");
}

void test_gravity_is_physical()
{
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.motion.gravity_scale = 1.0f;
    ps_soa::lut_set luts;
    ps_soa::bake_lut_set(desc, desc.bake_features(), luts);
    const auto record = make_record(math::vec3(0.0f), math::vec3(0.0f), 5.0f);
    const auto state = ps_soa::evaluate_particle(record, make_params(desc, 1.0f), luts);
    check(std::abs(state.position.y + 0.5f * k_standard_gravity) < 1.0e-3f, "a particle falls 4.905 m in its first second at gravity 1");
}

void test_legacy_motion_conversion_is_exact()
{
    // Old model: p(t) = S + D (2t - t^2) + G t^2 over life t, G = 9.81 g L^2 down.
    const float lifetime = 2.0f;
    const math::vec3 distance(0.0f, 3.0f, 1.0f);
    const float old_gravity = 0.4f;
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.motion.gravity_scale = old_gravity * 2.0f;
    desc.motion.speed_over_life = math::gradient<float>();
    desc.motion.speed_over_life.add_point(1.0f, 0.0f);
    desc.motion.speed_over_life.add_point(0.0f, 1.0f);
    ps_soa::lut_set luts;
    ps_soa::bake_lut_set(desc, desc.bake_features(), luts);
    const auto record = make_record(math::vec3(0.0f), distance * (2.0f / lifetime), lifetime);
    bool matches = true;
    for(float life : {0.0f, 0.2f, 0.5f, 0.8f, 0.99f})
    {
        const float age = life * lifetime;
        const auto state = ps_soa::evaluate_particle(record, make_params(desc, age), luts);
        const math::vec3 gravity_offset(0.0f, -k_standard_gravity * old_gravity * lifetime * lifetime, 0.0f);
        const math::vec3 expected = distance * (2.0f * life - life * life) + gravity_offset * life * life;
        matches = matches && is_near(state.position, expected, 1.0e-3f);
    }
    check(matches, "a converted old scene moves exactly as the old curve did");
}

void test_legacy_scene_loads_converted()
{
    const std::string document = R"({"particle": {
        "lifetime": {"count": 2.0},
        "emission_lifetime": {"count": 3.0},
        "gravity_scale": 0.5,
        "velocity_damping": 0.25,
        "size_by_speed_velocity_range": {"min": 1.0, "max": 3.0},
        "velocity_gradient": {"linked": true, "channels": {
            "value0": {"keys": [{"position": 0.0, "value": 4.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 4.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]},
            "value1": {"keys": [{"position": 0.0, "value": 6.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 6.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]}}}}})";
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<particle_emitter_component>(entity);
    particle_emitter_component::on_create_component(registry, entity);
    auto& emitter = registry.get<particle_emitter_component>(entity);
    std::stringstream stream(document);
    {
        ser20::iarchive_associative_t ar(stream);
        check(try_load(ar, ser20::make_nvp("particle", emitter)), "an old particle emitter loads");
    }
    const float tolerance = 1.0e-4f;
    check(std::abs(emitter.get_lifetime().min - 2.0f) < tolerance && std::abs(emitter.get_lifetime().max - 2.0f) < tolerance,
          "its lifetime becomes the lifetime range");
    check(std::abs(emitter.get_duration().count() - 3.0f) < tolerance, "emission_lifetime becomes the duration");
    check(std::abs(emitter.get_start_speed().min - 3.0f) < tolerance && std::abs(emitter.get_start_speed().max - 4.5f) < tolerance,
          "the launch speed is twice the old distance (less damping) per lifetime");
    check(std::abs(emitter.get_speed_over_life().sample(0.0f) - 1.0f) < tolerance &&
              std::abs(emitter.get_speed_over_life().sample(1.0f)) < tolerance,
          "the speed slows linearly to a stop, as the old curve did");
    check(std::abs(emitter.get_gravity_scale() - 1.0f) < tolerance, "gravity doubles");
    check(std::abs(emitter.get_size_by_speed_speed_range().max - 6.0f) < tolerance,
          "speed ranges double (the old speed read half the true one)");
    particle_emitter_component::on_destroy_component(registry, entity);
}

void test_slot_allocator()
{
    ps_soa::particle_slot_allocator slots;
    slots.reset(130);
    for(uint32_t i = 0; i < 130; ++i)
    {
        slots.allocate();
    }
    check(slots.allocate() == 130, "a full allocator returns its capacity");
    slots.release(5);
    slots.release(70);
    check(slots.allocate() == 5, "the lowest free slot is handed out first");
    check(slots.get_high_water() == 130, "the high water stays while the top slot lives");
    for(uint32_t i = 129; i >= 60; --i)
    {
        slots.release(i);
    }
    check(slots.get_high_water() == 60, "the high water falls to the highest live slot");
    check(slots.get_used_count() == 60, "the used count follows");

    ps_soa::particle_death_queue deaths;
    deaths.push(30, 1);
    deaths.push(10, 2);
    deaths.push(20, 3);
    std::vector<uint32_t> order;
    deaths.pop_dead(25,
                    [&](const ps_soa::particle_death_queue::entry& dead)
                    {
                        order.push_back(dead.slot);
                    });
    check(order.size() == 2 && order[0] == 2 && order[1] == 3 && deaths.size() == 1, "deaths come out in death order, up to now");
}

struct emitter_run
{
    ps_soa::emitter_handle handle{};
    ps_soa::emitter_desc desc{};
    ps_soa::emitter_transform_state transform{};
};

auto make_run(uint32_t capacity, const ps_soa::emitter_desc& desc) -> emitter_run
{
    emitter_run run;
    run.handle = ps_soa::create_emitter(capacity);
    run.desc = desc;
    return run;
}

void step(emitter_run& run, float dt)
{
    ps_soa::update_emitter(run.handle, dt, run.desc, run.transform, run.desc.playback);
}

auto base_desc() -> ps_soa::emitter_desc
{
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.emission.particles_per_second = 30.0f;
    desc.motion.lifetime = frange_t(1.0f, 1.5f);
    desc.emission.shape = ps_soa::emitter_shape::sphere;
    desc.emission.direction = ps_soa::emitter_direction::outward;
    return desc;
}

auto positions(ps_soa::emitter_handle handle) -> std::vector<math::vec3>
{
    std::vector<ps_soa::particle_state> states;
    ps_soa::get_particle_states(handle, states);
    std::vector<math::vec3> result;
    for(const auto& state : states)
    {
        result.push_back(state.position);
    }
    return result;
}

void test_seeds()
{
    auto a = make_run(64, base_desc());
    auto b = make_run(64, base_desc());
    for(int i = 0; i < 20; ++i)
    {
        step(a, k_frame_seconds);
        step(b, k_frame_seconds);
    }
    check(positions(a.handle) != positions(b.handle), "two identical emitters started together play different particles");
    ps_soa::destroy_emitter(a.handle);
    ps_soa::destroy_emitter(b.handle);

    auto fixed = base_desc();
    fixed.emission.use_fixed_seed = true;
    fixed.emission.random_seed = 42;
    auto c = make_run(64, fixed);
    auto d = make_run(64, fixed);
    for(int i = 0; i < 20; ++i)
    {
        step(c, k_frame_seconds);
        step(d, k_frame_seconds);
    }
    const auto first = positions(c.handle);
    check(!first.empty() && first == positions(d.handle), "a fixed seed replays the same particles in every emitter");
    ps_soa::reset_emitter(c.handle);
    for(int i = 0; i < 20; ++i)
    {
        step(c, k_frame_seconds);
    }
    check(first == positions(c.handle), "a reset emitter with a fixed seed replays its particles");
    ps_soa::destroy_emitter(c.handle);
    ps_soa::destroy_emitter(d.handle);
}

void test_duration()
{
    auto desc = base_desc();
    desc.emission.loop = false;
    desc.emission.duration = 0.5f;
    desc.emission.particles_per_second = 20.0f;
    desc.motion.lifetime = frange_t(0.3f, 0.3f);
    auto run = make_run(256, desc);
    uint32_t peak = 0;
    int frames = 0;
    while(run.desc.playback.playing && frames < 600)
    {
        step(run, k_frame_seconds);
        peak = math::max(peak, ps_soa::get_num_particles(run.handle));
        ++frames;
    }
    // 0.5 s at 20/s is about 10 particles, each living 0.3 s: at most 6-7 alive at once, and the
    // emitter stops once the last one dies (0.75-0.8 s), not when the capacity is used up.
    check(peak >= 5 && peak <= 7, "a one-shot emitter emits for its duration");
    check(!run.desc.playback.playing && frames > 40 && frames < 55, "it stops when its last particle is gone");
    ps_soa::destroy_emitter(run.handle);
}

void test_sub_frame_ages()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 600.0f;
    auto run = make_run(1024, desc);
    step(run, 1.0f / 30.0f);
    std::vector<ps_soa::particle_state> states;
    ps_soa::get_particle_states(run.handle, states);
    float youngest = 1.0f;
    float oldest = 0.0f;
    for(const auto& state : states)
    {
        youngest = math::min(youngest, state.age);
        oldest = math::max(oldest, state.age);
    }
    check(states.size() == 20, "600 per second over 1/30 s spawns 20");
    check(oldest - youngest > 0.025f, "particles of one frame are born at their own times, not all at its end");
    ps_soa::destroy_emitter(run.handle);
}

/// Authored bursts fire their counts at their cycle times (a one-shot emitter with no rate emits only them),
/// and rate over distance spaces its particles along the emitter's path.
void test_bursts_and_distance()
{
    auto desc = base_desc();
    desc.emission.loop = false;
    desc.emission.duration = 1.0f;
    desc.emission.particles_per_second = 0.0f;
    desc.motion.lifetime = frange_t(5.0f, 5.0f);
    ps_soa::emitter_burst burst;
    burst.time = 0.2f;
    burst.count = frange_t(5.0f, 5.0f);
    burst.cycles = 3;
    burst.interval = 0.25f;
    desc.emission.bursts.push_back(burst);
    auto run = make_run(256, desc);
    // Firings at 0.2, 0.45 and 0.7 s.
    for(int i = 0; i < 30; ++i)
    {
        step(run, k_frame_seconds);
    }
    check(ps_soa::get_num_particles(run.handle) == 10, "two of three firings by 0.5 s");
    for(int i = 0; i < 60; ++i)
    {
        step(run, k_frame_seconds);
    }
    check(ps_soa::get_num_particles(run.handle) == 15, "three firings of five per cycle");
    std::vector<ps_soa::particle_state> states;
    ps_soa::get_particle_states(run.handle, states);
    float oldest = 0.0f;
    for(const auto& state : states)
    {
        oldest = math::max(oldest, state.age);
    }
    check(std::abs(oldest - (1.5f - 0.2f)) < 0.01f, "a burst's particles are born at its time, not at the frame's end");
    ps_soa::destroy_emitter(run.handle);

    auto trail = base_desc();
    trail.emission.particles_per_second = 0.0f;
    trail.emission.particles_per_meter = 4.0f;
    trail.motion.lifetime = frange_t(5.0f, 5.0f);
    trail.emission.shape_scale = math::vec3(0.0f);
    auto moving = make_run(512, trail);
    step(moving, k_frame_seconds);
    for(int i = 1; i <= 10; ++i)
    {
        moving.transform.current.set_position(math::vec3(float(i), 0.0f, 0.0f));
        step(moving, k_frame_seconds);
    }
    check(ps_soa::get_num_particles(moving.handle) == 40, "4 per metre over 10 m spawns 40");
    ps_soa::get_particle_states(moving.handle, states);
    float leftmost = 100.0f;
    for(const auto& state : states)
    {
        leftmost = math::min(leftmost, state.position.x);
    }
    check(std::abs(leftmost - 0.25f) < 0.01f, "distance spawns are spaced along the path");
    ps_soa::destroy_emitter(moving.handle);
}

/// A cone's rim launches at the cone angle, a torus spawns on its tube, a mesh on its triangles.
void test_shapes()
{
    auto states = std::vector<ps_soa::particle_state>{};
    auto cone = base_desc();
    cone.emission.shape = ps_soa::emitter_shape::cone;
    cone.emission.spawn_location = ps_soa::spawn_location::surface;
    cone.emission.direction = ps_soa::emitter_direction::up;
    cone.emission.cone_angle = 30.0f;
    cone.motion.start_speed = frange_t(2.0f, 2.0f);
    auto cone_run = make_run(256, cone);
    for(int i = 0; i < 30; ++i)
    {
        step(cone_run, k_frame_seconds);
    }
    ps_soa::get_particle_states(cone_run.handle, states);
    bool is_on_slant = !states.empty();
    for(const auto& state : states)
    {
        is_on_slant = is_on_slant && std::abs(math::normalize(state.velocity).y - std::cos(30.0f * 3.14159265f / 180.0f)) < 1.0e-3f;
    }
    check(is_on_slant, "a cone's rim launches at the cone angle");
    ps_soa::destroy_emitter(cone_run.handle);

    auto torus = base_desc();
    torus.emission.shape = ps_soa::emitter_shape::torus;
    torus.emission.spawn_location = ps_soa::spawn_location::surface;
    torus.emission.torus_thickness = 0.2f;
    torus.motion.start_speed = frange_t(0.0f, 0.0f);
    auto torus_run = make_run(256, torus);
    for(int i = 0; i < 30; ++i)
    {
        step(torus_run, k_frame_seconds);
    }
    ps_soa::get_particle_states(torus_run.handle, states);
    bool is_on_tube = !states.empty();
    for(const auto& state : states)
    {
        const math::vec2 flat(state.position.x, state.position.z);
        const math::vec2 ring = math::normalize(flat);
        const float tube = math::length(math::vec3(flat.x - ring.x, state.position.y, flat.y - ring.y));
        is_on_tube = is_on_tube && std::abs(tube - 0.2f) < 1.0e-3f;
    }
    check(is_on_tube, "a torus surface spawns on its tube");
    ps_soa::destroy_emitter(torus_run.handle);

    auto triangle = std::make_shared<ps_soa::emission_mesh>();
    triangle->positions = {math::vec3(0.0f, 0.5f, 0.0f), math::vec3(1.0f, 0.5f, 0.0f), math::vec3(0.0f, 0.5f, 1.0f)};
    triangle->normals = {math::vec3(0.0f, 1.0f, 0.0f), math::vec3(0.0f, 1.0f, 0.0f), math::vec3(0.0f, 1.0f, 0.0f)};
    triangle->indices = {0, 1, 2};
    triangle->cumulative_area = {0.5f};
    triangle->bounds = math::bbox(math::vec3(0.0f, 0.5f, 0.0f), math::vec3(1.0f, 0.5f, 1.0f));
    auto mesh_desc = base_desc();
    mesh_desc.emission.shape = ps_soa::emitter_shape::mesh;
    mesh_desc.emission.mesh = triangle;
    mesh_desc.motion.start_speed = frange_t(0.0f, 0.0f);
    auto mesh_run = make_run(256, mesh_desc);
    for(int i = 0; i < 30; ++i)
    {
        step(mesh_run, k_frame_seconds);
    }
    ps_soa::get_particle_states(mesh_run.handle, states);
    bool is_on_triangle = !states.empty();
    for(const auto& state : states)
    {
        const math::vec3 p = state.position;
        is_on_triangle = is_on_triangle && std::abs(p.y - 0.5f) < 1.0e-4f && p.x >= -1.0e-4f && p.z >= -1.0e-4f &&
                         p.x + p.z <= 1.0f + 1.0e-4f;
    }
    check(is_on_triangle, "a mesh shape spawns on its triangles");
    ps_soa::destroy_emitter(mesh_run.handle);
}

/// The turbulence noise tiles, displaces a particle by at most twice the strength, and the bounds hold it.
void test_turbulence()
{
    const auto& noise = ps_soa::get_particle_noise();
    const math::vec3 probe(0.31f, 0.72f, 0.13f);
    check(math::length(noise.sample(probe) - noise.sample(probe + math::vec3(1.0f, -1.0f, 2.0f))) < 1.0e-5f,
          "the turbulence noise tiles");
    constexpr float strength = 0.75f;
    auto calm = base_desc();
    calm.emission.use_fixed_seed = true;
    calm.emission.random_seed = 9u;
    calm.motion.lifetime = frange_t(3.0f, 3.0f);
    auto stormy = calm;
    stormy.motion.turbulence_strength = strength;
    stormy.motion.turbulence_frequency = 2.0f;
    stormy.motion.turbulence_speed = 1.5f;
    auto calm_run = make_run(512, calm);
    auto stormy_run = make_run(512, stormy);
    bool is_bounded = true;
    bool is_contained = true;
    float largest = 0.0f;
    for(int i = 0; i < 120; ++i)
    {
        step(calm_run, k_frame_seconds);
        step(stormy_run, k_frame_seconds);
        const auto calm_positions = positions(calm_run.handle);
        const auto stormy_positions = positions(stormy_run.handle);
        if(calm_positions.size() != stormy_positions.size())
        {
            is_bounded = false;
            break;
        }
        math::bbox bounds;
        ps_soa::get_aabb(stormy_run.handle, bounds);
        for(size_t p = 0; p < calm_positions.size(); ++p)
        {
            const float moved = math::length(stormy_positions[p] - calm_positions[p]);
            largest = math::max(largest, moved);
            is_bounded = is_bounded && moved <= 2.0f * strength + 1.0e-3f;
            is_contained = is_contained && bounds.contains_point(stormy_positions[p]);
        }
    }
    check(is_bounded && largest > 0.05f, "turbulence displaces particles, by at most twice its strength");
    check(is_contained, "the bounds hold every turbulent particle");
    ps_soa::destroy_emitter(calm_run.handle);
    ps_soa::destroy_emitter(stormy_run.handle);
}

/// Collision planes kill or bounce particles where their paths first cross them, and the emitter reports
/// births, deaths and contacts where they happened.
void test_collision_and_events()
{
    constexpr float floor = -1.0f;
    constexpr float tolerance = 2.0e-3f;
    auto desc = base_desc();
    desc.emission.direction = ps_soa::emitter_direction::up;
    desc.emission.shape_scale = math::vec3(0.0f);
    desc.motion.start_speed = frange_t(1.0f, 2.0f);
    desc.motion.gravity_scale = 1.0f;
    desc.motion.lifetime = frange_t(4.0f, 4.0f);
    desc.motion.collision = ps_soa::collision_mode::kill;
    desc.motion.collision_planes = {math::vec4(0.0f, 1.0f, 0.0f, floor)};
    // The particle's centre touches the plane: the plane itself is where the contacts land.
    desc.motion.collision_radius_scale = 0.0f;
    desc.event_mask = uint32_t(ps_soa::particle_event_type::birth) | uint32_t(ps_soa::particle_event_type::death) |
                      uint32_t(ps_soa::particle_event_type::collision);
    auto kill = make_run(256, desc);
    std::vector<ps_soa::particle_event> events;
    bool stays_above = true;
    for(int i = 0; i < 180; ++i)
    {
        step(kill, k_frame_seconds);
        for(const auto& position : positions(kill.handle))
        {
            stays_above = stays_above && position.y >= floor - tolerance;
        }
        ps_soa::take_particle_events(kill.handle, events);
    }
    int births = 0;
    int deaths = 0;
    int contacts = 0;
    bool contacts_on_floor = true;
    for(const auto& event : events)
    {
        births += event.type == ps_soa::particle_event_type::birth ? 1 : 0;
        deaths += event.type == ps_soa::particle_event_type::death ? 1 : 0;
        if(event.type == ps_soa::particle_event_type::collision)
        {
            ++contacts;
            contacts_on_floor = contacts_on_floor && std::abs(event.position.y - floor) < tolerance;
        }
    }
    check(stays_above, "killed particles never go below their plane");
    check(contacts > 10 && contacts_on_floor, "contacts are reported on the plane");
    check(births > 0 && deaths == contacts && births >= deaths, "every kill is a death at the contact");
    ps_soa::destroy_emitter(kill.handle);

    desc.motion.collision = ps_soa::collision_mode::bounce;
    desc.motion.restitution = 0.6f;
    desc.motion.max_bounces = 2;
    auto bounce = make_run(256, desc);
    bool bounces_stay_above = true;
    bool rises_after_contact = false;
    events.clear();
    for(int i = 0; i < 240; ++i)
    {
        step(bounce, k_frame_seconds);
        std::vector<ps_soa::particle_state> states;
        ps_soa::get_particle_states(bounce.handle, states);
        for(const auto& state : states)
        {
            bounces_stay_above = bounces_stay_above && state.position.y >= floor - tolerance;
            rises_after_contact = rises_after_contact || (state.position.y < floor + 0.05f && state.velocity.y > 0.5f);
        }
        ps_soa::take_particle_events(bounce.handle, events);
    }
    check(bounces_stay_above && rises_after_contact, "bounced particles leave their plane upward");
    ps_soa::destroy_emitter(bounce.handle);

    // With a radius, a particle touches the plane with its edge: half its size above it.
    constexpr float size = 0.4f;
    desc.appearance.size_over_life = math::gradient<frange_t>();
    desc.appearance.size_over_life.add_point(frange_t(size, size), 0.0f);
    desc.appearance.size_over_life.add_point(frange_t(size, size), 1.0f);
    desc.motion.collision = ps_soa::collision_mode::kill;
    desc.motion.collision_radius_scale = 1.0f;
    auto edge = make_run(256, desc);
    events.clear();
    for(int i = 0; i < 180; ++i)
    {
        step(edge, k_frame_seconds);
        ps_soa::take_particle_events(edge.handle, events);
    }
    int edge_contacts = 0;
    bool contacts_at_edge = true;
    for(const auto& event : events)
    {
        if(event.type == ps_soa::particle_event_type::collision)
        {
            ++edge_contacts;
            contacts_at_edge = contacts_at_edge && std::abs(event.position.y - (floor + 0.5f * size)) < tolerance;
        }
    }
    check(edge_contacts > 10 && contacts_at_edge, "with a radius, contacts land half the particle's size above the plane");
    ps_soa::destroy_emitter(edge.handle);
}

void test_catch_up()
{
    auto desc = base_desc();
    desc.motion.lifetime = frange_t(2.0f, 2.0f);
    desc.emission.use_fixed_seed = true;
    auto awake = make_run(256, desc);
    auto frozen = make_run(256, desc);
    for(int i = 0; i < 30; ++i)
    {
        step(awake, k_frame_seconds);
        step(frozen, k_frame_seconds);
    }
    for(int i = 0; i < 90; ++i)
    {
        step(awake, k_frame_seconds);
        ps_soa::update_emitter_frozen(frozen.handle, k_frame_seconds, frozen.desc, frozen.transform, true);
    }
    step(awake, k_frame_seconds);
    step(frozen, k_frame_seconds);
    const uint32_t awake_count = ps_soa::get_num_particles(awake.handle);
    const uint32_t woken_count = ps_soa::get_num_particles(frozen.handle);
    check(awake_count > 50 && woken_count + 2 >= awake_count && woken_count <= awake_count + 2,
          "an emitter woken after a culled freeze catches up to the one that kept running");
    ps_soa::destroy_emitter(awake.handle);
    ps_soa::destroy_emitter(frozen.handle);
}

void test_capacity()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 1000.0f;
    desc.motion.lifetime = frange_t(5.0f, 5.0f);
    auto run = make_run(50, desc);
    for(int i = 0; i < 30; ++i)
    {
        step(run, k_frame_seconds);
    }
    check(ps_soa::get_num_particles(run.handle) == 50, "an emitter never holds more than its capacity");
    ps_soa::destroy_emitter(run.handle);
}

void test_bounds_hold_every_particle()
{
    struct bounds_case
    {
        ps_soa::emitter_shape shape;
        ps_soa::emitter_direction direction;
        float gravity;
        float drag;
        bx::Easing::Enum easing;
        bool speed_curve;
        ps_soa::simulation_space space;
        float move_speed;
    };
    const bounds_case cases[] = {
        {ps_soa::emitter_shape::sphere, ps_soa::emitter_direction::outward, 0.0f, 0.0f, bx::Easing::Linear, false, ps_soa::simulation_space::world, 0.0f},
        {ps_soa::emitter_shape::box, ps_soa::emitter_direction::up, 1.0f, 0.0f, bx::Easing::Linear, false, ps_soa::simulation_space::world, 4.0f},
        {ps_soa::emitter_shape::hemisphere, ps_soa::emitter_direction::outward, -0.3f, 2.0f, bx::Easing::OutBack, false, ps_soa::simulation_space::world, 0.0f},
        {ps_soa::emitter_shape::circle, ps_soa::emitter_direction::inward, 0.5f, 0.0f, bx::Easing::InOutElastic, true, ps_soa::simulation_space::local, 2.0f},
        {ps_soa::emitter_shape::rect, ps_soa::emitter_direction::up, 2.0f, 0.5f, bx::Easing::Linear, true, ps_soa::simulation_space::world, 6.0f},
    };
    bool holds = true;
    int case_index = 0;
    for(const auto& bounds : cases)
    {
        auto desc = base_desc();
        desc.emission.shape = bounds.shape;
        desc.emission.direction = bounds.direction;
        desc.emission.direction_spread = 30.0f;
        desc.emission.shape_scale = math::vec3(2.0f, 1.0f, 3.0f);
        desc.motion.gravity_scale = bounds.gravity;
        desc.motion.drag = bounds.drag;
        desc.motion.motion_easing = bounds.easing;
        desc.motion.space = bounds.space;
        desc.motion.start_speed = frange_t(1.0f, 5.0f);
        desc.motion.inherit_velocity = 0.5f;
        if(bounds.speed_curve)
        {
            desc.motion.speed_over_life = math::gradient<float>();
            desc.motion.speed_over_life.add_point(2.0f, 0.0f);
            desc.motion.speed_over_life.add_point(-0.5f, 1.0f);
        }
        auto run = make_run(512, desc);
        for(int frame = 0; frame < 240; ++frame)
        {
            const float t = float(frame) * k_frame_seconds;
            run.transform.current.set_position(math::vec3(bounds.move_speed * t, 0.0f, 0.0f));
            run.transform.current.set_rotation(math::angleAxis(t * 0.7f, math::vec3(0.0f, 1.0f, 0.0f)));
            step(run, k_frame_seconds);
            math::bbox box;
            ps_soa::get_aabb(run.handle, box);
            std::vector<ps_soa::particle_state> states;
            std::vector<float> sizes;
            ps_soa::get_particle_states(run.handle, states);
            ps_soa::get_particle_sizes(run.handle, sizes);
            for(size_t i = 0; i < states.size(); ++i)
            {
                const math::vec3 pad(sizes[i] * 0.5f);
                const math::vec3 low = states[i].position - pad - box.min;
                const math::vec3 high = box.max - (states[i].position + pad);
                const bool inside = math::all(math::greaterThanEqual(low, math::vec3(-1.0e-3f))) &&
                                    math::all(math::greaterThanEqual(high, math::vec3(-1.0e-3f)));
                if(!inside && holds)
                {
                    std::printf("    case %d frame %d: particle at %.3f %.3f %.3f outside [%.3f %.3f %.3f]..[%.3f %.3f %.3f]\n",
                                case_index,
                                frame,
                                states[i].position.x,
                                states[i].position.y,
                                states[i].position.z,
                                box.min.x,
                                box.min.y,
                                box.min.z,
                                box.max.x,
                                box.max.y,
                                box.max.z);
                }
                holds = holds && inside;
            }
        }
        ps_soa::destroy_emitter(run.handle);
        ++case_index;
    }
    check(holds, "the analytic bounds hold every particle (shapes, gravity, drag, easing, speed curve, moving emitters)");
}

/// The per-axis size, spin, intensity and two-gradient curves against their closed forms.
void test_appearance_curves()
{
    constexpr float lifespan = 2.0f;
    constexpr float age = 1.5f;
    constexpr float spin = 3.0f;
    constexpr float tolerance = 1.0e-3f;
    ps_soa::emitter_desc desc;
    desc.reset();
    ps_soa::particle_record record = make_record(math::vec3(0.0f), math::vec3(0.0f), lifespan);
    record.seed = 12345u;
    const auto evaluate = [&](const ps_soa::emitter_desc& d, math::vec2& quad, float& angle, math::vec4& color)
    {
        auto luts = std::make_unique<ps_soa::lut_set>();
        ps_soa::bake_lut_set(d, d.bake_features(), *luts);
        ps_soa::emitter_eval_params params = make_params(d, age);
        params.spin = math::vec2(spin);
        params.rotation = math::vec2(0.0f);
        const ps_soa::particle_state state = ps_soa::evaluate_particle(record, params, *luts);
        quad = ps_soa::evaluate_particle_quad(record, state, params, *luts);
        angle = ps_soa::evaluate_particle_angle(record, state, params, *luts);
        color = ps_soa::evaluate_particle_color(record, state, params, *luts);
    };
    math::vec2 base_quad;
    float base_angle = 0.0f;
    math::vec4 base_color;
    evaluate(desc, base_quad, base_angle, base_color);
    check(std::abs(base_angle - spin * age) < tolerance, "constant spin turns spin x age");
    ps_soa::emitter_desc curved = desc;
    curved.appearance.size_axes_over_life = math::gradient<math::vec3>();
    curved.appearance.size_axes_over_life.add_point(math::vec3(2.0f, 0.5f, 4.0f), 0.0f);
    curved.appearance.size_axes_over_life.add_point(math::vec3(2.0f, 0.5f, 4.0f), 1.0f);
    curved.appearance.intensity_over_life = ps_soa::make_flat_curve(3.0f);
    // A spin curve rising 0 -> 1 over life turns by spin x lifespan x life^2 / 2.
    curved.render.spin_over_life = math::gradient<float>();
    curved.render.spin_over_life.add_point(0.0f, 0.0f);
    curved.render.spin_over_life.add_point(1.0f, 1.0f);
    math::vec2 quad;
    float angle = 0.0f;
    math::vec4 color;
    evaluate(curved, quad, angle, color);
    const float life = age / lifespan;
    check(std::abs(quad.x - base_quad.x * 2.0f) < tolerance && std::abs(quad.y - base_quad.y * 0.5f) < tolerance,
          "the size axes curve scales the width and the height separately");
    check(std::abs(angle - spin * lifespan * life * life * 0.5f) < tolerance, "spin curve turns by its integral");
    check(std::abs(color.r - base_color.r * 3.0f) < tolerance && std::abs(color.a - base_color.a) < tolerance,
          "intensity curve scales the light, not the opacity");
    ps_soa::emitter_desc mixed = desc;
    mixed.appearance.color_gradient = math::gradient<math::color>();
    mixed.appearance.color_gradient.add_point(math::color(1.0f, 1.0f, 1.0f, 1.0f), 0.0f);
    mixed.appearance.color_gradient.add_point(math::color(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
    mixed.appearance.color_gradient_b = math::gradient<math::color>();
    mixed.appearance.color_gradient_b.add_point(math::color(0.0f, 0.0f, 0.0f, 1.0f), 0.0f);
    mixed.appearance.color_gradient_b.add_point(math::color(0.0f, 0.0f, 0.0f, 1.0f), 1.0f);
    mixed.appearance.random_between_gradients = true;
    evaluate(mixed, quad, angle, color);
    const float pick = ps_soa::particle_random(record.seed, ps_soa::salt_gradient);
    check(std::abs(color.r - (1.0f - pick)) < tolerance, "two gradients mix by the particle's own pick");
}

/// Mesh particles: each matrix sits at its particle, scaled by its size times the 3D scale, its +Y along the
/// motion when aligned; last frame's matrix sits one frame back along the path.
void test_mesh_transforms()
{
    constexpr float tolerance = 2.0e-3f;
    const math::vec3 size_axes(0.5f, 2.0f, 1.0f);
    auto desc = base_desc();
    desc.render.render_mode = ps_soa::render_mode::mesh;
    desc.render.align_to_direction = true;
    desc.appearance.size_axes = size_axes;
    desc.emission.direction = ps_soa::emitter_direction::outward;
    desc.motion.start_speed = frange_t(2.0f, 3.0f);
    desc.motion.lifetime = frange_t(2.0f, 2.0f);
    auto run = make_run(256, desc);
    for(int frame = 0; frame < 30; ++frame)
    {
        step(run, k_frame_seconds);
    }
    std::vector<math::mat4> current;
    std::vector<math::mat4> previous;
    std::vector<float> opacity;
    std::vector<ps_soa::particle_state> states;
    std::vector<float> sizes;
    ps_soa::collect_particle_mesh_transforms(run.handle, current, previous, opacity);
    ps_soa::get_particle_states(run.handle, states);
    ps_soa::get_particle_sizes(run.handle, sizes);
    check(!current.empty() && current.size() == states.size() && previous.size() == states.size() &&
              opacity.size() == states.size(),
          "a matrix and an opacity per live particle, this frame and the last");
    bool is_placed = true;
    bool is_scaled = true;
    bool is_aligned = true;
    bool is_previous_back = true;
    for(size_t i = 0; i < states.size() && i < current.size(); ++i)
    {
        const math::mat4& transform = current[i];
        is_placed = is_placed && is_near(math::vec3(transform[3]), states[i].position, tolerance);
        is_scaled = is_scaled && std::abs(math::length(math::vec3(transform[0])) - sizes[i] * size_axes.x) < tolerance &&
                    std::abs(math::length(math::vec3(transform[1])) - sizes[i] * size_axes.y) < tolerance &&
                    std::abs(math::length(math::vec3(transform[2])) - sizes[i] * size_axes.z) < tolerance;
        const math::vec3 up = math::normalize(math::vec3(transform[1]));
        is_aligned = is_aligned && math::dot(up, math::normalize(states[i].velocity)) > 1.0f - tolerance;
        // No gravity, drag or curve: the path is a line, so last frame's place is exactly one frame back.
        if(states[i].age > k_frame_seconds)
        {
            const math::vec3 expected = states[i].position - states[i].velocity * k_frame_seconds;
            is_previous_back = is_previous_back && is_near(math::vec3(previous[i][3]), expected, tolerance);
        }
    }
    check(is_placed, "mesh matrices sit at their particles");
    check(is_scaled, "mesh matrices scale by size x the 3D scale");
    check(is_aligned, "aligned meshes point +Y along the motion");
    check(is_previous_back, "last frame's mesh matrices sit one frame back along the path");
    ps_soa::destroy_emitter(run.handle);
}

/// The size axes curve's z sizes the depth of mesh particles, its x and y their width and height.
void test_mesh_size_axes_curve()
{
    constexpr float tolerance = 2.0e-3f;
    const math::vec3 axes_over_life(1.5f, 0.5f, 3.0f);
    auto desc = base_desc();
    desc.render.render_mode = ps_soa::render_mode::mesh;
    desc.appearance.size_axes_over_life = math::gradient<math::vec3>();
    desc.appearance.size_axes_over_life.add_point(axes_over_life, 0.0f);
    desc.appearance.size_axes_over_life.add_point(axes_over_life, 1.0f);
    auto run = make_run(64, desc);
    for(int frame = 0; frame < 10; ++frame)
    {
        step(run, k_frame_seconds);
    }
    std::vector<math::mat4> current;
    std::vector<math::mat4> previous;
    std::vector<float> opacity;
    std::vector<float> sizes;
    ps_soa::collect_particle_mesh_transforms(run.handle, current, previous, opacity);
    ps_soa::get_particle_sizes(run.handle, sizes);
    bool is_scaled = !current.empty();
    for(size_t i = 0; i < current.size() && i < sizes.size(); ++i)
    {
        is_scaled = is_scaled && std::abs(math::length(math::vec3(current[i][0])) - sizes[i] * axes_over_life.x) < tolerance &&
                    std::abs(math::length(math::vec3(current[i][1])) - sizes[i] * axes_over_life.y) < tolerance &&
                    std::abs(math::length(math::vec3(current[i][2])) - sizes[i] * axes_over_life.z) < tolerance;
    }
    check(is_scaled, "mesh particles scale by every axis of the size axes curve, z included");
    ps_soa::destroy_emitter(run.handle);
}

/// Scenes saved with the old names: a renamed key loads from its former name, and the two quad axis curves become
/// the x and y channels of the size axes curve.
void test_renamed_keys_load()
{
    const std::string document = R"({"particle": {
        "start_speed": {"min": 1.0, "max": 1.0},
        "scale_gradient": {"linked": true, "channels": {
            "value0": {"keys": [{"position": 0.0, "value": 0.7, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 0.7, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]},
            "value1": {"keys": [{"position": 0.0, "value": 0.9, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 0.9, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]}}},
        "force_over_lifetime": {"x": 1.0, "y": 2.0, "z": 3.0},
        "rotation_speed": {"min": 10.0, "max": 20.0},
        "size_x_over_life": {"linked": true, "channels": {
            "value0": {"keys": [{"position": 0.0, "value": 2.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 4.0, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]}}},
        "size_y_over_life": {"linked": true, "channels": {
            "value0": {"keys": [{"position": 0.0, "value": 0.5, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0},
                                {"position": 1.0, "value": 0.5, "in_tangent": 0.0, "out_tangent": 0.0, "interpolation": 1, "tangent_mode": 0}]}}},
        "render_mode": 4,
        "billboard_mode": 1}})";
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<particle_emitter_component>(entity);
    particle_emitter_component::on_create_component(registry, entity);
    auto& emitter = registry.get<particle_emitter_component>(entity);
    std::stringstream stream(document);
    {
        ser20::iarchive_associative_t ar(stream);
        check(try_load(ar, ser20::make_nvp("particle", emitter)), "a particle emitter with the old names loads");
    }
    const float tolerance = 1.0e-4f;
    const frange_t size = emitter.get_size_over_life().sample(0.5f);
    check(std::abs(size.min - 0.7f) < tolerance && std::abs(size.max - 0.9f) < tolerance,
          "scale_gradient loads into size_over_life");
    check(is_near(emitter.get_acceleration(), math::vec3(1.0f, 2.0f, 3.0f), tolerance),
          "force_over_lifetime loads into acceleration");
    check(std::abs(emitter.get_spin_speed().min - 10.0f) < tolerance && std::abs(emitter.get_spin_speed().max - 20.0f) < tolerance,
          "rotation_speed loads into spin_speed");
    const math::vec3 axes = emitter.get_size_axes_over_life().sample(0.5f);
    check(is_near(axes, math::vec3(3.0f, 0.5f, 1.0f), tolerance),
          "the x and y size curves become the size axes curve's channels, z stays 1");
    check(emitter.get_render_mode() == ps_soa::render_mode::stretched,
          "billboard_mode does not override a render_mode saved next to it");
    particle_emitter_component::on_destroy_component(registry, entity);
}

/// A quad stretched against the camera's velocity stays inside its cull sphere widened by the camera reach,
/// whatever the directions: |v - c| <= |v| + |c|.
void test_camera_stretch_stays_culled_conservatively()
{
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.render.render_mode = ps_soa::render_mode::stretched;
    desc.render.stretch_speed_scale = 0.2f;
    desc.appearance.size_axes = math::vec3(1.0f, 2.0f, 1.0f);
    auto luts = std::make_unique<ps_soa::lut_set>();
    ps_soa::bake_lut_set(desc, desc.bake_features(), *luts);
    ps_soa::emitter_eval_params params = make_params(desc, 0.5f);
    params.render_mode = ps_soa::render_mode::stretched;
    params.stretch_length_scale = 1.0f;
    params.stretch_speed_scale = desc.render.stretch_speed_scale;
    params.stretch_camera_velocity_scale = 1.0f;
    params.size_axes = desc.appearance.size_axes;
    const math::vec3 cameras[] = {math::vec3(8.0f, 0.0f, 0.0f), math::vec3(0.0f, 30.0f, 0.0f), math::vec3(-5.0f, 5.0f, 5.0f)};
    const math::vec3 launches[] = {math::vec3(0.0f, -8.0f, 0.0f), math::vec3(3.0f, 0.0f, 1.0f)};
    bool is_covered = true;
    // An off-centre pivot pushes the whole stretched quad away from its particle, the camera's share included.
    for(const float pivot_pad : {1.0f, 1.8f})
    {
        params.pivot_pad = pivot_pad;
        for(const auto& launch : launches)
        {
            const ps_soa::particle_record record = make_record(math::vec3(0.0f), launch, 2.0f);
            const ps_soa::particle_state state = ps_soa::evaluate_particle(record, params, *luts);
            const math::vec2 quad = ps_soa::evaluate_particle_quad(record, state, params, *luts);
            const float size = ps_soa::evaluate_particle_size(record, state, params, *luts);
            for(const auto& camera : cameras)
            {
                const float stretched =
                    size * params.stretch_length_scale + math::length(state.velocity - camera) * params.stretch_speed_scale;
                const float needed =
                    0.5f * pivot_pad * math::length(math::vec2(quad.x * params.size_axes.x, stretched * params.size_axes.y));
                const float radius = ps_soa::get_particle_cull_radius(quad, params) +
                                     ps_soa::get_camera_stretch_reach(params, math::length(camera), 1.0f);
                is_covered = is_covered && radius + 1.0e-4f >= needed;
            }
        }
    }
    check(is_covered, "the cull radius plus the camera reach covers a quad stretched against the camera, pivot pushed");
    params.render_mode = ps_soa::render_mode::billboard;
    check(ps_soa::get_camera_stretch_reach(params, 10.0f, 1.0f) == 0.0f, "only stretched quads reach further with the camera");
}

/// The signed area of a cutout (texture coordinates, v down): positive when it runs counter-clockwise on screen.
auto get_cutout_area(const ps_soa::cutout_polygon& cutout) -> float
{
    float area = 0.0f;
    for(uint32_t i = 0; i < ps_soa::cutout_vertex_count; ++i)
    {
        const math::vec2 a(cutout[i].x, 1.0f - cutout[i].y);
        const math::vec2 b(cutout[(i + 1) % ps_soa::cutout_vertex_count].x, 1.0f - cutout[(i + 1) % ps_soa::cutout_vertex_count].y);
        area += a.x * b.y - a.y * b.x;
    }
    return 0.5f * area;
}

/// True when @p uv (v down) lies inside the convex cutout (or on its edge).
auto is_inside_cutout(const ps_soa::cutout_polygon& cutout, const math::vec2& uv) -> bool
{
    const math::vec2 point(uv.x, 1.0f - uv.y);
    for(uint32_t i = 0; i < ps_soa::cutout_vertex_count; ++i)
    {
        const math::vec2 a(cutout[i].x, 1.0f - cutout[i].y);
        const math::vec2 b(cutout[(i + 1) % ps_soa::cutout_vertex_count].x, 1.0f - cutout[(i + 1) % ps_soa::cutout_vertex_count].y);
        const math::vec2 edge = b - a;
        if(math::dot(edge, edge) < 1.0e-12f)
        {
            continue;
        }
        if(edge.x * (point.y - a.y) - edge.y * (point.x - a.x) < -1.0e-5f)
        {
            return false;
        }
    }
    return true;
}

/// RGBA8 texels of a @p width x @p height sheet, opaque inside the discs (centre x, centre y, radius in texels).
auto make_disc_texels(uint32_t width, uint32_t height, const std::vector<math::vec3>& discs) -> std::vector<uint8_t>
{
    std::vector<uint8_t> texels(size_t(width) * height * 4, 255u);
    for(uint32_t y = 0; y < height; ++y)
    {
        for(uint32_t x = 0; x < width; ++x)
        {
            bool is_opaque = false;
            for(const auto& disc : discs)
            {
                is_opaque = is_opaque || math::length(math::vec2(float(x) + 0.5f, float(y) + 0.5f) - math::vec2(disc)) < disc.z;
            }
            texels[(size_t(y) * width + x) * 4 + 3] = is_opaque ? 255u : 0u;
        }
    }
    return texels;
}

/// Cutouts: around every texel above the threshold, inside the frame, smaller than it for a round texture; an empty
/// frame collapses, a full one keeps the whole frame, a blended frame covers the next one too.
void test_cutout_polygons()
{
    const auto hull = ps_soa::get_convex_hull({{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.25f, 0.75f}});
    float hull_area = 0.0f;
    for(size_t i = 0; i < hull.size(); ++i)
    {
        hull_area += hull[i].x * hull[(i + 1) % hull.size()].y - hull[i].y * hull[(i + 1) % hull.size()].x;
    }
    check(hull.size() == 4 && std::abs(0.5f * hull_area - 1.0f) < 1.0e-5f, "the hull of a square and inner points is the square, counter-clockwise");

    constexpr uint32_t size = 64;
    ps_soa::cutout_sheet sheet;
    const auto disc_texels = make_disc_texels(size, size, {math::vec3(32.0f, 32.0f, 20.0f)});
    const auto disc = ps_soa::bake_cutouts(disc_texels.data(), size, size, sheet);
    bool is_covered = disc.size() == 1;
    bool is_inside_frame = true;
    for(const auto& point : disc.front())
    {
        is_inside_frame = is_inside_frame && point.x >= 0.0f && point.x <= 1.0f && point.y >= 0.0f && point.y <= 1.0f;
    }
    for(uint32_t y = 0; y < size && is_covered; ++y)
    {
        for(uint32_t x = 0; x < size; ++x)
        {
            if(disc_texels[(size_t(y) * size + x) * 4 + 3] > 0u)
            {
                is_covered = is_covered && is_inside_cutout(disc.front(), math::vec2(float(x) + 0.5f, float(y) + 0.5f) / float(size));
            }
        }
    }
    const float disc_area = get_cutout_area(disc.front());
    const float circle_area = 3.14159265f * (20.0f / 64.0f) * (20.0f / 64.0f);
    check(is_covered, "a round texture's cutout covers every opaque texel");
    check(is_inside_frame, "the cutout stays inside its frame");
    check(disc_area >= circle_area && disc_area < 0.5f, "the cutout hugs the disc: smaller than the frame, counter-clockwise");

    const std::vector<uint8_t> empty(size_t(size) * size * 4, 0u);
    check(std::abs(get_cutout_area(ps_soa::bake_cutouts(empty.data(), size, size, sheet).front())) < 1.0e-6f,
          "an empty frame collapses");
    const std::vector<uint8_t> full(size_t(size) * size * 4, 255u);
    check(std::abs(get_cutout_area(ps_soa::bake_cutouts(full.data(), size, size, sheet).front()) - 1.0f) < 1.0e-4f,
          "a full frame keeps the whole frame");

    // Two frames side by side: a disc on the left of the first, on the right of the second.
    const auto pair = make_disc_texels(size * 2, size, {math::vec3(16.0f, 32.0f, 10.0f), math::vec3(64.0f + 48.0f, 32.0f, 10.0f)});
    sheet.tiles_x = 2;
    sheet.is_blended = true;
    const auto blended = ps_soa::bake_cutouts(pair.data(), size * 2, size, sheet);
    check(blended.size() == 2 && is_inside_cutout(blended[0], math::vec2(48.0f, 32.0f) / float(size)) &&
              is_inside_cutout(blended[0], math::vec2(16.0f, 32.0f) / float(size)),
          "a blended frame's cutout covers the next frame's texels too");
    check(ps_soa::get_next_cutout_frame(1, sheet) == 0 && ps_soa::get_next_cutout_frame(0, sheet) == 1,
          "blended frames wrap to the first");
}

/// An emitted particle with an exact start and launch, living @p lifetime seconds.
auto make_exact_emit(const math::vec3& position, const math::vec3& velocity, float lifetime) -> ps_soa::emit_params
{
    ps_soa::emit_params params;
    params.position = position;
    params.velocity = velocity;
    params.lifetime = lifetime;
    params.mask = ps_soa::emit_params::position_flag | ps_soa::emit_params::velocity_flag |
                  ps_soa::emit_params::lifetime_flag;
    return params;
}

/// A prewarmed emitter starts in its steady state: particles of every age, not one shell born together.
void test_prewarm_spreads_ages()
{
    auto desc = base_desc();
    desc.emission.prewarm = true;
    desc.emission.particles_per_second = 20.0f;
    desc.motion.lifetime = frange_t(2.0f, 2.0f);
    auto run = make_run(256, desc);
    step(run, k_frame_seconds);
    std::vector<ps_soa::particle_state> states;
    ps_soa::get_particle_states(run.handle, states);
    float youngest = 10.0f;
    float oldest = 0.0f;
    for(const auto& state : states)
    {
        youngest = math::min(youngest, state.age);
        oldest = math::max(oldest, state.age);
    }
    check(states.size() >= 38 && states.size() <= 42, "prewarm fills one lifetime of emission");
    check(youngest < 0.1f && oldest > 1.8f, "prewarmed particles have every age up to the lifetime");
    ps_soa::destroy_emitter(run.handle);
}

/// Particles that die early on a plane free their place at once: the count and the capacity see them gone.
void test_early_deaths_free_capacity()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 100.0f;
    desc.emission.shape_scale = math::vec3(0.0f);
    desc.motion.start_speed = frange_t(0.0f, 0.0f);
    desc.motion.lifetime = frange_t(5.0f, 5.0f);
    desc.motion.gravity_scale = 1.0f;
    desc.motion.collision = ps_soa::collision_mode::bounce;
    desc.motion.max_bounces = 0;
    desc.motion.collision_radius_scale = 0.0f;
    desc.motion.collision_planes = {math::vec4(0.0f, 1.0f, 0.0f, -1.0f)};
    auto run = make_run(100, desc);
    for(int i = 0; i < 180; ++i)
    {
        step(run, k_frame_seconds);
    }
    std::vector<ps_soa::particle_state> states;
    ps_soa::get_particle_states(run.handle, states);
    const uint32_t count = ps_soa::get_num_particles(run.handle);
    // A fall of 1 m takes 0.45 s: about 45 alive at 100 per second, whatever the 5 s lifetime.
    check(count == uint32_t(states.size()), "the live count is the particles alive");
    check(count >= 35 && count <= 55, "early deaths leave room: the emitter keeps spawning at its rate");
    ps_soa::destroy_emitter(run.handle);
}

/// A mesh particle spinning 90 degrees a second has turned 90 degrees after a second.
void test_mesh_spin_rate()
{
    auto desc = base_desc();
    desc.render.render_mode = ps_soa::render_mode::mesh;
    desc.render.start_rotation = frange_t(0.0f, 0.0f);
    desc.render.spin_speed = frange_t(90.0f, 90.0f);
    desc.emission.particles_per_second = 0.0f;
    desc.motion.start_speed = frange_t(0.0f, 0.0f);
    auto run = make_run(16, desc);
    ps_soa::emit(run.handle, 1, make_exact_emit(math::vec3(0.0f), math::vec3(0.0f), 4.0f));
    for(int i = 0; i < 60; ++i)
    {
        step(run, k_frame_seconds);
    }
    std::vector<math::mat4> current;
    std::vector<math::mat4> previous;
    std::vector<float> opacity;
    std::vector<ps_soa::particle_state> states;
    ps_soa::collect_particle_mesh_transforms(run.handle, current, previous, opacity);
    ps_soa::get_particle_states(run.handle, states);
    bool is_turned = !current.empty();
    for(size_t i = 0; i < current.size(); ++i)
    {
        const math::mat3 basis(math::normalize(math::vec3(current[i][0])),
                               math::normalize(math::vec3(current[i][1])),
                               math::normalize(math::vec3(current[i][2])));
        const float angle = math::degrees(2.0f * std::acos(math::clamp(std::abs(math::quat_cast(basis).w), 0.0f, 1.0f)));
        is_turned = is_turned && std::abs(angle - 90.0f * states[i].age) < 1.0f;
    }
    check(is_turned, "mesh particles spin at their rate in degrees per second");
    ps_soa::destroy_emitter(run.handle);
}

/// Two planes crossed within one search step: the particle stops at the one it reaches first.
void test_earliest_plane_wins()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 0.0f;
    desc.motion.gravity_scale = 1.0f;
    desc.motion.collision = ps_soa::collision_mode::kill;
    desc.motion.collision_radius_scale = 0.0f;
    // A floor at y = 0 listed first, then a wall at x = 1 facing back toward the emitter.
    desc.motion.collision_planes = {math::vec4(0.0f, 1.0f, 0.0f, 0.0f), math::vec4(-1.0f, 0.0f, 0.0f, -1.0f)};
    desc.event_mask = uint32_t(ps_soa::particle_event_type::collision);
    auto run = make_run(16, desc);
    // The wall is reached after 25 ms, the floor after 31 ms: both inside the first step.
    ps_soa::emit(run.handle, 1, make_exact_emit(math::vec3(0.0f, 0.25f, 0.0f), math::vec3(40.0f, -8.0f, 0.0f), 1.0f));
    std::vector<ps_soa::particle_event> events;
    for(int i = 0; i < 10; ++i)
    {
        step(run, k_frame_seconds);
        ps_soa::take_particle_events(run.handle, events);
    }
    check(events.size() == 1 && std::abs(events[0].position.x - 1.0f) < 2.0e-3f, "the earliest crossing is the contact");
    ps_soa::destroy_emitter(run.handle);
}

/// A bounce carries the speed curve on: the particle leaves the plane as fast as it hit it (restitution 1),
/// not at the curve's starting factor.
void test_bounce_keeps_speed_curve()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 0.0f;
    desc.motion.gravity_scale = 1.0f;
    desc.motion.speed_over_life = math::gradient<float>();
    desc.motion.speed_over_life.add_point(2.0f, 0.0f);
    desc.motion.speed_over_life.add_point(0.2f, 1.0f);
    desc.motion.collision = ps_soa::collision_mode::bounce;
    desc.motion.restitution = 1.0f;
    desc.motion.friction = 0.0f;
    desc.motion.max_bounces = 3;
    desc.motion.collision_radius_scale = 0.0f;
    desc.motion.collision_planes = {math::vec4(0.0f, 1.0f, 0.0f, 0.0f)};
    auto run = make_run(16, desc);
    ps_soa::emit(run.handle, 1, make_exact_emit(math::vec3(0.0f, 1.0f, 0.0f), math::vec3(2.0f, 0.0f, 0.0f), 4.0f));
    float before = 0.0f;
    float after = 0.0f;
    float previous_vertical = -1.0f;
    for(int i = 0; i < 120 && after == 0.0f; ++i)
    {
        step(run, 1.0f / 240.0f);
        std::vector<ps_soa::particle_state> states;
        ps_soa::get_particle_states(run.handle, states);
        if(states.empty())
        {
            continue;
        }
        if(previous_vertical < 0.0f && states[0].velocity.y > 0.0f)
        {
            after = std::abs(states[0].velocity.x);
        }
        else
        {
            before = std::abs(states[0].velocity.x);
        }
        previous_vertical = states[0].velocity.y;
    }
    check(after > 0.0f && std::abs(after - before) < 0.05f * before, "the speed curve goes on through a bounce");
    ps_soa::destroy_emitter(run.handle);
}

/// A mirrored emitter draws its particles at the size of an unmirrored one, and a local emitter scaled flat
/// still evaluates to finite positions.
void test_scale_edge_cases()
{
    auto desc = base_desc();
    desc.emission.use_fixed_seed = true;
    auto plain = make_run(64, desc);
    auto mirrored = make_run(64, desc);
    mirrored.transform.current.set_scale(math::vec3(-1.0f, 1.0f, 1.0f));
    mirrored.transform.previous = mirrored.transform.current;
    for(int i = 0; i < 30; ++i)
    {
        step(plain, k_frame_seconds);
        step(mirrored, k_frame_seconds);
    }
    std::vector<float> plain_sizes;
    std::vector<float> mirrored_sizes;
    ps_soa::get_particle_sizes(plain.handle, plain_sizes);
    ps_soa::get_particle_sizes(mirrored.handle, mirrored_sizes);
    check(!plain_sizes.empty() && plain_sizes == mirrored_sizes, "a mirrored emitter keeps its particles' sizes");
    ps_soa::destroy_emitter(plain.handle);
    ps_soa::destroy_emitter(mirrored.handle);

    auto flat_desc = base_desc();
    flat_desc.motion.space = ps_soa::simulation_space::local;
    flat_desc.motion.gravity_scale = 1.0f;
    auto flat = make_run(64, flat_desc);
    flat.transform.current.set_scale(math::vec3(1.0f, 0.0f, 1.0f));
    flat.transform.previous = flat.transform.current;
    for(int i = 0; i < 30; ++i)
    {
        step(flat, k_frame_seconds);
    }
    bool is_finite = !positions(flat.handle).empty();
    for(const auto& position : positions(flat.handle))
    {
        is_finite = is_finite && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }
    check(is_finite, "a local emitter scaled flat evaluates to finite positions");
    ps_soa::destroy_emitter(flat.handle);
}

/// Raising the rate does not backdate a burst of particles from the old rate's interval.
void test_rate_increase()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 1.0f;
    desc.motion.lifetime = frange_t(5.0f, 5.0f);
    auto run = make_run(4096, desc);
    for(int i = 0; i < 50; ++i)
    {
        step(run, k_frame_seconds);
    }
    const uint32_t before = ps_soa::get_num_particles(run.handle);
    run.desc.emission.particles_per_second = 1000.0f;
    step(run, k_frame_seconds);
    const uint32_t spawned = ps_soa::get_num_particles(run.handle) - before;
    check(spawned >= 15 && spawned <= 19, "a raised rate spawns one frame of the new rate");
    ps_soa::destroy_emitter(run.handle);
}

/// A trail's cull sphere holds its whole past path, also when drag has slowed the particle below the speed it
/// trailed at.
void test_trail_reach_holds_the_past_path()
{
    constexpr float lifespan = 2.0f;
    constexpr float trail_length = 0.5f;
    constexpr int path_samples = 16;
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.render.render_mode = ps_soa::render_mode::trail;
    desc.motion.drag = 3.0f;
    desc.motion.gravity_scale = 1.0f;
    auto luts = std::make_unique<ps_soa::lut_set>();
    ps_soa::bake_lut_set(desc, desc.bake_features(), *luts);
    const ps_soa::particle_record record = make_record(math::vec3(0.0f), math::vec3(10.0f, 4.0f, 0.0f), lifespan);
    bool is_held = true;
    for(const float age : {0.3f, 0.8f, 1.5f})
    {
        ps_soa::emitter_eval_params params = make_params(desc, age);
        params.render_mode = ps_soa::render_mode::trail;
        params.trail_length = trail_length;
        const ps_soa::particle_state state = ps_soa::evaluate_particle(record, params, *luts);
        const float radius = ps_soa::get_particle_cull_radius(ps_soa::evaluate_particle_quad(record, state, params, *luts), params);
        for(int i = 0; i <= path_samples; ++i)
        {
            const float earlier = math::max(age - trail_length * float(i) / float(path_samples), 0.0f);
            const ps_soa::particle_state past = ps_soa::evaluate_particle(record, make_params(desc, earlier), *luts);
            is_held = is_held && math::distance(past.position, state.position) <= radius + 1.0e-4f;
        }
    }
    check(is_held, "a trail's cull sphere holds its past path under drag");
}

/// The integer constants particle_common.sh defines (PARTICLE_NAME 12u), by name.
auto parse_particle_shader_constants(const std::string& path) -> std::map<std::string, uint32_t>
{
    std::map<std::string, uint32_t> constants;
    std::ifstream file(path);
    const std::regex define(R"(^#define (PARTICLE_\w+) (\d+)u\s*$)");
    std::string line;
    while(std::getline(file, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::smatch match;
        if(std::regex_match(line, match, define))
        {
            constants[match[1].str()] = uint32_t(std::stoul(match[2].str()));
        }
    }
    return constants;
}

/// The ribbon facing reaches the static row the vertex shader reads (camera 0, launch direction 1).
void test_ribbon_facing_reaches_the_static_row()
{
    std::printf("test_ribbon_facing_reaches_the_static_row\n");
    ps_soa::emitter_eval_params params;
    check(ps_soa::pack_static_row(params)[ps_soa::static_slot_ribbon].x == 0.0f, "a ribbon faces the camera by default");
    params.ribbon_facing = ps_soa::ribbon_facing::launch_direction;
    check(ps_soa::pack_static_row(params)[ps_soa::static_slot_ribbon].x == 1.0f,
          "launch direction facing is packed for the shader");
}

/// The layout the shaders read (row sizes, slots, LUT rows, feature flags, salts) matches the C++ one.
void test_shader_layout_matches_cpp()
{
#ifdef GI_TESTS_SHADER_DIR
    struct shader_constant
    {
        const char* name;
        uint32_t value;
    };
    const auto flag = [](ps_soa::emitter_feature feature)
    {
        return uint32_t(feature);
    };
    const shader_constant expected[] = {
        {"PARTICLE_RECORD_UINTS", ps_soa::record_uints},
        {"PARTICLE_STATIC_VEC4S", ps_soa::static_row_vec4s},
        {"PARTICLE_DYNAMIC_UINTS", ps_soa::dynamic_row_uints},
        {"PARTICLE_LUT_SIZE", ps_soa::lut_size},
        {"PARTICLE_LUT_ROWS", ps_soa::lut_rows},
        {"PARTICLE_LUT_ROW_COLOR", ps_soa::lut_row_color},
        {"PARTICLE_LUT_ROW_LIFE", ps_soa::lut_row_life},
        {"PARTICLE_LUT_ROW_COLOR_BY_SPEED", ps_soa::lut_row_color_by_speed},
        {"PARTICLE_LUT_ROW_SPEED", ps_soa::lut_row_speed},
        {"PARTICLE_LUT_ROW_SHAPE", ps_soa::lut_row_shape},
        {"PARTICLE_LUT_ROW_COLOR_B", ps_soa::lut_row_color_b},
        {"PARTICLE_LUT_ROW_TURBULENCE_SIZE_Z", ps_soa::lut_row_turbulence_size_z},
        {"PARTICLE_STATIC_APPEARANCE", ps_soa::static_slot_appearance},
        {"PARTICLE_STATIC_SIZE_AXES", ps_soa::static_slot_size_axes},
        {"PARTICLE_STATIC_SIZE_BY_SPEED", ps_soa::static_slot_size_by_speed},
        {"PARTICLE_STATIC_RENDER", ps_soa::static_slot_render},
        {"PARTICLE_STATIC_SHEET", ps_soa::static_slot_sheet},
        {"PARTICLE_STATIC_ROTATION", ps_soa::static_slot_rotation},
        {"PARTICLE_STATIC_SPIN", ps_soa::static_slot_spin},
        {"PARTICLE_STATIC_VIEW_SHAPING", ps_soa::static_slot_view_shaping},
        {"PARTICLE_STATIC_STRETCH", ps_soa::static_slot_stretch},
        {"PARTICLE_STATIC_TURBULENCE", ps_soa::static_slot_turbulence},
        {"PARTICLE_STATIC_NOISE", ps_soa::static_slot_noise},
        {"PARTICLE_STATIC_DISTANCE", ps_soa::static_slot_distance},
        {"PARTICLE_STATIC_TRAIL", ps_soa::static_slot_trail},
        {"PARTICLE_STATIC_FACING", ps_soa::static_slot_facing},
        {"PARTICLE_STATIC_SPEED_BOUND", ps_soa::static_slot_speed_bound},
        {"PARTICLE_STATIC_RIBBON", ps_soa::static_slot_ribbon},
        {"PARTICLE_DYNAMIC_LOCAL_TO_WORLD", ps_soa::dynamic_slot_local_to_world},
        {"PARTICLE_DYNAMIC_ROTATION", ps_soa::dynamic_slot_rotation},
        {"PARTICLE_DYNAMIC_ACCELERATION", ps_soa::dynamic_slot_acceleration},
        {"PARTICLE_DYNAMIC_DRAG", ps_soa::dynamic_slot_drag},
        {"PARTICLE_DYNAMIC_NOW", ps_soa::dynamic_slot_now},
        {"PARTICLE_DYNAMIC_SYSTEM_SCALE", ps_soa::dynamic_slot_system_scale},
        {"PARTICLE_DYNAMIC_FRAME_TIME", ps_soa::dynamic_slot_frame_time},
        {"PARTICLE_DYNAMIC_ORIGIN_MOTION", ps_soa::dynamic_slot_origin_motion},
        {"PARTICLE_FLAG_ALIGN", flag(ps_soa::emitter_feature::align_to_direction)},
        {"PARTICLE_FLAG_TEXSHEET", flag(ps_soa::emitter_feature::texsheet)},
        {"PARTICLE_FLAG_COLOR_BY_SPEED", flag(ps_soa::emitter_feature::color_by_speed)},
        {"PARTICLE_FLAG_SIZE_BY_SPEED", flag(ps_soa::emitter_feature::size_by_speed)},
        {"PARTICLE_FLAG_LOCAL_SPACE", flag(ps_soa::emitter_feature::local_space)},
        {"PARTICLE_FLAG_EASE", flag(ps_soa::emitter_feature::non_linear_ease)},
        {"PARTICLE_FLAG_SPEED_CURVE", flag(ps_soa::emitter_feature::speed_curve)},
        {"PARTICLE_FLAG_FRAME_BLEND", flag(ps_soa::emitter_feature::frame_blending)},
        {"PARTICLE_FLAG_TEX_RANDOMIZE", flag(ps_soa::emitter_feature::texsheet_randomize)},
        {"PARTICLE_FLAG_TEX_SINGLE_PASS", flag(ps_soa::emitter_feature::texsheet_single_pass)},
        {"PARTICLE_FLAG_RANDOM_SPIN_DIR", flag(ps_soa::emitter_feature::random_spin_direction)},
        {"PARTICLE_FLAG_EROSION", flag(ps_soa::emitter_feature::erosion)},
        {"PARTICLE_FLAG_SUBPIXEL_FADE", flag(ps_soa::emitter_feature::subpixel_fade)},
        {"PARTICLE_FLAG_CAMERA_FADE", flag(ps_soa::emitter_feature::camera_fade)},
        {"PARTICLE_FLAG_SIZE_AXIS_CURVE", flag(ps_soa::emitter_feature::size_axis_curve)},
        {"PARTICLE_FLAG_SPIN_CURVE", flag(ps_soa::emitter_feature::spin_curve)},
        {"PARTICLE_FLAG_INTENSITY_CURVE", flag(ps_soa::emitter_feature::intensity_curve)},
        {"PARTICLE_FLAG_TWO_GRADIENTS", flag(ps_soa::emitter_feature::two_gradients)},
        {"PARTICLE_FLAG_FRAME_CURVE", flag(ps_soa::emitter_feature::texsheet_frame_curve)},
        {"PARTICLE_FLAG_RANDOM_ROW", flag(ps_soa::emitter_feature::texsheet_random_row)},
        {"PARTICLE_FLAG_TURBULENCE", flag(ps_soa::emitter_feature::turbulence)},
        {"PARTICLE_FLAG_DRAW_DISTANCE", flag(ps_soa::emitter_feature::draw_distance)},
        {"PARTICLE_FLAG_LIT_VOLUMETRIC", flag(ps_soa::emitter_feature::lit_volumetric)},
        {"PARTICLE_FLAG_LIT_SURFACE", flag(ps_soa::emitter_feature::lit_surface)},
        {"PARTICLE_SALT_ROTATION", ps_soa::salt_rotation},
        {"PARTICLE_SALT_SPIN", ps_soa::salt_spin},
        {"PARTICLE_SALT_SPIN_DIRECTION", ps_soa::salt_spin_direction},
        {"PARTICLE_SALT_SHEET", ps_soa::salt_sheet},
        {"PARTICLE_SALT_GRADIENT", ps_soa::salt_gradient},
        {"PARTICLE_SALT_SHEET_ROW", ps_soa::salt_sheet_row},
    };
    const auto shader = parse_particle_shader_constants(std::string(GI_TESTS_SHADER_DIR) + "/particles/particle_common.sh");
    bool is_matching = !shader.empty();
    for(const auto& constant : expected)
    {
        const auto found = shader.find(constant.name);
        const bool is_same = found != shader.end() && found->second == constant.value;
        if(!is_same)
        {
            std::printf("  %s differs between particle_common.sh and C++\n", constant.name);
        }
        is_matching = is_matching && is_same;
    }
    // Every integer the shader defines is one of them: a new slot or flag lands in the table too.
    bool is_owned = true;
    for(const auto& [name, value] : shader)
    {
        const bool owned = std::any_of(std::begin(expected),
                                       std::end(expected),
                                       [&](const shader_constant& constant)
                                       {
                                           return name == constant.name;
                                       });
        if(!owned)
        {
            std::printf("  %s in particle_common.sh has no C++ counterpart in the test's table\n", name.c_str());
        }
        is_owned = is_owned && owned;
    }
    check(is_matching, "the shader layout constants match the C++ ones");
    check(is_owned, "every shader layout constant has a C++ counterpart");
#else
    check(false, "GI_TESTS_SHADER_DIR is not defined by the build: the layout parity cannot run");
#endif
}

/// Woken after a long freeze, a looping burst emitter shows its newest cycles' particles.
void test_burst_catch_up_keeps_newest()
{
    auto desc = base_desc();
    desc.emission.particles_per_second = 0.0f;
    desc.emission.duration = 1.0f;
    desc.emission.bursts = {ps_soa::emitter_burst{}};
    desc.emission.bursts[0].time = 0.0f;
    desc.emission.bursts[0].count = frange_t(10.0f, 10.0f);
    desc.motion.lifetime = frange_t(3.0f, 3.0f);
    auto run = make_run(256, desc);
    step(run, k_frame_seconds);
    for(int i = 0; i < 120 * 60; ++i)
    {
        ps_soa::update_emitter_frozen(run.handle, k_frame_seconds, run.desc, run.transform, true);
    }
    step(run, k_frame_seconds);
    check(ps_soa::get_num_particles(run.handle) >= 20, "a long catch-up fires the bursts of its last cycles");
    ps_soa::destroy_emitter(run.handle);
}

auto run_particle_motion_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_closed_form_matches_integration();
    test_gravity_is_physical();
    test_legacy_motion_conversion_is_exact();
    test_slot_allocator();
    test_appearance_curves();
    test_camera_stretch_stays_culled_conservatively();
    test_trail_reach_holds_the_past_path();
    test_shader_layout_matches_cpp();
    test_ribbon_facing_reaches_the_static_row();
    test_cutout_polygons();
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        std::printf("  FAIL: bgfx did not initialise on the Noop renderer\n");
        gfx::shutdown();
        return 1;
    }
    ps_soa::init(64);
    test_legacy_scene_loads_converted();
    test_seeds();
    test_duration();
    test_sub_frame_ages();
    test_bursts_and_distance();
    test_shapes();
    test_turbulence();
    test_collision_and_events();
    test_mesh_transforms();
    test_mesh_size_axes_curve();
    test_renamed_keys_load();
    test_catch_up();
    test_capacity();
    test_bounds_hold_every_particle();
    test_prewarm_spreads_ages();
    test_early_deaths_free_capacity();
    test_mesh_spin_rate();
    test_earliest_plane_wins();
    test_bounce_keeps_speed_curve();
    test_scale_edge_cases();
    test_rate_increase();
    test_burst_catch_up_keeps_newest();
    ps_soa::shutdown();
    gfx::shutdown();
    std::printf("particle motion: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("particle motion", run_particle_motion_suite)
