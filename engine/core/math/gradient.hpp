#pragma once
#include "gradient.h"

namespace math
{

template<typename T>
auto gradient<T>::add_point(const T& element, float progress) -> size_t
{
    point_t point;
    point.element = element;
    point.progress = progress;
    points_.emplace_back(point);

    std::sort(points_.begin(), points_.end());

    auto it = std::find(points_.begin(), points_.end(), point);

    mark_lut_dirty(); // Invalidate LUT when gradient changes
    return std::distance(points_.begin(), it);
}

template<typename T>
void gradient<T>::remove_point(int index)
{
    if(index >= int(points_.size()))
    {
        return;
    }

    points_.erase(points_.begin() + index);
    std::sort(points_.begin(), points_.end());
    mark_lut_dirty(); // Invalidate LUT when gradient changes
}

template<typename T>
void gradient<T>::set_points(const points_t& points)
{
    points_ = points;
    std::sort(points_.begin(), points_.end());
    mark_lut_dirty(); // Invalidate LUT when gradient changes
}

template<typename T>
auto gradient<T>::get_points() const noexcept -> const points_t&
{
    return points_;
}

template<typename T>
void gradient<T>::reverse()
{
    for(auto& p : points_)
    {
        p.progress = 1.0f - p.progress;
    }
    std::sort(points_.begin(), points_.end());
    mark_lut_dirty(); // Invalidate LUT when gradient changes
}

template<typename T>
void gradient<T>::set_progress(int index, float progress)
{
    if(index >= int(points_.size()))
    {
        return;
    }

    points_[index].progress = progress;
    std::sort(points_.begin(), points_.end());
    mark_lut_dirty(); // Invalidate LUT when gradient changes
}

template<typename T>
auto gradient<T>::get_progress(int index) -> float
{
    if(index >= int(points_.size()))
    {
        return 0.0f;
    }

    return points_[index].progress;
}

template<typename T>
void gradient<T>::set_element(int index, const T& element)
{
    if(index >= int(points_.size()))
    {
        return;
    }

    points_[index].element = element;
    mark_lut_dirty(); // Invalidate LUT when gradient changes
}

template<typename T>
auto gradient<T>::get_element(int index) -> T
{
    if(index >= int(points_.size()))
    {
        return {};
    }

    return points_[index].element;
}

template<typename T>
auto gradient<T>::is_valid() const noexcept -> bool
{
    return (false == points_.empty());
}

template<typename T>
auto gradient<T>::sample(float progress) const -> T
{
    // Use LUT if available, otherwise fall back to original implementation
    if(has_lut())
    {
        regenerate_lut_if_needed();
        return sample_from_lut(progress);
    }
    else
    {
        return sample_original(progress);
    }
}

template<typename T>
auto gradient<T>::sample_original(float progress) const -> T
{
    if(false == is_valid())
    {
        return {};
    }

    int low = 0;
    int high = points_.size() - 1;
    int middle = 0;

    while(low <= high)
    {
        middle = (low + high) / 2;
        const auto& point = points_[middle];
        if(point.progress > progress)
        {
            high = middle - 1;
        }
        else if(point.progress < progress)
        {
            low = middle + 1;
        }
        else
        {
            return point.element;
        }
    }

    if(points_[middle].progress > progress)
    {
        middle--;
    }

    int first = middle;
    int second = middle + 1;

    if(second >= 0 && size_t(second) >= points_.size())
    {
        return points_.back().element;
    }

    if(first < 0)
    {
        return points_.front().element;
    }

    const auto& point_first = points_[first];
    const auto& point_second = points_[second];

    switch(interpolation_mode_)
    {
        case gradient_interpolation_mode_t::constant:
        {
            return point_first.element;
        }
        case gradient_interpolation_mode_t::linear:
        {
            const auto abs_progress = (progress - point_first.progress) / (point_second.progress - point_first.progress);
            return gradient_lerp(point_first.element, point_second.element, abs_progress);
        }
    }

    return point_first.element;
}

template<typename T>
void gradient<T>::set_interpolation_mode(gradient_interpolation_mode_t mode)
{
    interpolation_mode_ = mode;
    mark_lut_dirty(); // Invalidate LUT when interpolation mode changes
}

template<typename T>
auto gradient<T>::get_interpolation_mode() const noexcept -> gradient_interpolation_mode_t
{
    return interpolation_mode_;
}

template<typename T>
auto gradient<T>::operator==(const gradient<T>& other) const -> bool
{
    if(interpolation_mode_ != other.interpolation_mode_)
    {
        return false;
    }

    return points_ == other.points_;
}

// LUT Implementation
template<typename T>
void gradient<T>::generate_lut(size_t lut_size)
{
    lut_size_ = lut_size;
    lut_.resize(lut_size_);
    
    // Pre-sample the gradient into the LUT
    for(size_t i = 0; i < lut_size_; ++i)
    {
        const float progress = float(i) / float(lut_size_ - 1);
        lut_[i] = sample_original(progress);
    }
    
    lut_dirty_ = false;
}

template<typename T>
void gradient<T>::clear_lut()
{
    lut_.clear();
    lut_size_ = 0;
    lut_dirty_ = true;
}

template<typename T>
void gradient<T>::regenerate_lut_if_needed() const
{
    if(lut_dirty_ && !lut_.empty())
    {
        // Regenerate LUT using current settings
        const_cast<gradient<T>*>(this)->generate_lut(lut_size_);
    }
}

template<typename T>
auto gradient<T>::sample_from_lut(float progress) const -> T
{
    if(lut_.empty())
    {
        return {};
    }
    
    // Clamp progress to [0, 1]
    progress = std::max(0.0f, std::min(1.0f, progress));
    
    // Calculate LUT index with fractional part
    const float index_f = progress * float(lut_size_ - 1);
    const size_t index = size_t(index_f);
    const float frac = index_f - float(index);
    
    // Handle edge case
    if(index >= lut_size_ - 1)
    {
        return lut_.back();
    }
    
    // Linear interpolation between LUT entries for smooth results
    return gradient_lerp(lut_[index], lut_[index + 1], frac);
}

} // namespace math
