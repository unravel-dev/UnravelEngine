#pragma once
#include "gizmo.h"

#include <engine/ecs/ecs.h>

#include <engine/assets/asset_handle.h>
#include <graphics/texture.h>
namespace unravel
{
struct gizmo_entity : public gizmo
{
    void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
    void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) override;
};

GIZMO_REFLECT(gizmo_entity, entt::handle)

} // namespace unravel
