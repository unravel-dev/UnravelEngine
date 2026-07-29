#include "audio_source_component.h"

#include <engine/audio/ecs/systems/audio_system.h>
#include <engine/engine.h>
#include <audiopp/exception.h>
#include <logging/logging.h>
#include <limits>

namespace unravel
{

namespace
{
auto get_mixer_gain(audio_bus bus) -> float
{
    auto& ctx = engine::context();
    if(!ctx.has<audio_system>())
    {
        return 1.0f;
    }
    return ctx.get_cached<audio_system>().get_effective_bus_gain(bus);
}
} // namespace

void audio_source_component::on_play_begin()
{
    source_.reset();
    auto_play_consumed_ = false;
}
void audio_source_component::on_play_end()
{
    auto_play_consumed_ = true;
    if(source_)
    {
        source_->stop();
        source_.reset();
    }
}

void audio_source_component::update(const math::transform& t, delta_t dt)
{
    if(get_autoplay() && !auto_play_consumed_)
    {
        if(play())
        {
            auto_play_consumed_ = true;
        }
    }

    if(!source_)
    {
        return;
    }

    source_->update(std::chrono::milliseconds(16));
    if(spatial_)
    {
        auto pos = t.get_position();
        auto forward = t.z_unit_axis();
        auto up = t.y_unit_axis();
        source_->set_position({{pos.x, pos.y, pos.z}});
        source_->set_orientation({{forward.x, forward.y, forward.z}}, {{up.x, up.y, up.z}});
    }
    else
    {
        // Listener-relative at the listener origin = non-spatial / UI / music.
        source_->set_position({{0.0f, 0.0f, 0.0f}});
    }

    if(source_->is_stopped())
    {
        source_.reset();
    }
}

void audio_source_component::set_loop(bool on)
{
    loop_ = on;

    if(!source_)
    {
        return;
    }
    source_->set_loop(on);
}

void audio_source_component::set_volume(float volume)
{
    volume_ = math::clamp(volume, 0.0f, 1.0f);
    apply_mixer_volume();
}

void audio_source_component::set_pitch(float pitch)
{
    pitch_ = math::clamp(pitch, 0.0f, 5.0f);

    if(!source_)
    {
        return;
    }
    source_->set_pitch(pitch_);
}

void audio_source_component::set_volume_rolloff(float rolloff)
{
    volume_rolloff_ = math::clamp(rolloff, 0.0f, 10.0f);

    if(!source_)
    {
        return;
    }
    // Non-spatial sources should not distance-attenuate.
    source_->set_volume_rolloff(spatial_ ? volume_rolloff_ : 0.0f);
}

void audio_source_component::set_range(const frange_t& range)
{
    range_ = range;
    range_.min = math::clamp(range_.min, 0.0f, range_.max);
    range_.max = math::clamp(range_.max, range_.min, std::numeric_limits<float>::max());


    if(!source_)
    {
        return;
    }
    source_->set_distance(range_.min, range_.max);
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
    if(!source_)
    {
        return;
    }
    source_->set_playback_position(offset);
}

auto audio_source_component::get_playback_position() const -> audio::duration_t
{
    if(!source_)
    {
        return {};
    }
    return source_->get_playback_position();
}

auto audio_source_component::get_playback_duration() const -> audio::duration_t
{
    if(!source_)
    {
        return {};
    }

    return source_->get_playback_duration();
}

auto audio_source_component::play() -> bool
{
    if(!source_)
    {
        if(!create_source())
        {
            return false;
        }
    }

    if(source_ && sound_)
    {
        source_->bind(*sound_.get());
        source_->play();
        return true;
    }

    return false;
}

void audio_source_component::stop()
{
    if(!source_)
    {
        return;
    }
    source_->stop();
    source_.reset();
}

void audio_source_component::pause()
{
    if(!source_)
    {
        return;
    }
    source_->pause();
}

void audio_source_component::resume()
{
    if(!source_)
    {
        return;
    }
    source_->resume();
}

void audio_source_component::set_mute(bool mute)
{
    muted_ = mute;

    if(!source_)
    {
        return;
    }

    if(mute)
    {
        source_->mute();
    }
    else
    {
        source_->unmute();
    }
}

auto audio_source_component::is_muted() const -> bool
{
    return muted_;
}

auto audio_source_component::is_playing() const -> bool
{
    if(!source_)
    {
        return false;
    }
    return source_->is_playing();
}

auto audio_source_component::is_paused() const -> bool
{
    if(!source_)
    {
        return false;
    }
    return source_->is_paused();
}

auto audio_source_component::is_looping() const -> bool
{
    return loop_;
}

void audio_source_component::set_spatial(bool spatial)
{
    spatial_ = spatial;
    if(!source_)
    {
        return;
    }
    source_->set_source_relative(!spatial_);
    source_->set_volume_rolloff(spatial_ ? volume_rolloff_ : 0.0f);
    if(!spatial_)
    {
        source_->set_position({{0.0f, 0.0f, 0.0f}});
    }
}

auto audio_source_component::is_spatial() const -> bool
{
    return spatial_;
}

void audio_source_component::set_bus(audio_bus bus)
{
    bus_ = bus;
    apply_mixer_volume();
}

auto audio_source_component::get_bus() const -> audio_bus
{
    return bus_;
}

void audio_source_component::set_priority(int priority)
{
    priority_ = priority;
}

auto audio_source_component::get_priority() const -> int
{
    return priority_;
}

auto audio_source_component::has_source() const -> bool
{
    return static_cast<bool>(source_);
}

void audio_source_component::release_source()
{
    if(!source_)
    {
        return;
    }
    source_->force_stop();
    source_.reset();
}

void audio_source_component::apply_mixer_volume()
{
    if(!source_)
    {
        return;
    }
    const float gain = volume_ * get_mixer_gain(bus_);
    source_->set_volume(math::clamp(gain, 0.0f, 1.0f));
}

void audio_source_component::set_clip(const asset_handle<audio_clip>& clip)
{
    stop();

    sound_ = clip;

    apply_all();
}

auto audio_source_component::get_clip() const -> const asset_handle<audio_clip>&
{
    return sound_;
}

auto audio_source_component::has_bound_sound() const -> bool
{
    if(!source_)
    {
        return false;
    }

    return source_->has_bound_sound();
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

auto audio_source_component::is_sound_valid() const -> bool
{
    return sound_;
}

auto audio_source_component::create_source() -> bool
{
    auto& ctx = engine::context();
    if(ctx.has<audio_system>())
    {
        ctx.get_cached<audio_system>().ensure_voice_capacity(*this);
    }

    try
    {
        source_ = std::make_shared<audio::source>();
        apply_all();
        return true;
    }
    catch(const audio::exception& e)
    {
        // Retry once after forcing a steal — OpenAL hard-cap may still be hit.
        if(ctx.has<audio_system>())
        {
            ctx.get_cached<audio_system>().ensure_voice_capacity(*this);
            try
            {
                source_ = std::make_shared<audio::source>();
                apply_all();
                return true;
            }
            catch(const audio::exception& retry_error)
            {
                APPLOG_ERROR(retry_error.what());
                return false;
            }
        }
        APPLOG_ERROR(e.what());
        return false;
    }
}

} // namespace unravel
