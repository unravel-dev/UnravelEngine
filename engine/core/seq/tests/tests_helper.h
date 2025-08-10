#pragma once
#include <chrono>
#include <numeric>
#include <random>

namespace helper
{
namespace details
{
auto rd_gen() -> std::mt19937&;
}

template<typename T>
struct is_chrono_duration : std::false_type
{
};
template<typename R, typename P>
struct is_chrono_duration<std::chrono::duration<R, P>> : std::true_type
{
};

template<typename T, std::enable_if_t<is_chrono_duration<T>::value, void*> = nullptr>
inline auto random_value(T min, T max) -> T
{
    std::uniform_int_distribution<int64_t> dist(min.count(), max.count());
    return T{dist(details::rd_gen())};
}

template<typename T, std::enable_if_t<std::is_integral<T>::value, void*> = nullptr>
inline auto random_value(T min, T max) -> T
{
    std::uniform_int_distribution<T> dist(min, max);
    return dist(details::rd_gen());
}

template<typename T, std::enable_if_t<std::is_floating_point<T>::value, void*> = nullptr>
inline auto random_value(T min, T max) -> T
{
    std::uniform_real_distribution<T> dist(min, max);
    return dist(details::rd_gen());
}

template<typename T>
inline T random_value()
{
    return random_value(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
}

template<typename T, std::enable_if_t<std::is_integral<T>::value, void*> = nullptr>
inline auto compare(const T& a, const T& b) -> bool
{
    return a == b;
}

template<typename T, std::enable_if_t<std::is_floating_point<T>::value, void*> = nullptr>
inline auto compare(const T& a, const T& b, T epsilon = T(0.001)) -> bool
{
    auto diff = std::abs(a - b);
    if(diff <= epsilon)
        return true;

    if(diff < std::max(std::abs(a), std::abs(b)) * epsilon)
        return true;

    return false;
}
} // namespace helper
