#include "reflection_probe_system.h"
#include <engine/events.h>

#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>

#include <logging/logging.h>

namespace unravel
{

auto reflection_probe_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto reflection_probe_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void reflection_probe_system::on_frame_update(scene& scn, delta_t dt)
{
    scn.registry->view<transform_component, reflection_probe_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& probe, auto&& active)
        {
            probe.update();
        });
}

void reflection_probe_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{

}

} // namespace unravel
