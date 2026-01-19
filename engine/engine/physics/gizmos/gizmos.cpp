#include "gizmos.h"

#include <engine/rendering/mesh.h>
#include <bx/math.h>
#include <graphics/debugdraw.h>
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
    
    // Extract vertex positions and convert to DdVertex format
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
    
    // Extract indices (faces are already triangles, 3 indices per face)
    const uint32_t index_count = face_count * 3;
    
    // Create geometry handle
    GeometryHandle geom_handle = ddCreateGeometry(static_cast<uint32_t>(vertices.size()),
                                                   vertices.data(),
                                                   index_count,
                                                   index_data,
                                                   true); // Use 32-bit indices
    
    if(isValid(geom_handle))
    {
        // Set color based on collision type
        // Green for convex, blue for concave
        dde.setColor(sh.collision_type == mesh_collision_type::convex ? 0xff00ff00 : 0xff0000ff);
        
        // Enable wireframe mode for better visibility
        dde.setWireframe(true);

        math::transform local_transform;
        local_transform.set_position(sh.center);
        dde.pushTransform((const float*)local_transform);
        // Draw the geometry
        dde.draw(geom_handle);
        dde.popTransform();
        // Clean up geometry handle
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
