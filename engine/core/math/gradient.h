#pragma once

#include "curve.h"

namespace math
{

/**
 * @brief A curve whose channels are linked: one key of every channel at the same position.
 *
 * The name this shape carried before a curve could key its channels apart. New code should say
 * @ref curve and reach for @ref curve::set_linked when it wants channels to move independently.
 */
template<typename T>
using gradient = curve<T>;

template<typename T>
using gradient_point = curve_point<T>;

} // namespace math
