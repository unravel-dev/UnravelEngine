#pragma once
#include "inspector.h"

#include <engine/ecs/components/prefab_component.h>

namespace unravel
{

struct inspector_prefab_component : public inspector
{
    REFLECTABLEV(inspector_prefab_component, inspector)

    inspect_result inspect(rtti::context& ctx,
                           rttr::variant& var,
                           const var_info& info,
                           const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_prefab_component, prefab_component)

} // namespace unravel
