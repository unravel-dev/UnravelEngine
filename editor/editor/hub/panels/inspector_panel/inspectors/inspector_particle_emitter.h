#pragma once
#include "inspector.h"

#include <engine/rendering/ecs/components/particle_emitter_component.h>

namespace unravel
{

struct inspector_particle_emitter_component : public crtp_meta_type<inspector_particle_emitter_component, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};

REFLECT_INSPECTOR_INLINE(inspector_particle_emitter_component, particle_emitter_component)

} // namespace unravel
