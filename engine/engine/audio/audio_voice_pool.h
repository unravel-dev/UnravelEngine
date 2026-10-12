#pragma once
#include <engine/engine_export.h>

#include <engine/audio/audio_bus.h>
#include <engine/audio/audio_clip.h>
#include <engine/audio/volume_fade.h>

#include <math/math.h>
#include <uuid/uuid.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace audio
{
class source;
}

namespace unravel
{

/**
 * @brief How a one-shot voice plays.
 */
struct one_shot_params
{
    /// Gain before the bus and master gains, 0..1.
    float volume = 1.0f;
    /// Playback rate, 0..5 (1 = original pitch).
    float pitch = 1.0f;
    /// Mixer bus the voice plays on.
    audio_bus bus = audio_bus::sfx;
    /// Voice-steal priority (higher is kept).
    int priority = 0;
    /// True plays at position in the world; false plays 2D at the listener.
    bool spatial = false;
    /// World position of a spatial voice.
    math::vec3 position{};
};

/**
 * @brief Fire-and-forget voices: one OpenAL source per sound, no entity. A voice is addressed by
 * an id (slot + generation), so an id of a finished voice is harmless.
 */
class audio_voice_pool
{
public:
    /// Effective (master x bus) gain of every bus.
    using bus_gains = std::array<float, audio_bus_count>;

    /// What the voice cap may steal from the pool.
    struct steal_candidate
    {
        /// Id of the voice, 0 when the pool has none.
        uint64_t voice = 0;
        int priority = 0;
    };

    audio_voice_pool();
    ~audio_voice_pool();
    audio_voice_pool(const audio_voice_pool&) = delete;
    auto operator=(const audio_voice_pool&) -> audio_voice_pool& = delete;

    /**
     * @brief Plays @p clip once. Returns the voice id, or 0 when the clip's limit refused it or no
     * source could be created.
     */
    auto play(const std::shared_ptr<audio_clip>& clip, const hpp::uuid& clip_uid, const one_shot_params& params)
        -> uint64_t;

    /**
     * @brief Limits how often @p clip_uid plays: at most @p max_voices at once (the oldest stops for a
     * new one; 0 = no limit) and no new one within @p min_interval seconds of the last.
     */
    void set_clip_limit(const hpp::uuid& clip_uid, int max_voices, float min_interval);

    void stop(uint64_t id);
    auto is_playing(uint64_t id) const -> bool;
    void set_volume(uint64_t id, float volume);
    void set_pitch(uint64_t id, float pitch);
    void fade_to(uint64_t id, float volume, float seconds, bool stop_at_end);

    /**
     * @brief Advances fades and frees finished voices. @p seconds is unscaled time.
     */
    void update(float seconds);

    /// Re-applies every voice's gain after the mixer changed.
    void set_bus_gains(const bus_gains& gains);
    void pause_all();
    void resume_all();
    /// Stops every voice and forgets the clip limits (a play session ended).
    void stop_all();

    auto get_live_count() const -> std::size_t;
    /// The voice the cap should steal first: lowest priority, then oldest.
    auto find_steal_candidate() const -> steal_candidate;

private:
    struct voice
    {
        std::shared_ptr<audio::source> source;
        /// Keeps the clip alive: a source only points at the sound it plays.
        std::shared_ptr<audio_clip> clip;
        hpp::uuid clip_uid{};
        audio_bus bus = audio_bus::sfx;
        int priority = 0;
        float volume = 1.0f;
        volume_fade fade{};
        uint32_t generation = 1;
        /// Play order, for oldest-first stealing.
        uint64_t serial = 0;
    };

    struct clip_limit
    {
        int max_voices = 0;
        std::chrono::duration<float> min_interval{0.0f};
        std::chrono::steady_clock::time_point last_start{};
        bool has_started = false;
    };

    auto find_voice(uint64_t id) -> voice*;
    auto find_voice(uint64_t id) const -> const voice*;
    auto make_id(uint32_t index) const -> uint64_t;
    auto admit(const hpp::uuid& clip_uid) -> bool;
    void apply_gain(voice& v) const;
    void release(uint32_t index);

    std::vector<voice> voices_;
    std::vector<uint32_t> free_slots_;
    std::unordered_map<hpp::uuid, clip_limit> limits_;
    bus_gains gains_{1.0f, 1.0f, 1.0f};
    uint64_t next_serial_ = 1;
};

} // namespace unravel
