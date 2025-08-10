#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{

struct inspector_physics_compound_shape : public inspector
{
    REFLECTABLEV(inspector_physics_compound_shape, inspector)

    inspect_result inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_physics_compound_shape, physics_compound_shape)
} // namespace unravel
