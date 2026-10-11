#pragma once

#include "inspector.h"

#include <math/curve.h>

namespace unravel
{

/**
 * @brief The inspector of every curve, whatever it holds.
 *
 * One body for all of them: it reads the channel count, names and colors out of
 * math::curve_traits<T> and hands the channels to the curve editor, which never sees T. Holding a
 * new value type in a curve takes a curve_traits specialization and one line below.
 */
template<typename T>
struct inspector_curve : public crtp_meta_type<inspector_curve<T>, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};

using inspector_curve_float = inspector_curve<float>;
using inspector_curve_vec2 = inspector_curve<math::vec2>;
using inspector_curve_vec3 = inspector_curve<math::vec3>;
using inspector_curve_color = inspector_curve<math::color>;
using inspector_curve_frange = inspector_curve<frange_t>;

REFLECT_INSPECTOR_INLINE(inspector_curve_float, math::curve<float>)
REFLECT_INSPECTOR_INLINE(inspector_curve_vec2, math::curve<math::vec2>)
REFLECT_INSPECTOR_INLINE(inspector_curve_vec3, math::curve<math::vec3>)
REFLECT_INSPECTOR_INLINE(inspector_curve_color, math::curve<math::color>)
REFLECT_INSPECTOR_INLINE(inspector_curve_frange, math::curve<frange_t>)

} // namespace unravel
