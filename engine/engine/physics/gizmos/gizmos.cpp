#include "gizmos.h"

#include <engine/rendering/mesh.h>
#include <bx/math.h>
#include <graphics/debugdraw.h>
#include <algorithm>
#include <vector>
#include <array>
namespace unravel
{

auto to_bx(const glm::vec3& data) -> bx::Vec3
{
    return {data.x, data.y, data.z};
}

void draw(DebugDrawEncoder& dde, const physics_sphere_shape& sh)
{
    bx::Sphere sphere;
    sphere.center = to_bx(sh.center);
    sphere.radius = sh.radius;
    dde.draw(sphere);
}

// void draw(DebugDrawEncoder& dde, const physics_plane_shape& sh)
// {
//     auto center = sh.normal * sh.constant;
//     dde.drawQuad(to_bx(-sh.normal), to_bx(center), 20);
// }

void draw(DebugDrawEncoder& dde, const physics_cylinder_shape& sh)
{
    math::vec3 axis{0, 1, 0};
    dde.drawCylinder(to_bx(sh.center + axis * -sh.length * 0.5f),
                     to_bx(sh.center + axis * sh.length * 0.5f),
                     sh.radius);
}

void draw(DebugDrawEncoder& dde, const physics_capsule_shape& sh)
{
    // auto axis = edyn::coordinate_axis_vector(sh.axis);
    math::vec3 axis{0, 1, 0};
    dde.drawCapsule(to_bx(sh.center + axis * -sh.length * 0.5f), to_bx(sh.center + axis * sh.length * 0.5f), sh.radius);
}

void draw(DebugDrawEncoder& dde, const physics_box_shape& sh)
{
    auto aabb = bx::Aabb{to_bx(sh.center - sh.extends * 0.5f), to_bx(sh.center + sh.extends * 0.5f)};
    dde.draw(aabb);
}

void draw(DebugDrawEncoder& dde, const physics_mesh_shape& sh)
{
    if(!sh.mesh_asset || !sh.mesh_asset.is_ready())
    {
        return;
    }
    
    const auto& mesh_ref = sh.mesh_asset.get();
    
    // Get vertex and index data from mesh
    auto* vertex_data = mesh_ref->get_system_vb();
    auto* index_data = mesh_ref->get_system_ib();
    auto vertex_count = mesh_ref->get_vertex_count();
    auto face_count = mesh_ref->get_face_count();
    const auto& vertex_format = mesh_ref->get_vertex_format();
    
    if(!vertex_data || !index_data || vertex_count == 0 || face_count == 0)
    {
        return;
    }
    
    // Find position attribute offset in vertex format
    auto position_offset = vertex_format.getOffset(bgfx::Attrib::Position);
    
    if(position_offset == UINT16_MAX)
    {
        return; // No position data
    }
    
    // Extract vertex positions (node-local space) into DdVertex format. Indices are global,
    // so a single shared vertex array is reused by every submesh geometry.
    std::vector<DdVertex> vertices;
    vertices.reserve(vertex_count);
    
    for(uint32_t i = 0; i < vertex_count; ++i)
    {
        std::array<float, 4> pos;
        gfx::vertex_unpack(pos.data(), gfx::attribute::Position, vertex_format, vertex_data, i);
        
        DdVertex v;
        v.x = pos[0];
        v.y = pos[1];
        v.z = pos[2];
        vertices.push_back(v);
    }
    
    // Set color based on collision type. Green for convex, blue for concave.
    dde.setColor(sh.collision_type == mesh_collision_type::convex ? 0xff00ff00 : 0xff0000ff);
    
    // Enable wireframe mode for better visibility
    dde.setWireframe(true);
    
    // Build one geometry per submesh and draw it with its own node transform. The vertex
    // buffer stores positions in node-local space; the renderer applies each submesh's
    // accumulated armature node transform (relative to the model root) on top, so we mirror
    // that here to keep the debug wireframe aligned with the rendered mesh and collision shape.
    const auto& submeshes = mesh_ref->get_submeshes();
    const auto node_transforms = mesh_ref->get_submesh_node_transforms();
    
    for(size_t s = 0; s < submeshes.size(); ++s)
    {
        const auto* submesh = submeshes[s];
        if(!submesh || submesh->face_start < 0 || submesh->face_count == 0)
        {
            continue;
        }
        
        const uint32_t face_begin = static_cast<uint32_t>(submesh->face_start);
        if(face_begin >= face_count)
        {
            continue;
        }
        const uint32_t face_render_count = std::min(submesh->face_count, face_count - face_begin);
        const uint32_t submesh_index_count = face_render_count * 3;
        const uint32_t* submesh_indices = index_data + static_cast<size_t>(face_begin) * 3;
        
        GeometryHandle geom_handle = ddCreateGeometry(static_cast<uint32_t>(vertices.size()),
                                                       vertices.data(),
                                                       submesh_index_count,
                                                       submesh_indices,
                                                       true); // Use 32-bit indices
        if(!isValid(geom_handle))
        {
            continue;
        }
        
        // Apply the shape center on top of the submesh node transform.
        math::transform local_transform;
        if(s < node_transforms.size())
        {
            local_transform = node_transforms[s];
        }
        local_transform.set_position(local_transform.get_position() + sh.center);
        dde.pushTransform((const float*)local_transform);
        dde.draw(geom_handle);
        dde.popTransform();
        ddDestroy(geom_handle);
    }
}

void draw(DebugDrawEncoder& dde, const physics_compound_shape& sh)
{
    hpp::visit(
        [&](auto&& s)
        {
            draw(dde, s);
        },
        sh.shape);
}

void draw(DebugDrawEncoder& dde, const std::vector<physics_compound_shape>& shapes)
{
    for(auto& shape : shapes)
    {
        draw(dde, shape);
    }
}


} // namespace unravel
