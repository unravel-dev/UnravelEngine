#include "audio_voice_pool.h"

#include <audiopp/exception.h>
#include <audiopp/source.h>
#include <logging/logging.h>

#include <algorithm>
#include <limits>

namespace unravel
{

namespace
{

constexpr float max_pitch = 5.0f;
constexpr uint32_t generation_shift = 32;
constexpr uint64_t slot_mask = 0xffffffffull;
/// Distance range of spatial voices, as an audio source's default range.
constexpr float spatial_min_distance = 1.0f;
constexpr float spatial_max_distance = 20.0f;

auto clamp_gain(float gain) -> float
{
    return std::clamp(gain, 0.0f, 1.0f);
}

} // namespace

audio_voice_pool::audio_voice_pool() = default;

audio_voice_pool::~audio_voice_pool() = default;

auto audio_voice_pool::make_id(uint32_t index) const -> uint64_t
{
    // The slot is stored 1-based so that 0 never names a voice.
    return (uint64_t(voices_[index].generation) << generation_shift) | uint64_t(index + 1u);
}

auto audio_voice_pool::find_voice(uint64_t id) -> voice*
{
    const auto slot = uint64_t(id & slot_mask);
    if(slot == 0 || slot > voices_.size())
    {
        return nullptr;
    }
    auto& v = voices_[slot - 1];
    if(!v.source || v.generation != uint32_t(id >> generation_shift))
    {
        return nullptr;
    }
    return &v;
}

auto audio_voice_pool::find_voice(uint64_t id) const -> const voice*
{
    return const_cast<audio_voice_pool*>(this)->find_voice(id);
}

auto audio_voice_pool::admit(const hpp::uuid& clip_uid) -> bool
{
    auto it = limits_.find(clip_uid);
    if(it == limits_.end())
    {
        return true;
    }
    auto& limit = it->second;
    const auto now = std::chrono::steady_clock::now();
    if(limit.has_started && now - limit.last_start < limit.min_interval)
    {
        return false;
    }
    while(limit.max_voices > 0)
    {
        int count = 0;
        uint32_t oldest = 0;
        uint64_t oldest_serial = std::numeric_limits<uint64_t>::max();
        for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
        {
            const auto& v = voices_[i];
            if(!v.source || v.clip_uid != clip_uid)
            {
                continue;
            }
            ++count;
            if(v.serial < oldest_serial)
            {
                oldest_serial = v.serial;
                oldest = i;
            }
        }
        if(count < limit.max_voices)
        {
            break;
        }
        release(oldest);
    }
    limit.last_start = now;
    limit.has_started = true;
    return true;
}

auto audio_voice_pool::play(const std::shared_ptr<audio_clip>& clip,
                            const hpp::uuid& clip_uid,
                            const one_shot_params& params) -> uint64_t
{
    if(!clip || !admit(clip_uid))
    {
        return 0;
    }
    std::shared_ptr<audio::source> source;
    try
    {
        source = std::make_shared<audio::source>();
    }
    catch(const audio::exception& e)
    {
        APPLOG_ERROR(e.what());
        return 0;
    }
    source->set_loop(false);
    source->set_source_relative(!params.spatial);
    // Non-spatial voices sit on the listener and must not distance-attenuate.
    source->set_volume_rolloff(params.spatial ? 1.0f : 0.0f);
    source->set_distance(spatial_min_distance, spatial_max_distance);
    const math::vec3 position = params.spatial ? params.position : math::vec3(0.0f);
    source->set_position({{position.x, position.y, position.z}});
    source->set_pitch(std::clamp(params.pitch, 0.0f, max_pitch));
    uint32_t index = 0;
    if(!free_slots_.empty())
    {
        index = free_slots_.back();
        free_slots_.pop_back();
    }
    else
    {
        index = uint32_t(voices_.size());
        voices_.emplace_back();
    }
    auto& v = voices_[index];
    v.source = source;
    v.clip = clip;
    v.clip_uid = clip_uid;
    v.bus = params.bus;
    v.priority = params.priority;
    v.volume = clamp_gain(params.volume);
    v.fade = {};
    v.serial = next_serial_++;
    apply_gain(v);
    source->bind(*clip);
    source->play();
    return make_id(index);
}

void audio_voice_pool::set_clip_limit(const hpp::uuid& clip_uid, int max_voices, float min_interval)
{
    if(max_voices <= 0 && min_interval <= 0.0f)
    {
        limits_.erase(clip_uid);
        return;
    }
    auto& limit = limits_[clip_uid];
    limit.max_voices = std::max(max_voices, 0);
    limit.min_interval = std::chrono::duration<float>(std::max(min_interval, 0.0f));
}

void audio_voice_pool::stop(uint64_t id)
{
    if(find_voice(id))
    {
        release(uint32_t((id & slot_mask) - 1));
    }
}

auto audio_voice_pool::is_playing(uint64_t id) const -> bool
{
    return find_voice(id) != nullptr;
}

void audio_voice_pool::set_volume(uint64_t id, float volume)
{
    if(auto v = find_voice(id))
    {
        v->volume = clamp_gain(volume);
        v->fade.is_active = false;
        apply_gain(*v);
    }
}

void audio_voice_pool::set_pitch(uint64_t id, float pitch)
{
    if(auto v = find_voice(id))
    {
        v->source->set_pitch(std::clamp(pitch, 0.0f, max_pitch));
    }
}

void audio_voice_pool::fade_to(uint64_t id, float volume, float seconds, bool stop_at_end)
{
    if(auto v = find_voice(id))
    {
        v->fade.start(v->volume, clamp_gain(volume), seconds, stop_at_end);
    }
}

void audio_voice_pool::update(float seconds)
{
    for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
    {
        auto& v = voices_[i];
        if(!v.source)
        {
            continue;
        }
        v.source->update(audio::duration_t(seconds));
        if(v.fade.is_active)
        {
            v.volume = clamp_gain(v.fade.advance(seconds));
            apply_gain(v);
            if(!v.fade.is_active && v.fade.stop_at_end)
            {
                release(i);
                continue;
            }
        }
        if(v.source->is_stopped())
        {
            release(i);
        }
    }
}

void audio_voice_pool::set_bus_gains(const bus_gains& gains)
{
    gains_ = gains;
    for(auto& v : voices_)
    {
        if(v.source)
        {
            apply_gain(v);
        }
    }
}

void audio_voice_pool::pause_all()
{
    for(auto& v : voices_)
    {
        if(v.source)
        {
            v.source->pause();
        }
    }
}

void audio_voice_pool::resume_all()
{
    for(auto& v : voices_)
    {
        if(v.source)
        {
            v.source->resume();
        }
    }
}

void audio_voice_pool::stop_all()
{
    for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
    {
        if(voices_[i].source)
        {
            release(i);
        }
    }
    limits_.clear();
}

auto audio_voice_pool::get_live_count() const -> std::size_t
{
    return std::size_t(std::count_if(voices_.begin(),
                                     voices_.end(),
                                     [](const voice& v)
                                     {
                                         return static_cast<bool>(v.source);
                                     }));
}

auto audio_voice_pool::find_steal_candidate() const -> steal_candidate
{
    steal_candidate best;
    uint64_t best_serial = std::numeric_limits<uint64_t>::max();
    for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
    {
        const auto& v = voices_[i];
        if(!v.source)
        {
            continue;
        }
        const bool is_better =
            best.voice == 0 || v.priority < best.priority || (v.priority == best.priority && v.serial < best_serial);
        if(is_better)
        {
            best.voice = make_id(i);
            best.priority = v.priority;
            best_serial = v.serial;
        }
    }
    return best;
}

void audio_voice_pool::apply_gain(voice& v) const
{
    const auto bus = std::min<std::size_t>(std::size_t(v.bus), audio_bus_count - 1);
    v.source->set_volume(clamp_gain(v.volume * gains_[bus]));
}

void audio_voice_pool::release(uint32_t index)
{
    auto& v = voices_[index];
    if(v.source)
    {
        v.source->force_stop();
    }
    v.source.reset();
    v.clip.reset();
    v.fade = {};
    ++v.generation;
    free_slots_.push_back(index);
}

} // namespace unravel
