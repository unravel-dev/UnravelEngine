#pragma once

#include "strip_builder.h"

#include <math/math.h>

#include <cstdint>

namespace unravel
{

/// What the face of a flat or rounded strip turns toward.
enum class strip_alignment : uint8_t
{
    /// The camera that draws it.
    camera,
    /// An axis of the entity: it turns with the entity.
    local_axis,
    /// An axis of the world.
    world_axis,
};

/**
 * @brief How a Line's or a Trail's strip is shaped, sized, coloured and textured along its length. Position 0 is the
 * strip's start (a line's first point, a trail's head) and 1 its end.
 */
struct strip_style
{
    strip_style();

    strip_shape shape = strip_shape::flat;
    strip_alignment alignment = strip_alignment::camera;
    /// Alignment local axis or world axis: the direction the face turns toward.
    math::vec3 axis{0.0f, 1.0f, 0.0f};
    /// The strip's width in metres, which Width Over Length scales along it.
    float width = 0.1f;
    math::gradient<float> width_over_length;
    /// The colour along the strip, written to the vertex colour: a material shows it through its Vertex Color node.
    math::gradient<math::color> color_over_length;
    strip_uv_mode uv_mode = strip_uv_mode::stretch;
    /// UV mode tile: metres of strip per repeat of the texture.
    float tile_length = 1.0f;
    /// Points added between each pair along a smooth curve (0: straight segments).
    uint32_t subdivisions = 0;
    /// Flat and rounded: cross-sections added around each sharp corner.
    uint32_t corner_vertices = 0;
    /// Flat and rounded: vertices of the half disc that closes each end. Tube: any number closes its ends.
    uint32_t cap_vertices = 0;
    /// Shape tube: the sides of its ring, STRIP_MIN_TUBE_SIDES to STRIP_MAX_TUBE_SIDES.
    uint32_t tube_sides = 8;

    /// Whether the strip's triangles depend on the camera that draws it.
    auto faces_camera() const -> bool;
    /// How far, in metres, the curve Subdivisions draw between the points can leave the box @p points_box around
    /// them: 0 without subdivisions.
    auto get_curve_reach(const math::bbox& points_box) const -> float;
    /// How far, in metres, the strip's triangles stand from its path at most: half its widest width, and as much again
    /// where Corner Vertices put the inner vertex of a corner's arc.
    auto get_width_reach(const strip_samples& curves) const -> float;
    /**
     * @brief What build_strip takes to build this strip from points that carry their place along it.
     * @param curves The style's two curves as tables (strip_sampler::get).
     * @param camera_position Where the camera stands, in the space the strip is built in.
     * @param rotation The entity's rotation: a local axis turns with it.
     */
    auto make_options(const strip_samples& curves, const math::vec3& camera_position, const math::quat& rotation) const -> strip_options;
};

/**
 * @brief Reads a strip_style's two curves into tables (strip_samples), and again only when a curve differs from the
 * one read before. The renderer that owns the style keeps one, so a build pays for the curves once and not per
 * point.
 */
class strip_sampler
{
public:
    /// The tables of @p style's curves as they are now.
    auto get(const strip_style& style) -> const strip_samples&;

private:
    strip_samples samples_;
    /// The curves the tables were read from.
    math::gradient<float> widths_;
    math::gradient<math::color> colors_;
    bool has_samples_ = false;
};

} // namespace unravel
