#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>

namespace unravel
{

struct inspector_light_component : public inspector
{
    REFLECTABLEV(inspector_light_component, inspector)

    inspect_result inspect(rtti::context& ctx,
                           rttr::variant& var,
                           const var_info& info,
                           const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_light_component, light_component)

} // namespace unravel
