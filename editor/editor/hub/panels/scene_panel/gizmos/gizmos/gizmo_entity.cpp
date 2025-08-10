#include "gizmo_entity.h"
#include "gizmos.h"

#include <engine/meta/ecs/components/all_components.h>

#include <engine/assets/asset_manager.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>

#include <editor/editing/thumbnail_manager.h>
#include <editor/editing/editing_manager.h>
#include <editor/imgui/imgui_interface.h>
#include <hpp/type_name.hpp>
#include <hpp/utility.hpp>

namespace unravel
{
namespace
{
auto to_bx(const math::vec3& data) -> bx::Vec3
{
    return {data.x, data.y, data.z};
}

auto from_bx(const bx::Vec3& data) -> math::vec3
{
    return {data.x, data.y, data.z};
}

} // namespace

void gizmo_entity::draw(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd1)
{
    auto e = var.get_value<entt::handle>();

    if(!e || !e.all_of<transform_component>())
        return;

    auto& transform_comp = e.get<transform_component>();
    const auto& world_transform = transform_comp.get_transform_global();

    gfx::dd_raii dd(dd1.view);
    if(e.all_of<camera_component>())
    {
        auto& selected_camera_comp = e.get<camera_component>();
        auto& selected_camera = selected_camera_comp.get_camera();
        const auto view_proj = selected_camera.get_view_projection();
        const auto bounds = selected_camera.get_local_bounding_box();
        DebugDrawEncoderScopePush scope(dd.encoder);
        dd.encoder.setColor(0xffffffff);
        dd.encoder.setWireframe(true);
        if(selected_camera.get_projection_mode() == projection_mode::perspective)
        {
            dd.encoder.drawFrustum(&view_proj);
        }
        else
        {
            bx::Aabb aabb;
            aabb.min = to_bx(bounds.min);
            aabb.max = to_bx(bounds.max);
            dd.encoder.pushTransform((const float*)world_transform);
            dd.encoder.draw(aabb);
            dd.encoder.popTransform();
        }
    }

    if(e.all_of<light_component>())
    {
        const auto& light_comp = e.get<light_component>();
        const auto& light = light_comp.get_light();

        if(light.type == light_type::spot)
        {
            auto adjacent = light.spot_data.get_range();
            {
                auto tan_angle = math::tan(math::radians(light.spot_data.get_outer_angle() * 0.5f));
                // oposite = tan * adjacent
                auto oposite = tan_angle * adjacent;
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xff00ff00);
                dd.encoder.setWireframe(true);
                dd.encoder.setLod(3);
                math::vec3 from = transform_comp.get_position_global();
                math::vec3 to = from + transform_comp.get_z_axis_local() * adjacent;
                dd.encoder.drawCone(to_bx(to), to_bx(from), oposite);
            }
            {
                auto tan_angle = math::tan(math::radians(light.spot_data.get_inner_angle() * 0.5f));
                // oposite = tan * adjacent
                auto oposite = tan_angle * adjacent;
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xff00ffff);
                dd.encoder.setWireframe(true);
                dd.encoder.setLod(3);
                math::vec3 from = transform_comp.get_position_global();
                math::vec3 to = from + transform_comp.get_z_axis_local() * adjacent;
                dd.encoder.drawCone(to_bx(to), to_bx(from), oposite);
            }
        }
        else if(light.type == light_type::point)
        {
            auto radius = light.point_data.range;
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xff00ff00);
            dd.encoder.setWireframe(true);
            math::vec3 center = transform_comp.get_position_global();
            dd.encoder.drawCircle(Axis::X, center.x, center.y, center.z, radius);
            dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, radius);
            dd.encoder.drawCircle(Axis::Z, center.x, center.y, center.z, radius);
        }
        else if(light.type == light_type::directional)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setLod(255);
            dd.encoder.setColor(0xff00ff00);
            dd.encoder.setWireframe(true);
            math::vec3 from1 = transform_comp.get_position_global();
            math::vec3 to1 = from1 + transform_comp.get_z_axis_local() * 1.0f;

            bx::Cylinder cylinder = {to_bx(from1), to_bx(to1), 0.1f};

            dd.encoder.draw(cylinder);
            math::vec3 from2 = to1;
            math::vec3 to2 = from2 + transform_comp.get_z_axis_local() * 0.5f;

            bx::Cone cone = {to_bx(from2), to_bx(to2), 0.25f};
            dd.encoder.draw(cone);
        }
    }

    if(e.all_of<reflection_probe_component>())
    {
        const auto& probe_comp = e.get<reflection_probe_component>();
        const auto& probe = probe_comp.get_probe();
        if(probe.type == probe_type::box)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xff00ff00);
            dd.encoder.setWireframe(true);
            dd.encoder.pushTransform((const float*)world_transform);
            bx::Aabb aabb;
            aabb.min = to_bx(-probe.box_data.extents);
            aabb.max = to_bx(probe.box_data.extents);

            dd.encoder.draw(aabb);
            dd.encoder.popTransform();
        }
        else
        {
            auto radius = probe.get_face_extents(0, world_transform);
            auto transform = world_transform;
            transform.reset_scale();

            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xff00ff00);
            dd.encoder.setWireframe(true);
            dd.encoder.pushTransform((const float*)transform);
            math::vec3 center{};
            dd.encoder.drawCircle(Axis::X, center.x, center.y, center.z, radius);
            dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, radius);
            dd.encoder.drawCircle(Axis::Z, center.x, center.y, center.z, radius);
        }
    }

    // if(e.all_of<model_component>())
    // {
    //     const auto& frustum = cam.get_frustum();
    //     const auto& model_comp = e.get<model_component>();

    //     // world bounds
    //     {
    //         auto world_bounds = model_comp.get_world_bounds();

    //         if(frustum.test_aabb(world_bounds))
    //         {
    //             DebugDrawEncoderScopePush scope(dd.encoder);
    //             dd.encoder.setColor(0xff00ffff);
    //             dd.encoder.setWireframe(true);
    //             bx::Aabb aabb;
    //             aabb.min = to_bx(world_bounds.min);
    //             aabb.max = to_bx(world_bounds.max);
    //             dd.encoder.draw(aabb);
    //         }
    //     }

    //     // local bounds
    //     {
    //         const auto& model = model_comp.get_model();
    //         if(!model.is_valid())
    //         {
    //             return;
    //         }

    //         const auto lod = model.get_lod(0);
    //         if(!lod)
    //         {
    //             return;
    //         }
    //         const auto& mesh = lod.get();
    //         const auto& bounds = mesh->get_bounds();
    //         // Test the bounding box of the mesh
    //         if(frustum.test_obb(bounds, world_transform))
    //         {
    //             DebugDrawEncoderScopePush scope(dd.encoder);
    //             dd.encoder.setColor(0xffffffff);
    //             dd.encoder.setWireframe(true);
    //             dd.encoder.pushTransform((const float*)world_transform);
    //             bx::Aabb aabb;
    //             aabb.min = to_bx(bounds.min);
    //             aabb.max = to_bx(bounds.max);
    //             dd.encoder.draw(aabb);
    //             dd.encoder.popTransform();
    //         }

    //         const auto& submeshes = model_comp.get_armature_entities();
    //         for(const auto& submesh : submeshes)
    //         {
    //             const auto& submesh_comp = submesh.try_get<submesh_component>();
    //             if(!submesh_comp)
    //             {
    //                 continue;
    //             }
    //             const auto& submesh_transform_comp = submesh.get<transform_component>();
    //             const auto& submesh_transform = submesh_transform_comp.get_transform_global();
    //             DebugDrawEncoderScopePush scope(dd.encoder);
    //             dd.encoder.setColor(0xffaaaaaa);
    //             dd.encoder.setWireframe(true);
    //             for(const auto submesh_id : submesh_comp->submeshes)
    //             {
    //                 const auto& submesh = mesh->get_submesh(submesh_id);

    //                 if(frustum.test_obb(submesh.bbox, submesh_transform))
    //                 {
    //                     dd.encoder.pushTransform((const float*)submesh_transform);
    //                     bx::Aabb aabb;
    //                     aabb.min = to_bx(submesh.bbox.min);
    //                     aabb.max = to_bx(submesh.bbox.max);
    //                     dd.encoder.draw(aabb);
    //                     dd.encoder.popTransform();
    //                 }
    //             }
    //         }
    //     }
    // }

    if(e.all_of<text_component>())
    {
        const auto& frustum = cam.get_frustum();
        const auto& text_comp = e.get<text_component>();

        // world bounds
        {
            auto bbox = text_comp.get_bounds();

            if(frustum.test_obb(bbox, world_transform))
            {
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xff00ffff);
                dd.encoder.setWireframe(true);
                dd.encoder.pushTransform((const float*)world_transform);
                bx::Aabb aabb;
                aabb.min = to_bx(bbox.min);
                aabb.max = to_bx(bbox.max);
                dd.encoder.draw(aabb);
                dd.encoder.popTransform();
            }
        }
    }

    hpp::for_each_tuple_type<all_inspectable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_inspectable_components>;
            auto component = e.try_get<ctype>();

            if(!component)
            {
                return;
            }

            ::unravel::draw_gizmo(ctx, component, cam, dd);
        });
}



void gizmo_entity::draw_billboard(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd)
{
    auto e = var.get_value<entt::handle>();

    if(!e || !e.all_of<transform_component>())
        return;


    auto& tm = ctx.get_cached<thumbnail_manager>();

    auto& em = ctx.get_cached<editing_manager>();

    auto& transform_comp = e.get<transform_component>();
    const auto& world_transform = transform_comp.get_transform_global();

    auto dist = math::distance(world_transform.get_position(), cam.get_position());
    dist = math::clamp(dist - std::min(dist, 1.0f), 0.0f, 1.0f);


    auto alpha = math::lerp(0.0f, em.billboard_data.opacity, dist / 1.0f);

    auto col = math::color::white();

    float tint = 1.0f;
    if(!transform_comp.is_active_global())
    {
        tint *= 0.5f;
    }

    auto icon = tm.get_gizmo_icon(e);

    if(e.all_of<light_component>())
    {
        const auto& light_comp = e.get<light_component>();
        const auto& light = light_comp.get_light();
        col = light.color;
    }

    if(!cam.test_billboard(em.billboard_data.size, world_transform))
        return; // completely outside → skip draw

    if(icon)
    {
        dd.encoder.setState(em.billboard_data.depth_aware, false, false);

        col.value.a = alpha;
        col.value *= tint;
        dd.encoder.setColor(col);

        gfx::draw_billboard(dd.encoder,
                            icon->native_handle(),
                            to_bx(world_transform.get_position()),
                            to_bx(cam.get_position()),
                            to_bx(cam.z_unit_axis()),
                            em.billboard_data.size);
        dd.encoder.setColor(0xffffffff);

        dd.encoder.setState(true, true, false);
    }
}
} // namespace unravel
