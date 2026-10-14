#pragma once
#include "curve.h"

#include <algorithm>
#include <cmath>

namespace math
{

template<typename T>
auto curve<T>::get_channel(size_t index) const -> const curve_channel&
{
    return channels_[std::min(index, channel_count - 1)];
}

template<typename T>
auto curve<T>::get_channel(size_t index) -> curve_channel&
{
    mark_bake_dirty();
    return channels_[std::min(index, channel_count - 1)];
}

template<typename T>
auto curve<T>::get_channels() const noexcept -> const channels_t&
{
    return channels_;
}

template<typename T>
void curve<T>::set_channels(const channels_t& channels)
{
    channels_ = channels;
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::is_linked() const noexcept -> bool
{
    return linked_;
}

template<typename T>
void curve<T>::set_linked(bool linked)
{
    linked_ = linked;
    if(!linked)
    {
        return;
    }
    // Every channel takes a key wherever any channel has one, holding the value it sampled there,
    // so linking keeps the curve's shape instead of discarding the keys of the other channels.
    const auto positions = collect_key_positions();
    channels_t linked_channels;
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        const auto& source = channels_[channel];
        if(source.is_empty())
        {
            continue;
        }
        curve_channel::keys_t keys;
        keys.reserve(positions.size());
        for(float position : positions)
        {
            curve_key key = source.get_key(0);
            const auto& source_keys = source.get_keys();
            const auto existing = std::find_if(source_keys.begin(),
                                               source_keys.end(),
                                               [position](const curve_key& candidate)
                                               { return candidate.position == position; });
            if(existing != source_keys.end())
            {
                key = *existing;
            }
            else
            {
                key.position = position;
                key.value = source.sample(position);
            }
            keys.push_back(key);
        }
        linked_channels[channel].set_keys(std::move(keys));
        linked_channels[channel].refresh_automatic_tangents();
    }
    channels_ = linked_channels;
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::is_valid() const noexcept -> bool
{
    for(const auto& channel : channels_)
    {
        if(!channel.is_empty())
        {
            return true;
        }
    }
    return false;
}

template<typename T>
auto curve<T>::get_domain() const -> frange_t
{
    bool has_any = false;
    frange_t domain{};
    for(const auto& channel : channels_)
    {
        if(channel.is_empty())
        {
            continue;
        }
        const frange_t channel_domain = channel.get_domain();
        domain.min = has_any ? std::min(domain.min, channel_domain.min) : channel_domain.min;
        domain.max = has_any ? std::max(domain.max, channel_domain.max) : channel_domain.max;
        has_any = true;
    }
    return domain;
}

template<typename T>
auto curve<T>::sample_direct(float position) const -> T
{
    T result{};
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        traits::set(result, channel, channels_[channel].sample(position));
    }
    return result;
}

template<typename T>
auto curve<T>::sample(float position) const -> T
{
    if(bake_table_.empty())
    {
        return sample_direct(position);
    }

    const float width = bake_domain_.max - bake_domain_.min;
    if(width <= 0.0f)
    {
        return bake_table_.front();
    }

    const float alpha = (position - bake_domain_.min) / width;
    if(alpha <= 0.0f)
    {
        return bake_table_.front();
    }
    if(alpha >= 1.0f)
    {
        return bake_table_.back();
    }

    const float entry = alpha * float(bake_table_.size() - 1);
    const size_t index = size_t(entry);
    if(index + 1 >= bake_table_.size())
    {
        return bake_table_.back();
    }
    if(!bake_interpolates_)
    {
        return bake_table_[index];
    }

    const float fraction = entry - float(index);
    T result{};
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        const float low = traits::get(bake_table_[index], channel);
        const float high = traits::get(bake_table_[index + 1], channel);
        traits::set(result, channel, low + (high - low) * fraction);
    }
    return result;
}

template<typename T>
void curve<T>::bake(size_t resolution)
{
    // A table of one entry cannot span anything, and the lookup divides by its last index.
    if(!is_valid() || resolution < 2)
    {
        clear_bake();
        return;
    }

    bake_domain_ = get_domain();
    bake_interpolates_ = true;
    for(const auto& channel : channels_)
    {
        if(channel.has_constant_key())
        {
            bake_interpolates_ = false;
            break;
        }
    }

    const float width = bake_domain_.max - bake_domain_.min;
    bake_table_.resize(resolution);
    for(size_t entry = 0; entry < resolution; ++entry)
    {
        const float alpha = float(entry) / float(resolution - 1);
        bake_table_[entry] = sample_direct(bake_domain_.min + width * alpha);
    }
}

template<typename T>
void curve<T>::clear_bake()
{
    bake_table_.clear();
    bake_domain_ = {};
    bake_interpolates_ = true;
}

template<typename T>
auto curve<T>::is_baked() const noexcept -> bool
{
    return !bake_table_.empty();
}

template<typename T>
void curve<T>::mark_bake_dirty()
{
    // The table is dropped rather than rebuilt: rebuilding on the next sample would mean a const
    // sample mutating the curve, which is not safe to share across threads.
    clear_bake();
}

template<typename T>
void curve<T>::reverse()
{
    const frange_t domain = get_domain();
    for(auto& channel : channels_)
    {
        channel.reverse(domain.min, domain.max);
    }
    mark_bake_dirty();
}

template<typename T>
void curve<T>::clear()
{
    for(auto& channel : channels_)
    {
        channel.clear();
    }
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::operator==(const curve<T>& other) const -> bool
{
    return linked_ == other.linked_ && channels_ == other.channels_;
}

template<typename T>
auto curve<T>::collect_key_positions() const -> std::vector<float>
{
    std::vector<float> positions;
    for(const auto& channel : channels_)
    {
        for(const auto& key : channel.get_keys())
        {
            positions.push_back(key.position);
        }
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    return positions;
}

// ---------------------------------------------------------------------------------
// The key-list view.

template<typename T>
auto curve<T>::add_point(const T& element, float progress) -> size_t
{
    size_t index = 0;
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        index = channels_[channel].add_key(progress, traits::get(element, channel));
    }
    for(auto& channel : channels_)
    {
        channel.refresh_automatic_tangents();
    }
    mark_bake_dirty();
    return index;
}

template<typename T>
void curve<T>::remove_point(int index)
{
    if(index < 0)
    {
        return;
    }
    for(auto& channel : channels_)
    {
        channel.remove_key(size_t(index));
        channel.refresh_automatic_tangents();
    }
    mark_bake_dirty();
}

template<typename T>
void curve<T>::set_points(const points_t& points)
{
    points_t sorted = points;
    std::sort(sorted.begin(), sorted.end());
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        curve_channel::keys_t keys;
        keys.reserve(sorted.size());
        for(const auto& point : sorted)
        {
            curve_key key;
            key.position = point.progress;
            key.value = traits::get(point.element, channel);
            keys.push_back(key);
        }
        channels_[channel].set_keys(std::move(keys));
        channels_[channel].refresh_automatic_tangents();
    }
    linked_ = true;
    mark_bake_dirty();
}

template<typename T>
void curve<T>::set_points_keeping_shape(const points_t& points)
{
    constexpr float same_position = 1.0e-5f;
    const channels_t previous = channels_;
    set_points(points);
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        curve_channel::keys_t keys = channels_[channel].get_keys();
        const auto& before = previous[channel].get_keys();
        for(curve_key& key : keys)
        {
            const auto match = std::find_if(before.begin(),
                                            before.end(),
                                            [&](const curve_key& old)
                                            {
                                                return std::abs(old.position - key.position) <= same_position;
                                            });
            if(match == before.end())
            {
                continue;
            }
            key.interpolation = match->interpolation;
            key.tangent_mode = match->tangent_mode;
            key.in_tangent = match->in_tangent;
            key.out_tangent = match->out_tangent;
        }
        channels_[channel].set_keys(std::move(keys));
        channels_[channel].refresh_automatic_tangents();
    }
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::get_points() const -> points_t
{
    const auto positions = collect_key_positions();
    points_t points;
    points.reserve(positions.size());
    for(float position : positions)
    {
        point_t point;
        point.progress = position;
        point.element = sample_direct(position);
        points.push_back(point);
    }
    return points;
}

template<typename T>
void curve<T>::set_progress(int index, float progress)
{
    // A key moved through the key-list view is free to pass its neighbours, so it goes through
    // the whole list rather than through curve_channel::set_key, which holds a key in place.
    points_t points = get_points();
    if(index < 0 || size_t(index) >= points.size())
    {
        return;
    }
    points[size_t(index)].progress = progress;
    set_points(points);
}

template<typename T>
auto curve<T>::get_progress(int index) const -> float
{
    if(index < 0)
    {
        return 0.0f;
    }
    const auto positions = collect_key_positions();
    if(size_t(index) >= positions.size())
    {
        return 0.0f;
    }
    return positions[size_t(index)];
}

template<typename T>
void curve<T>::set_element(int index, const T& element)
{
    if(index < 0)
    {
        return;
    }
    for(size_t channel = 0; channel < channel_count; ++channel)
    {
        auto& track = channels_[channel];
        if(size_t(index) >= track.get_key_count())
        {
            continue;
        }
        curve_key key = track.get_key(size_t(index));
        key.value = traits::get(element, channel);
        track.set_key(size_t(index), key);
        track.refresh_automatic_tangents();
    }
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::get_element(int index) const -> T
{
    if(index < 0)
    {
        return {};
    }
    const auto positions = collect_key_positions();
    if(size_t(index) >= positions.size())
    {
        return {};
    }
    return sample_direct(positions[size_t(index)]);
}

template<typename T>
void curve<T>::set_interpolation(curve_interpolation interpolation)
{
    for(auto& channel : channels_)
    {
        curve_channel::keys_t keys = channel.get_keys();
        for(auto& key : keys)
        {
            key.interpolation = interpolation;
        }
        channel.set_keys(std::move(keys));
        channel.refresh_automatic_tangents();
    }
    mark_bake_dirty();
}

template<typename T>
auto curve<T>::get_interpolation() const -> curve_interpolation
{
    bool has_any = false;
    curve_interpolation shared = curve_interpolation::linear;
    for(const auto& channel : channels_)
    {
        for(const auto& key : channel.get_keys())
        {
            if(!has_any)
            {
                shared = key.interpolation;
                has_any = true;
            }
            else if(key.interpolation != shared)
            {
                return curve_interpolation::linear;
            }
        }
    }
    return shared;
}

} // namespace math
