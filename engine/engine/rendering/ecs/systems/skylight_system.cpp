#include "skylight_system.h"
#include <engine/events.h>

#include <engine/rendering/atmosphere_transmittance.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <logging/logging.h>

namespace unravel
{

auto find_skylight_in_effect(entt::registry& registry) -> entt::handle
{
    entt::handle strongest_main;
    entt::handle strongest_any;
    float strongest_main_intensity = 0.0f;
    float strongest_any_intensity = 0.0f;
    for(auto e : registry.view<transform_component, skylight_component, active_component>())
    {
        const entt::handle skylight(registry, e);
        const float intensity = skylight.get<skylight_component>().get_irradiance_intensity();
        if(find_scene_group(skylight) == MAIN_SCENE_GROUP && (!strongest_main || intensity > strongest_main_intensity))
        {
            strongest_main = skylight;
            strongest_main_intensity = intensity;
        }
        if(!strongest_any || intensity > strongest_any_intensity)
        {
            strongest_any = skylight;
            strongest_any_intensity = intensity;
        }
    }
    return strongest_main ? strongest_main : strongest_any;
}

auto skylight_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto skylight_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void skylight_system::on_frame_update(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Skylight/System Update");
    scn.registry->view<skylight_component, active_component>().each(
        [&](auto e, auto&& skylight, auto&& active)
        {
            skylight.update(times.scaled);
        });
    update_atmosphere_sun_transmittance(scn);
}

void skylight_system::update_atmosphere_sun_transmittance(scene& scn)
{
    scn.registry->view<light_component>().each(
        [&](auto e, auto&& light_comp) { light_comp.set_atmosphere_transmittance(math::vec3(1.0f)); });
    // The atmosphere sun light is tinted with the sky atmosphere's ground-level transmittance. The sun is the
    // directional light on the procedural sky's own entity (the light the Perez sky takes its direction from); a
    // cubemap sky has no atmosphere, so its light keeps its own color, and so does the sun of a sky not in effect.
    const auto sky = find_skylight_in_effect(*scn.registry);
    if(!sky)
    {
        return;
    }
    const auto& skylight = sky.get<skylight_component>();
    auto* light_comp = sky.try_get<light_component>();
    if(light_comp == nullptr || skylight.get_mode() != skylight_component::sky_mode::perez ||
       !skylight.get_atmosphere_sun() || light_comp->get_light().type != light_type::directional)
    {
        return;
    }
    // The light shines along its z axis; the sun lies the other way.
    const auto direction_to_sun = -sky.get<transform_component>().get_transform_global().z_unit_axis();
    light_comp->set_atmosphere_transmittance(compute_atmosphere_sun_transmittance(direction_to_sun));
}

void skylight_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{

}

} // namespace unravel
