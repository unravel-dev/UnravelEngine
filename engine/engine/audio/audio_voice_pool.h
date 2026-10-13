#pragma once
#include <engine/engine_export.h>

#include <engine/audio/audio_bus.h>
#include <engine/audio/audio_clip.h>
#include <engine/audio/volume_fade.h>

#include <base/basetypes.hpp>
#include <hpp/optional.hpp>
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

class audio_voice_pool;

/// Highest playback rate a voice takes (1 = original pitch).
inline constexpr float audio_max_pitch = 5.0f;
/// Distance range a spatial voice starts with, in metres.
inline constexpr float audio_default_min_distance = 1.0f;
inline constexpr float audio_default_max_distance = 20.0f;

/**
 * @brief Clamps a gain to 0..1. NaN becomes 0, so it never reaches the device as an invalid gain.
 */
auto clamp_audio_gain(float gain) -> float;

/**
 * @brief Orders a distance range: min is clamped to 0..max, max to at least min.
 */
auto clamp_audio_range(const frange_t& range) -> frange_t;

/**
 * @brief Places @p source in the world (attenuated by @p rolloff) or on the listener, where it plays
 * 2D and does not distance-attenuate.
 */
void apply_voice_space(audio::source& source, bool spatial, float rolloff);

/**
 * @brief What decides which voice the voice cap frees first.
 */
struct audio_voice_rank
{
    /// Looping voices go last: they never end on their own, so freeing one cuts a sound for good.
    bool is_looping = false;
    /// Higher priority is kept longer.
    int priority = 0;
    /// A one-shot goes before a component voice of the same priority.
    bool is_one_shot = false;
    /// Play order: the older voice goes first.
    uint64_t serial = 0;
};

/**
 * @brief True when the voice cap frees @p lhs before @p rhs: non-looping first, then lower priority,
 * then one-shots, then the older voice.
 */
auto is_freed_before(const audio_voice_rank& lhs, const audio_voice_rank& rhs) -> bool;

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
 * @brief What the pool tracks for a voice an owner holds through an audio_voice_handle. The owner sets
 * up the rest of the source (space, distance, pitch, clip) itself.
 */
struct owned_voice_params
{
    /// Gain before the bus and master gains, 0..1.
    float volume = 1.0f;
    /// Mixer bus the voice plays on.
    audio_bus bus = audio_bus::sfx;
    /// Voice-steal priority (higher is kept).
    int priority = 0;
    /// Whether the source loops.
    bool loop = false;
};

/**
 * @brief Owns one pooled voice. The voice stops when the handle is reset, destroyed or assigned over.
 * Copies are empty: a voice has one owner. Every call does nothing once the voice ended, was freed by
 * the voice cap, or the pool is gone.
 */
class audio_voice_handle
{
public:
    audio_voice_handle() = default;
    ~audio_voice_handle();
    audio_voice_handle(const audio_voice_handle& rhs) noexcept;
    auto operator=(const audio_voice_handle& rhs) -> audio_voice_handle&;
    audio_voice_handle(audio_voice_handle&& rhs) noexcept;
    auto operator=(audio_voice_handle&& rhs) noexcept -> audio_voice_handle&;

    /// The voice's source, or null when the voice is not alive.
    auto get_source() const -> audio::source*;
    auto is_alive() const -> bool;
    void set_volume(float volume);
    void set_bus(audio_bus bus);
    void set_priority(int priority);
    void set_loop(bool loop);
    /// Stops the voice and empties the handle.
    void reset();

private:
    friend class audio_voice_pool;
    audio_voice_handle(std::weak_ptr<audio_voice_pool*> pool, uint64_t id);
    auto get_pool() const -> audio_voice_pool*;

    std::weak_ptr<audio_voice_pool*> pool_;
    uint64_t id_ = 0;
};

/**
 * @brief Every OpenAL source the engine plays: entity-less one-shots and the voices audio source
 * components hold through an audio_voice_handle. A new voice over the cap frees the live voice
 * is_freed_before ranks first. A voice is addressed by an id (slot + generation), so an id of a
 * finished voice is harmless.
 */
class audio_voice_pool
{
public:
    /// Effective (master x bus) gain of every bus.
    using bus_gains = std::array<float, audio_bus_count>;

    /**
     * @param max_voices Soft cap on live voices; a new voice over it frees one first.
     */
    explicit audio_voice_pool(std::size_t max_voices);
    ~audio_voice_pool();
    audio_voice_pool(const audio_voice_pool&) = delete;
    auto operator=(const audio_voice_pool&) -> audio_voice_pool& = delete;

    /**
     * @brief Plays @p clip once. Returns the voice id, or 0 when the clip has no sound, its limit
     * refused it or no source could be created. A refused clip frees nothing.
     */
    auto play(const std::shared_ptr<audio_clip>& clip, const hpp::uuid& clip_uid, const one_shot_params& params)
        -> uint64_t;

    /**
     * @brief Creates a stopped voice for an owner. The handle is empty when no source could be created.
     */
    auto acquire(const owned_voice_params& params) -> audio_voice_handle;

    /**
     * @brief Limits how often @p clip_uid plays as a one-shot: at most @p max_voices at once (the oldest
     * stops for a new one; 0 = no limit) and no new one within @p min_interval seconds of the last.
     */
    void set_clip_limit(const hpp::uuid& clip_uid, int max_voices, float min_interval);

    void stop(uint64_t id);
    /// Whether the voice is alive (playing, or paused with the game).
    auto is_playing(uint64_t id) const -> bool;
    auto get_source(uint64_t id) const -> audio::source*;
    /// Sets the gain before the bus and master gains; ends a running fade.
    void set_volume(uint64_t id, float volume);
    void set_pitch(uint64_t id, float pitch);
    /// Sets a spatial voice's distance range: full volume within min, silent from max.
    void set_distance(uint64_t id, const frange_t& range);
    void set_bus(uint64_t id, audio_bus bus);
    void set_priority(uint64_t id, int priority);
    void set_loop(uint64_t id, bool loop);
    void fade_to(uint64_t id, float volume, float seconds, bool stop_at_end);

    /**
     * @brief Streams, advances fades and frees finished voices. @p seconds is unscaled time.
     */
    void update(float seconds);

    /// Re-applies every voice's gain after the mixer changed.
    void set_bus_gains(const bus_gains& gains);
    void pause_all();
    void resume_all();
    /// Stops every voice and forgets the clip limits (a play session ended).
    void stop_all();

    auto get_live_count() const -> std::size_t;

private:
    struct voice
    {
        std::unique_ptr<audio::source> source;
        /// Keeps a one-shot's clip alive: a source only points at the sound it plays. An owner's clip
        /// lives as long as the owner keeps it.
        std::shared_ptr<audio_clip> clip;
        /// Clip of a one-shot, for its clip limit.
        hpp::uuid clip_uid{};
        audio_bus bus = audio_bus::sfx;
        bool is_one_shot = true;
        bool is_looping = false;
        int priority = 0;
        float volume = 1.0f;
        volume_fade fade{};
        uint32_t generation = 1;
        /// Play order, for oldest-first freeing.
        uint64_t serial = 0;
    };

    struct clip_limit
    {
        int max_voices = 0;
        std::chrono::duration<float> min_interval{0.0f};
        std::chrono::steady_clock::time_point last_start{};
        bool has_started = false;
    };

    using time_point = std::chrono::steady_clock::time_point;

    auto find_voice(uint64_t id) -> voice*;
    auto find_voice(uint64_t id) const -> const voice*;
    auto make_id(uint32_t index) const -> uint64_t;
    auto is_start_allowed(const hpp::uuid& clip_uid, time_point now) const -> bool;
    void mark_started(const hpp::uuid& clip_uid, time_point now);
    void free_clip_overflow(const hpp::uuid& clip_uid);
    auto create_source() -> std::unique_ptr<audio::source>;
    void make_room();
    auto find_steal_candidate() const -> hpp::optional<uint32_t>;
    auto add_voice(std::unique_ptr<audio::source> source, const owned_voice_params& params, bool is_one_shot)
        -> uint32_t;
    void apply_gain(voice& v) const;
    void release(uint32_t index);

    std::vector<voice> voices_;
    std::vector<uint32_t> free_slots_;
    std::unordered_map<hpp::uuid, clip_limit> limits_;
    bus_gains gains_{1.0f, 1.0f, 1.0f};
    std::size_t max_voices_ = 0;
    std::size_t live_count_ = 0;
    uint64_t next_serial_ = 1;
    /// Handles hold a weak reference to this, so a handle that outlives the pool does nothing.
    std::shared_ptr<audio_voice_pool*> self_;
};

} // namespace unravel
