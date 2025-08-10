#include "camera_system.h"

#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>

#include <logging/logging.h>

namespace unravel
{

auto camera_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto camera_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void camera_system::on_frame_update(scene& scn, delta_t dt)
{

}


void camera_system::on_frame_before_render(scene& scn, delta_t dt)
{
    scn.registry->view<transform_component, camera_component>().each(
        [&](auto e, auto&& transform, auto&& camera)
        {
            camera.update(transform.get_transform_global());
        });
}


void camera_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    for(auto e : entities)
    {
        if(auto camera_comp = e.try_get<camera_component>())
        {
            auto& transform_comp = e.get<transform_component>();
            camera_comp->update(transform_comp.get_transform_global());
        }
    }
}


} // namespace unravel
