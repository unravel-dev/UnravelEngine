/*
 * Validation suite for the audio voice pool.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "audio voice pool"
 *
 * Every OpenAL source the engine plays lives in audio_voice_pool: entity-less one-shots and the voices
 * audio source components hold through an audio_voice_handle. This pins:
 *   - the voice cap frees non-looping first, then lower priority, then one-shots, then the older voice;
 *   - a one-shot refused by its clip limit frees nothing, and one at its clip's voice limit stops only
 *     its own oldest voice;
 *   - handles: one owner (copies are empty), release on reset / destruction, harmless after a steal or
 *     after the pool is gone;
 *   - mixer gains reach component voices; finished voices are freed by update; stop_all forgets limits;
 *   - an audio source component takes no voice without a playable clip and loses a stolen one cleanly.
 * The runner has no audio device, so the suite opens OpenAL's silent null backend itself; the rank checks
 * run even when no device can be opened.
 */

#include "../tests.h"

#include <engine/assets/asset_handle.h>
#include <engine/audio/audio_voice_pool.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/threading/threader.h>

#include <audiopp/device.h>
#include <audiopp/exception.h>
#include <audiopp/source.h>
#include <uuid/uuid.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

constexpr uint32_t test_sample_rate = 22050;
constexpr uint8_t test_bits_per_sample = 16;
/// Outlives every check that expects a voice to keep playing.
constexpr float long_clip_seconds = 10.0f;
/// Ends well inside the reap check's wait.
constexpr float short_clip_seconds = 0.05f;
constexpr auto reap_timeout = std::chrono::seconds(3);
constexpr auto reap_poll = std::chrono::milliseconds(10);
constexpr float gain_tolerance = 1e-4f;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto is_near(float actual, float expected) -> bool
{
    return std::fabs(actual - expected) <= gain_tolerance;
}

auto make_clip(float seconds) -> std::shared_ptr<audio_clip>
{
    audio::sound_data data;
    data.info.sample_rate = test_sample_rate;
    data.info.bits_per_sample = test_bits_per_sample;
    data.info.channels = 1;
    data.info.frames = uint64_t(seconds * float(test_sample_rate));
    data.info.duration = audio::duration_t(seconds);
    data.data.assign(std::size_t(data.info.frames) * (test_bits_per_sample / 8), 0);
    return std::make_shared<audio_clip>(std::move(data));
}

auto make_clip_handle(rtti::context& ctx, const std::shared_ptr<audio_clip>& clip) -> asset_handle<audio_clip>
{
    asset_handle<audio_clip> handle;
    handle.set_internal_ids(generate_uuid());
    handle.set_internal_job(ctx.get_cached<threader>().pool->schedule("test_audio_clip", [clip]() { return clip; }).share());
    handle.get();
    return handle;
}

auto one_shot(int priority) -> one_shot_params
{
    one_shot_params params;
    params.priority = priority;
    return params;
}

/// The runner has no device: point OpenAL at its silent null backend before the first device opens.
void use_silent_audio_backend()
{
#ifdef _WIN32
    _putenv_s("ALSOFT_DRIVERS", "null");
#else
    setenv("ALSOFT_DRIVERS", "null", 1);
#endif
}

void test_rank_order()
{
    std::printf("test_rank_order\n");
    const audio_voice_rank looping{true, -10, true, 1};
    const audio_voice_rank high{false, 10, false, 2};
    check(is_freed_before(high, looping), "a non-looping voice goes before a looping one of lower priority");
    const audio_voice_rank low{false, 1, false, 3};
    check(is_freed_before(low, high), "lower priority goes first");
    const audio_voice_rank older_component{false, 3, false, 1};
    const audio_voice_rank newer_one_shot{false, 3, true, 9};
    check(is_freed_before(newer_one_shot, older_component),
          "a one-shot goes before a component voice of the same priority, even a newer one");
    const audio_voice_rank lower_component{false, 2, false, 5};
    check(is_freed_before(lower_component, newer_one_shot), "a component of lower priority goes before a one-shot");
    const audio_voice_rank old_one_shot{false, 0, true, 1};
    const audio_voice_rank new_one_shot{false, 0, true, 2};
    check(is_freed_before(old_one_shot, new_one_shot), "the older voice goes first");
    check(!is_freed_before(new_one_shot, old_one_shot), "the newer voice is kept");
    check(!is_freed_before(old_one_shot, old_one_shot), "a voice does not rank before itself");
}

void test_cap_frees_lowest_rank(const std::shared_ptr<audio_clip>& clip)
{
    std::printf("test_cap_frees_lowest_rank\n");
    const auto uid = generate_uuid();
    audio_voice_pool pool(4);
    auto looping = pool.acquire({1.0f, audio_bus::music, 0, true});
    auto component = pool.acquire({1.0f, audio_bus::sfx, 5, false});
    const auto low = pool.play(clip, uid, one_shot(0));
    const auto mid = pool.play(clip, uid, one_shot(3));
    check(looping.is_alive() && component.is_alive() && low != 0 && mid != 0, "four voices start under a cap of four");
    check(pool.get_live_count() == 4, "the live count covers component voices and one-shots");
    const auto high = pool.play(clip, uid, one_shot(10));
    check(high != 0, "a voice over the cap still starts");
    check(!pool.is_playing(low), "the cap freed the lowest-priority one-shot");
    check(pool.is_playing(mid) && looping.is_alive() && component.is_alive(), "the cap freed only one voice");
    check(pool.get_live_count() == 4, "the live count stays at the cap");
    const auto tie = pool.play(clip, uid, one_shot(5));
    check(tie != 0 && !pool.is_playing(mid), "the next lowest one-shot (priority 3) goes next");
    const auto tie_2 = pool.play(clip, uid, one_shot(0));
    check(tie_2 != 0 && !pool.is_playing(tie) && component.is_alive(),
          "at equal priority the one-shot goes before the component voice");
    component.set_priority(-1);
    const auto after_drop = pool.play(clip, uid, one_shot(0));
    check(after_drop != 0 && !component.is_alive() && component.get_source() == nullptr,
          "a component voice whose priority dropped is freed, and its handle goes empty");
    check(pool.is_playing(tie_2) && pool.is_playing(high), "the one-shots above it are kept");
    pool.play(clip, uid, one_shot(100));
    pool.play(clip, uid, one_shot(100));
    pool.play(clip, uid, one_shot(100));
    pool.play(clip, uid, one_shot(100));
    check(looping.is_alive(), "a looping voice goes last, whatever its priority");
    audio_voice_pool loops_only(2);
    auto low_loop = loops_only.acquire({1.0f, audio_bus::music, 0, true});
    auto high_loop = loops_only.acquire({1.0f, audio_bus::music, 5, true});
    check(loops_only.play(clip, uid, one_shot(0)) != 0, "a one-shot starts over a pool of loops");
    check(!low_loop.is_alive() && high_loop.is_alive(), "among loops the lower priority goes first");
}

void test_refused_one_shot_frees_nothing(const std::shared_ptr<audio_clip>& clip)
{
    std::printf("test_refused_one_shot_frees_nothing\n");
    const auto limited = generate_uuid();
    audio_voice_pool pool(2);
    pool.set_clip_limit(limited, 0, 60.0f);
    const auto other = pool.play(clip, generate_uuid(), one_shot(0));
    const auto first = pool.play(clip, limited, one_shot(0));
    check(other != 0 && first != 0, "the pool fills to its cap");
    const auto refused = pool.play(clip, limited, one_shot(10));
    check(refused == 0, "a start inside the clip's minimum interval is refused");
    check(pool.is_playing(other) && pool.is_playing(first), "a refused start freed no voice");
    check(pool.get_live_count() == 2, "the live count is unchanged");
    pool.stop_all();
    check(pool.get_live_count() == 0, "stop_all frees every voice");
    check(pool.play(clip, limited, one_shot(0)) != 0, "stop_all forgets the clip limits");
}

void test_clip_limit_frees_only_its_own(const std::shared_ptr<audio_clip>& clip)
{
    std::printf("test_clip_limit_frees_only_its_own\n");
    const auto coin = generate_uuid();
    audio_voice_pool pool(3);
    pool.set_clip_limit(coin, 2, 0.0f);
    auto music = pool.acquire({1.0f, audio_bus::music, -5, false});
    const auto coin_1 = pool.play(clip, coin, one_shot(0));
    const auto coin_2 = pool.play(clip, coin, one_shot(0));
    check(music.is_alive() && coin_1 != 0 && coin_2 != 0, "the pool fills to its cap");
    const auto coin_3 = pool.play(clip, coin, one_shot(0));
    check(coin_3 != 0 && !pool.is_playing(coin_1), "the clip's oldest voice stops for the new one");
    check(music.is_alive(), "the lowest-ranked voice is not freed as well");
    check(pool.is_playing(coin_2) && pool.get_live_count() == 3, "only one voice was freed");
}

void test_handles()
{
    std::printf("test_handles\n");
    audio_voice_pool pool(4);
    auto handle = pool.acquire({0.8f, audio_bus::sfx, 0, false});
    check(handle.is_alive() && handle.get_source() != nullptr, "acquire returns a live voice");
    audio_voice_handle copy(handle);
    check(!copy.is_alive() && handle.is_alive(), "a copy is empty and the original keeps the voice");
    auto moved = std::move(handle);
    check(moved.is_alive() && !handle.is_alive() && pool.get_live_count() == 1, "a move hands the voice over");
    pool.set_bus_gains({0.5f, 1.0f, 1.0f});
    check(is_near(moved.get_source()->get_volume(), 0.4f), "the bus gain scales the voice gain");
    moved.set_bus(audio_bus::music);
    check(is_near(moved.get_source()->get_volume(), 0.8f), "a bus change re-applies the gain");
    moved.set_volume(0.25f);
    check(is_near(moved.get_source()->get_volume(), 0.25f), "a volume change re-applies the gain");
    moved.set_volume(std::nanf(""));
    check(is_near(moved.get_source()->get_volume(), 0.0f), "a NaN volume becomes silence");
    moved.reset();
    check(!moved.is_alive() && pool.get_live_count() == 0, "reset releases the voice");
    {
        auto scoped = pool.acquire({});
        check(pool.get_live_count() == 1, "a scoped handle holds a voice");
    }
    check(pool.get_live_count() == 0, "a destroyed handle releases its voice");
    audio_voice_handle survivor;
    {
        audio_voice_pool short_lived(2);
        survivor = short_lived.acquire({});
        check(survivor.is_alive(), "a handle from a second pool is live");
    }
    check(!survivor.is_alive() && survivor.get_source() == nullptr, "a handle that outlives its pool is empty");
    survivor.set_volume(0.5f);
    survivor.set_loop(true);
    survivor.reset();
    check(true, "calls on a handle that outlived its pool do nothing");
    check(pool.play(std::make_shared<audio_clip>(), generate_uuid(), one_shot(0)) == 0,
          "a clip without a sound takes no voice");
    check(pool.get_live_count() == 0, "the live count is unchanged");
}

void test_update_frees_finished(const std::shared_ptr<audio_clip>& short_clip)
{
    std::printf("test_update_frees_finished\n");
    audio_voice_pool pool(4);
    const auto id = pool.play(short_clip, generate_uuid(), one_shot(0));
    check(id != 0, "the short clip starts");
    const auto deadline = std::chrono::steady_clock::now() + reap_timeout;
    while(pool.is_playing(id) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(reap_poll);
        pool.update(std::chrono::duration<float>(reap_poll).count());
    }
    check(!pool.is_playing(id) && pool.get_live_count() == 0, "update frees a voice that finished");
    const auto faded = pool.play(short_clip, generate_uuid(), one_shot(0));
    pool.fade_to(faded, 0.0f, 0.0f, true);
    pool.update(0.0f);
    check(!pool.is_playing(faded), "a fade that stops at its end frees the voice");
}

void test_component_voices(rtti::context& ctx, const std::shared_ptr<audio_clip>& clip)
{
    std::printf("test_component_voices\n");
    auto& system = ctx.get_cached<audio_system>();
    auto& pool = system.get_voices();
    pool.stop_all();
    audio_source_component no_clip;
    check(!no_clip.play() && pool.get_live_count() == 0, "a source without a clip takes no voice");
    audio_source_component empty_clip;
    empty_clip.set_clip(asset_handle<audio_clip>::get_empty());
    check(!empty_clip.play() && pool.get_live_count() == 0, "a source with an empty clip takes no voice");
    audio_source_component source;
    source.set_clip(make_clip_handle(ctx, clip));
    source.set_priority(7);
    source.set_volume(0.8f);
    check(source.play() && source.has_source() && source.is_playing(), "a source with a clip plays");
    check(pool.get_live_count() == 1, "the source's voice lives in the system's pool");
    check(source.has_bound_sound(), "the clip is bound to the voice");
    audio_source_component copy = source;
    check(!copy.has_source() && source.has_source(), "a copied source does not take the voice");
    std::vector<uint64_t> fillers;
    for(std::size_t i = 1; i < audio_system::max_concurrent_voices; ++i)
    {
        fillers.push_back(pool.play(clip, generate_uuid(), one_shot(5)));
    }
    check(pool.get_live_count() == audio_system::max_concurrent_voices, "one-shots fill the pool to the cap");
    pool.play(clip, generate_uuid(), one_shot(5));
    check(source.has_source() && !pool.is_playing(fillers.front()), "the cap freed the oldest one-shot, not the source");
    source.set_priority(1);
    pool.play(clip, generate_uuid(), one_shot(5));
    check(!source.has_source() && !source.is_playing(), "a source whose priority dropped below the one-shots is freed");
    check(is_near(float(source.get_playback_position().count()), 0.0f), "a freed source reads as stopped");
    pool.stop_all();
    check(source.play() && source.has_source(), "a freed source plays again on a new voice");
    source.stop();
    check(!source.has_source() && pool.get_live_count() == 0, "stop releases the voice");
}

auto run_audio_voice_pool_suite(rtti::context& ctx) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_rank_order();
    use_silent_audio_backend();
    std::unique_ptr<audio::device> device;
    try
    {
        device = std::make_unique<audio::device>();
    }
    catch(const audio::exception& e)
    {
        std::printf("  SKIP device checks: %s\n", e.what());
    }
    if(device)
    {
        std::printf("  device: %s\n", device->get_device_id().c_str());
        // Sources must be gone before the device closes: every pool below is scoped inside this block.
        const auto clip = make_clip(long_clip_seconds);
        const auto short_clip = make_clip(short_clip_seconds);
        test_cap_frees_lowest_rank(clip);
        test_refused_one_shot_frees_nothing(clip);
        test_clip_limit_frees_only_its_own(clip);
        test_handles();
        test_update_frees_finished(short_clip);
        test_component_voices(ctx, clip);
        ctx.get_cached<audio_system>().get_voices().stop_all();
    }
    std::printf("audio voice pool: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("audio voice pool", run_audio_voice_pool_suite)
