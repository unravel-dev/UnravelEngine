#include "dynamic_geometry_draw.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/ecs/components/line_component.h>
#include <engine/rendering/ecs/components/trail_component.h>

#include <graphics/vertex_decl.h>

#include <cstring>

namespace unravel
{

auto upload_dynamic_geometry(const dynamic_geometry& geometry,
                             bgfx::TransientVertexBuffer& vertices,
                             bgfx::TransientIndexBuffer& indices) -> bool
{
    const auto& layout = gfx::mesh_vertex::get_layout();
    const uint32_t vertex_count = geometry.get_vertex_count();
    const uint32_t index_count = geometry.get_index_count();
    if(index_count == 0 || !gfx::check_avail_transient_buffers(vertex_count, layout, index_count, true))
    {
        return false;
    }
    bgfx::allocTransientVertexBuffer(&vertices, vertex_count, layout);
    std::memcpy(vertices.data, geometry.get_vertex_data(), geometry.get_vertex_data_size());
    bgfx::allocTransientIndexBuffer(&indices, index_count, true);
    std::memcpy(indices.data, geometry.get_indices().data(), size_t(index_count) * sizeof(uint32_t));
    return true;
}

auto bind_dynamic_geometry(const dynamic_geometry& geometry) -> bool
{
    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    if(!upload_dynamic_geometry(geometry, vertices, indices))
    {
        return false;
    }
    bgfx::setVertexBuffer(0, &vertices, 0, geometry.get_vertex_count());
    bgfx::setIndexBuffer(&indices, 0, geometry.get_index_count());
    return true;
}

auto get_dynamic_geometry_world(const math::vec3& origin) -> math::mat4
{
    return math::translate(math::mat4(1.0f), origin);
}

auto get_dynamic_renderer_bounds(entt::handle entity) -> math::bbox
{
    math::bbox bounds;
    const auto* transform = entity.try_get<transform_component>();
    if(transform == nullptr)
    {
        return bounds;
    }
    const auto& world = transform->get_transform_global();
    const auto add_box = [&bounds](const math::bbox& box)
    {
        if(box.is_populated())
        {
            bounds.add_point(box.min);
            bounds.add_point(box.max);
        }
    };
    if(const auto* line = entity.try_get<line_component>())
    {
        add_box(line->get_world_bounds(world));
    }
    if(const auto* trail = entity.try_get<trail_component>())
    {
        add_box(trail->get_world_bounds(world));
    }
    return bounds;
}

auto draw_dynamic_renderers(entt::handle entity,
                            const camera& cam,
                            const math::vec3& origin,
                            const strip_width_floor& width_floor,
                            dynamic_geometry& scratch,
                            const dynamic_renderer_draw& draw) -> bool
{
    const auto* transform = entity.try_get<transform_component>();
    if(transform == nullptr)
    {
        return false;
    }
    const auto& world = transform->get_transform_global();
    const math::vec3 camera_position = cam.get_position();
    bool is_drawn = false;
    const auto draw_one = [&](auto* renderer)
    {
        if(renderer == nullptr)
        {
            return;
        }
        // The material first: what a build keeps follows it.
        renderer->refresh_material(dynamic_renderer::default_strip_material());
        scratch.clear();
        renderer->build(world, camera_position, origin, scratch, width_floor);
        if(!scratch.is_empty())
        {
            draw(scratch, renderer->get_drawn_material());
            is_drawn = true;
        }
    };
    draw_one(entity.try_get<line_component>());
    draw_one(entity.try_get<trail_component>());
    return is_drawn;
}

} // namespace unravel
