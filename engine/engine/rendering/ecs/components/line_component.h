#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/dynamic_geometry/dynamic_geometry.h>
#include <engine/rendering/dynamic_geometry/dynamic_renderer.h>
#include <engine/rendering/dynamic_geometry/strip_style.h>
#include <math/math.h>

#include <vector>

namespace unravel
{

/**
 * @brief A line: a strip through a list of points, drawn with a Surface material. A laser or a beam (two points a
 * script moves), a rope or a cable, lightning, an aim arc, a path, an outline.
 *
 * The points are in the entity's space, or in the world's with World Space on. The strip is built on the CPU and
 * shaded as any mesh: an Opaque or Dithered material draws it into the G-buffer, lit by everything, and a see-through
 * one in the translucency pass, sorted with the particles and the see-through models. A strip that faces the camera
 * is built for each camera that draws it; any other is built when it changes and copied until then.
 */
class line_component : public component_crtp<line_component>, public dynamic_renderer
{
public:
    /// Called when the component is created: it starts on the engine's material for it, so its overrides have
    /// a material to be of.
    static void on_create_component(entt::registry& r, entt::entity e);

    /// The points the line runs through, in order: a metre along the entity's X axis by default.
    std::vector<math::vec3> points{math::vec3(-0.5f, 0.0f, 0.0f), math::vec3(0.5f, 0.0f, 0.0f)};
    /// The points are in the world's space: the line stays where it is when the entity moves.
    bool world_space = false;
    /// The last point joins the first.
    bool loop = false;
    /// How the strip is shaped, sized, coloured and textured along its length.
    strip_style strip;

    /// The most points a line holds: a list longer than this is cut where it is built.
    static constexpr size_t MAX_POINTS = 4096;

    /**
     * @brief Adds the line's triangles to @p out, placed relative to @p origin (a point near the camera, so their
     * numbers stay small far from the world's origin).
     * @param world The entity's world transform.
     * @param camera_position Where the camera that draws the line stands, in the world.
     * @param width_floor The least width the strip gets: the editor's picking widens a thin line.
     */
    void build(const math::transform& world,
               const math::vec3& camera_position,
               const math::vec3& origin,
               dynamic_geometry& out,
               const strip_width_floor& width_floor = {}) const;

    /// The box around the line in the world: its points grown by half its widest width and by how far its curve
    /// leaves them.
    auto get_world_bounds(const math::transform& world) const -> math::bbox;

private:
    /// Adds the line's triangles to @p out, placed relative to @p origin, for a camera at @p camera_position.
    void build_strip_geometry(const math::transform& world,
                              const math::vec3& camera_position,
                              const math::transform::position_t& origin,
                              dynamic_geometry& out,
                              const strip_width_floor& width_floor) const;
    /// Whether a build keeps the geometry for the next one: a strip that does not face the camera, or one that does
    /// and casts a shadow, which the frame builds once for its shadow maps and once for its view.
    auto keeps_geometry(const strip_width_floor& width_floor) const -> bool;
    /// A hash of everything the line's triangles follow, but where the line is: its points, its strip, how the
    /// entity is turned and scaled, and @p camera_offset, where the camera stands from a strip that faces it.
    auto get_geometry_key(const math::transform& world, const math::vec3& camera_offset) const -> uint64_t;
    /// The point the kept geometry is placed relative to: the entity, or the first point of a World Space line.
    auto get_kept_origin(const math::transform& world) const -> math::transform::position_t;

    /// The strip's curves as tables, read again when a curve changes.
    mutable strip_sampler sampler_;
    /// The geometry kept from the build before (keeps_geometry): placed relative to get_kept_origin, built from what
    /// kept_key_ is the hash of. A build of the same line for the same camera copies it.
    mutable dynamic_geometry kept_geometry_;
    mutable uint64_t kept_key_ = 0;
    mutable bool has_kept_geometry_ = false;
};

} // namespace unravel
