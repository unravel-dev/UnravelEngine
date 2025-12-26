#include "gizmos.h"
#include "gizmo_entity.h"
#include "gizmo_physics_component.h"
#include "reflection/reflection.h"
#include <engine/rendering/camera.h>
#include "../gizmos_renderer.h"


namespace unravel
{

gizmo_registry::gizmo_registry()
{
    auto base_gizmo_type = entt::resolve<gizmo>();
    auto gizmo_types = entt::get_derived_types(base_gizmo_type);
    for(auto& gizmo_type : gizmo_types)
    {
        auto inspected_type_var = entt::get_attribute(gizmo_type, "inspected_type");
        if(inspected_type_var)
        {
            // auto inspected_type = inspected_type_var.cast<entt::meta_type>();
            gizmo_type.invoke("create_and_register"_hs, {}, inspected_type_var, entt::forward_as_meta(type_map));
        }
    }
}

auto get_gizmo(rtti::context& ctx, const entt::meta_type& type) -> std::shared_ptr<gizmo>
{
    auto& registry = ctx.get_cached<gizmo_registry>();

    auto it = registry.type_map.find(type.info().index());
    if(it == registry.type_map.end())
    {
        return nullptr;
    }

    return it->second;
}

void draw_gizmo_var(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d)
{
    entt::as_derived(var);
    auto type = var.type();

    auto giz = get_gizmo(ctx, type);
    if(giz)
    {
        giz->draw(ctx, var, cam, dd, dd_2d);
    }
}
void draw_gizmo_billboard_var(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd)
{
    entt::as_derived(var);
    auto type = var.type();

    auto giz = get_gizmo(ctx, type);
    if(giz)
    {
        giz->draw_billboard(ctx, var, cam, dd);
    }
}
} // namespace unravel
