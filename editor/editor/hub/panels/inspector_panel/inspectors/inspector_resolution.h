#pragma once

#include "inspector.h"
#include <engine/settings/settings.h>

namespace unravel
{

struct inspector_resolution_settings : public inspector
{
    REFLECTABLEV(inspector_resolution_settings, inspector)
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata)
        -> inspect_result;
};
REFLECT_INSPECTOR_INLINE(inspector_resolution_settings, settings::resolution_settings)

} // namespace unravel 