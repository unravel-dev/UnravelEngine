#include "feature_limits.h"

#include <algorithm>

namespace unravel
{

auto limit_light(const light& source, const feature_limits& limits) -> light
{
    light limited = source;
    limited.shadow_params.resolution = std::min(limited.shadow_params.resolution, limits.max_shadow_resolution);
    const auto filter = limited.shadow_params.type;
    const bool is_soft = filter == sm_impl::pcss || filter == sm_impl::vsm || filter == sm_impl::esm;
    if(!limits.soft_shadows && is_soft)
    {
        limited.shadow_params.type = sm_impl::pcf;
    }
    if(!limits.contact_shadows)
    {
        limited.contact_shadow.enabled = false;
    }
    return limited;
}

} // namespace unravel
