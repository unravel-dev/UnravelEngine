#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/ecs.h>
#include <engine/rendering/dynamic_geometry/dynamic_geometry.h>
#include <engine/rendering/dynamic_geometry/strip_builder.h>

#include <graphics/graphics.h>
#include <math/math.h>

#include <functional>
#include <memory>

namespace unravel
{
class camera;
class material;

/// Copies @p geometry into the frame's transient buffers. False, copying nothing, when it is empty or the frame has
/// no room left for it.
auto upload_dynamic_geometry(const dynamic_geometry& geometry,
                             bgfx::TransientVertexBuffer& vertices,
                             bgfx::TransientIndexBuffer& indices) -> bool;

/// Sets @p geometry as the next draw's vertices and indices, from the frame's transient buffers. False, setting
/// nothing, when it is empty or the frame has no room left for it: the draw is skipped.
auto bind_dynamic_geometry(const dynamic_geometry& geometry) -> bool;

/// The world matrix of geometry placed relative to @p origin.
auto get_dynamic_geometry_world(const math::vec3& origin) -> math::mat4;

/// The box around what the Line and the Trail of @p entity draw, in the world. Not populated when the entity has
/// neither, or they draw nothing.
auto get_dynamic_renderer_bounds(entt::handle entity) -> math::bbox;

/// Takes what one component of an entity draws: its triangles, and the material it draws them with, brought up to
/// date (null while none can draw yet).
using dynamic_renderer_draw = std::function<void(const dynamic_geometry& geometry, const std::shared_ptr<material>& drawn)>;

/**
 * @brief Builds what the Line and what the Trail of @p entity draw for @p cam, placed relative to @p origin, and
 * hands each that has triangles to @p draw with its own material.
 * @param width_floor The least width of a strip: the editor's picking widens the ones too thin to hit.
 * @param scratch The geometry each one is built into: it keeps its memory between calls.
 * @return False when the entity has neither, or they draw nothing.
 */
auto draw_dynamic_renderers(entt::handle entity,
                            const camera& cam,
                            const math::vec3& origin,
                            const strip_width_floor& width_floor,
                            dynamic_geometry& scratch,
                            const dynamic_renderer_draw& draw) -> bool;

} // namespace unravel
