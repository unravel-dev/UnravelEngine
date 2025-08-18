#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{

struct inspector_layer : public crtp_meta_type<inspector_layer, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_layer, layer_mask)
} // namespace unravel
