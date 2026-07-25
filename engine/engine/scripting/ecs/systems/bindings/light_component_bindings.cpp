#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/light.h>

namespace unravel
{
namespace
{

void internal_m2n_light_set_color(entt::entity id, const math::color& color)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.color = color;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().color;
    }
    return math::color::white();
}

auto internal_m2n_light_get_type(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return static_cast<uint8_t>(comp->get_light().type);
    }
    return static_cast<uint8_t>(light_type::directional);
}

void internal_m2n_light_set_type(entt::entity id, uint8_t type)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.type = static_cast<light_type>(type);
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_intensity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().intensity;
    }
    return 0.0f;
}

void internal_m2n_light_set_intensity(entt::entity id, float intensity)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.intensity = intensity;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_casts_shadows(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().casts_shadows;
    }
    return false;
}

void internal_m2n_light_set_casts_shadows(entt::entity id, bool casts_shadows)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.casts_shadows = casts_shadows;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_range(entt::entity id) -> float
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        const auto& l = comp->get_light();
        if(l.type == light_type::spot)
        {
            return l.spot_data.get_range();
        }
        return l.point_data.range;
    }
    return 0.0f;
}

void internal_m2n_light_set_range(entt::entity id, float range)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.spot_data.set_range(range);
        l.point_data.range = range;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_spot_outer_angle(entt::entity id) -> float
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().spot_data.get_outer_angle();
    }
    return 0.0f;
}

void internal_m2n_light_set_spot_outer_angle(entt::entity id, float angle)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.spot_data.set_outer_angle(angle);
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_spot_inner_angle(entt::entity id) -> float
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().spot_data.get_inner_angle();
    }
    return 0.0f;
}

void internal_m2n_light_set_spot_inner_angle(entt::entity id, float angle)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.spot_data.set_inner_angle(angle);
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_point_exponent_falloff(entt::entity id) -> float
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().point_data.exponent_falloff;
    }
    return 0.0f;
}

void internal_m2n_light_set_point_exponent_falloff(entt::entity id, float falloff)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.point_data.exponent_falloff = falloff;
        comp->set_light(l);
    }
}

} // namespace

void register_light_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.LightComponent");
    reg.add_internal_call("internal_m2n_light_get_color", dotnet_internal_call(internal_m2n_light_get_color));
    reg.add_internal_call("internal_m2n_light_set_color", dotnet_internal_call(internal_m2n_light_set_color));
    reg.add_internal_call("internal_m2n_light_get_type", dotnet_internal_call(internal_m2n_light_get_type));
    reg.add_internal_call("internal_m2n_light_set_type", dotnet_internal_call(internal_m2n_light_set_type));
    reg.add_internal_call("internal_m2n_light_get_intensity", dotnet_internal_call(internal_m2n_light_get_intensity));
    reg.add_internal_call("internal_m2n_light_set_intensity", dotnet_internal_call(internal_m2n_light_set_intensity));
    reg.add_internal_call("internal_m2n_light_get_casts_shadows",
                          dotnet_internal_call(internal_m2n_light_get_casts_shadows));
    reg.add_internal_call("internal_m2n_light_set_casts_shadows",
                          dotnet_internal_call(internal_m2n_light_set_casts_shadows));
    reg.add_internal_call("internal_m2n_light_get_range", dotnet_internal_call(internal_m2n_light_get_range));
    reg.add_internal_call("internal_m2n_light_set_range", dotnet_internal_call(internal_m2n_light_set_range));
    reg.add_internal_call("internal_m2n_light_get_spot_outer_angle",
                          dotnet_internal_call(internal_m2n_light_get_spot_outer_angle));
    reg.add_internal_call("internal_m2n_light_set_spot_outer_angle",
                          dotnet_internal_call(internal_m2n_light_set_spot_outer_angle));
    reg.add_internal_call("internal_m2n_light_get_spot_inner_angle",
                          dotnet_internal_call(internal_m2n_light_get_spot_inner_angle));
    reg.add_internal_call("internal_m2n_light_set_spot_inner_angle",
                          dotnet_internal_call(internal_m2n_light_set_spot_inner_angle));
    reg.add_internal_call("internal_m2n_light_get_point_exponent_falloff",
                          dotnet_internal_call(internal_m2n_light_get_point_exponent_falloff));
    reg.add_internal_call("internal_m2n_light_set_point_exponent_falloff",
                          dotnet_internal_call(internal_m2n_light_set_point_exponent_falloff));
}

} // namespace unravel
