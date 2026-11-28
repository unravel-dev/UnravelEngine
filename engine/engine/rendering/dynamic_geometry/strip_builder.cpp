#include "strip_builder.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace unravel
{

namespace
{
/// Points closer than this, in metres, are one point.
constexpr float STRIP_MIN_SEGMENT = 1.0e-5f;
/// A direction shorter than this, squared, has no side: the strip keeps the side of the cross-section before.
constexpr float STRIP_MIN_SIDE_SQ = 1.0e-10f;
/// A corner turns as an arc when the cosine of its turn is below this (a turn above about 20 degrees).
constexpr float STRIP_CORNER_COSINE = 0.94f;
/// A turn whose cosine is below this folds back on itself: no arc fits it.
constexpr float STRIP_FOLD_COSINE = -0.98f;
/// The edges of a rounded band lean this far from its face: the cosine and the sine of 70 degrees.
constexpr float STRIP_ROUNDED_EDGE_COSINE = 0.342f;
constexpr float STRIP_ROUNDED_EDGE_SINE = 0.94f;
constexpr float STRIP_PI = 3.14159265358979f;

/// The entry of a strip_samples table before @p along and the share of the way to the next one. A value that is no
/// number reads the table's start.
auto find_strip_sample(float along, float& share) -> size_t
{
    const float clamped = along > 0.0f ? std::min(along, 1.0f) : 0.0f;
    const float place = clamped * float(strip_samples::COUNT - 1);
    const size_t index = std::min(size_t(place), strip_samples::COUNT - 2);
    share = place - float(index);
    return index;
}

/// A point of the path a strip is built along: a source point or one of the curve between two, with the metres of
/// strip before it and its place among the source points (the index of the one before it plus the share of the way
/// to the next).
struct strip_path_point
{
    math::vec3 position{};
    float width = 1.0f;
    math::vec4 color{1.0f};
    float distance = 0.0f;
    float segment = 0.0f;
};

/// A corner of a band that turns as an arc: the directions the strip reaches it and leaves it by.
struct strip_corner
{
    math::vec3 incoming{0.0f, 0.0f, 1.0f};
    math::vec3 outgoing{0.0f, 0.0f, 1.0f};
};

/// One cross-section of a strip: where it stands, the direction the strip runs there, and its width, colour and
/// texture coordinate along the strip.
struct strip_cross_section
{
    math::vec3 position{};
    math::vec3 direction{0.0f, 0.0f, 1.0f};
    float width = 1.0f;
    math::vec4 color{1.0f};
    float v = 0.0f;
    /// The corner whose arc the cross-section is a step of, counted from 1; 0 for a cross-section of no arc.
    uint32_t corner = 0;
};

/// 1 where the second texture coordinate of @p sections grows along the strip, -1 where it falls (a trail's tiled
/// distances fall from its head to its tail): a vertex's bitangent runs the way that coordinate grows.
auto get_v_way(const std::vector<strip_cross_section>& sections) -> float
{
    return sections.back().v < sections.front().v ? -1.0f : 1.0f;
}

/// The frame a band's cross-section is drawn in.
struct strip_band_frame
{
    math::vec3 side{1.0f, 0.0f, 0.0f};
    math::vec3 normal{0.0f, 1.0f, 0.0f};
};

/// The cosine and the sine of the angle each vertex of a ring stands at.
struct strip_ring_angle
{
    float cosine = 1.0f;
    float sine = 0.0f;
};

/// A cross-section of a sheet: the edge there, its colour, the metres its middle travelled before it and its place
/// among the source points.
struct strip_sheet_section
{
    math::vec3 base{};
    math::vec3 tip{};
    math::vec4 color{1.0f};
    float distance = 0.0f;
    float segment = 0.0f;
};

/// What a build works in: the lists keep their memory from one build to the next.
struct strip_scratch
{
    std::vector<strip_point> distinct;
    std::vector<strip_path_point> path;
    std::vector<math::vec3> directions;
    std::vector<strip_corner> corners;
    std::vector<strip_cross_section> sections;
    std::vector<strip_ring_angle> ring;
    std::vector<math::vec3> radials;
    std::vector<sheet_point> distinct_sheet;
    std::vector<strip_sheet_section> sheet_sections;
};

auto get_strip_scratch() -> strip_scratch&
{
    thread_local strip_scratch scratch;
    return scratch;
}

/// Calls @p build with each run of @p points: from the first point, or one that starts a run, to the one before the
/// next that starts one.
template<typename Point, typename Build>
void for_each_strip_run(hpp::span<const Point> points, Build&& build)
{
    size_t first = 0;
    for(size_t i = 1; i <= points.size(); ++i)
    {
        if(i == points.size() || points[i].starts_run)
        {
            build(points.subspan(first, i - first));
            first = i;
        }
    }
}

/// Whether @p points are more than one run.
template<typename Point>
auto has_strip_runs(hpp::span<const Point> points) -> bool
{
    return std::any_of(points.begin() + (points.empty() ? 0 : 1),
                       points.end(),
                       [](const Point& point)
                       {
                           return point.starts_run;
                       });
}

/// Whether every coordinate of @p point is a number.
auto is_strip_point_finite(const math::vec3& point) -> bool
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

/// The source points without the ones that repeat the point before them, and, in a loop, without a last point that
/// repeats the first, into @p distinct. A point that is no number is left out.
void get_distinct_points(hpp::span<const strip_point> points, bool is_loop, std::vector<strip_point>& distinct)
{
    distinct.clear();
    distinct.reserve(points.size());
    for(const auto& point : points)
    {
        const bool is_new = distinct.empty() || math::distance(distinct.back().position, point.position) > STRIP_MIN_SEGMENT;
        if(is_new && is_strip_point_finite(point.position))
        {
            distinct.push_back(point);
        }
    }
    if(is_loop && distinct.size() > 2 && math::distance(distinct.front().position, distinct.back().position) <= STRIP_MIN_SEGMENT)
    {
        distinct.pop_back();
    }
}

/// The point at @p t between @p b and @p c on the smooth curve through @p a, @p b, @p c and @p d (Catmull-Rom).
auto get_curve_point(const math::vec3& a, const math::vec3& b, const math::vec3& c, const math::vec3& d, float t) -> math::vec3
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * b) + (c - a) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 + (3.0f * b - a - 3.0f * c + d) * t3);
}

/// How many points a strip adds between each pair of @p segments segments to stay within @p budget cross-sections.
auto get_subdivisions(uint32_t wanted, size_t segments, uint32_t budget) -> uint32_t
{
    if(segments == 0 || budget < 2)
    {
        return 0;
    }
    const size_t per_segment = (size_t(budget) - 1) / segments;
    return per_segment == 0 ? 0 : uint32_t(std::min<size_t>(wanted, per_segment - 1));
}

/// A step along the smooth curve through a strip's points: the segment it is on, the share of the way along it, and
/// the points the curve bends by there. An open strip has no point before its first and none after its last.
struct strip_curve_step
{
    size_t segment = 0;
    float t = 0.0f;
    size_t before = 0;
    size_t from = 0;
    size_t to = 0;
    size_t after = 0;
    bool has_before = false;
    bool has_after = false;
};

/// Calls @p visit with each step along the curve through @p count points: every point but the last of an open strip
/// (t 0), and @p subdivisions places between it and the next.
template<typename Visit>
void for_each_curve_step(size_t count, bool is_loop, uint32_t subdivisions, Visit&& visit)
{
    const size_t segments = is_loop ? count : count - 1;
    for(size_t segment = 0; segment < segments; ++segment)
    {
        strip_curve_step step;
        step.segment = segment;
        step.from = segment;
        step.to = (segment + 1) % count;
        step.has_before = is_loop || segment > 0;
        step.has_after = is_loop || segment + 2 < count;
        step.before = (segment + count - 1) % count;
        step.after = (segment + 2) % count;
        for(uint32_t i = 0; i <= subdivisions; ++i)
        {
            step.t = float(i) / float(subdivisions + 1);
            visit(step);
        }
    }
}

/// The point of a curve step between @p from and @p to, bent by @p before and @p after: an end without a neighbour
/// curves as if the strip went on straight.
auto get_curve_step_point(const strip_curve_step& step,
                          const math::vec3& before,
                          const math::vec3& from,
                          const math::vec3& to,
                          const math::vec3& after) -> math::vec3
{
    if(step.t == 0.0f)
    {
        return from;
    }
    return get_curve_point(step.has_before ? before : from * 2.0f - to, from, to, step.has_after ? after : to * 2.0f - from, step.t);
}

/// The width of a cross-section at @p position that is @p width wide before the options' floor.
auto apply_width_floor(float width, const math::vec3& position, const strip_options& options) -> float
{
    if(!options.width_floor.is_set())
    {
        return width;
    }
    const float floor = options.width_floor.constant + options.width_floor.per_metre * math::distance(position, options.camera_position);
    return std::max(width, floor);
}

/// The path through the distinct @p points, into @p path: the points and the curve between them, closed onto the
/// first point in a loop, each with its width, its colour and the metres of strip before it.
void make_strip_path(const std::vector<strip_point>& points, const strip_options& options, std::vector<strip_path_point>& path)
{
    path.clear();
    const size_t count = points.size();
    if(count < 2)
    {
        return;
    }
    const bool is_loop = options.is_loop && count > 2;
    const size_t segments = is_loop ? count : count - 1;
    const uint32_t subdivisions = get_subdivisions(options.subdivisions, segments, options.max_cross_sections);
    path.reserve(segments * (subdivisions + 1) + 1);
    // The point at @p position, @p t of the way from @p from to a point at @p to_along and @p to_distance.
    const auto add_point = [&](const math::vec3& position, const strip_point& from, const strip_point& to, float to_along, float t, float segment)
    {
        strip_path_point point;
        point.position = position;
        point.segment = segment;
        point.distance = math::mix(from.distance, to.distance, t);
        if(options.curves != nullptr)
        {
            const float along = math::mix(from.along, to_along, t);
            point.width = std::max(options.width, 0.0f) * options.curves->get_width_scale(along);
            point.color = options.curves->get_color(along);
        }
        else
        {
            point.width = math::mix(from.width, to.width, t);
            point.color = math::mix(from.color, to.color, t);
        }
        // A width that is no number draws nothing.
        point.width = std::isfinite(point.width) ? std::max(point.width, 0.0f) : 0.0f;
        point.width = apply_width_floor(point.width, position, options);
        path.push_back(point);
    };
    for_each_curve_step(count,
                        is_loop,
                        subdivisions,
                        [&](const strip_curve_step& step)
                        {
                            const auto& from = points[step.from];
                            const auto& to = points[step.to];
                            // A loop's last segment ends where the curves do, on its first point.
                            const float to_along = is_loop && step.to == 0 ? 1.0f : to.along;
                            const math::vec3 position =
                                get_curve_step_point(step, points[step.before].position, from.position, to.position, points[step.after].position);
                            add_point(position, from, to, to_along, step.t, float(step.segment) + step.t);
                        });
    // The closing point reads the curves at their end: a loop's first point again, at position 1.
    const auto& last = is_loop ? points.front() : points.back();
    add_point(last.position, last, last, is_loop ? 1.0f : last.along, 1.0f, float(segments));
    if(is_loop)
    {
        path.back().distance = points.back().distance;
    }
    if(!options.has_point_distances)
    {
        path.front().distance = 0.0f;
        for(size_t i = 1; i < path.size(); ++i)
        {
            path[i].distance = path[i - 1].distance + math::distance(path[i - 1].position, path[i].position);
        }
    }
}

/// The texture coordinate along a strip at a point @p distance metres and @p segment source points along it, of a
/// strip that starts at @p first_distance and is @p length metres long.
auto get_strip_v(float distance, float segment, float first_distance, float length, const strip_options& options) -> float
{
    switch(options.uv_mode)
    {
        case strip_uv_mode::tile:
            return distance / std::max(options.tile_length, STRIP_MIN_TILE_LENGTH);
        case strip_uv_mode::per_segment:
            return segment;
        case strip_uv_mode::stretch:
        default:
            return length != 0.0f ? (distance - first_distance) / length : 0.0f;
    }
}

/// The unit direction from @p from to @p to; false when they are one point.
auto get_direction(const math::vec3& from, const math::vec3& to, math::vec3& direction) -> bool
{
    const math::vec3 offset = to - from;
    const float length = math::length(offset);
    if(length <= STRIP_MIN_SEGMENT)
    {
        return false;
    }
    direction = offset / length;
    return true;
}

/// What the face of a band at @p position turns toward: the camera it is built for, or the options' normal.
auto get_band_facing(const math::vec3& position, const strip_options& options) -> math::vec3
{
    return options.facing == strip_facing::camera ? options.camera_position - position : options.normal;
}

/// The cosine of the turn a band makes in its own plane at a corner at @p position, which it reaches along
/// @p incoming and leaves along @p outgoing: the angle between its side before the corner and after it. A turn out
/// of that plane (a ramp) has no inner and no outer edge and reads 1, as does a corner where the strip runs at what
/// it faces.
auto get_band_turn_cosine(const math::vec3& position, const math::vec3& incoming, const math::vec3& outgoing, const strip_options& options)
    -> float
{
    const math::vec3 toward = get_band_facing(position, options);
    const math::vec3 side_before = math::cross(incoming, toward);
    const math::vec3 side_after = math::cross(outgoing, toward);
    const float lengths_sq = math::dot(side_before, side_before) * math::dot(side_after, side_after);
    return lengths_sq > STRIP_MIN_SIDE_SQ * STRIP_MIN_SIDE_SQ ? math::dot(side_before, side_after) / std::sqrt(lengths_sq) : 1.0f;
}

/// Whether a band turns at a corner at @p position sharply enough, in its own plane, for an arc of cross-sections.
auto is_arc_corner(const math::vec3& position, const math::vec3& incoming, const math::vec3& outgoing, const strip_options& options) -> bool
{
    const float cosine = get_band_turn_cosine(position, incoming, outgoing, options);
    return cosine < STRIP_CORNER_COSINE && cosine > STRIP_FOLD_COSINE;
}

/// How many cross-sections each corner's arc adds between its two ends, so that @p arcs arcs fit a path of @p count
/// points into the options' budget. 0: the corners stay as they are.
auto get_arc_vertices(size_t arcs, size_t count, const strip_options& options) -> uint32_t
{
    // Two are kept for the joint of a loop, whose arc is split over the strip's two ends.
    const size_t taken = count + 2;
    if(arcs == 0 || options.max_cross_sections <= taken)
    {
        return 0;
    }
    // An arc of n vertices puts n + 2 cross-sections where the corner had one.
    const size_t per_arc = (options.max_cross_sections - taken) / arcs;
    return per_arc > 1 ? uint32_t(std::min<size_t>(options.corner_vertices, per_arc - 1)) : 0;
}

/// The cross-sections of @p path, into the scratch: one at each point along the mean of the directions it is reached
/// and left by, and, with corner vertices on a band, an arc of them around each sharp corner. In a loop the first
/// and the last stand on the same point and run the same way, each with its half of the arc when the joint is a
/// sharp corner.
void make_cross_sections(const std::vector<strip_path_point>& path, const strip_options& options, strip_scratch& scratch)
{
    auto& sections = scratch.sections;
    auto& directions = scratch.directions;
    auto& corners = scratch.corners;
    sections.clear();
    corners.clear();
    const size_t count = path.size();
    if(count < 2)
    {
        return;
    }
    const bool is_loop = options.is_loop && count > 3;
    const bool has_arcs = options.corner_vertices > 0 && options.shape != strip_shape::tube;
    const float first_distance = path.front().distance;
    const float length = path.back().distance - first_distance;
    directions.assign(count - 1, math::vec3(0.0f, 0.0f, 1.0f));
    // A step too short to have a direction runs the way of the one before it, and the ones before the first that has
    // one run its way.
    size_t first_told = count - 1;
    for(size_t i = 0; i + 1 < count; ++i)
    {
        if(get_direction(path[i].position, path[i + 1].position, directions[i]))
        {
            first_told = std::min(first_told, i);
        }
        else if(i > 0)
        {
            directions[i] = directions[i - 1];
        }
    }
    for(size_t i = 0; i < first_told && first_told + 1 < count; ++i)
    {
        directions[i] = directions[first_told];
    }
    const bool has_joint_arc = has_arcs && is_loop && is_arc_corner(path.front().position, directions[count - 2], directions[0], options);
    size_t arcs = has_joint_arc ? 1 : 0;
    if(has_arcs)
    {
        for(size_t i = 1; i + 1 < count; ++i)
        {
            arcs += is_arc_corner(path[i].position, directions[i - 1], directions[i], options) ? 1 : 0;
        }
    }
    const uint32_t arc_vertices = get_arc_vertices(arcs, count, options);
    sections.reserve(count + arcs * (arc_vertices + 1) + 2);
    const auto add_section = [&](const strip_path_point& point, const math::vec3& direction, uint32_t corner)
    {
        strip_cross_section section;
        section.position = point.position;
        section.direction = direction;
        section.width = point.width;
        section.color = point.color;
        section.v = get_strip_v(point.distance, point.segment, first_distance, length, options);
        section.corner = corner;
        sections.push_back(section);
    };
    // The part of the arc of corner @p corner at @p point between the shares @p from and @p to of its turn: a
    // cross-section at each end of the part, and the arc's own steps between them.
    const auto add_arc = [&](const strip_path_point& point, uint32_t corner, float from, float to)
    {
        const auto& turn = corners[corner - 1];
        const auto add_step = [&](float t)
        {
            add_section(point, math::normalize(math::mix(turn.incoming, turn.outgoing, t)), corner);
        };
        add_step(from);
        for(uint32_t step = 1; step <= arc_vertices; ++step)
        {
            const float t = float(step) / float(arc_vertices + 1);
            if(t > from && t < to)
            {
                add_step(t);
            }
        }
        add_step(to);
    };
    uint32_t joint_corner = 0;
    for(size_t i = 0; i < count; ++i)
    {
        const bool is_end = i == 0 || i + 1 == count;
        // A loop's two ends are one corner, reached by its last segment and left by its first.
        const math::vec3 incoming = i > 0 ? directions[i - 1] : (is_loop ? directions[count - 2] : directions[0]);
        const math::vec3 outgoing = i + 1 < count ? directions[i] : (is_loop ? directions[0] : directions[count - 2]);
        const bool is_arc =
            arc_vertices > 0 && (!is_end || has_joint_arc) && is_arc_corner(path[i].position, incoming, outgoing, options);
        if(!is_arc)
        {
            const math::vec3 mean = incoming + outgoing;
            add_section(path[i], math::dot(mean, mean) > STRIP_MIN_SIDE_SQ ? math::normalize(mean) : outgoing, 0);
            continue;
        }
        if(!is_end)
        {
            corners.push_back({incoming, outgoing});
            add_arc(path[i], uint32_t(corners.size()), 0.0f, 1.0f);
            continue;
        }
        // The joint's arc leaves from the middle of its turn at the strip's start and comes back to it at its end.
        if(joint_corner == 0)
        {
            corners.push_back({incoming, outgoing});
            joint_corner = uint32_t(corners.size());
        }
        if(i == 0)
        {
            add_arc(path[i], joint_corner, 0.5f, 1.0f);
        }
        else
        {
            add_arc(path[i], joint_corner, 0.0f, 0.5f);
        }
    }
}

/// A unit vector across @p direction, for a cross-section nothing else gives a side.
auto get_any_side(const math::vec3& direction) -> math::vec3
{
    const math::vec3 up = std::abs(direction.y) < 0.99f ? math::vec3(0.0f, 1.0f, 0.0f) : math::vec3(1.0f, 0.0f, 0.0f);
    return math::normalize(math::cross(direction, up));
}

/// The frame of a band standing at @p position and running along @p direction: its side across the strip and the
/// normal of its face, which turns toward the camera or along the options' normal. Where the strip runs straight at
/// what it faces, it keeps @p previous.
auto get_band_frame(const math::vec3& position, const math::vec3& direction, const strip_options& options, const strip_band_frame* previous)
    -> strip_band_frame
{
    math::vec3 side = math::cross(direction, get_band_facing(position, options));
    if(math::dot(side, side) < STRIP_MIN_SIDE_SQ)
    {
        side = previous != nullptr ? previous->side : get_any_side(direction);
        // The side kept may lean along a strip that has turned since.
        side = side - direction * math::dot(side, direction);
        if(math::dot(side, side) < STRIP_MIN_SIDE_SQ)
        {
            side = get_any_side(direction);
        }
    }
    strip_band_frame frame;
    frame.side = math::normalize(side);
    frame.normal = math::normalize(math::cross(frame.side, direction));
    return frame;
}

/// The normal of a band's vertex @p across its width (-1 one edge, 0 the middle, 1 the other): the face's, leaning
/// toward the edge on a rounded band.
auto get_band_normal(const strip_band_frame& frame, float across, strip_shape shape) -> math::vec3
{
    if(shape != strip_shape::rounded || across == 0.0f)
    {
        return frame.normal;
    }
    return math::normalize(frame.side * (across * STRIP_ROUNDED_EDGE_SINE) + frame.normal * STRIP_ROUNDED_EDGE_COSINE);
}

/// The half disc that closes a band's end: a fan around @p section from one edge, around the end, to the other.
/// @p outward is the direction the end points in: against the strip at its start, along it at its end.
void add_band_cap(const strip_cross_section& section,
                  const strip_band_frame& frame,
                  const math::vec3& outward,
                  const strip_options& options,
                  dynamic_geometry& out)
{
    const float half_width = section.width * 0.5f;
    // The fan runs clockwise about the face's normal: from the left edge at the start, from the right at the end.
    const float start_side = math::dot(outward, section.direction) < 0.0f ? -1.0f : 1.0f;
    dynamic_vertex vertex;
    vertex.color = section.color;
    vertex.tangent = frame.side;
    vertex.bitangent = section.direction;
    vertex.normal = frame.normal;
    vertex.position = section.position;
    vertex.uv = math::vec2(0.5f, section.v);
    const uint32_t center = out.add_vertex(vertex);
    const uint32_t steps = options.cap_vertices + 1;
    uint32_t previous = 0;
    for(uint32_t step = 0; step <= steps; ++step)
    {
        const float angle = STRIP_PI * float(step) / float(steps);
        const float across = start_side * std::cos(angle);
        const math::vec3 radial = frame.side * across + outward * std::sin(angle);
        vertex.position = section.position + radial * half_width;
        vertex.uv = math::vec2(0.5f + 0.5f * across, section.v);
        vertex.normal = options.shape == strip_shape::rounded
                            ? math::normalize(radial * STRIP_ROUNDED_EDGE_SINE + frame.normal * STRIP_ROUNDED_EDGE_COSINE)
                            : frame.normal;
        const uint32_t index = out.add_vertex(vertex);
        if(step > 0)
        {
            out.add_triangle(center, previous, index);
        }
        previous = index;
    }
}

/// The inside of a corner's arc: the edge of the band that keeps one place through the arc, and that place. The arc
/// fans the other edge around the corner, so the band never folds over itself.
struct strip_arc_inside
{
    /// The corner the place is of, counted from 1; 0 for none.
    uint32_t corner = 0;
    /// -1 or 1: which edge of the band is the inner one.
    float edge = 0.0f;
    math::vec3 position{};
};

/// The inside of the arc @p section is a step of: where the inner edges of the band before the corner and after it
/// meet, no farther from the corner than STRIP_MAX_MITRE half widths.
auto get_arc_inside(const strip_cross_section& section,
                    const strip_corner& corner,
                    const strip_options& options,
                    const strip_band_frame* previous) -> strip_arc_inside
{
    const math::vec3 middle = math::normalize(corner.incoming + corner.outgoing);
    const strip_band_frame frame = get_band_frame(section.position, middle, options, previous);
    const strip_band_frame before = get_band_frame(section.position, corner.incoming, options, previous);
    strip_arc_inside inside;
    inside.corner = section.corner;
    // The strip turns toward its inner edge.
    inside.edge = math::dot(corner.outgoing - corner.incoming, frame.side) > 0.0f ? 1.0f : -1.0f;
    // Half the turn the band makes in its own plane: the sharper it is, the farther in its inner edges meet.
    const float half_turn_cosine = std::max(math::dot(frame.side, before.side), 1.0f / STRIP_MAX_MITRE);
    inside.position = section.position + frame.side * (inside.edge * section.width * 0.5f / half_turn_cosine);
    return inside;
}

/// A flat or rounded band through @p sections: two or three vertices at each, the outer edge fanned around each
/// corner's arc, and half discs at the ends of an open one with cap vertices.
void add_band(const strip_scratch& scratch, const strip_options& options, dynamic_geometry& out)
{
    const auto& sections = scratch.sections;
    const uint32_t columns = options.shape == strip_shape::rounded ? 3 : 2;
    const float v_way = get_v_way(sections);
    strip_band_frame first_frame;
    strip_band_frame frame;
    strip_arc_inside inside;
    uint32_t previous_row = 0;
    for(size_t i = 0; i < sections.size(); ++i)
    {
        const auto& section = sections[i];
        const strip_band_frame* previous_frame = i > 0 ? &frame : nullptr;
        if(section.corner != 0 && section.corner != inside.corner)
        {
            inside = get_arc_inside(section, scratch.corners[section.corner - 1], options, previous_frame);
        }
        frame = get_band_frame(section.position, section.direction, options, previous_frame);
        if(i == 0)
        {
            first_frame = frame;
        }
        const bool is_arc_step = section.corner != 0;
        // Through an arc the inner edge and the middle keep their place: no triangle joins a vertex to itself.
        const bool continues_arc = is_arc_step && i > 0 && sections[i - 1].corner == section.corner;
        const float half_width = section.width * 0.5f;
        const auto get_across = [columns](uint32_t column)
        {
            return columns == 2 ? (column == 0 ? -1.0f : 1.0f) : float(column) - 1.0f;
        };
        const auto keeps_place = [&](uint32_t column)
        {
            const float across = get_across(column);
            return continues_arc && (across == 0.0f || across == inside.edge);
        };
        dynamic_vertex vertex;
        vertex.color = section.color;
        vertex.tangent = frame.side;
        vertex.bitangent = section.direction * v_way;
        uint32_t row = 0;
        for(uint32_t column = 0; column < columns; ++column)
        {
            const float across = get_across(column);
            const bool is_inner = is_arc_step && across == inside.edge;
            vertex.position = is_inner ? inside.position : section.position + frame.side * (across * half_width);
            vertex.normal = get_band_normal(frame, across, options.shape);
            vertex.uv = math::vec2(0.5f + 0.5f * across, section.v);
            const uint32_t index = out.add_vertex(vertex);
            row = column == 0 ? index : row;
        }
        if(i > 0)
        {
            for(uint32_t column = 0; column + 1 < columns; ++column)
            {
                if(!keeps_place(column))
                {
                    out.add_triangle(previous_row + column, previous_row + column + 1, row + column);
                }
                if(!keeps_place(column + 1))
                {
                    out.add_triangle(previous_row + column + 1, row + column + 1, row + column);
                }
            }
        }
        previous_row = row;
    }
    if(!options.is_loop && options.cap_vertices > 0 && !sections.empty())
    {
        add_band_cap(sections.front(), first_frame, -sections.front().direction, options, out);
        add_band_cap(sections.back(), frame, sections.back().direction, options, out);
    }
}

/// The angles of a ring of @p sides sides, its first vertex repeated at its end, into @p ring.
void make_ring_angles(uint32_t sides, std::vector<strip_ring_angle>& ring)
{
    ring.resize(size_t(sides) + 1);
    for(uint32_t side = 0; side <= sides; ++side)
    {
        const float angle = 2.0f * STRIP_PI * float(side) / float(sides);
        ring[side] = {std::cos(angle), std::sin(angle)};
    }
}

/// Where the first vertex of each cross-section's ring stands, into @p radials: each follows the one before it, laid
/// back across the strip's new direction, so the tube twists as little as its path allows. A loop spreads what
/// twist is left at its joint over its length, so the last ring meets the first.
void make_tube_radials(const std::vector<strip_cross_section>& sections, bool is_loop, std::vector<math::vec3>& radials)
{
    radials.resize(sections.size());
    math::vec3 radial = get_any_side(sections.front().direction);
    for(size_t i = 0; i < sections.size(); ++i)
    {
        const auto& direction = sections[i].direction;
        const math::vec3 carried = radial - direction * math::dot(radial, direction);
        radial = math::dot(carried, carried) > STRIP_MIN_SIDE_SQ ? math::normalize(carried) : get_any_side(direction);
        radials[i] = radial;
    }
    if(!is_loop || sections.size() < 2)
    {
        return;
    }
    const auto& joint = sections.front().direction;
    const float twist = std::atan2(math::dot(math::cross(radials.back(), radials.front()), joint), math::dot(radials.back(), radials.front()));
    for(size_t i = 1; i < sections.size(); ++i)
    {
        const float angle = twist * float(i) / float(sections.size() - 1);
        radials[i] = radials[i] * std::cos(angle) + math::cross(sections[i].direction, radials[i]) * std::sin(angle);
    }
}

/// The disc that closes a tube's end at @p section, facing @p outward: a fan over the ring's vertices, which stand
/// @p radial_a and @p radial_b across the tube.
void add_tube_cap(const strip_cross_section& section,
                  const math::vec3& radial_a,
                  const math::vec3& radial_b,
                  const math::vec3& outward,
                  const std::vector<strip_ring_angle>& ring,
                  dynamic_geometry& out)
{
    const uint32_t sides = uint32_t(ring.size()) - 1;
    const float radius = section.width * 0.5f;
    const bool faces_along = math::dot(outward, section.direction) > 0.0f;
    dynamic_vertex vertex;
    vertex.color = section.color;
    vertex.normal = outward;
    vertex.tangent = radial_a;
    vertex.bitangent = radial_b;
    vertex.position = section.position;
    vertex.uv = math::vec2(0.5f, section.v);
    const uint32_t center = out.add_vertex(vertex);
    uint32_t previous = 0;
    for(uint32_t side = 0; side <= sides; ++side)
    {
        const math::vec3 radial = radial_a * ring[side].cosine + radial_b * ring[side].sine;
        vertex.position = section.position + radial * radius;
        vertex.uv = math::vec2(float(side) / float(sides), section.v);
        const uint32_t index = out.add_vertex(vertex);
        if(side > 0)
        {
            // The ring turns clockwise seen along the strip.
            if(faces_along)
            {
                out.add_triangle(center, previous, index);
            }
            else
            {
                out.add_triangle(center, index, previous);
            }
        }
        previous = index;
    }
}

/// A tube through the scratch's cross-sections: a ring of vertices around each, and discs at the ends of an open one
/// with cap vertices.
void add_tube(strip_scratch& scratch, const strip_options& options, dynamic_geometry& out)
{
    const auto& sections = scratch.sections;
    const auto& ring = scratch.ring;
    const uint32_t sides = std::clamp(options.tube_sides, STRIP_MIN_TUBE_SIDES, STRIP_MAX_TUBE_SIDES);
    make_ring_angles(sides, scratch.ring);
    make_tube_radials(sections, options.is_loop, scratch.radials);
    const float v_way = get_v_way(sections);
    uint32_t previous_ring = 0;
    for(size_t i = 0; i < sections.size(); ++i)
    {
        const auto& section = sections[i];
        const math::vec3& radial_a = scratch.radials[i];
        const math::vec3 radial_b = math::cross(section.direction, radial_a);
        const float radius = section.width * 0.5f;
        dynamic_vertex vertex;
        vertex.color = section.color;
        vertex.bitangent = section.direction * v_way;
        uint32_t first_of_ring = 0;
        for(uint32_t side = 0; side <= sides; ++side)
        {
            const float cosine = ring[side].cosine;
            const float sine = ring[side].sine;
            vertex.normal = radial_a * cosine + radial_b * sine;
            vertex.tangent = radial_b * cosine - radial_a * sine;
            vertex.position = section.position + vertex.normal * radius;
            vertex.uv = math::vec2(float(side) / float(sides), section.v);
            const uint32_t index = out.add_vertex(vertex);
            first_of_ring = side == 0 ? index : first_of_ring;
        }
        if(i > 0)
        {
            for(uint32_t side = 0; side < sides; ++side)
            {
                out.add_triangle(previous_ring + side, previous_ring + side + 1, first_of_ring + side);
                out.add_triangle(previous_ring + side + 1, first_of_ring + side + 1, first_of_ring + side);
            }
        }
        previous_ring = first_of_ring;
    }
    if(!options.is_loop && options.cap_vertices > 0)
    {
        const auto& first = sections.front();
        const auto& last = sections.back();
        add_tube_cap(first, scratch.radials.front(), math::cross(first.direction, scratch.radials.front()), -first.direction, ring, out);
        add_tube_cap(last, scratch.radials.back(), math::cross(last.direction, scratch.radials.back()), last.direction, ring, out);
    }
}

/// One run of a strip: the triangles through @p points, which no break divides.
void build_strip_run(hpp::span<const strip_point> points, const strip_options& options, strip_scratch& scratch, dynamic_geometry& out)
{
    get_distinct_points(points, options.is_loop, scratch.distinct);
    // A loop's last cross-section stands on its first point again.
    const size_t closing = options.is_loop ? 1 : 0;
    const size_t most_points = options.max_cross_sections > closing ? options.max_cross_sections - closing : 0;
    if(scratch.distinct.size() > most_points)
    {
        scratch.distinct.resize(most_points);
    }
    strip_options used = options;
    // Two points make no loop.
    used.is_loop = options.is_loop && scratch.distinct.size() > 2;
    make_strip_path(scratch.distinct, used, scratch.path);
    make_cross_sections(scratch.path, used, scratch);
    if(scratch.sections.size() < 2)
    {
        return;
    }
    if(used.shape == strip_shape::tube)
    {
        add_tube(scratch, used, out);
    }
    else
    {
        add_band(scratch, used, out);
    }
}

/// The sheet points without the ones whose edge stands where the edge before it does, into @p distinct.
void get_distinct_sheet_points(hpp::span<const sheet_point> points, std::vector<sheet_point>& distinct)
{
    distinct.clear();
    distinct.reserve(points.size());
    for(const auto& point : points)
    {
        const bool is_finite = is_strip_point_finite(point.base) && is_strip_point_finite(point.tip);
        const bool is_new = is_finite && (distinct.empty() || math::distance(distinct.back().base, point.base) > STRIP_MIN_SEGMENT ||
                                          math::distance(distinct.back().tip, point.tip) > STRIP_MIN_SEGMENT);
        if(is_new)
        {
            distinct.push_back(point);
        }
    }
}

/// The cross-sections of a sheet through the distinct @p points, into @p sections: the edges at the points and along
/// the curves between them, each with its colour and the metres its middle travelled before it.
void make_sheet_sections(const std::vector<sheet_point>& points, const strip_options& options, std::vector<strip_sheet_section>& sections)
{
    sections.clear();
    const size_t count = points.size();
    if(count < 2)
    {
        return;
    }
    const size_t segments = count - 1;
    const uint32_t subdivisions = get_subdivisions(options.subdivisions, segments, options.max_cross_sections);
    sections.reserve(segments * (subdivisions + 1) + 1);
    const auto add_section = [&](const math::vec3& base, const math::vec3& tip, const sheet_point& from, const sheet_point& to, float t, float segment)
    {
        strip_sheet_section section;
        section.base = base;
        section.tip = tip;
        section.segment = segment;
        section.distance = math::mix(from.distance, to.distance, t);
        section.color = options.curves != nullptr ? options.curves->get_color(math::mix(from.along, to.along, t))
                                                  : math::mix(from.color, to.color, t);
        sections.push_back(section);
    };
    for_each_curve_step(count,
                        false,
                        subdivisions,
                        [&](const strip_curve_step& step)
                        {
                            const auto& before = points[step.before];
                            const auto& from = points[step.from];
                            const auto& to = points[step.to];
                            const auto& after = points[step.after];
                            add_section(get_curve_step_point(step, before.base, from.base, to.base, after.base),
                                        get_curve_step_point(step, before.tip, from.tip, to.tip, after.tip),
                                        from,
                                        to,
                                        step.t,
                                        float(step.segment) + step.t);
                        });
    const auto& last = points.back();
    add_section(last.base, last.tip, last, last, 0.0f, float(segments));
    if(!options.has_point_distances)
    {
        sections.front().distance = 0.0f;
        for(size_t i = 1; i < sections.size(); ++i)
        {
            const math::vec3 moved = (sections[i].base + sections[i].tip) * 0.5f - (sections[i - 1].base + sections[i - 1].tip) * 0.5f;
            sections[i].distance = sections[i - 1].distance + math::length(moved);
        }
    }
}

/// The triangles of a sheet through @p sections: two vertices at each, the edge's base and its tip.
void add_sheet(const std::vector<strip_sheet_section>& sections, const strip_options& options, dynamic_geometry& out)
{
    const float first_distance = sections.front().distance;
    const float length = sections.back().distance - first_distance;
    // The second texture coordinate falls along a sheet whose tiled distances fall: the bitangent follows it.
    const float v_way = get_strip_v(sections.back().distance, sections.back().segment, first_distance, length, options) <
                                get_strip_v(sections.front().distance, sections.front().segment, first_distance, length, options)
                            ? -1.0f
                            : 1.0f;
    uint32_t previous_row = 0;
    math::vec3 normal(0.0f, 1.0f, 0.0f);
    for(size_t i = 0; i < sections.size(); ++i)
    {
        const auto& section = sections[i];
        const auto& next = sections[std::min(i + 1, sections.size() - 1)];
        const auto& previous = sections[i > 0 ? i - 1 : 0];
        const math::vec3 across = section.tip - section.base;
        const math::vec3 along = (next.base + next.tip) * 0.5f - (previous.base + previous.tip) * 0.5f;
        const math::vec3 facing = math::cross(across, along);
        // An edge that stood still, or has no length, keeps the face of the cross-section before it.
        if(math::dot(facing, facing) > STRIP_MIN_SIDE_SQ)
        {
            normal = math::normalize(facing);
        }
        dynamic_vertex vertex;
        vertex.color = section.color;
        vertex.normal = normal;
        vertex.tangent = math::dot(across, across) > STRIP_MIN_SIDE_SQ ? math::normalize(across) : math::vec3(1.0f, 0.0f, 0.0f);
        vertex.bitangent = (math::dot(along, along) > STRIP_MIN_SIDE_SQ ? math::normalize(along) : math::vec3(0.0f, 0.0f, 1.0f)) * v_way;
        const float v = get_strip_v(section.distance, section.segment, first_distance, length, options);
        vertex.position = section.base;
        vertex.uv = math::vec2(0.0f, v);
        const uint32_t row = out.add_vertex(vertex);
        vertex.position = section.tip;
        vertex.uv = math::vec2(1.0f, v);
        out.add_vertex(vertex);
        if(i > 0)
        {
            out.add_triangle(previous_row, previous_row + 1, row);
            out.add_triangle(previous_row + 1, row + 1, row);
        }
        previous_row = row;
    }
}
} // namespace

auto strip_samples::get_width_scale(float along) const -> float
{
    float share = 0.0f;
    const size_t index = find_strip_sample(along, share);
    if(!interpolates_width)
    {
        return width_scales[share < 0.5f ? index : index + 1];
    }
    return math::mix(width_scales[index], width_scales[index + 1], share);
}

auto strip_samples::get_color(float along) const -> math::vec4
{
    float share = 0.0f;
    const size_t index = find_strip_sample(along, share);
    if(!interpolates_color)
    {
        return colors[share < 0.5f ? index : index + 1];
    }
    return math::mix(colors[index], colors[index + 1], share);
}

void build_strip(hpp::span<const strip_point> points, const strip_options& options, dynamic_geometry& out)
{
    auto& scratch = get_strip_scratch();
    strip_options used = options;
    // Runs are open strips: only a strip in one piece closes onto its first point.
    used.is_loop = options.is_loop && !has_strip_runs(points);
    for_each_strip_run(points,
                       [&](hpp::span<const strip_point> run)
                       {
                           build_strip_run(run, used, scratch, out);
                       });
}

void build_sheet(hpp::span<const sheet_point> points, const strip_options& options, dynamic_geometry& out)
{
    auto& scratch = get_strip_scratch();
    for_each_strip_run(points,
                       [&](hpp::span<const sheet_point> run)
                       {
                           get_distinct_sheet_points(run, scratch.distinct_sheet);
                           make_sheet_sections(scratch.distinct_sheet, options, scratch.sheet_sections);
                           if(scratch.sheet_sections.size() >= 2)
                           {
                               add_sheet(scratch.sheet_sections, options, out);
                           }
                       });
}

} // namespace unravel
