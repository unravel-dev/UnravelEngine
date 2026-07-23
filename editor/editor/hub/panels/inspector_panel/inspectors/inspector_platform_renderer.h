#pragma once

#include "inspector.h"
#include <engine/settings/boot_config.h>

namespace unravel
{

struct inspector_platform_renderer_settings
    : public crtp_meta_type<inspector_platform_renderer_settings, inspector>
{
    auto inspect(rtti::context& ctx,
                 entt::meta_any& var,
                 const meta_any_proxy& var_proxy,
                 const var_info& info,
                 const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_platform_renderer_settings, platform_renderer_settings)

} // namespace unravel
