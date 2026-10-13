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
 * plays only until the emitter's first frame. Pins the size curve: every key counts, a curve with two
 * keys gives exactly the linear blend between the birth and death sizes, and a burst's size scales
 * the curve.
 */

#include "../tests.h"

#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/particles/ps/emitter_desc.h>
#include <engine/rendering/particles/ps/particle_scale_curve.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <graphics/graphics.h>

#include <cstdio>
#include <string>
#include <utility>
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

/// Largest difference between two sizes that still counts as the same size.
constexpr float k_size_tolerance = 1.0e-5f;

auto make_size_curve(const std::vector<std::pair<float, frange_t>>& keys) -> math::gradient<frange_t>
{
    math::gradient<frange_t> curve;
    for(const auto& key : keys)
    {
        curve.add_point(key.second, key.first);
    }
    return curve;
}

auto is_same_size(float actual, float expected) -> bool
{
    return math::abs(actual - expected) <= k_size_tolerance;
}

auto get_size_at(const math::gradient<frange_t>& curve, const ps_soa::particle_scale_pick& pick, float life) -> float
{
    return ps_soa::sample_particle_scale(curve, ps_soa::get_scale_curve_widths(curve), pick, life);
}

auto make_pick(float start, float end) -> ps_soa::particle_scale_pick
{
    ps_soa::particle_scale_pick pick;
    pick.start = start;
    pick.end = end;
    return pick;
}

void test_two_key_size_is_linear()
{
    // The range is wider at death than at birth, so the drift between the two picks is weighted.
    const auto curve = make_size_curve({{0.0f, frange_t(0.1f, 0.2f)}, {1.0f, frange_t(0.3f, 0.7f)}});
    bool is_linear = true;
    for(float start_pick : {0.0f, 0.3f, 0.9f})
    {
        for(float end_pick : {0.1f, 0.6f, 1.0f})
        {
            for(float life : {0.0f, 0.2f, 0.5f, 0.85f, 1.0f})
            {
                const float birth_size = math::mix(0.1f, 0.2f, start_pick);
                const float death_size = math::mix(0.3f, 0.7f, end_pick);
                const float expected = math::mix(birth_size, death_size, life);
                is_linear = is_linear && is_same_size(get_size_at(curve, make_pick(start_pick, end_pick), life), expected);
            }
        }
    }
    check(is_linear, "a two-key size curve gives mix(size at birth, size at death, life)");
}

void test_every_size_key_counts()
{
    const auto pick = make_pick(0.0f, 0.0f);
    const auto pop_in = make_size_curve(
        {{0.0f, frange_t(0.0f, 0.0f)}, {0.1f, frange_t(1.0f, 1.0f)}, {1.0f, frange_t(1.0f, 1.0f)}});
    check(is_same_size(get_size_at(pop_in, pick, 0.0f), 0.0f) && is_same_size(get_size_at(pop_in, pick, 0.05f), 0.5f) &&
              is_same_size(get_size_at(pop_in, pick, 0.1f), 1.0f) && is_same_size(get_size_at(pop_in, pick, 0.6f), 1.0f),
          "a size pops in: 0 at birth, full at 10% of the life, full to the end");
    const auto grow_shrink = make_size_curve(
        {{0.0f, frange_t(0.0f, 0.0f)}, {0.5f, frange_t(2.0f, 2.0f)}, {1.0f, frange_t(0.0f, 0.0f)}});
    check(is_same_size(get_size_at(grow_shrink, pick, 0.25f), 1.0f) &&
              is_same_size(get_size_at(grow_shrink, pick, 0.5f), 2.0f) &&
              is_same_size(get_size_at(grow_shrink, pick, 1.0f), 0.0f),
          "a size grows then shrinks");
    check(is_same_size(ps_soa::get_scale_curve_peak(grow_shrink), 2.0f),
          "culling sizes a grow-then-shrink curve by its mid-life peak, not its ends");
    const auto ranged = make_size_curve(
        {{0.0f, frange_t(1.0f, 2.0f)}, {0.5f, frange_t(4.0f, 6.0f)}, {1.0f, frange_t(1.0f, 2.0f)}});
    check(is_same_size(get_size_at(ranged, make_pick(1.0f, 1.0f), 0.5f), 6.0f),
          "a particle that picks the max follows the max curve through a middle key");
}

void test_burst_size_scales_the_curve()
{
    const auto growing = make_size_curve({{0.0f, frange_t(1.0f, 1.0f)}, {1.0f, frange_t(3.0f, 3.0f)}});
    auto pick = make_pick(0.0f, 0.0f);
    ps_soa::fit_scale_pick_to_size(growing, ps_soa::get_scale_curve_widths(growing), 2.0f, pick);
    check(is_same_size(get_size_at(growing, pick, 0.0f), 2.0f) && is_same_size(get_size_at(growing, pick, 1.0f), 6.0f),
          "a burst size starts the curve at that size and keeps its end to start ratio");
    const auto pop_in = make_size_curve(
        {{0.0f, frange_t(0.0f, 0.0f)}, {0.1f, frange_t(1.0f, 1.0f)}, {1.0f, frange_t(1.0f, 1.0f)}});
    pick = make_pick(0.0f, 0.0f);
    ps_soa::fit_scale_pick_to_size(pop_in, ps_soa::get_scale_curve_widths(pop_in), 2.0f, pick);
    check(is_same_size(get_size_at(pop_in, pick, 0.0f), 0.0f) && is_same_size(get_size_at(pop_in, pick, 0.6f), 2.0f),
          "a curve that starts at 0 is scaled to peak at the burst size, keeping its pop-in");
    const auto zero = make_size_curve({{0.0f, frange_t(0.0f, 0.0f)}, {1.0f, frange_t(0.0f, 0.0f)}});
    pick = make_pick(0.0f, 0.0f);
    ps_soa::fit_scale_pick_to_size(zero, ps_soa::get_scale_curve_widths(zero), 2.0f, pick);
    check(is_same_size(get_size_at(zero, pick, 0.3f), 2.0f), "a curve that is 0 throughout holds the burst size");
}

auto run_particle_emitter_desc_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_texture_sheet_feature();
    test_two_key_size_is_linear();
    test_every_size_key_counts();
    test_burst_size_scales_the_curve();
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
