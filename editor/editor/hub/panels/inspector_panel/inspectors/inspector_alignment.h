#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/text_component.h>

namespace unravel
{

struct inspector_alignment : public inspector
{
    REFLECTABLEV(inspector_alignment, inspector)

    inspect_result inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_alignment, alignment)

struct inspector_text_style : public inspector
{
    REFLECTABLEV(inspector_text_style, inspector)
    void before_inspect(const rttr::property& prop) override;

    inspect_result inspect(rtti::context& ctx,
                           rttr::variant& var,
                           const var_info& info,
                           const meta_getter& get_metadata) override;
};

REFLECT_INSPECTOR_INLINE(inspector_text_style, text_style)


struct inspector_text_style_flags : public inspector
{
    REFLECTABLEV(inspector_text_style_flags, inspector)

    inspect_result inspect(rtti::context& ctx,
                           rttr::variant& var,
                           const var_info& info,
                           const meta_getter& get_metadata) override;
};

REFLECT_INSPECTOR_INLINE(inspector_text_style_flags, text_style_flags)
} // namespace unravel
