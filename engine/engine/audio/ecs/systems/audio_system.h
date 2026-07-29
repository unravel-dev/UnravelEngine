#pragma once
#include <engine/engine_export.h>

#include <engine/audio/audio_bus.h>
#include <audiopp/device.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <cstddef>
#include <memory>

namespace unravel
{

class audio_source_component;

/**
 * @class audio_system
 * @brief Manages the audio operations and integrates with the audio backend.
 */
class audio_system
{
public:
    /// Soft cap on concurrent OpenAL sources before voice stealing.
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
     * @brief Frees a voice if at the soft cap so a new OpenAL source can be created.
     * Steals the lowest-priority non-looping playing source when needed.
     */
    void ensure_voice_capacity(audio_source_component& requesting);

    /**
     * @brief Re-applies volume on all live sources after mixer gains change.
     */
    void refresh_source_volumes(rtti::context& ctx);

private:
    /**
     * @brief Updates the audio system for each frame.
     * @param ctx The context for the update.
     * @param dt The delta time for the frame.
     */
    void on_frame_update(rtti::context& ctx, delta_t dt);

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
     * @brief Skips the next frame update.
     * @param ctx The context for the update.
     */
    void on_skip_next_frame(rtti::context& ctx);

    /// Sentinel value to manage shared resources.
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);
    /// The audio device used for playback.
    std::unique_ptr<audio::device> device_;
    float master_volume_ = 1.0f;
    float bus_volumes_[audio_bus_count] = {1.0f, 1.0f, 1.0f};
};

} // namespace unravel
