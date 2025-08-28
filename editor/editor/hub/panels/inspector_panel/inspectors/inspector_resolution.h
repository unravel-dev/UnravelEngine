#pragma once

#include "inspector.h"
#include <engine/settings/settings.h>

namespace unravel
{

struct inspector_resolution_settings : public crtp_meta_type<inspector_resolution_settings, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_resolution_settings, settings::resolution_settings)

} // namespace unravel 