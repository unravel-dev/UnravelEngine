#include "curve.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace math
{
namespace
{
/// Alpha of a channel color: the plotted lines read as lines, not as full-strength UI accents.
constexpr float CURVE_CHANNEL_COLOR_ALPHA = 1.0f;
/// A segment narrower than this is treated as a step: dividing by it would blow up the slope.
constexpr float CURVE_MIN_SEGMENT_WIDTH = 1e-8f;

auto evaluate_segment(const curve_key& start, const curve_key& end, float position) -> float
{
    const float width = end.position - start.position;
    if(width <= CURVE_MIN_SEGMENT_WIDTH)
    {
        return end.value;
    }
    const float alpha = (position - start.position) / width;
    switch(start.interpolation)
    {
        case curve_interpolation::constant:
            return start.value;
        case curve_interpolation::linear:
            return start.value + (end.value - start.value) * alpha;
        case curve_interpolation::smooth:
        {
            // Cubic Hermite. The tangents are slopes in value per unit of position, so they are
            // scaled by the segment's width to become the basis' tangent vectors.
            const float alpha2 = alpha * alpha;
            const float alpha3 = alpha2 * alpha;
            const float h00 = 2.0f * alpha3 - 3.0f * alpha2 + 1.0f;
            const float h10 = alpha3 - 2.0f * alpha2 + alpha;
            const float h01 = -2.0f * alpha3 + 3.0f * alpha2;
            const float h11 = alpha3 - alpha2;
            return h00 * start.value + h10 * width * start.out_tangent + h01 * end.value +
                   h11 * width * end.in_tangent;
        }
    }
    return start.value;
}
} // namespace

namespace detail
{
auto get_curve_channel_color(size_t channel) -> color
{
    switch(channel)
    {
        case 0:
            return color(0.90f, 0.30f, 0.32f, CURVE_CHANNEL_COLOR_ALPHA);
        case 1:
            return color(0.45f, 0.80f, 0.35f, CURVE_CHANNEL_COLOR_ALPHA);
        case 2:
            return color(0.35f, 0.58f, 0.92f, CURVE_CHANNEL_COLOR_ALPHA);
        default:
            return color(0.82f, 0.82f, 0.86f, CURVE_CHANNEL_COLOR_ALPHA);
    }
}

auto get_vector_channel_name(size_t channel) -> const char*
{
    switch(channel)
    {
        case 0:
            return "X";
        case 1:
            return "Y";
        case 2:
            return "Z";
        default:
            return "W";
    }
}

auto get_color_channel_name(size_t channel) -> const char*
{
    switch(channel)
    {
        case 0:
            return "R";
        case 1:
            return "G";
        case 2:
            return "B";
        default:
            return "A";
    }
}
} // namespace detail

auto curve_key::operator==(const curve_key& other) const -> bool
{
    return position == other.position && value == other.value && in_tangent == other.in_tangent &&
           out_tangent == other.out_tangent && interpolation == other.interpolation &&
           tangent_mode == other.tangent_mode;
}

auto curve_channel::add_key(float position, float value) -> size_t
{
    curve_key key;
    key.position = position;
    key.value = value;
    const auto at = std::upper_bound(keys_.begin(), keys_.end(), key);
    const auto index = size_t(std::distance(keys_.begin(), at));
    // A key dropped into a curve takes the shape of the segment it lands in, so adding one to a
    // smooth curve does not leave a linear notch in it.
    if(!keys_.empty())
    {
        const curve_key& neighbour = index > 0 ? keys_[index - 1] : keys_.front();
        key.interpolation = neighbour.interpolation;
        key.tangent_mode = neighbour.tangent_mode;
    }
    keys_.insert(at, key);
    return index;
}

void curve_channel::remove_key(size_t index)
{
    if(index >= keys_.size())
    {
        return;
    }
    keys_.erase(keys_.begin() + std::ptrdiff_t(index));
}

void curve_channel::clear()
{
    keys_.clear();
}

auto curve_channel::get_keys() const noexcept -> const keys_t&
{
    return keys_;
}

void curve_channel::set_keys(keys_t keys)
{
    keys_ = std::move(keys);
    std::stable_sort(keys_.begin(), keys_.end());
}

auto curve_channel::get_key_count() const noexcept -> size_t
{
    return keys_.size();
}

auto curve_channel::get_key(size_t index) const -> curve_key
{
    if(index >= keys_.size())
    {
        return {};
    }
    return keys_[index];
}

void curve_channel::set_key(size_t index, const curve_key& key)
{
    if(index >= keys_.size())
    {
        return;
    }
    keys_[index] = key;
    const float lowest = index > 0 ? keys_[index - 1].position : -std::numeric_limits<float>::max();
    const float highest = index + 1 < keys_.size() ? keys_[index + 1].position : std::numeric_limits<float>::max();
    keys_[index].position = std::clamp(keys_[index].position, lowest, highest);
}

auto curve_channel::is_empty() const noexcept -> bool
{
    return keys_.empty();
}

auto curve_channel::get_domain() const -> frange_t
{
    if(keys_.empty())
    {
        return {};
    }
    return frange_t(keys_.front().position, keys_.back().position);
}

auto curve_channel::has_constant_key() const -> bool
{
    return std::any_of(keys_.begin(),
                       keys_.end(),
                       [](const curve_key& key) { return key.interpolation == curve_interpolation::constant; });
}

auto curve_channel::sample(float position) const -> float
{
    if(keys_.empty())
    {
        return 0.0f;
    }
    // Past either end the end value holds rather than extrapolating, so a curve authored over a
    // lifetime stays bounded outside it.
    if(position <= keys_.front().position)
    {
        return keys_.front().value;
    }
    if(position >= keys_.back().position)
    {
        return keys_.back().value;
    }

    const curve_key probe{position, 0.0f, 0.0f, 0.0f, curve_interpolation::linear, curve_tangent_mode::automatic};
    const auto upper = std::upper_bound(keys_.begin(), keys_.end(), probe);
    const auto lower = std::prev(upper);
    return evaluate_segment(*lower, *upper, position);
}

void curve_channel::refresh_automatic_tangents()
{
    const size_t count = keys_.size();
    for(size_t index = 0; index < count; ++index)
    {
        curve_key& key = keys_[index];
        if(key.tangent_mode == curve_tangent_mode::free)
        {
            continue;
        }
        if(key.tangent_mode == curve_tangent_mode::flat)
        {
            key.in_tangent = 0.0f;
            key.out_tangent = 0.0f;
            continue;
        }
        // A key with a neighbour either side takes the slope through them, the Catmull-Rom
        // tangent, so the curve flows through it instead of easing at every key.
        //
        // The ends go level. Taking the slope of their one segment there would reproduce that
        // segment exactly, so two smooth keys would draw the straight line they were asked to
        // round off; level ends give the eased shape that is the point of asking for smooth, and
        // they keep the curve from overshooting past the outermost key.
        const bool is_end = index == 0 || index + 1 == count;
        float slope = 0.0f;
        if(!is_end)
        {
            const curve_key& previous = keys_[index - 1];
            const curve_key& next = keys_[index + 1];
            const float width = next.position - previous.position;
            slope = width > CURVE_MIN_SEGMENT_WIDTH ? (next.value - previous.value) / width : 0.0f;
        }
        key.in_tangent = slope;
        key.out_tangent = slope;
    }
}

void curve_channel::reverse(float domain_min, float domain_max)
{
    const float sum = domain_min + domain_max;
    for(auto& key : keys_)
    {
        key.position = sum - key.position;
        // Mirroring the shape mirrors the slopes too, and swaps which side each one is on.
        const float in_tangent = key.in_tangent;
        key.in_tangent = -key.out_tangent;
        key.out_tangent = -in_tangent;
    }
    // Mirroring the positions put the keys in descending order; this restores ascending order
    // without re-sorting, which would have to break ties arbitrarily.
    std::reverse(keys_.begin(), keys_.end());
    // A key's interpolation governs the segment leaving it. Reversed, the segment that used to
    // leave a key now arrives at it, so each key takes the interpolation of the one after it.
    for(size_t index = 0; index + 1 < keys_.size(); ++index)
    {
        keys_[index].interpolation = keys_[index + 1].interpolation;
    }
}

auto curve_channel::operator==(const curve_channel& other) const -> bool
{
    return keys_ == other.keys_;
}

} // namespace math
