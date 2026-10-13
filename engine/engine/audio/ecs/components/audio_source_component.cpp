#include "audio_source_component.h"

#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/engine.h>
#include <limits>

namespace unravel
{

namespace
{
constexpr float max_volume_rolloff = 10.0f;

auto find_voice_pool() -> audio_voice_pool*
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>())
    {
        return nullptr;
    }
    return &ctx.get_cached<audio_system>().get_voices();
}
} // namespace

void audio_source_component::on_play_begin()
{
    voice_.reset();
    auto_play_consumed_ = false;
}
void audio_source_component::on_play_end()
{
    auto_play_consumed_ = true;
    voice_.reset();
}

void audio_source_component::update(const math::transform& t)
{
    if(get_autoplay() && !auto_play_consumed_)
    {
        if(play())
        {
            auto_play_consumed_ = true;
        }
    }

    auto source = get_source();
    if(!source)
    {
        return;
    }

    if(spatial_)
    {
        auto pos = t.get_position();
        auto forward = t.z_unit_axis();
        auto up = t.y_unit_axis();
        source->set_position({{pos.x, pos.y, pos.z}});
        source->set_orientation({{forward.x, forward.y, forward.z}}, {{up.x, up.y, up.z}});
    }
    else
    {
        // Listener-relative at the listener origin = non-spatial / UI / music.
        source->set_position({{0.0f, 0.0f, 0.0f}});
    }
}

void audio_source_component::set_loop(bool on)
{
    loop_ = on;
    voice_.set_loop(on);
}

void audio_source_component::set_volume(float volume)
{
    fade_.is_active = false;
    volume_ = clamp_audio_gain(volume);
    voice_.set_volume(volume_);
}

void audio_source_component::fade_to(float volume, float seconds, bool stop_at_end)
{
    fade_.start(volume_, clamp_audio_gain(volume), seconds, stop_at_end);
}

void audio_source_component::update_fade(float seconds)
{
    if(!fade_.is_active)
    {
        return;
    }
    volume_ = clamp_audio_gain(fade_.advance(seconds));
    voice_.set_volume(volume_);
    if(!fade_.is_active && fade_.stop_at_end)
    {
        stop();
    }
}

void audio_source_component::set_pitch(float pitch)
{
    pitch_ = math::clamp(pitch, 0.0f, audio_max_pitch);

    if(auto source = get_source())
    {
        source->set_pitch(pitch_);
    }
}

void audio_source_component::set_volume_rolloff(float rolloff)
{
    volume_rolloff_ = math::clamp(rolloff, 0.0f, max_volume_rolloff);

    if(auto source = get_source())
    {
        apply_voice_space(*source, spatial_, volume_rolloff_);
    }
}

void audio_source_component::set_range(const frange_t& range)
{
    range_ = clamp_audio_range(range);
    if(auto source = get_source())
    {
        source->set_distance(range_.min, range_.max);
    }
}

void audio_source_component::set_autoplay(bool on)
{
    auto_play_ = on;
}

auto audio_source_component::get_autoplay() const -> bool
{
    return auto_play_;
}

auto audio_source_component::get_volume() const -> float
{
    return volume_;
}

auto audio_source_component::get_pitch() const -> float
{
    return pitch_;
}

auto audio_source_component::get_volume_rolloff() const -> float
{
    return volume_rolloff_;
}

auto audio_source_component::get_range() const -> const frange_t&
{
    return range_;
}

void audio_source_component::set_playback_position(audio::duration_t offset)
{
    if(auto source = get_source())
    {
        source->set_playback_position(offset);
    }
}

auto audio_source_component::get_playback_position() const -> audio::duration_t
{
    auto source = get_source();
    return source ? source->get_playback_position() : audio::duration_t{};
}

auto audio_source_component::get_playback_duration() const -> audio::duration_t
{
    auto source = get_source();
    return source ? source->get_playback_duration() : audio::duration_t{};
}

auto audio_source_component::play() -> bool
{
    // Check the clip first: a source without a playable clip would hold a voice it never uses.
    if(!sound_)
    {
        return false;
    }
    // get() hands back the shared empty clip for a failed load.
    auto clip = sound_.get();
    if(!clip || !clip->is_valid())
    {
        return false;
    }

    if(!voice_.is_alive() && !create_source())
    {
        return false;
    }

    auto source = get_source();
    source->bind(*clip);
    source->play();
    return true;
}

void audio_source_component::stop()
{
    voice_.reset();
}

void audio_source_component::pause()
{
    if(auto source = get_source())
    {
        source->pause();
    }
}

void audio_source_component::resume()
{
    if(auto source = get_source())
    {
        source->resume();
    }
}

void audio_source_component::set_mute(bool mute)
{
    muted_ = mute;

    auto source = get_source();
    if(!source)
    {
        return;
    }

    if(mute)
    {
        source->mute();
    }
    else
    {
        source->unmute();
    }
}

auto audio_source_component::is_muted() const -> bool
{
    return muted_;
}

auto audio_source_component::is_playing() const -> bool
{
    auto source = get_source();
    return source && source->is_playing();
}

auto audio_source_component::is_paused() const -> bool
{
    auto source = get_source();
    return source && source->is_paused();
}

auto audio_source_component::is_looping() const -> bool
{
    return loop_;
}

void audio_source_component::set_spatial(bool spatial)
{
    spatial_ = spatial;
    if(auto source = get_source())
    {
        apply_voice_space(*source, spatial_, volume_rolloff_);
    }
}

auto audio_source_component::is_spatial() const -> bool
{
    return spatial_;
}

void audio_source_component::set_bus(audio_bus bus)
{
    bus_ = bus;
    voice_.set_bus(bus);
}

auto audio_source_component::get_bus() const -> audio_bus
{
    return bus_;
}

void audio_source_component::set_priority(int priority)
{
    priority_ = priority;
    voice_.set_priority(priority);
}

auto audio_source_component::get_priority() const -> int
{
    return priority_;
}

auto audio_source_component::has_source() const -> bool
{
    return voice_.is_alive();
}

void audio_source_component::set_clip(const asset_handle<audio_clip>& clip)
{
    const bool was_playing = is_playing();
    const bool was_paused = is_paused();

    stop();

    sound_ = clip;

    apply_all();

    if(was_playing)
    {
        play();
    }
    else if(was_paused)
    {
        if(play())
        {
            pause();
        }
    }
}

auto audio_source_component::get_clip() const -> const asset_handle<audio_clip>&
{
    return sound_;
}

auto audio_source_component::has_bound_sound() const -> bool
{
    auto source = get_source();
    return source && source->has_bound_sound();
}

void audio_source_component::apply_all()
{
    set_loop(loop_);
    set_spatial(spatial_);
    set_volume(volume_);
    set_pitch(pitch_);
    set_volume_rolloff(volume_rolloff_);
    set_range(range_);
    set_autoplay(auto_play_);
    set_mute(muted_);
}

auto audio_source_component::create_source() -> bool
{
    auto pool = find_voice_pool();
    if(!pool)
    {
        return false;
    }
    voice_ = pool->acquire({volume_, bus_, priority_, loop_});
    if(!voice_.is_alive())
    {
        return false;
    }
    apply_all();
    return true;
}

auto audio_source_component::get_source() const -> audio::source*
{
    return voice_.get_source();
}

} // namespace unravel
