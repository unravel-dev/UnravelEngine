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

void gizmo_entity::draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd1)
{
    auto e = var.cast<entt::handle>();

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

    if(e.all_of<particle_emitter_component>())
    {
        const auto& frustum = cam.get_frustum();
        const auto& particle_emitter_comp = e.get<particle_emitter_component>();
        
        // Draw world bounds
        const auto& bounds = particle_emitter_comp.get_world_bounds();
        if(frustum.test_aabb(bounds))
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xff00ffff);
            dd.encoder.setWireframe(true);
            bx::Aabb aabb;
            aabb.min = to_bx(bounds.min);
            aabb.max = to_bx(bounds.max);
            dd.encoder.draw(aabb);
        }

        // Draw emission shape
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xffff8000); // Orange color for emission shape
            dd.encoder.setWireframe(true);
            dd.encoder.pushTransform((const float*)world_transform);
            
            const auto shape = particle_emitter_comp.get_shape();
            const auto direction = particle_emitter_comp.get_direction();
            const float shape_size = 1.0f; // Base size for visualization
            
            switch(shape)
            {
                case EmitterShape::Sphere:
                {
                    // Draw a wireframe sphere
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    dd.encoder.drawCircle(Axis::X, center.x, center.y, center.z, shape_size);
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);
                    dd.encoder.drawCircle(Axis::Z, center.x, center.y, center.z, shape_size);
                    break;
                }
                case EmitterShape::Hemisphere:
                {
                    // Draw hemisphere (half sphere facing up)
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    // Full circles on X and Z axes
                    dd.encoder.drawCircle(Axis::X, center.x, center.y, center.z, shape_size);
                    dd.encoder.drawCircle(Axis::Z, center.x, center.y, center.z, shape_size);
                    // Draw lines to indicate hemisphere boundary
                    dd.encoder.moveTo(-shape_size, 0.0f, 0.0f);
                    dd.encoder.lineTo(shape_size, 0.0f, 0.0f);
                    dd.encoder.moveTo(0.0f, 0.0f, -shape_size);
                    dd.encoder.lineTo(0.0f, 0.0f, shape_size);
                    break;
                }
                case EmitterShape::Circle:
                {
                    // Draw a circle in the XZ plane
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);
                    break;
                }
                case EmitterShape::Disc:
                {
                    // Draw a filled disc representation (circle with cross lines)
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);
                    // Add cross lines to indicate it's filled
                    dd.encoder.moveTo(-shape_size, 0.0f, 0.0f);
                    dd.encoder.lineTo(shape_size, 0.0f, 0.0f);
                    dd.encoder.moveTo(0.0f, 0.0f, -shape_size);
                    dd.encoder.lineTo(0.0f, 0.0f, shape_size);
                    break;
                }
                case EmitterShape::Rect:
                {
                    // Draw a rectangle in the XZ plane
                    const float half_size = shape_size;
                    bx::Aabb rect_aabb;
                    rect_aabb.min = {-half_size, -0.01f, -half_size};
                    rect_aabb.max = {half_size, 0.01f, half_size};
                    dd.encoder.draw(rect_aabb);
                    break;
                }
                default:
                    break;
            }
            
            // Draw direction indicators
            dd.encoder.setColor(0xff00ff00); // Green color for direction
            const float arrow_length = shape_size * 0.5f;
            const float arrow_head_size = arrow_length * 0.2f;
            
            switch(direction)
            {
                case EmitterDirection::Up:
                {
                    // Draw upward arrows
                    dd.encoder.moveTo(0.0f, 0.0f, 0.0f);
                    dd.encoder.lineTo(0.0f, arrow_length, 0.0f);
                    // Arrow head
                    dd.encoder.moveTo(0.0f, arrow_length, 0.0f);
                    dd.encoder.lineTo(-arrow_head_size, arrow_length - arrow_head_size, 0.0f);
                    dd.encoder.moveTo(0.0f, arrow_length, 0.0f);
                    dd.encoder.lineTo(arrow_head_size, arrow_length - arrow_head_size, 0.0f);
                    dd.encoder.moveTo(0.0f, arrow_length, 0.0f);
                    dd.encoder.lineTo(0.0f, arrow_length - arrow_head_size, -arrow_head_size);
                    dd.encoder.moveTo(0.0f, arrow_length, 0.0f);
                    dd.encoder.lineTo(0.0f, arrow_length - arrow_head_size, arrow_head_size);
                    break;
                }
                case EmitterDirection::Outward:
                {
                    // Draw multiple outward arrows
                    const int num_arrows = 6;
                    for(int i = 0; i < num_arrows; ++i)
                    {
                        const float angle = (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(num_arrows);
                        const float cos_a = math::cos(angle);
                        const float sin_a = math::sin(angle);
                        
                        // Arrow shaft
                        dd.encoder.moveTo(cos_a * shape_size * 0.3f, 0.0f, sin_a * shape_size * 0.3f);
                        dd.encoder.lineTo(cos_a * arrow_length, 0.0f, sin_a * arrow_length);
                        
                        // Arrow head
                        const float head_x = cos_a * arrow_length;
                        const float head_z = sin_a * arrow_length;
                        const float back_x = cos_a * (arrow_length - arrow_head_size);
                        const float back_z = sin_a * (arrow_length - arrow_head_size);
                        
                        dd.encoder.moveTo(head_x, 0.0f, head_z);
                        dd.encoder.lineTo(back_x - sin_a * arrow_head_size * 0.5f, 0.0f, back_z + cos_a * arrow_head_size * 0.5f);
                        dd.encoder.moveTo(head_x, 0.0f, head_z);
                        dd.encoder.lineTo(back_x + sin_a * arrow_head_size * 0.5f, 0.0f, back_z - cos_a * arrow_head_size * 0.5f);
                        dd.encoder.moveTo(head_x, 0.0f, head_z);
                        dd.encoder.lineTo(back_x, arrow_head_size * 0.5f, back_z);
                    }
                    break;
                }
                default:
                    break;
            }
            
            dd.encoder.popTransform();
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

            auto var_comp = entt::forward_as_meta(*component);
            ::unravel::draw_gizmo_var(ctx, var_comp, cam, dd);
        });
}



void gizmo_entity::draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd)
{
    auto e = var.cast<entt::handle>();

    if(!e || !e.all_of<transform_component>())
        return;


    auto& tm = ctx.get_cached<thumbnail_manager>();

    auto& em = ctx.get_cached<editing_manager>();

    auto& transform_comp = e.get<transform_component>();
    const auto& world_transform = transform_comp.get_transform_global();

    constexpr float MIN_VISIBLE_DISTANCE = 1.0f;
    constexpr float MIN_FADE_RANGE = 0.5f;

    constexpr float MAX_VISIBLE_DISTANCE = 50.0f;
    constexpr float MAX_FADE_RANGE = 25.0f;

    auto dist = math::distance(world_transform.get_position(), cam.get_position());
    
    // Calculate distance-based alpha: full visibility in range (MIN_VISIBLE_DISTANCE - MAX_VISIBLE_DISTANCE), fade outside
    float distance_alpha = 1.0f;
    if(dist < MIN_VISIBLE_DISTANCE)
    {
        // Fade from 0 to 1 as distance goes from (MIN_VISIBLE_DISTANCE - MIN_FADE_RANGE) to MIN_VISIBLE_DISTANCE
        float fade_start = MIN_VISIBLE_DISTANCE - MIN_FADE_RANGE;
        distance_alpha = math::clamp((dist - fade_start) / MIN_FADE_RANGE, 0.0f, 1.0f);
    }
    else if(dist > MAX_VISIBLE_DISTANCE)
    {
        // Fade from 1 to 0 as distance goes from MAX_VISIBLE_DISTANCE to (MAX_VISIBLE_DISTANCE + MAX_FADE_RANGE)
        distance_alpha = math::clamp(1.0f - (dist - MAX_VISIBLE_DISTANCE) / MAX_FADE_RANGE, 0.0f, 1.0f);
    }
    // else: dist is in range [MIN_VISIBLE_DISTANCE, MAX_VISIBLE_DISTANCE], keep distance_alpha = 1.0f

    auto alpha = em.billboard_data.opacity * distance_alpha;
    
    // Early return if completely transparent
    if(alpha <= 0.0f)
        return;

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
