#pragma once
#include "inspector.h"

#include <engine/scripting/ecs/components/script_component.h>

namespace unravel
{

struct inspector_mono_object : public inspector
{
    REFLECTABLEV(inspector_mono_object, inspector)

    inspect_result inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_mono_object, mono::mono_object)

struct inspector_mono_scoped_object : public inspector_mono_object
{
    REFLECTABLEV(inspector_mono_scoped_object, inspector_mono_object)
};

REFLECT_INSPECTOR_INLINE(inspector_mono_scoped_object, mono::mono_scoped_object)
} // namespace unravel
