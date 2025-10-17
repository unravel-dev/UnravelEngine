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
}

template<typename T>
void gradient<T>::set_points(const points_t& points)
{
    points_ = points;
    std::sort(points_.begin(), points_.end());
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

} // namespace math
