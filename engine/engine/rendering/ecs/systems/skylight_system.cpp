#include "skylight_system.h"
#include <engine/events.h>

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
}

void skylight_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{

}

} // namespace unravel
