#pragma once
#include "inspector.h"

#include <engine/ecs/ecs.h>

namespace unravel
{
struct inspector_entity : public inspector
{
    REFLECTABLEV(inspector_entity, inspector)

    auto inspect_as_property(rtti::context& ctx, entt::handle& data) -> inspect_result;
    auto inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata) -> inspect_result;

private:
    ImGuiTextFilter filter_;
};

REFLECT_INSPECTOR_INLINE(inspector_entity, entt::handle)
} // namespace unravel
