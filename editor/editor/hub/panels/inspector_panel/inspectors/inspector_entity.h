#pragma once
#include "inspector.h"

#include <engine/ecs/ecs.h>

namespace unravel
{
struct inspector_entity : public crtp_meta_type<inspector_entity, inspector>
{
    auto inspect_as_property(rtti::context& ctx, entt::handle& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

private:
    ImGuiTextFilter filter_;
};

REFLECT_INSPECTOR_INLINE(inspector_entity, entt::handle)

} // namespace unravel
