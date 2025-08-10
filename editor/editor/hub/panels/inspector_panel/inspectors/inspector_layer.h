#pragma once
#include "inspector.h"

#include <engine/physics/ecs/components/physics_component.h>

namespace unravel
{

struct inspector_layer : public inspector
{
    REFLECTABLEV(inspector_layer, inspector)

    inspect_result inspect(rtti::context& ctx, rttr::variant& var, const var_info& info, const meta_getter& get_metadata);
};

REFLECT_INSPECTOR_INLINE(inspector_layer, layer_mask)
} // namespace unravel
