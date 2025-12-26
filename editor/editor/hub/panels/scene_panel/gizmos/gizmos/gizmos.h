#pragma once

#include "entt/core/type_info.hpp"
#include "gizmo.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <reflection/reflection.h>
#include <reflection/registration.h>


namespace unravel
{

struct gizmo_registry
{
    gizmo_registry();

    std::unordered_map<entt::id_type, std::shared_ptr<gizmo>> type_map;
};

void draw_gizmo_var(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d);

template<typename T>
void draw_gizmo(rtti::context& ctx, T& obj, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d)
{
    entt::meta_any var = entt::forward_as_meta(obj);
    draw_gizmo_var(ctx, var, cam, dd, dd_2d);
}

void draw_gizmo_billboard_var(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd);

template<typename T>
void draw_billboard_gizmo(rtti::context& ctx, T& obj, const camera& cam, gfx::dd_raii& dd)
{
    entt::meta_any var = entt::forward_as_meta(obj);
    draw_gizmo_billboard_var(ctx, var, cam, dd);
}

} // namespace unravel
