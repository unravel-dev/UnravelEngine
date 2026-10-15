#pragma once

#include <editor/hub/hub.h>
#include <editor/hub/panels/panel.h>
#include <editor/hub/panels/scene_panel/scene_panel.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>

#include <context/context.hpp>

#include <string>

namespace unravel::mcp
{

inline auto resolve_scene_panel(rtti::context& ctx) -> scene_panel&
{
    return ctx.get_cached<hub>().get_panels().get_scene_panel();
}

/// The camera the Scene view renders with; empty, with the reason in @p error, before it exists.
inline auto resolve_scene_camera(rtti::context& ctx, std::string& error) -> entt::handle
{
    auto camera = resolve_scene_panel(ctx).get_camera();
    if(!camera || !camera.all_of<transform_component, camera_component>())
    {
        error = "Scene panel camera not available";
        return {};
    }
    return camera;
}

} // namespace unravel::mcp
