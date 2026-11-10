#include "feature_limits.h"

#include <algorithm>

namespace unravel
{

namespace
{

auto is_soft_filter(sm_impl filter) -> bool
{
    return filter == sm_impl::pcss || filter == sm_impl::vsm || filter == sm_impl::esm;
}

/// Lowers @p l's shadow filter to the widest @p limits allow.
void limit_shadow_filter(light& l, const feature_limits& limits)
{
    if(limits.max_shadow_filtering == shadow_filtering_limit::hard)
    {
        l.shadow_params.type = sm_impl::hard;
        return;
    }
    const bool is_narrow = limits.max_shadow_filtering == shadow_filtering_limit::pcf_3x3;
    if(is_soft_filter(l.shadow_params.type) && (is_narrow || !limits.soft_shadows))
    {
        l.shadow_params.type = sm_impl::pcf;
    }
    if(is_narrow && l.shadow_params.type == sm_impl::pcf)
    {
        narrow_pcf_to_tent3(l);
    }
}

} // namespace

auto limit_light(const light& source, const feature_limits& limits) -> light
{
    light limited = source;
    limited.shadow_params.resolution = std::min(limited.shadow_params.resolution, limits.max_shadow_resolution);
    limit_shadow_filter(limited, limits);
    limited.directional_shadow_params.num_splits = static_cast<uint8_t>(
        std::min(int(limited.directional_shadow_params.num_splits), limits.max_shadow_cascades));
    if(!limits.contact_shadows)
    {
        limited.contact_shadow.enabled = false;
    }
    return limited;
}

} // namespace unravel
