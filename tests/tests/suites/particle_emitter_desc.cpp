/*
 * Validation suite for the particle emitter description: which kernel features a description bakes.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "particle emitter desc"
 *
 * Pins the texture sheet: a sheet of more than one tile always draws one tile per particle. With cycles 0 a
 * particle holds one tile (the first, or a random one with randomize) rather than showing the whole sheet.
 * Pins prewarm: a looping emitter that prewarms starts with one particle lifetime of emission alive, the
 * first time it plays after it is created or reset. Pins auto play: it decides whether an emitter
 * plays only until the emitter's first frame.
 */

#include "../tests.h"

#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/particles/ps/emitter_desc.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/graphics.h>

#include <cstdio>
#include <string>

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

auto has_texture_sheet(const math::vec2& tiles, float cycles, bool randomize) -> bool
{
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.render.tex_sheet_tiles = tiles;
    desc.render.tex_sheet_cycles = cycles;
    desc.render.tex_sheet_randomize = randomize;
    return ps_soa::has_feature(desc.bake_features(), ps_soa::emitter_feature::texsheet);
}

void test_texture_sheet_feature()
{
    check(has_texture_sheet({8.0f, 8.0f}, 1.0f, false), "an animated sheet draws one tile at a time");
    check(has_texture_sheet({4.0f, 4.0f}, 0.0f, true), "a sheet of variants with cycles 0 draws one random tile");
    check(has_texture_sheet({4.0f, 1.0f}, 0.0f, false), "a sheet with cycles 0 holds its first tile");
    check(!has_texture_sheet({1.0f, 1.0f}, 0.0f, false), "a single texture is drawn whole");
    check(!has_texture_sheet({1.0f, 1.0f}, 2.0f, true), "a single texture is drawn whole whatever the cycles");
    check(!has_texture_sheet({0.0f, 4.0f}, 1.0f, false), "an empty tile count is drawn whole");
}

/// Emission rate and particle lifetime of the prewarm tests: 20 per second living 2 seconds keep 40 alive.
constexpr float k_prewarm_rate = 20.0f;
constexpr float k_prewarm_lifetime = 2.0f;
constexpr uint32_t k_prewarm_steady_count = 40;
constexpr float k_frame_seconds = 1.0f / 60.0f;

auto make_prewarm_desc(bool prewarm, bool loop) -> ps_soa::emitter_desc
{
    ps_soa::emitter_desc desc;
    desc.reset();
    desc.emission.particles_per_second = k_prewarm_rate;
    desc.motion.lifetime = k_prewarm_lifetime;
    desc.emission.loop = loop;
    desc.emission.prewarm = prewarm;
    return desc;
}

/// Particles alive after one frame of an emitter made from @p desc, simulated on the CPU.
auto count_after_first_frame(ps_soa::emitter_desc desc) -> uint32_t
{
    const auto handle =
        ps_soa::create_emitter(ps_soa::emitter_shape::sphere, ps_soa::emitter_direction::up, 256);
    ps_soa::set_emitter_sim_backend(handle, ps_soa::particle_sim_backend::cpu);
    ps_soa::emitter_transform_state transform{};
    ps_soa::update_emitter(handle, k_frame_seconds, desc, transform, desc.playback);
    const uint32_t count = ps_soa::get_num_particles(handle);
    ps_soa::destroy_emitter(handle);
    return count;
}

void test_prewarm()
{
    check(!make_prewarm_desc(false, true).emission.prewarm, "prewarm is off by default");
    check(count_after_first_frame(make_prewarm_desc(true, true)) == k_prewarm_steady_count,
          "a prewarmed looping emitter starts with rate x lifetime particles alive");
    check(count_after_first_frame(make_prewarm_desc(false, true)) < 2, "without prewarm it starts empty");
    check(count_after_first_frame(make_prewarm_desc(true, false)) < 2, "a one-shot emitter never prewarms");

    auto desc = make_prewarm_desc(true, true);
    const auto handle =
        ps_soa::create_emitter(ps_soa::emitter_shape::sphere, ps_soa::emitter_direction::up, 256);
    ps_soa::set_emitter_sim_backend(handle, ps_soa::particle_sim_backend::cpu);
    ps_soa::emitter_transform_state transform{};
    desc.playback.playing = false;
    ps_soa::update_emitter(handle, k_frame_seconds, desc, transform, desc.playback);
    check(ps_soa::get_num_particles(handle) == 0, "a stopped emitter does not prewarm");
    desc.playback.playing = true;
    ps_soa::update_emitter(handle, k_frame_seconds, desc, transform, desc.playback);
    check(ps_soa::get_num_particles(handle) == k_prewarm_steady_count, "it prewarms when it first plays");
    ps_soa::reset_emitter(handle);
    ps_soa::update_emitter(handle, k_frame_seconds, desc, transform, desc.playback);
    check(ps_soa::get_num_particles(handle) == k_prewarm_steady_count, "a reset emitter prewarms again");
    ps_soa::destroy_emitter(handle);
}

void test_auto_play()
{
    check(ps_soa::emitter_desc{}.playback.auto_play, "auto play is on by default");
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<particle_emitter_component>(entity);
    particle_emitter_component::on_create_component(registry, entity);
    auto& emitter = registry.get<particle_emitter_component>(entity);
    emitter.set_auto_play(false);
    check(!emitter.is_playing(), "an emitter loaded with auto play off starts stopped");
    emitter.set_last_render_frame(1);
    emitter.set_auto_play(true);
    check(!emitter.is_playing(), "after its first frame, auto play no longer starts it");
    check(emitter.get_auto_play(), "the flag is kept for the next copy");
    particle_emitter_component::on_destroy_component(registry, entity);
}

auto run_particle_emitter_desc_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_texture_sheet_feature();
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    if(!gfx::init(init))
    {
        std::printf("  FAIL: bgfx did not initialise on the Noop renderer\n");
        gfx::shutdown();
        return 1;
    }
    ps_soa::init(16);
    test_prewarm();
    test_auto_play();
    ps_soa::shutdown();
    gfx::shutdown();
    std::printf("particle emitter desc: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("particle emitter desc", run_particle_emitter_desc_suite)
