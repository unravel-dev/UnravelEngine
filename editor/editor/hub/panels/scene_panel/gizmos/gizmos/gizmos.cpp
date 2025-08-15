#include "gizmos.h"
#include <engine/rendering/camera.h>
#include "gizmo_entity.h"
#include "gizmo_physics_component.h"
#include "reflection/reflection.h"

namespace unravel
{

gizmo_registry::gizmo_registry()
{
    auto inspector_types = rttr::type::get<gizmo>().get_derived_classes();
    for(auto& inspector_type : inspector_types)
    {
        auto inspected_type_var = inspector_type.get_metadata("inspected_type");
        if(inspected_type_var)
        {
            auto inspected_type = inspected_type_var.get_value<rttr::type>();
            auto inspector_var = inspector_type.create();
            if(inspector_var)
            {
                type_map[inspected_type] = inspector_var.get_value<std::shared_ptr<gizmo>>();
            }
        }
    }

    auto gizmo_types = entt::get_derived_types(entt::resolve<gizmo>());
    for(auto& gizmo_type : gizmo_types)
    {
        auto inspected_type_var = entt::get_attribute(gizmo_type, "inspected_type");
        if(inspected_type_var)
        {
            auto inspected_type = inspected_type_var.cast<entt::meta_type>();
            if(inspected_type)
            {
                auto gizmo_instance = gizmo_type.invoke("create"_hs, {});
                auto gizmo_ptr = gizmo_instance.cast<std::shared_ptr<gizmo>>();
                type_map_entt[inspected_type.id()] = gizmo_ptr;
            }
        }
    }
}


auto get_gizmo(rtti::context& ctx, rttr::type type) -> std::shared_ptr<gizmo>
{
    auto& registry = ctx.get_cached<gizmo_registry>();

    auto it = registry.type_map.find(type);
    if(it == registry.type_map.end())
    {
        return nullptr;
    }

    return it->second;
}


void draw_gizmo_var(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd)
{
    rttr::instance object = var;
    auto type = object.get_derived_type();

    auto giz = get_gizmo(ctx, type);
    if(giz)
    {
        giz->draw(ctx, var, cam, dd);
    }
}

void draw_gizmo_billboard_var(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd)
{
    rttr::instance object = var;
    auto type = object.get_derived_type();

    auto giz = get_gizmo(ctx, type);
    if(giz)
    {
        giz->draw_billboard(ctx, var, cam, dd);
    }
}
} // namespace unravel
