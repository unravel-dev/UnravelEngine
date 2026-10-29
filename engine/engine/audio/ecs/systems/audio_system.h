#pragma once
#include <engine/engine_export.h>

#include <engine/audio/audio_bus.h>
#include <engine/audio/audio_voice_pool.h>
#include <engine/audio/volume_fade.h>
#include <engine/assets/asset_handle.h>
#include <audiopp/device.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/frame_times.h>
#include <cstddef>
#include <memory>

namespace unravel
{

/**
 * @class audio_system
 * @brief Manages the audio operations and integrates with the audio backend.
 */
class audio_system
{
public:
    /// Soft cap on concurrent voices (component sources and one-shots) before voice stealing.
    static constexpr std::size_t max_concurrent_voices = 64;

    /**
     * @brief Initializes the audio system with the given context.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the audio system with the given context.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Sets the master mixer gain applied to every source.
     * @param volume Gain in [0, 1].
     */
    void set_master_volume(float volume);

    /**
     * @brief Gets the master mixer gain.
     */
    auto get_master_volume() const -> float;

    /**
     * @brief Sets a bus gain (SFX / Music / UI).
     * @param bus Target bus.
     * @param volume Gain in [0, 1].
     */
    void set_bus_volume(audio_bus bus, float volume);

    /**
     * @brief Gets a bus gain.
     */
    auto get_bus_volume(audio_bus bus) const -> float;

    /**
     * @brief Combined master * bus gain for a source bus.
     */
    auto get_effective_bus_gain(audio_bus bus) const -> float;

    /**
     * @brief Ramps a bus gain to @p target over @p seconds of unscaled time. A later
     * set_bus_volume or fade replaces it.
     */
    void fade_bus_volume(audio_bus bus, float target, float seconds);

    /**
     * @brief Plays @p clip once on a pooled voice, without an entity.
     * @return The voice id for the voice calls of get_voices(), or 0 when it did not play.
     */
    auto play_one_shot(const asset_handle<audio_clip>& clip, const one_shot_params& params) -> uint64_t;

    /**
     * @brief Every playing source: one-shots (stop, fade, pitch, per-clip limits) and the voices audio
     * source components hold. It applies the voice cap.
     */
    auto get_voices() -> audio_voice_pool&;

private:
    /**
     * @brief Stores a bus gain and re-applies the mixer to every source and voice.
     */
    void apply_bus_volume(audio_bus bus, float volume);

    /**
     * @brief Pushes the effective bus gains to every voice.
     */
    void refresh_voice_gains();

    /**
     * @brief Advances bus fades, source fades and one-shot voices by unscaled time.
     */
    void update_fades(rtti::context& ctx, float seconds);

    /**
     * @brief Updates the audio system for each frame.
     * @param ctx The context for the update.
     * @param times The frame's times: listeners follow the scaled time, fades the unscaled one.
     */
    void on_frame_update(rtti::context& ctx, const frame_times& times);

    /**
     * @brief Called when playback begins.
     * @param ctx The context for the playback.
     */
    void on_play_begin(rtti::context& ctx);

    /**
     * @brief Called when playback ends.
     * @param ctx The context for the playback.
     */
    void on_play_end(rtti::context& ctx);

    /**
     * @brief Called when playback is paused.
     * @param ctx The context for the playback.
     */
    void on_pause(rtti::context& ctx);

    /**
     * @brief Called when playback is resumed.
     * @param ctx The context for the playback.
     */
    void on_resume(rtti::context& ctx);

    /**
     * @brief Pauses the audio device while the application is in the background.
     * @param ctx The engine context.
     */
    void on_app_enter_background(rtti::context& ctx);

    /**
     * @brief Resumes the audio device when the application is back in the foreground.
     * @param ctx The engine context.
     */
    void on_app_enter_foreground(rtti::context& ctx);

    /// Sentinel value to manage shared resources.
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    /// The audio device used for playback.
    std::unique_ptr<audio::device> device_;
    /// After the device: the voices release their sources before the device closes.
    audio_voice_pool voices_{max_concurrent_voices};
    float master_volume_ = 1.0f;
    float bus_volumes_[audio_bus_count] = {1.0f, 1.0f, 1.0f};
    volume_fade bus_fades_[audio_bus_count] = {};
};

} // namespace unravel
