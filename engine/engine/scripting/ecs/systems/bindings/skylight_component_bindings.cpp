#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{

auto internal_m2n_skylight_get_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return static_cast<int>(comp->get_mode());
    }
    return static_cast<int>(skylight_component::sky_mode::perez);
}

void internal_m2n_skylight_set_mode(entt::entity id, int mode)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_mode(static_cast<skylight_component::sky_mode>(mode));
    }
}

auto internal_m2n_skylight_get_turbidity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_turbidity();
    }
    return 0.0f;
}

void internal_m2n_skylight_set_turbidity(entt::entity id, float turbidity)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_turbidity(turbidity);
    }
}

auto internal_m2n_skylight_get_cloud_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return static_cast<int>(comp->get_cloud_mode());
    }
    return 0;
}

void internal_m2n_skylight_set_cloud_mode(entt::entity id, int mode)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_cloud_mode(static_cast<skylight_component::cloud_mode>(mode));
    }
}

auto internal_m2n_skylight_get_cloud_coverage(entt::entity id) -> float
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_cloud_coverage();
    }
    return 0.0f;
}

void internal_m2n_skylight_set_cloud_coverage(entt::entity id, float coverage)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_cloud_coverage(coverage);
    }
}

auto internal_m2n_skylight_get_irradiance_intensity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_irradiance_intensity();
    }
    return 0.0f;
}

void internal_m2n_skylight_set_irradiance_intensity(entt::entity id, float intensity)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_irradiance_intensity(intensity);
    }
}

auto internal_m2n_skylight_get_irradiance_quality(entt::entity id) -> int
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return static_cast<int>(comp->get_irradiance_quality());
    }
    return 0;
}

void internal_m2n_skylight_set_irradiance_quality(entt::entity id, int quality)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_irradiance_quality(static_cast<skylight_component::irradiance_quality>(quality));
    }
}

auto internal_m2n_skylight_get_irradiance_use_sky(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_irradiance_use_sky();
    }
    return true;
}

void internal_m2n_skylight_set_irradiance_use_sky(entt::entity id, bool use_sky)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_irradiance_use_sky(use_sky);
    }
}

auto internal_m2n_skylight_get_sky_brightness(entt::entity id) -> float
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_sky_brightness();
    }
    return 1.0f;
}

void internal_m2n_skylight_set_sky_brightness(entt::entity id, float brightness)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        comp->set_sky_brightness(brightness);
    }
}

auto internal_m2n_skylight_get_cubemap(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        return comp->get_cubemap().uid();
    }
    return {};
}

void internal_m2n_skylight_set_cubemap(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<skylight_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        comp->set_cubemap(am.get_asset<gfx::texture>(uid));
    }
}

} // namespace

void register_skylight_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.SkylightComponent");
    reg.add_internal_call("internal_m2n_skylight_get_mode", dotnet_internal_call(internal_m2n_skylight_get_mode));
    reg.add_internal_call("internal_m2n_skylight_set_mode", dotnet_internal_call(internal_m2n_skylight_set_mode));
    reg.add_internal_call("internal_m2n_skylight_get_turbidity",
                          dotnet_internal_call(internal_m2n_skylight_get_turbidity));
    reg.add_internal_call("internal_m2n_skylight_set_turbidity",
                          dotnet_internal_call(internal_m2n_skylight_set_turbidity));
    reg.add_internal_call("internal_m2n_skylight_get_cloud_mode",
                          dotnet_internal_call(internal_m2n_skylight_get_cloud_mode));
    reg.add_internal_call("internal_m2n_skylight_set_cloud_mode",
                          dotnet_internal_call(internal_m2n_skylight_set_cloud_mode));
    reg.add_internal_call("internal_m2n_skylight_get_cloud_coverage",
                          dotnet_internal_call(internal_m2n_skylight_get_cloud_coverage));
    reg.add_internal_call("internal_m2n_skylight_set_cloud_coverage",
                          dotnet_internal_call(internal_m2n_skylight_set_cloud_coverage));
    reg.add_internal_call("internal_m2n_skylight_get_irradiance_intensity",
                          dotnet_internal_call(internal_m2n_skylight_get_irradiance_intensity));
    reg.add_internal_call("internal_m2n_skylight_set_irradiance_intensity",
                          dotnet_internal_call(internal_m2n_skylight_set_irradiance_intensity));
    reg.add_internal_call("internal_m2n_skylight_get_irradiance_quality",
                          dotnet_internal_call(internal_m2n_skylight_get_irradiance_quality));
    reg.add_internal_call("internal_m2n_skylight_set_irradiance_quality",
                          dotnet_internal_call(internal_m2n_skylight_set_irradiance_quality));
    reg.add_internal_call("internal_m2n_skylight_get_irradiance_use_sky",
                          dotnet_internal_call(internal_m2n_skylight_get_irradiance_use_sky));
    reg.add_internal_call("internal_m2n_skylight_set_irradiance_use_sky",
                          dotnet_internal_call(internal_m2n_skylight_set_irradiance_use_sky));
    reg.add_internal_call("internal_m2n_skylight_get_sky_brightness",
                          dotnet_internal_call(internal_m2n_skylight_get_sky_brightness));
    reg.add_internal_call("internal_m2n_skylight_set_sky_brightness",
                          dotnet_internal_call(internal_m2n_skylight_set_sky_brightness));
    reg.add_internal_call("internal_m2n_skylight_get_cubemap",
                          dotnet_internal_call(internal_m2n_skylight_get_cubemap));
    reg.add_internal_call("internal_m2n_skylight_set_cubemap",
                          dotnet_internal_call(internal_m2n_skylight_set_cubemap));
}

} // namespace unravel
