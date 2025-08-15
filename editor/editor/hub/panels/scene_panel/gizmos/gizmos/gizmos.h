#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <reflection/reflection.h>
#include <reflection/registration.h>
#include "entt/core/type_info.hpp"
#include "gizmo.h"

namespace unravel
{

struct gizmo_registry
{
    gizmo_registry();

    std::unordered_map<rttr::type, std::shared_ptr<gizmo>> type_map;
    std::unordered_map<entt::id_type, std::shared_ptr<gizmo>> type_map_entt;
};

void draw_gizmo_var(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd);

template<typename T>
void draw_gizmo(rtti::context& ctx, T* obj, const camera& cam, gfx::dd_raii& dd)
{
    rttr::variant var = obj;
    draw_gizmo_var(ctx, var, cam, dd);
}

template<typename T>
void draw_gizmo(rtti::context& ctx, T& obj, const camera& cam, gfx::dd_raii& dd)
{
    draw_gizmo(ctx, &obj, cam, dd);
}

void draw_gizmo_billboard_var(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd);

template<typename T>
void draw_gizmo_billboard(rtti::context& ctx, T* obj, const camera& cam, gfx::dd_raii& dd)
{
    rttr::variant var = obj;
    draw_gizmo_billboard_var(ctx, var, cam, dd);
}

template<typename T>
void draw_billboard_gizmo(rtti::context& ctx, T& obj, const camera& cam, gfx::dd_raii& dd)
{
    draw_billboard_gizmo(ctx, &obj, cam, dd);
}


} // namespace unravel
