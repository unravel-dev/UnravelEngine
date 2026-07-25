#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/volume_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_volume_get_mode(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        return static_cast<uint8_t>(comp->mode);
    }
    return 0;
}

void internal_m2n_volume_set_mode(entt::entity id, uint8_t mode)
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        comp->mode = static_cast<volume_mode>(mode);
    }
}

auto internal_m2n_volume_get_priority(entt::entity id) -> int
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        return comp->priority;
    }
    return 0;
}

void internal_m2n_volume_set_priority(entt::entity id, int priority)
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        comp->priority = priority;
    }
}

auto internal_m2n_volume_get_weight(entt::entity id) -> float
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        return comp->weight;
    }
    return 0.0f;
}

void internal_m2n_volume_set_weight(entt::entity id, float weight)
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        comp->weight = weight;
    }
}

auto internal_m2n_volume_get_blend_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        return comp->blend_distance;
    }
    return 0.0f;
}

void internal_m2n_volume_set_blend_distance(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        comp->blend_distance = distance;
    }
}

auto internal_m2n_volume_get_extents(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        return comp->extents;
    }
    return {};
}

void internal_m2n_volume_set_extents(entt::entity id, const math::vec3& extents)
{
    if(auto comp = safe_get_component<volume_component>(id))
    {
        comp->extents = extents;
    }
}

} // namespace

void register_volume_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.VolumeComponent");
    reg.add_internal_call("internal_m2n_volume_get_mode", dotnet_internal_call(internal_m2n_volume_get_mode));
    reg.add_internal_call("internal_m2n_volume_set_mode", dotnet_internal_call(internal_m2n_volume_set_mode));
    reg.add_internal_call("internal_m2n_volume_get_priority",
                          dotnet_internal_call(internal_m2n_volume_get_priority));
    reg.add_internal_call("internal_m2n_volume_set_priority",
                          dotnet_internal_call(internal_m2n_volume_set_priority));
    reg.add_internal_call("internal_m2n_volume_get_weight",
                          dotnet_internal_call(internal_m2n_volume_get_weight));
    reg.add_internal_call("internal_m2n_volume_set_weight",
                          dotnet_internal_call(internal_m2n_volume_set_weight));
    reg.add_internal_call("internal_m2n_volume_get_blend_distance",
                          dotnet_internal_call(internal_m2n_volume_get_blend_distance));
    reg.add_internal_call("internal_m2n_volume_set_blend_distance",
                          dotnet_internal_call(internal_m2n_volume_set_blend_distance));
    reg.add_internal_call("internal_m2n_volume_get_extents",
                          dotnet_internal_call(internal_m2n_volume_get_extents));
    reg.add_internal_call("internal_m2n_volume_set_extents",
                          dotnet_internal_call(internal_m2n_volume_set_extents));
}

} // namespace unravel
