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

void skylight_system::on_frame_update(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Skylight/System Update");
    scn.registry->view<skylight_component, active_component>().each(
        [&](auto e, auto&& skylight, auto&& active)
        {
            skylight.update(dt);
        });
    update_atmosphere_sun_transmittance(scn);
}

void skylight_system::update_atmosphere_sun_transmittance(scene& scn)
{
    scn.registry->view<light_component>().each(
        [&](auto e, auto&& light_comp) { light_comp.set_atmosphere_transmittance(math::vec3(1.0f)); });
    // UE tints its atmosphere sun light with the sky atmosphere's ground-level transmittance. Here the sun is the
    // directional light on the procedural sky's own entity (the light the Perez sky takes its direction from); a
    // cubemap sky has no atmosphere, so its light keeps its own color.
    scn.registry->view<skylight_component, light_component, transform_component, active_component>().each(
        [&](auto e, auto&& skylight, auto&& light_comp, auto&& transform_comp, auto&& active)
        {
            if(skylight.get_mode() != skylight_component::sky_mode::perez || !skylight.get_atmosphere_sun() ||
               light_comp.get_light().type != light_type::directional)
            {
                return;
            }
            // The light shines along its z axis; the sun lies the other way.
            const auto direction_to_sun = -transform_comp.get_transform_global().z_unit_axis();
            light_comp.set_atmosphere_transmittance(compute_atmosphere_sun_transmittance(direction_to_sun));
        });
}

void skylight_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{

}

} // namespace unravel
