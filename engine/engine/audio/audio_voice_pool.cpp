#include "audio_voice_pool.h"

#include <audiopp/exception.h>
#include <audiopp/source.h>
#include <logging/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace unravel
{

namespace
{

constexpr uint32_t generation_shift = 32;
constexpr uint64_t slot_mask = 0xffffffffull;
/// Rolloff of a spatial one-shot, as an audio source's default.
constexpr float one_shot_rolloff = 1.0f;

auto clamp_pitch(float pitch) -> float
{
    return std::clamp(pitch, 0.0f, audio_max_pitch);
}

} // namespace

auto clamp_audio_gain(float gain) -> float
{
    // fmax drops a NaN operand, so NaN lands on 0.
    return std::fmin(1.0f, std::fmax(0.0f, gain));
}

auto clamp_audio_range(const frange_t& range) -> frange_t
{
    frange_t result = range;
    result.min = math::clamp(result.min, 0.0f, result.max);
    result.max = math::clamp(result.max, result.min, std::numeric_limits<float>::max());
    return result;
}

void apply_voice_space(audio::source& source, bool spatial, float rolloff)
{
    source.set_source_relative(!spatial);
    // Listener-relative voices sit on the listener and must not distance-attenuate.
    source.set_volume_rolloff(spatial ? rolloff : 0.0f);
    if(!spatial)
    {
        source.set_position({{0.0f, 0.0f, 0.0f}});
    }
}

auto is_freed_before(const audio_voice_rank& lhs, const audio_voice_rank& rhs) -> bool
{
    return std::make_tuple(lhs.is_looping, lhs.priority, !lhs.is_one_shot, lhs.serial) <
           std::make_tuple(rhs.is_looping, rhs.priority, !rhs.is_one_shot, rhs.serial);
}

audio_voice_handle::audio_voice_handle(std::weak_ptr<audio_voice_pool*> pool, uint64_t id)
    : pool_(std::move(pool))
    , id_(id)
{
}

audio_voice_handle::~audio_voice_handle()
{
    reset();
}

audio_voice_handle::audio_voice_handle(const audio_voice_handle& /*rhs*/) noexcept
{
}

auto audio_voice_handle::operator=(const audio_voice_handle& rhs) -> audio_voice_handle&
{
    if(this != &rhs)
    {
        reset();
    }
    return *this;
}

audio_voice_handle::audio_voice_handle(audio_voice_handle&& rhs) noexcept
    : pool_(std::move(rhs.pool_))
    , id_(std::exchange(rhs.id_, 0))
{
}

auto audio_voice_handle::operator=(audio_voice_handle&& rhs) noexcept -> audio_voice_handle&
{
    if(this != &rhs)
    {
        reset();
        pool_ = std::move(rhs.pool_);
        id_ = std::exchange(rhs.id_, 0);
    }
    return *this;
}

auto audio_voice_handle::get_pool() const -> audio_voice_pool*
{
    if(id_ == 0)
    {
        return nullptr;
    }
    const auto pool = pool_.lock();
    return pool ? *pool : nullptr;
}

auto audio_voice_handle::get_source() const -> audio::source*
{
    auto pool = get_pool();
    return pool ? pool->get_source(id_) : nullptr;
}

auto audio_voice_handle::is_alive() const -> bool
{
    auto pool = get_pool();
    return pool && pool->is_playing(id_);
}

void audio_voice_handle::set_volume(float volume)
{
    if(auto pool = get_pool())
    {
        pool->set_volume(id_, volume);
    }
}

void audio_voice_handle::set_bus(audio_bus bus)
{
    if(auto pool = get_pool())
    {
        pool->set_bus(id_, bus);
    }
}

void audio_voice_handle::set_priority(int priority)
{
    if(auto pool = get_pool())
    {
        pool->set_priority(id_, priority);
    }
}

void audio_voice_handle::set_loop(bool loop)
{
    if(auto pool = get_pool())
    {
        pool->set_loop(id_, loop);
    }
}

void audio_voice_handle::reset()
{
    if(auto pool = get_pool())
    {
        pool->stop(id_);
    }
    pool_.reset();
    id_ = 0;
}

audio_voice_pool::audio_voice_pool(std::size_t max_voices)
    : max_voices_(max_voices)
    , self_(std::make_shared<audio_voice_pool*>(this))
{
}

audio_voice_pool::~audio_voice_pool()
{
    // Expire the handles first: an owner destroyed later must not reach a dead pool.
    self_.reset();
    stop_all();
}

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

auto audio_voice_pool::is_start_allowed(const hpp::uuid& clip_uid, time_point now) const -> bool
{
    auto it = limits_.find(clip_uid);
    if(it == limits_.end())
    {
        return true;
    }
    const auto& limit = it->second;
    return !limit.has_started || now - limit.last_start >= limit.min_interval;
}

void audio_voice_pool::mark_started(const hpp::uuid& clip_uid, time_point now)
{
    auto it = limits_.find(clip_uid);
    if(it != limits_.end())
    {
        it->second.last_start = now;
        it->second.has_started = true;
    }
}

void audio_voice_pool::free_clip_overflow(const hpp::uuid& clip_uid)
{
    auto it = limits_.find(clip_uid);
    if(it == limits_.end())
    {
        return;
    }
    const int max_voices = it->second.max_voices;
    while(max_voices > 0)
    {
        int count = 0;
        uint32_t oldest = 0;
        uint64_t oldest_serial = std::numeric_limits<uint64_t>::max();
        for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
        {
            const auto& v = voices_[i];
            if(!v.source || !v.is_one_shot || v.clip_uid != clip_uid)
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
        if(count < max_voices)
        {
            return;
        }
        release(oldest);
    }
}

auto audio_voice_pool::create_source() -> std::unique_ptr<audio::source>
{
    try
    {
        return std::make_unique<audio::source>();
    }
    catch(const audio::exception&)
    {
    }
    // The device ran out of sources below the cap: free one voice and try once more.
    if(const auto candidate = find_steal_candidate())
    {
        release(*candidate);
    }
    try
    {
        return std::make_unique<audio::source>();
    }
    catch(const audio::exception& e)
    {
        APPLOG_ERROR(e.what());
        return nullptr;
    }
}

void audio_voice_pool::make_room()
{
    while(live_count_ >= max_voices_)
    {
        const auto candidate = find_steal_candidate();
        if(!candidate)
        {
            return;
        }
        release(*candidate);
    }
}

auto audio_voice_pool::find_steal_candidate() const -> hpp::optional<uint32_t>
{
    hpp::optional<uint32_t> best;
    audio_voice_rank best_rank;
    for(uint32_t i = 0; i < uint32_t(voices_.size()); ++i)
    {
        const auto& v = voices_[i];
        if(!v.source)
        {
            continue;
        }
        const audio_voice_rank rank{v.is_looping, v.priority, v.is_one_shot, v.serial};
        if(!best || is_freed_before(rank, best_rank))
        {
            best = i;
            best_rank = rank;
        }
    }
    return best;
}

auto audio_voice_pool::add_voice(std::unique_ptr<audio::source> source,
                                 const owned_voice_params& params,
                                 bool is_one_shot) -> uint32_t
{
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
    v.source = std::move(source);
    v.clip.reset();
    v.clip_uid = {};
    v.bus = params.bus;
    v.is_one_shot = is_one_shot;
    v.is_looping = params.loop;
    v.priority = params.priority;
    v.volume = clamp_audio_gain(params.volume);
    v.fade = {};
    v.serial = next_serial_++;
    ++live_count_;
    v.source->set_loop(params.loop);
    apply_gain(v);
    return index;
}

auto audio_voice_pool::play(const std::shared_ptr<audio_clip>& clip,
                            const hpp::uuid& clip_uid,
                            const one_shot_params& params) -> uint64_t
{
    const auto now = std::chrono::steady_clock::now();
    if(!clip || !clip->is_valid() || !is_start_allowed(clip_uid, now))
    {
        return 0;
    }
    auto source = create_source();
    if(!source)
    {
        return 0;
    }
    // A clip at its own limit makes room by stopping its oldest voice, before anything else is freed.
    free_clip_overflow(clip_uid);
    make_room();
    apply_voice_space(*source, params.spatial, one_shot_rolloff);
    source->set_distance(audio_default_min_distance, audio_default_max_distance);
    if(params.spatial)
    {
        source->set_position({{params.position.x, params.position.y, params.position.z}});
    }
    source->set_pitch(clamp_pitch(params.pitch));
    const owned_voice_params voice_params{params.volume, params.bus, params.priority, false};
    const auto index = add_voice(std::move(source), voice_params, true);
    auto& v = voices_[index];
    v.clip = clip;
    v.clip_uid = clip_uid;
    mark_started(clip_uid, now);
    v.source->bind(*clip);
    v.source->play();
    return make_id(index);
}

auto audio_voice_pool::acquire(const owned_voice_params& params) -> audio_voice_handle
{
    auto source = create_source();
    if(!source)
    {
        return {};
    }
    make_room();
    const auto index = add_voice(std::move(source), params, false);
    return audio_voice_handle(self_, make_id(index));
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

auto audio_voice_pool::get_source(uint64_t id) const -> audio::source*
{
    const auto v = find_voice(id);
    return v ? v->source.get() : nullptr;
}

void audio_voice_pool::set_volume(uint64_t id, float volume)
{
    if(auto v = find_voice(id))
    {
        v->volume = clamp_audio_gain(volume);
        v->fade.is_active = false;
        apply_gain(*v);
    }
}

void audio_voice_pool::set_pitch(uint64_t id, float pitch)
{
    if(auto v = find_voice(id))
    {
        v->source->set_pitch(clamp_pitch(pitch));
    }
}

void audio_voice_pool::set_distance(uint64_t id, const frange_t& range)
{
    if(auto v = find_voice(id))
    {
        const auto clamped = clamp_audio_range(range);
        v->source->set_distance(clamped.min, clamped.max);
    }
}

void audio_voice_pool::set_bus(uint64_t id, audio_bus bus)
{
    if(auto v = find_voice(id))
    {
        v->bus = bus;
        apply_gain(*v);
    }
}

void audio_voice_pool::set_priority(uint64_t id, int priority)
{
    if(auto v = find_voice(id))
    {
        v->priority = priority;
    }
}

void audio_voice_pool::set_loop(uint64_t id, bool loop)
{
    if(auto v = find_voice(id))
    {
        v->is_looping = loop;
        v->source->set_loop(loop);
    }
}

void audio_voice_pool::fade_to(uint64_t id, float volume, float seconds, bool stop_at_end)
{
    if(auto v = find_voice(id))
    {
        v->fade.start(v->volume, clamp_audio_gain(volume), seconds, stop_at_end);
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
            v.volume = clamp_audio_gain(v.fade.advance(seconds));
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
        release(i);
    }
    limits_.clear();
}

auto audio_voice_pool::get_live_count() const -> std::size_t
{
    return live_count_;
}

void audio_voice_pool::apply_gain(voice& v) const
{
    const auto bus = std::min<std::size_t>(std::size_t(v.bus), audio_bus_count - 1);
    v.source->set_volume(clamp_audio_gain(v.volume * gains_[bus]));
}

void audio_voice_pool::release(uint32_t index)
{
    auto& v = voices_[index];
    if(!v.source)
    {
        return;
    }
    v.source->force_stop();
    v.source.reset();
    v.clip.reset();
    v.fade = {};
    ++v.generation;
    --live_count_;
    free_slots_.push_back(index);
}

} // namespace unravel
