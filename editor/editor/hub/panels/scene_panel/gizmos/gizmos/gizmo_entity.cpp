#include "gizmo_entity.h"
#include "gizmos.h"
#include "../gizmos_renderer.h"
#include "glm/ext.hpp"
#include "imgui/imgui_internal.h"

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
auto vec3_to_bx(const math::vec3& data) -> bx::Vec3
{
    return {data.x, data.y, data.z};
}

auto vec3_from_bx(const bx::Vec3& data) -> math::vec3
{
    return {data.x, data.y, data.z};
}

} // namespace

void gizmo_entity::draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd1, dd_2d_raii& dd_2d)
{
    auto e = var.cast<entt::handle>();

    if(!e || !e.all_of<transform_component>())
        return;

    auto& em = ctx.get_cached<editing_manager>();
    auto& transform_comp = e.get<transform_component>();
    const auto& world_transform = transform_comp.get_transform_global();

    gfx::dd_raii dd(dd1.view);
    if(e.all_of<camera_component>() && em.gizmos.show_camera)
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
            aabb.min = vec3_to_bx(bounds.min);
            aabb.max = vec3_to_bx(bounds.max);
            dd.encoder.pushTransform((const float*)world_transform);
            dd.encoder.draw(aabb);
            dd.encoder.popTransform();
            
        }
    }

    if(e.all_of<light_component>() && em.gizmos.show_light)
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
                dd.encoder.drawCone(vec3_to_bx(to), vec3_to_bx(from), oposite);
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
                dd.encoder.drawCone(vec3_to_bx(to), vec3_to_bx(from), oposite);
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

            bx::Cylinder cylinder = {vec3_to_bx(from1), vec3_to_bx(to1), 0.1f};

            dd.encoder.draw(cylinder);
            math::vec3 from2 = to1;
            math::vec3 to2 = from2 + transform_comp.get_z_axis_local() * 0.5f;

            bx::Cone cone = {vec3_to_bx(from2), vec3_to_bx(to2), 0.25f};
            dd.encoder.draw(cone);
        }
    }

    if(e.all_of<reflection_probe_component>() && em.gizmos.show_reflection_probe)
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
            aabb.min = vec3_to_bx(-probe.box_data.extents);
            aabb.max = vec3_to_bx(probe.box_data.extents);
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

    if(e.all_of<volume_component>() && em.gizmos.show_volume)
    {
        const auto& volume_comp = e.get<volume_component>();
        if(volume_comp.mode == volume_mode::local)
        {
            const auto& volume = volume_comp.get_local_bounds();
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.pushTransform((const float*)world_transform);
            dd.encoder.setColor(0x11ffff00);
            dd.encoder.setWireframe(false);

            dd.encoder.setState(true, false, true);

            {

                bx::Aabb aabb;
                aabb.min = vec3_to_bx(volume.min);
                aabb.max = vec3_to_bx(volume.max);
                dd.encoder.draw(aabb);
            }
            {
                dd.encoder.setColor(0xff00ff00);

                dd.encoder.setWireframe(true);
                bx::Aabb aabb;
                aabb.min = vec3_to_bx(volume.min);
                aabb.max = vec3_to_bx(volume.max);
                dd.encoder.draw(aabb);
            }
            {
                dd.encoder.setWireframe(false);
                dd.encoder.setColor(0x1100ff00);

                bx::Aabb aabb;
                aabb.min = vec3_to_bx(volume.min - volume_comp.blend_distance);
                aabb.max = vec3_to_bx(volume.max + volume_comp.blend_distance);
                dd.encoder.draw(aabb);
            }

            dd.encoder.setState(false, false, true);

            dd.encoder.popTransform();
        }
    }

    if(e.all_of<model_component>() && em.gizmos.show_model)
    {
        const auto& frustum = cam.get_frustum();
        auto& model_comp = e.get<model_component>();
        const auto& model = model_comp.get_model();
        if(!model.is_valid())
        {
            return;
        }

        // world bounds
        if(em.gizmos.show_model_bounds)
        {
            auto world_bounds = model_comp.get_world_bounds();

            if(frustum.test_aabb(world_bounds))
            {
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xff00ffff);
                dd.encoder.setWireframe(true);
                bx::Aabb aabb;
                aabb.min = vec3_to_bx(world_bounds.min);
                aabb.max = vec3_to_bx(world_bounds.max);
                dd.encoder.draw(aabb);
            }
        }

        auto& current_lod_data = model_comp.get_lod_data_for_camera(&cam, gfx::get_render_frame());

        current_lod_data.calculate_screen_rect(cam);

        const auto lod = model.get_lod(current_lod_data.current_lod_index);
        if(!lod)
        {
            return;
        }
        const auto& mesh = lod.get();
        const auto& bounds = mesh->get_bounds();
        // Test the bounding box of the mesh
        bool visible = frustum.test_obb(bounds, world_transform);
        // local bounds
        if(em.gizmos.show_model_local_bounds)
        {

            if(visible)
            {
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xffffffff);
                dd.encoder.setWireframe(true);
                dd.encoder.pushTransform((const float*)world_transform);
                bx::Aabb aabb;
                aabb.min = vec3_to_bx(bounds.min);
                aabb.max = vec3_to_bx(bounds.max);
                dd.encoder.draw(aabb);
                dd.encoder.popTransform();
            }

            if(em.gizmos.show_model_submesh_local_bounds)
            {
                const auto& submeshes = model_comp.get_armature_entities();
                for(const auto& submesh : submeshes)
                {
                    const auto& submesh_comp = submesh.try_get<submesh_component>();
                    if(!submesh_comp)
                    {
                        continue;
                    }
                    const auto& submesh_transform_comp = submesh.get<transform_component>();
                    const auto& submesh_transform = submesh_transform_comp.get_transform_global();
                    DebugDrawEncoderScopePush scope(dd.encoder);
                    dd.encoder.setColor(0xffaaaaaa);
                    dd.encoder.setWireframe(true);
                    for(const auto submesh_id : submesh_comp->submeshes)
                    {
                        const auto& submesh = mesh->get_submesh(submesh_id);
    
                        if(frustum.test_obb(submesh->bbox, submesh_transform))
                        {
                            dd.encoder.pushTransform((const float*)submesh_transform);
                            bx::Aabb aabb;
                            aabb.min = vec3_to_bx(submesh->bbox.min);
                            aabb.max = vec3_to_bx(submesh->bbox.max);
                            dd.encoder.draw(aabb);
                            dd.encoder.popTransform();
                        }
                    }
                }
            }
        }


        //lods
        if(em.gizmos.show_model_lod && visible)
        {   
     
            dd_2d.callbacks.push_back([current_lod_data]()
            {
                auto window = ImGui::GetCurrentWindow();
                auto draw_list = window->DrawList;

                const auto& rect = current_lod_data.rect;
                if(rect.width() > 0 && rect.height() > 0)
                {
    
                    draw_list->AddRect(ImVec2(rect.left, rect.top), ImVec2(rect.right, rect.bottom), IM_COL32(255, 255, 255, 255));
                    auto draw_aligned_text = [&](ImVec2 pos, float align,const std::string& text)
                    {
                        auto text_size = ImGui::CalcTextSize(text.c_str());
                        pos.x += (rect.width() - text_size.x) * align;
                        draw_list->AddText(pos, IM_COL32(255, 255, 255, 255), text.c_str());
                    };
                    draw_aligned_text(ImVec2(rect.left, rect.bottom), 0.5f, fmt::format("LOD: {}", current_lod_data.current_lod_index));
                }
            });
            

        }

        
    }

    if(e.all_of<text_component>() && em.gizmos.show_text)
    {
        const auto& frustum = cam.get_frustum();
        const auto& text_comp = e.get<text_component>();
        auto bbox = text_comp.get_bounds();
        if(frustum.test_obb(bbox, world_transform))
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xff00ffff);
            dd.encoder.setWireframe(true);
            dd.encoder.pushTransform((const float*)world_transform);
            bx::Aabb aabb;
            aabb.min = vec3_to_bx(bbox.min);
            aabb.max = vec3_to_bx(bbox.max);
            dd.encoder.draw(aabb);
            dd.encoder.popTransform();
        }
    }

    if(e.all_of<particle_emitter_component>() && em.gizmos.show_particle_emitter)
    {
        const auto& frustum = cam.get_frustum();
        const auto& particle_emitter_comp = e.get<particle_emitter_component>();
        
        // Draw world bounds
        if(em.gizmos.show_particle_emitter_bounds)
        {
            const auto& bounds = particle_emitter_comp.get_world_bounds();
            if(frustum.test_aabb(bounds))
            {
                DebugDrawEncoderScopePush scope(dd.encoder);
                dd.encoder.setColor(0xff00ffff);
                dd.encoder.setWireframe(true);
                bx::Aabb aabb;
                aabb.min = vec3_to_bx(bounds.min);
                aabb.max = vec3_to_bx(bounds.max);
                dd.encoder.draw(aabb);
            }
        }

        // Draw emission shape
        if(em.gizmos.show_particle_emitter_shape)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(0xffff8000); // Orange color for emission shape
            dd.encoder.setWireframe(true);
            dd.encoder.pushTransform((const float*)world_transform);
            
            const auto shape = particle_emitter_comp.get_shape();
            const auto scale = particle_emitter_comp.get_emission_shape_scale();
            const auto position = particle_emitter_comp.get_emission_shape_position();
            const auto direction = particle_emitter_comp.get_direction();
            const float shape_size = 1.0f; // Base size for visualization
            
            switch(shape)
            {
                case EmitterShape::Sphere:
                {
                    auto transform = math::translate(math::mat4(1.0f), position) * math::scale(math::mat4(1.0f), scale);
                    dd.encoder.pushTransform(math::value_ptr(transform));

                    // Draw a wireframe sphere
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    dd.encoder.drawCircle(Axis::X, center.x, center.y, center.z, shape_size);
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);
                    dd.encoder.drawCircle(Axis::Z, center.x, center.y, center.z, shape_size);

                    dd.encoder.popTransform();
                    break;
                }
                case EmitterShape::Hemisphere:
                {
                    auto transform = math::translate(math::mat4(1.0f), position) * math::scale(math::mat4(1.0f), scale);
                    dd.encoder.pushTransform(math::value_ptr(transform));

                    // Draw hemisphere (half sphere facing up)
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    
                    // Draw the base circle (full circle at Y=0)
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);
                    
                    // Draw vertical arcs to form the hemisphere dome
                    // Each arc goes from one side of the base circle, over the top, to the other side
                    const int num_arcs = 8; // Number of vertical arcs around the hemisphere
                    for(int i = 0; i < num_arcs; ++i)
                    {
                        const float angle = (3.14159265f * static_cast<float>(i)) / static_cast<float>(num_arcs);
                        const float cos_a = math::cos(angle);
                        const float sin_a = math::sin(angle);
                        
                        // Position each arc at a different point around the base circle
                        // and draw a 180-degree arc that goes up and over
                        const float x_pos = cos_a * shape_size;
                        const float z_pos = sin_a * shape_size;
                        
                        // Draw arc in the plane that contains the Y axis and the radial direction
                        // We need to use moveTo/lineTo to manually create the hemisphere arcs
                        const int arc_segments = 16;
                        bool first_point = true;
                        
                        for(int j = 0; j <= arc_segments; ++j)
                        {
                            const float arc_angle = (3.14159265f * static_cast<float>(j)) / static_cast<float>(arc_segments);
                            const float y = shape_size * math::sin(arc_angle);
                            const float radius_at_height = shape_size * math::cos(arc_angle);
                            
                            const float x = cos_a * radius_at_height;
                            const float z = sin_a * radius_at_height;
                            
                            if(first_point)
                            {
                                dd.encoder.moveTo(x, y, z);
                                first_point = false;
                            }
                            else
                            {
                                dd.encoder.lineTo(x, y, z);
                            }
                        }
                    }
                    
                    // Draw horizontal circles at different heights to show the hemisphere shape
                    const int num_horizontal_circles = 2;
                    for(int i = 1; i <= num_horizontal_circles; ++i)
                    {
                        const float height_ratio = static_cast<float>(i) / static_cast<float>(num_horizontal_circles + 1);
                        const float y_pos = shape_size * height_ratio;
                        const float radius_at_height = shape_size * math::sqrt(1.0f - height_ratio * height_ratio);
                        
                        // Draw circles at different heights
                        dd.encoder.drawCircle(Axis::Y, center.x, y_pos, center.z, radius_at_height);
                    }

                    dd.encoder.popTransform();
                    break;
                }
                case EmitterShape::Circle:
                {
                    auto transform = math::translate(math::mat4(1.0f), position) * math::scale(math::mat4(1.0f), scale);
                    dd.encoder.pushTransform(math::value_ptr(transform));

                    // Draw a circle in the XZ plane
                    math::vec3 center{0.0f, 0.0f, 0.0f};
                    dd.encoder.drawCircle(Axis::Y, center.x, center.y, center.z, shape_size);

                    dd.encoder.popTransform();
                    break;
                }
                case EmitterShape::Box:
                {
                    // Draw a box
                    const float half_size = shape_size;
                    bx::Aabb box_aabb;
                    box_aabb.min = {-half_size * scale.x + position.x, -half_size * scale.y + position.y, -half_size * scale.z + position.z};
                    box_aabb.max = {half_size * scale.x + position.x, half_size * scale.y + position.y, half_size * scale.z + position.z};
                    dd.encoder.draw(box_aabb);
                    break;
                }
                case EmitterShape::Rect:
                {
                    // Draw a rectangle in the XZ plane
                    const float half_size = shape_size;
                    bx::Aabb rect_aabb;
                    rect_aabb.min = {-half_size * scale.x + position.x, -0.01f, -half_size * scale.z + position.z};
                    rect_aabb.max = {half_size * scale.x + position.x, 0.01f, half_size * scale.z + position.z};
                    dd.encoder.draw(rect_aabb);
                    break;
                }
                default:
                    break;
            }
            
            if(em.gizmos.show_particle_emitter_direction)
            {
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
                            const float angle =
                                (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(num_arrows);
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
                            dd.encoder.lineTo(back_x - sin_a * arrow_head_size * 0.5f,
                                              0.0f,
                                              back_z + cos_a * arrow_head_size * 0.5f);
                            dd.encoder.moveTo(head_x, 0.0f, head_z);
                            dd.encoder.lineTo(back_x + sin_a * arrow_head_size * 0.5f,
                                              0.0f,
                                              back_z - cos_a * arrow_head_size * 0.5f);
                            dd.encoder.moveTo(head_x, 0.0f, head_z);
                            dd.encoder.lineTo(back_x, arrow_head_size * 0.5f, back_z);
                        }
                        break;
                    }
                    case EmitterDirection::Inward:
                    {
                        // Draw multiple inward arrows
                        const int num_arrows = 6;
                        for(int i = 0; i < num_arrows; ++i)
                        {
                            const float angle =
                                (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(num_arrows);
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
                            dd.encoder.lineTo(back_x - sin_a * arrow_head_size * 0.5f,
                                              0.0f,
                                              back_z + cos_a * arrow_head_size * 0.5f);
                            dd.encoder.moveTo(head_x, 0.0f, head_z);
                            dd.encoder.lineTo(back_x + sin_a * arrow_head_size * 0.5f,
                                              0.0f,
                                              back_z - cos_a * arrow_head_size * 0.5f);
                            dd.encoder.moveTo(head_x, 0.0f, head_z);
                            dd.encoder.lineTo(back_x, arrow_head_size * 0.5f, back_z);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
            
            dd.encoder.popTransform();
        }
    }

    if(em.gizmos.show_component_gizmos)
    {
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
                ::unravel::draw_gizmo_var(ctx, var_comp, cam, dd, dd_2d);
            });
    }
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
        dd.encoder.setState(em.billboard_data.depth_aware, false, true);
        col.value.a = alpha;

        col.value *= tint;
        dd.encoder.setColor(col);

        gfx::draw_billboard(dd.encoder,
                            icon->native_handle(),
                            vec3_to_bx(world_transform.get_position()),
                            vec3_to_bx(cam.get_position()),
                            vec3_to_bx(cam.z_unit_axis()),
                            em.billboard_data.size);
        dd.encoder.setColor(0xffffffff);

        dd.encoder.setState(true, true, true);
    }
}
} // namespace unravel
