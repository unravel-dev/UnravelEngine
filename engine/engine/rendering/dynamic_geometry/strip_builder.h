#pragma once

#include "dynamic_geometry.h"

#include <hpp/span.hpp>
#include <math/math.h>

#include <array>
#include <cstdint>

namespace unravel
{

/// The most points a strip adds between two of its points, around a corner and around an end.
inline constexpr uint32_t STRIP_MAX_ADDED_VERTICES = 16;
/// The fewest and the most sides of a tube's ring.
inline constexpr uint32_t STRIP_MIN_TUBE_SIDES = 3;
inline constexpr uint32_t STRIP_MAX_TUBE_SIDES = 32;
/// The shortest length, in metres, one repeat of a tiled texture takes.
inline constexpr float STRIP_MIN_TILE_LENGTH = 0.001f;
/// The inner vertex of a corner's arc stands no farther from the corner than this many half widths: at a sharper
/// turn it would run far along the strip.
inline constexpr float STRIP_MAX_MITRE = 2.0f;

/// The cross-section a strip draws at each of its points.
enum class strip_shape : uint8_t
{
    /// Two vertices: a flat band.
    flat,
    /// Three vertices whose normals fan from one edge over the middle to the other, so the band shades as a cylinder.
    rounded,
    /// A ring of vertices: real geometry, right from every side and in its shadow.
    tube,
};

/// What a flat or rounded strip's face turns toward.
enum class strip_facing : uint8_t
{
    /// The camera the strip is built for.
    camera,
    /// A direction in world space (strip_options::normal).
    normal,
};

/// How the texture coordinate along a strip runs.
enum class strip_uv_mode : uint8_t
{
    /// 0 at the first point, 1 at the last.
    stretch,
    /// One repeat every strip_options::tile_length metres, so the texture keeps its size as the strip grows.
    tile,
    /// One repeat between each pair of points.
    per_segment,
};

/**
 * @brief A strip's two curves read at evenly spaced places along it: the scale of its width and its colour, linear
 * as a vertex colour takes it. A build reads the tables at every cross-section and pays no curve and no gamma
 * conversion there. Between two places a value is their mix, or the nearer one's when its curve has a key that
 * holds its value (interpolates_width, interpolates_color off), so a step stays a step.
 */
struct strip_samples
{
    static constexpr size_t COUNT = 65;

    std::array<float, COUNT> width_scales{};
    std::array<math::vec4, COUNT> colors{};
    /// The largest of the width scales.
    float widest_scale = 1.0f;
    bool interpolates_width = true;
    bool interpolates_color = true;
    /// A hash of both tables: two sets of samples with the same key hold the same values.
    uint64_t key = 0;

    /// The scale of the width at @p along, 0 at the strip's start to 1 at its end.
    auto get_width_scale(float along) const -> float;
    /// The linear colour at @p along, 0 at the strip's start to 1 at its end.
    auto get_color(float along) const -> math::vec4;
};

/// The least width a strip is built with: the editor's picking widens the ones too thin to hit.
struct strip_width_floor
{
    /// Metres, anywhere.
    float constant = 0.0f;
    /// Metres per metre between a cross-section and the camera the strip is built for: a width in pixels.
    float per_metre = 0.0f;

    auto is_set() const -> bool
    {
        return constant > 0.0f || per_metre > 0.0f;
    }
};

/// A point a strip runs through.
struct strip_point
{
    /// Its place in world space.
    math::vec3 position{0.0f, 0.0f, 0.0f};
    /// The strip's width here, in metres, and its colour, linear and between 0 and 1. Cross-sections between two
    /// points mix theirs. Not read when the options carry curves.
    float width = 1.0f;
    math::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /// With curves in the options: the place along the strip the curves are read at here, 0 to 1.
    float along = 0.0f;
    /// With strip_options::has_point_distances: the metres of strip before this point, which a tiled texture
    /// follows.
    float distance = 0.0f;
    /// No strip joins this point to the one before it: it starts a strip of its own.
    bool starts_run = false;
};

/// How build_strip turns points into triangles.
struct strip_options
{
    strip_shape shape = strip_shape::flat;
    strip_facing facing = strip_facing::camera;
    /// Facing normal: the direction the face turns toward, in world space.
    math::vec3 normal{0.0f, 1.0f, 0.0f};
    /// Where the camera the strip is built for stands, in the space the points are in.
    math::vec3 camera_position{0.0f, 0.0f, 0.0f};
    /// Shape tube: the sides of the ring, STRIP_MIN_TUBE_SIDES to STRIP_MAX_TUBE_SIDES.
    uint32_t tube_sides = 8;
    strip_uv_mode uv_mode = strip_uv_mode::stretch;
    /// UV mode tile: metres of strip per repeat of the texture.
    float tile_length = 1.0f;
    /// Points added between each pair along a smooth curve through them (0: straight segments).
    uint32_t subdivisions = 0;
    /// Flat and rounded: cross-sections added around each sharp corner, so it turns as an arc.
    uint32_t corner_vertices = 0;
    /// Flat and rounded: vertices of the half disc that closes each end. Tube: any number closes each end with a disc.
    uint32_t cap_vertices = 0;
    /// The last point joins the first. A strip of more than one run is no loop.
    bool is_loop = false;
    /// The most cross-sections one run of a strip is built with; the subdivisions and the corners shrink to keep
    /// under it, and points past it are left out.
    uint32_t max_cross_sections = 4096;
    /// The curves every cross-section reads its width and its colour from, at its place along the strip
    /// (strip_point::along, mixed between two points). Null: the points' own width and colour.
    const strip_samples* curves = nullptr;
    /// With curves: the width, in metres, the width curve scales.
    float width = 0.1f;
    strip_width_floor width_floor;
    /// The points carry how far along the strip they are (strip_point::distance): the texture coordinate of UV mode
    /// tile follows it, so a pattern stays on the points of a strip that grows at one end and ends at the other.
    bool has_point_distances = false;
};

/**
 * @brief Adds the triangles of a strip through @p points to @p out: a band, a shaded band or a tube whose first
 * texture coordinate runs across it (0 to 1, around a tube) and whose second runs along it. A point that starts a
 * run ends the strip before it. Fewer than two distinct points in a run add nothing.
 */
void build_strip(hpp::span<const strip_point> points, const strip_options& options, dynamic_geometry& out);

/// A cross-section of a sheet: the two ends of an edge that swept through space.
struct sheet_point
{
    math::vec3 base{0.0f, 0.0f, 0.0f};
    math::vec3 tip{0.0f, 1.0f, 0.0f};
    /// The sheet's colour here, linear and between 0 and 1. Not read when the options carry curves.
    math::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    /// With curves in the options: the place along the sheet the colour curve is read at here, 0 to 1.
    float along = 0.0f;
    /// With strip_options::has_point_distances: the metres the edge's middle travelled before this point.
    float distance = 0.0f;
    /// No sheet joins this point to the one before it.
    bool starts_run = false;
};

/**
 * @brief Adds the triangles of the sheet an edge swept through @p points to @p out: its first texture coordinate runs
 * from the edge's base (0) to its tip (1) and its second along the sweep by the options' UV mode. The options'
 * subdivisions run both ends along smooth curves, so a fast swing draws an arc. The sheet's width is the edge's own:
 * the options' shape, facing, width, corners and caps do not apply. Fewer than two distinct points in a run add
 * nothing.
 */
void build_sheet(hpp::span<const sheet_point> points, const strip_options& options, dynamic_geometry& out);

} // namespace unravel
