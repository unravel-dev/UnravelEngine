#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/text_component.h>

namespace unravel
{

struct inspector_alignment : public crtp_meta_type<inspector_alignment, inspector>
{

    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_getter& var_getter, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_alignment, alignment)

struct inspector_text_style : public crtp_meta_type<inspector_text_style, inspector>
{
    void before_inspect(const entt::meta_data& prop) override;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_getter& var_getter, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_text_style, text_style)


struct inspector_text_style_flags : public crtp_meta_type<inspector_text_style_flags, inspector>
{

    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_getter& var_getter, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_text_style_flags, text_style_flags)
} // namespace unravel
