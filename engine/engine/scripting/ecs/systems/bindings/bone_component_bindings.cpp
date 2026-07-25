#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/model_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_bone_get_index(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<bone_component>(id))
    {
        return comp->bone_index;
    }
    return 0;
}

void internal_m2n_bone_set_index(entt::entity id, uint32_t index)
{
    if(auto comp = safe_get_component<bone_component>(id))
    {
        comp->bone_index = index;
    }
}

} // namespace

void register_bone_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.BoneComponent");
    reg.add_internal_call("internal_m2n_bone_get_index", dotnet_internal_call(internal_m2n_bone_get_index));
    reg.add_internal_call("internal_m2n_bone_set_index", dotnet_internal_call(internal_m2n_bone_set_index));
}

} // namespace unravel
