/*
 * RmlUi meshes prepared for drawing straight into a 3D view with anti-aliased outlines
 */

#include "RmlUi_WorldMesh.h"

#include <RmlUi/Core/Math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace unravel
{
namespace
{
/// Mesh pixels within which two coordinates count as the same: RmlUi computes shared corners alike.
constexpr float SAME_COORDINATE_EPSILON = 0.01f;
/// Components within which two unit normals count as the same direction.
constexpr float SAME_DIRECTION_EPSILON = 1e-3f;
/// Cosine of the angle between two inward normals at or above which two edges face the same way, and
/// at or below whose negative they face each other.
constexpr float SAME_SIDE_COSINE = 0.99f;
/// Cosine of the angle between the inward normals of a strip's two long edges at or below which they
/// face each other closely enough (about 25 degrees off) for the strip's coverage.
constexpr float STRIP_FACING_COSINE = -0.9f;
/// Steps per mesh pixel of the coordinates that key lines, points and edges.
constexpr float KEY_STEPS = 64.0f;
constexpr size_t TRIANGLE_INDICES = 3;
constexpr size_t RECTANGLE_INDICES = 6;
constexpr size_t RECTANGLE_CORNERS = 4;
constexpr size_t RECTANGLE_SIDES = 4;
constexpr size_t OUTLINE_EDGES_PER_CORNER = 2;
constexpr size_t STRIP_SHARED_CORNERS = 2;
constexpr float RECTANGLE_KIND = 0.0f;
constexpr float TRIANGLE_KIND = 1.0f;
constexpr float FRINGE_KIND = 2.0f;
constexpr float STRIP_KIND = 3.0f;
/// A rectangle side value (RmlUi_WorldShapeVertex::outline_sides_or_edge1) and an edge line's fourth
/// component where the shape fades across it.
constexpr float FADING_SIDE = 1.0f;
/// A rectangle side value where the rectangle reaches under a later shape without fading.
constexpr float UNDERLAY_SIDE = 2.0f;
/// A rectangle side value where a later draw covers the rectangle and it pulls in.
constexpr float PULLED_SIDE = 3.0f;
/// RmlUi_WorldShapeVertex::kind y of a fringe that reaches under a later shape without fading.
constexpr float UNDERLAY_FRINGE = 1.0f;
/// A fringe's corners: the edge's two ends, then their outer copies in reverse.
constexpr std::array<int, RECTANGLE_INDICES> FRINGE_INDICES = {0, 1, 2, 0, 2, 3};
/// Twice the area in square mesh pixels below which a triangle maps no texture coordinates.
constexpr float MIN_TRIANGLE_DETERMINANT = 1e-6f;

/// What a side of a rectangle or an edge of a triangle is to the rest of its mesh, and to the draws after it.
enum class edge_role
{
    /// Shapes that look alike continue across it: it neither fades nor moves.
    interior,
    /// No other shape continues across it: it fades, and a draw before it may compare its outline with it.
    outline,
    /// An earlier shape of another colour continues across it: it fades over that shape.
    over,
    /// A later shape of another colour, or a later draw, continues across it: it reaches under that shape
    /// without fading, so the later shape's fade blends with it rather than with whatever lies behind.
    under,
    /// A later draw runs along it on the same side, over the shape: the shape pulls in under that draw,
    /// so its own edge never shows through the later fade.
    covered,
};

auto is_fading(edge_role role) -> bool
{
    return role == edge_role::outline || role == edge_role::over;
}

auto is_underlay(edge_role role) -> bool
{
    return role == edge_role::under;
}

auto is_covered(edge_role role) -> bool
{
    return role == edge_role::covered;
}

/// One shape of a mesh: an axis-aligned rectangle of two triangles, or a lone triangle.
struct mesh_shape
{
    bool is_rectangle = false;
    std::array<int, RECTANGLE_INDICES> indices{};
    size_t index_count = 0;
    /// The rectangle's distinct corners (source vertex indices) and extent; unused for a triangle.
    std::array<int, RECTANGLE_CORNERS> corners{};
    Rml::Vector2f min;
    Rml::Vector2f max;
    /// The roles of the rectangle's sides (left, top, right, bottom) or of the triangle's edges (from
    /// corner 0 to 1, 1 to 2 and 2 to 0).
    std::array<edge_role, RECTANGLE_SIDES> roles{};
    /// How far, in mesh pixels, each side or edge that reaches under a later shape may go.
    std::array<float, RECTANGLE_SIDES> underlay_limits{};
    /// A triangle that makes a thin quad with the next or the previous one: its long edge, the quad's
    /// diagonal, and the long edge of its partner, which faces it across the quad.
    bool is_strip = false;
    size_t strip_edge = 0;
    size_t strip_diagonal = 0;
    std::array<float, 4> strip_partner_line{};
};

/// One side of a rectangle or edge of a triangle, by the vertices at its ends.
struct shape_edge
{
    const Rml::Vertex* from = nullptr;
    const Rml::Vertex* to = nullptr;
};

/// The colours at both ends of an edge, summed: two shapes whose edges sum alike look alike across it.
using edge_color = std::array<int, 4>;

auto to_key(float coordinate) -> int64_t
{
    return static_cast<int64_t>(std::llround(coordinate * KEY_STEPS));
}

/// An axis-aligned line: x = coordinate when vertical, y = coordinate when horizontal.
struct line_key
{
    bool is_vertical = false;
    int64_t coordinate = 0;

    auto operator==(const line_key& other) const -> bool
    {
        return is_vertical == other.is_vertical && coordinate == other.coordinate;
    }
    auto operator<(const line_key& other) const -> bool
    {
        return is_vertical < other.is_vertical || (is_vertical == other.is_vertical && coordinate < other.coordinate);
    }
    /// The key RmlUi_WorldOutline sorts axis-aligned edges by: one integer per line.
    auto to_outline_key() const -> int64_t
    {
        return coordinate * 2 + (is_vertical ? 1 : 0);
    }
};

/// A position, at the precision that keys points and edges.
struct point_key
{
    int64_t x = 0;
    int64_t y = 0;

    auto operator==(const point_key& other) const -> bool
    {
        return x == other.x && y == other.y;
    }
    auto operator<(const point_key& other) const -> bool
    {
        return x < other.x || (x == other.x && y < other.y);
    }
};

struct point_key_hash
{
    auto operator()(const point_key& key) const -> size_t
    {
        return std::hash<int64_t>()(key.x * 73856093 ^ key.y * 19349663);
    }
};

/// An edge between two positions, the same whichever way round.
struct edge_key
{
    point_key first;
    point_key second;

    auto operator==(const edge_key& other) const -> bool
    {
        return first == other.first && second == other.second;
    }
    auto operator<(const edge_key& other) const -> bool
    {
        return first < other.first || (first == other.first && second < other.second);
    }
};

struct edge_key_hash
{
    auto operator()(const edge_key& key) const -> size_t
    {
        return point_key_hash()(key.first) * 31 + point_key_hash()(key.second);
    }
};

/// The stretch of a line one shape's edge lies on.
struct axis_edge
{
    line_key line;
    float from = 0.0f;
    float to = 0.0f;
    size_t shape = 0;
    edge_color color{};
};

/// A shape's edge that is not axis-aligned, by its two ends.
struct slanted_edge
{
    edge_key key;
    size_t shape = 0;
    edge_color color{};
};

/// The edges of every shape of a mesh, sorted: axis-aligned ones by the line they lie on, the others by
/// their ends. Sorted arrays rather than maps keep the build of a mesh to two allocations.
struct mesh_edges
{
    std::vector<axis_edge> axis_aligned;
    std::vector<slanted_edge> slanted;
};

/// The outline edges of the draws after a mesh's draw, by index, sorted as mesh_edges sorts its own.
struct later_outline
{
    Rml::Span<const RmlUi_WorldOutlineEdge> edges;
    std::vector<std::pair<line_key, size_t>> axis_aligned;
    std::vector<std::pair<edge_key, size_t>> slanted;
};

/// The entries of a sorted array whose key (as @p get_key reads it) equals @p key.
template<typename Entry, typename Key, typename GetKey>
auto find_key_range(const std::vector<Entry>& entries, const Key& key, GetKey get_key)
    -> std::pair<typename std::vector<Entry>::const_iterator, typename std::vector<Entry>::const_iterator>
{
    const auto first = std::lower_bound(entries.begin(),
                                        entries.end(),
                                        key,
                                        [&](const Entry& entry, const Key& value) { return get_key(entry) < value; });
    const auto last = std::upper_bound(first,
                                       entries.end(),
                                       key,
                                       [&](const Key& value, const Entry& entry) { return value < get_key(entry); });
    return {first, last};
}

/// What the draws after a mesh's draw make of one of its edges.
struct later_verdict
{
    /// They run along the whole edge on the same side, over the shape.
    bool is_covered = false;
    /// They run along the whole edge from the other side.
    bool is_bordered = false;
    /// The least depth of those running along it from the other side, in mesh pixels.
    float depth = std::numeric_limits<float>::max();
};

/// An edge of the mesh seen from one of its ends: its line and its other end.
struct corner_edge
{
    std::array<float, 4> line{};
    Rml::Vector2f other_end;
};

using point_edges_map = std::unordered_map<point_key, std::vector<corner_edge>, point_key_hash>;

/// A lone triangle's interior edge slot taken by an outline edge that meets the triangle at a corner.
struct corner_line
{
    std::array<float, 4> line{};
    /// The sine of the narrowest angle at the corner between the line and the triangle: the smaller, the
    /// further the triangle runs within a pixel of the line.
    float spread = 0.0f;
};

auto is_same(float a, float b) -> bool
{
    return std::abs(a - b) <= SAME_COORDINATE_EPSILON;
}

auto is_axis_aligned(Rml::Vector2f a, Rml::Vector2f b) -> bool
{
    return is_same(a.x, b.x) || is_same(a.y, b.y);
}

auto make_point_key(Rml::Vector2f position) -> point_key
{
    return {to_key(position.x), to_key(position.y)};
}

auto make_edge_key(Rml::Vector2f a, Rml::Vector2f b) -> edge_key
{
    const point_key key_a = make_point_key(a);
    const point_key key_b = make_point_key(b);
    return key_b < key_a ? edge_key{key_b, key_a} : edge_key{key_a, key_b};
}

/// The line an axis-aligned edge from @p a to @p b lies on.
auto make_line_key(Rml::Vector2f a, Rml::Vector2f b) -> line_key
{
    const bool is_vertical = is_same(a.x, b.x);
    return {is_vertical, to_key(is_vertical ? a.x : a.y)};
}

/// The keys RmlUi_WorldOutline sorts its edges by.
auto make_outline_line_key(Rml::Vector2f a, Rml::Vector2f b) -> int64_t
{
    return make_line_key(a, b).to_outline_key();
}

auto make_outline_edge_key(Rml::Vector2f a, Rml::Vector2f b) -> int64_t
{
    return static_cast<int64_t>(edge_key_hash()(make_edge_key(a, b)));
}

/// The stretch an axis-aligned edge covers along its line.
auto get_span(Rml::Vector2f a, Rml::Vector2f b) -> std::pair<float, float>
{
    const bool is_vertical = is_same(a.x, b.x);
    const float from = is_vertical ? a.y : a.x;
    const float to = is_vertical ? b.y : b.x;
    return {Rml::Math::Min(from, to), Rml::Math::Max(from, to)};
}

auto has_same_ends(Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d) -> bool
{
    const auto is_at = [](Rml::Vector2f p, Rml::Vector2f q) { return is_same(p.x, q.x) && is_same(p.y, q.y); };
    return (is_at(a, c) && is_at(b, d)) || (is_at(a, d) && is_at(b, c));
}

auto make_edge_color(const shape_edge& edge) -> edge_color
{
    const Rml::ColourbPremultiplied& a = edge.from->colour;
    const Rml::ColourbPremultiplied& b = edge.to->colour;
    return {a.red + b.red, a.green + b.green, a.blue + b.blue, a.alpha + b.alpha};
}

/// Collects the distinct vertex indices of two triangles, false unless there are exactly four.
auto collect_corners(const int* pair, std::array<int, RECTANGLE_CORNERS>& corners) -> bool
{
    size_t count = 0;
    for(size_t i = 0; i < RECTANGLE_INDICES; ++i)
    {
        const auto end = corners.begin() + static_cast<std::ptrdiff_t>(count);
        if(std::find(corners.begin(), end, pair[i]) != end)
        {
            continue;
        }
        if(count == RECTANGLE_CORNERS)
        {
            return false;
        }
        corners[count++] = pair[i];
    }
    return count == RECTANGLE_CORNERS;
}

/// Whether four corners are those of an axis-aligned rectangle whose texture coordinates follow the axes.
auto is_axis_aligned_rectangle(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape) -> bool
{
    if(shape.max.x - shape.min.x <= SAME_COORDINATE_EPSILON || shape.max.y - shape.min.y <= SAME_COORDINATE_EPSILON)
    {
        return false;
    }
    int corner_mask = 0;
    for(const int corner : shape.corners)
    {
        const Rml::Vector2f position = vertices[corner].position;
        const bool is_left = is_same(position.x, shape.min.x);
        const bool is_top = is_same(position.y, shape.min.y);
        if((!is_left && !is_same(position.x, shape.max.x)) || (!is_top && !is_same(position.y, shape.max.y)))
        {
            return false;
        }
        corner_mask |= 1 << ((is_left ? 0 : 1) + (is_top ? 0 : 2));
    }
    if(corner_mask != 0b1111)
    {
        return false;
    }
    // u may only change along x and v along y, so the rectangle's texture coordinates are a rectangle too.
    for(const int a : shape.corners)
    {
        for(const int b : shape.corners)
        {
            const Rml::Vertex& first = vertices[a];
            const Rml::Vertex& second = vertices[b];
            if((is_same(first.position.x, second.position.x) && first.tex_coord.x != second.tex_coord.x) ||
               (is_same(first.position.y, second.position.y) && first.tex_coord.y != second.tex_coord.y))
            {
                return false;
            }
        }
    }
    return true;
}

auto try_make_rectangle(Rml::Span<const Rml::Vertex> vertices, const int* pair, mesh_shape& shape) -> bool
{
    if(!collect_corners(pair, shape.corners))
    {
        return false;
    }
    shape.min = vertices[shape.corners[0]].position;
    shape.max = shape.min;
    for(const int corner : shape.corners)
    {
        shape.min = Rml::Math::Min(shape.min, vertices[corner].position);
        shape.max = Rml::Math::Max(shape.max, vertices[corner].position);
    }
    if(!is_axis_aligned_rectangle(vertices, shape))
    {
        return false;
    }
    shape.is_rectangle = true;
    std::copy(pair, pair + RECTANGLE_INDICES, shape.indices.begin());
    shape.index_count = RECTANGLE_INDICES;
    return true;
}

/// Walks the triangles in order: two that form a rectangle become one shape, any other is a shape alone.
auto collect_shapes(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) -> std::vector<mesh_shape>
{
    std::vector<mesh_shape> shapes;
    shapes.reserve(indices.size() / TRIANGLE_INDICES);
    for(size_t i = 0; i + TRIANGLE_INDICES <= indices.size();)
    {
        mesh_shape shape;
        if(i + RECTANGLE_INDICES <= indices.size() && try_make_rectangle(vertices, &indices[i], shape))
        {
            shapes.push_back(shape);
            i += RECTANGLE_INDICES;
            continue;
        }
        std::copy(&indices[i], &indices[i] + TRIANGLE_INDICES, shape.indices.begin());
        shape.index_count = TRIANGLE_INDICES;
        shapes.push_back(shape);
        i += TRIANGLE_INDICES;
    }
    return shapes;
}

auto get_edge_count(const mesh_shape& shape) -> size_t
{
    return shape.is_rectangle ? RECTANGLE_SIDES : TRIANGLE_INDICES;
}

/// The vertex at a corner of a rectangle.
auto find_rectangle_corner(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape, float x, float y) -> const Rml::Vertex*
{
    for(const int corner : shape.corners)
    {
        const Rml::Vertex& vertex = vertices[corner];
        if(is_same(vertex.position.x, x) && is_same(vertex.position.y, y))
        {
            return &vertex;
        }
    }
    return &vertices[shape.corners[0]];
}

/// A shape's sides or edges, in the order of mesh_shape::roles.
auto get_shape_edges(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape) -> std::array<shape_edge, RECTANGLE_SIDES>
{
    if(shape.is_rectangle)
    {
        const Rml::Vertex* top_left = find_rectangle_corner(vertices, shape, shape.min.x, shape.min.y);
        const Rml::Vertex* top_right = find_rectangle_corner(vertices, shape, shape.max.x, shape.min.y);
        const Rml::Vertex* bottom_left = find_rectangle_corner(vertices, shape, shape.min.x, shape.max.y);
        const Rml::Vertex* bottom_right = find_rectangle_corner(vertices, shape, shape.max.x, shape.max.y);
        return {shape_edge{top_left, bottom_left},
                shape_edge{top_left, top_right},
                shape_edge{top_right, bottom_right},
                shape_edge{bottom_left, bottom_right}};
    }
    std::array<shape_edge, RECTANGLE_SIDES> edges{};
    for(size_t corner = 0; corner < TRIANGLE_INDICES; ++corner)
    {
        edges[corner] = {&vertices[shape.indices[corner]], &vertices[shape.indices[(corner + 1) % TRIANGLE_INDICES]]};
    }
    return edges;
}

void add_edge(mesh_edges& edges, const shape_edge& edge, size_t shape)
{
    const Rml::Vector2f a = edge.from->position;
    const Rml::Vector2f b = edge.to->position;
    const edge_color color = make_edge_color(edge);
    if(is_axis_aligned(a, b))
    {
        const auto [from, to] = get_span(a, b);
        edges.axis_aligned.push_back({make_line_key(a, b), from, to, shape, color});
        return;
    }
    edges.slanted.push_back({make_edge_key(a, b), shape, color});
}

auto map_edges(Rml::Span<const Rml::Vertex> vertices, const std::vector<mesh_shape>& shapes) -> mesh_edges
{
    mesh_edges edges;
    edges.axis_aligned.reserve(shapes.size() * RECTANGLE_SIDES);
    for(size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
    {
        const mesh_shape& shape = shapes[shape_index];
        const auto shape_edges = get_shape_edges(vertices, shape);
        for(size_t edge = 0; edge < get_edge_count(shape); ++edge)
        {
            add_edge(edges, shape_edges[edge], shape_index);
        }
    }
    std::sort(edges.axis_aligned.begin(),
              edges.axis_aligned.end(),
              [](const axis_edge& a, const axis_edge& b) { return a.line < b.line || (a.line == b.line && a.from < b.from); });
    std::sort(edges.slanted.begin(), edges.slanted.end(), [](const slanted_edge& a, const slanted_edge& b) { return a.key < b.key; });
    return edges;
}

/// Calls @p visit with each other shape that has an edge along @p edge, and the colour of that edge.
void visit_neighbours(const mesh_edges& edges,
                      const shape_edge& edge,
                      size_t shape,
                      const std::function<void(size_t, const edge_color&)>& visit)
{
    const Rml::Vector2f a = edge.from->position;
    const Rml::Vector2f b = edge.to->position;
    if(!is_axis_aligned(a, b))
    {
        const auto [first, last] = find_key_range(edges.slanted, make_edge_key(a, b), [](const slanted_edge& entry) { return entry.key; });
        for(auto owner = first; owner != last; ++owner)
        {
            if(owner->shape != shape)
            {
                visit(owner->shape, owner->color);
            }
        }
        return;
    }
    const auto [from, to] = get_span(a, b);
    const auto [first, last] = find_key_range(edges.axis_aligned, make_line_key(a, b), [](const axis_edge& entry) { return entry.line; });
    for(auto span = first; span != last && span->from < to; ++span)
    {
        const float overlap = Rml::Math::Min(to, span->to) - Rml::Math::Max(from, span->from);
        if(span->shape != shape && overlap > SAME_COORDINATE_EPSILON)
        {
            visit(span->shape, span->color);
        }
    }
}

/// The role of @p edge of @p shape, from the shapes that continue across it and the order they draw in.
auto classify_edge(const mesh_edges& edges, const shape_edge& edge, size_t shape) -> edge_role
{
    if((edge.to->position - edge.from->position).Magnitude() <= SAME_COORDINATE_EPSILON)
    {
        return edge_role::interior;
    }
    const edge_color color = make_edge_color(edge);
    bool has_neighbour = false;
    bool has_earlier_other_color = false;
    bool has_later_other_color = false;
    visit_neighbours(edges,
                     edge,
                     shape,
                     [&](size_t other, const edge_color& other_color)
                     {
                         has_neighbour = true;
                         if(other_color != color)
                         {
                             (other < shape ? has_earlier_other_color : has_later_other_color) = true;
                         }
                     });
    if(!has_neighbour)
    {
        return edge_role::outline;
    }
    if(has_earlier_other_color)
    {
        return edge_role::over;
    }
    return has_later_other_color ? edge_role::under : edge_role::interior;
}

/// The line through @p a and @p b as "inward normal x, y, offset, fading flag", inward toward @p opposite.
auto make_edge_line(Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f opposite, bool is_fading_edge) -> std::array<float, 4>
{
    const Rml::Vector2f direction = b - a;
    const float length = direction.Magnitude();
    if(length <= SAME_COORDINATE_EPSILON)
    {
        return {};
    }
    Rml::Vector2f normal(-direction.y / length, direction.x / length);
    if(normal.DotProduct(opposite - a) < 0.0f)
    {
        normal = -normal;
    }
    return {normal.x, normal.y, -normal.DotProduct(a), is_fading_edge ? FADING_SIDE : 0.0f};
}

/// The distance from @p position to an edge line, positive inside.
auto get_line_distance(const std::array<float, 4>& line, Rml::Vector2f position) -> float
{
    return line[0] * position.x + line[1] * position.y + line[2];
}

auto get_line_normal(const std::array<float, 4>& line) -> Rml::Vector2f
{
    return {line[0], line[1]};
}

/// The lines of a rectangle's sides (left, top, right, bottom), inward.
auto get_rectangle_side_lines(const mesh_shape& rectangle) -> std::array<std::array<float, 4>, RECTANGLE_SIDES>
{
    const Rml::Vector2f low = rectangle.min;
    const Rml::Vector2f high = rectangle.max;
    return {std::array<float, 4>{1.0f, 0.0f, -low.x, FADING_SIDE},
            std::array<float, 4>{0.0f, 1.0f, -low.y, FADING_SIDE},
            std::array<float, 4>{-1.0f, 0.0f, high.x, FADING_SIDE},
            std::array<float, 4>{0.0f, -1.0f, high.y, FADING_SIDE}};
}

/// The line of a shape's side or edge, inward.
auto get_inward_line(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape, size_t edge) -> std::array<float, 4>
{
    if(shape.is_rectangle)
    {
        return get_rectangle_side_lines(shape)[edge];
    }
    return make_edge_line(vertices[shape.indices[edge]].position,
                          vertices[shape.indices[(edge + 1) % TRIANGLE_INDICES]].position,
                          vertices[shape.indices[(edge + 2) % TRIANGLE_INDICES]].position,
                          false);
}

/// A shape's corners; a triangle repeats its first.
auto get_shape_corners(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape) -> std::array<Rml::Vector2f, RECTANGLE_CORNERS>
{
    const std::array<int, RECTANGLE_CORNERS> sources =
        shape.is_rectangle ? shape.corners : std::array<int, RECTANGLE_CORNERS>{shape.indices[0], shape.indices[1], shape.indices[2], shape.indices[0]};
    std::array<Rml::Vector2f, RECTANGLE_CORNERS> corners;
    std::transform(sources.begin(), sources.end(), corners.begin(), [&](int source) { return vertices[source].position; });
    return corners;
}

/// How far, in mesh pixels, a shape reaches beyond @p line on its inner side.
auto get_depth_inside(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape, const std::array<float, 4>& line) -> float
{
    float depth = 0.0f;
    for(const Rml::Vector2f corner : get_shape_corners(vertices, shape))
    {
        depth = Rml::Math::Max(depth, get_line_distance(line, corner));
    }
    return depth;
}

/// How far, in mesh pixels, a shape may reach under the later shapes of other colours across its edge:
/// the least depth of those shapes beyond the edge, so it never shows past them.
auto get_underlay_limit(Rml::Span<const Rml::Vertex> vertices,
                        const std::vector<mesh_shape>& shapes,
                        const mesh_edges& edges,
                        size_t shape_index,
                        size_t edge_index) -> float
{
    const mesh_shape& shape = shapes[shape_index];
    const shape_edge edge = get_shape_edges(vertices, shape)[edge_index];
    const std::array<float, 4> line = get_inward_line(vertices, shape, edge_index);
    const std::array<float, 4> outward_line = {-line[0], -line[1], -line[2], line[3]};
    const edge_color color = make_edge_color(edge);
    float limit = std::numeric_limits<float>::max();
    visit_neighbours(edges,
                     edge,
                     shape_index,
                     [&](size_t other, const edge_color& other_color)
                     {
                         if(other > shape_index && other_color != color)
                         {
                             limit = Rml::Math::Min(limit, get_depth_inside(vertices, shapes[other], outward_line));
                         }
                     });
    return limit;
}

auto index_later_edges(Rml::Span<const RmlUi_WorldOutlineEdge> edges) -> later_outline
{
    later_outline later;
    later.edges = edges;
    for(size_t index = 0; index < edges.size(); ++index)
    {
        const RmlUi_WorldOutlineEdge& edge = edges[index];
        if(is_axis_aligned(edge.from, edge.to))
        {
            later.axis_aligned.emplace_back(make_line_key(edge.from, edge.to), index);
        }
        else
        {
            later.slanted.emplace_back(make_edge_key(edge.from, edge.to), index);
        }
    }
    std::sort(later.axis_aligned.begin(), later.axis_aligned.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::sort(later.slanted.begin(), later.slanted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return later;
}

/// Whether @p spans together cover the whole stretch from @p from to @p to.
auto covers_span(std::vector<std::pair<float, float>> spans, float from, float to) -> bool
{
    std::sort(spans.begin(), spans.end());
    float reached = from;
    for(const auto& [span_from, span_to] : spans)
    {
        if(span_from > reached + SAME_COORDINATE_EPSILON)
        {
            break;
        }
        reached = Rml::Math::Max(reached, span_to);
    }
    return reached >= to - SAME_COORDINATE_EPSILON;
}

/// Compares an edge of the mesh, from @p a to @p b with @p inward_normal, with the later draws' outline.
auto judge_against_later(const later_outline& later, Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f inward_normal) -> later_verdict
{
    later_verdict verdict;
    std::vector<std::pair<float, float>> same_side;
    std::vector<std::pair<float, float>> other_side;
    const auto sort_out = [&](const RmlUi_WorldOutlineEdge& edge, std::pair<float, float> span)
    {
        const float facing = edge.inward_normal.DotProduct(inward_normal);
        if(facing >= SAME_SIDE_COSINE)
        {
            same_side.push_back(span);
        }
        else if(facing <= -SAME_SIDE_COSINE)
        {
            other_side.push_back(span);
            verdict.depth = Rml::Math::Min(verdict.depth, edge.depth);
        }
    };
    const auto get_key = [](const auto& entry) { return entry.first; };
    if(is_axis_aligned(a, b))
    {
        const auto [first, last] = find_key_range(later.axis_aligned, make_line_key(a, b), get_key);
        for(auto entry = first; entry != last; ++entry)
        {
            const RmlUi_WorldOutlineEdge& edge = later.edges[entry->second];
            sort_out(edge, get_span(edge.from, edge.to));
        }
        const auto [from, to] = get_span(a, b);
        verdict.is_covered = covers_span(same_side, from, to);
        verdict.is_bordered = !verdict.is_covered && covers_span(other_side, from, to);
        return verdict;
    }
    const auto [first, last] = find_key_range(later.slanted, make_edge_key(a, b), get_key);
    for(auto entry = first; entry != last; ++entry)
    {
        const RmlUi_WorldOutlineEdge& edge = later.edges[entry->second];
        if(has_same_ends(a, b, edge.from, edge.to))
        {
            sort_out(edge, {0.0f, 1.0f});
        }
    }
    verdict.is_covered = !same_side.empty();
    verdict.is_bordered = !verdict.is_covered && !other_side.empty();
    return verdict;
}

void assign_roles(Rml::Span<const Rml::Vertex> vertices,
                  std::vector<mesh_shape>& shapes,
                  const mesh_edges& edges,
                  const later_outline& later)
{
    for(size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
    {
        mesh_shape& shape = shapes[shape_index];
        const auto shape_edges = get_shape_edges(vertices, shape);
        for(size_t edge = 0; edge < get_edge_count(shape); ++edge)
        {
            edge_role& role = shape.roles[edge];
            role = classify_edge(edges, shape_edges[edge], shape_index);
            if(role == edge_role::under)
            {
                shape.underlay_limits[edge] = get_underlay_limit(vertices, shapes, edges, shape_index, edge);
            }
            if(role == edge_role::interior || later.edges.empty())
            {
                continue;
            }
            const later_verdict verdict = judge_against_later(later,
                                                              shape_edges[edge].from->position,
                                                              shape_edges[edge].to->position,
                                                              get_line_normal(get_inward_line(vertices, shape, edge)));
            if(verdict.is_covered)
            {
                role = edge_role::covered;
            }
            else if(verdict.is_bordered)
            {
                shape.underlay_limits[edge] = role == edge_role::under ? Rml::Math::Min(shape.underlay_limits[edge], verdict.depth)
                                                                       : verdict.depth;
                role = edge_role::under;
            }
        }
    }
}

/// Whether a triangle lies on the inner side of @p line.
auto is_inside_line(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& triangle, const std::array<float, 4>& line) -> bool
{
    const auto corners = get_shape_corners(vertices, triangle);
    return std::all_of(corners.begin(),
                       corners.end(),
                       [&](Rml::Vector2f corner) { return get_line_distance(line, corner) >= -SAME_COORDINATE_EPSILON; });
}

/// The edge of a triangle between two of the shared corners, or none.
auto find_diagonal(const mesh_shape& triangle, const std::array<point_key, STRIP_SHARED_CORNERS>& shared, Rml::Span<const Rml::Vertex> vertices)
    -> size_t
{
    for(size_t edge = 0; edge < TRIANGLE_INDICES; ++edge)
    {
        const point_key from = make_point_key(vertices[triangle.indices[edge]].position);
        const point_key to = make_point_key(vertices[triangle.indices[(edge + 1) % TRIANGLE_INDICES]].position);
        if((from == shared[0] && to == shared[1]) || (from == shared[1] && to == shared[0]))
        {
            return edge;
        }
    }
    return TRIANGLE_INDICES;
}

/// The corners two triangles share, by position; false unless there are exactly two.
auto find_shared_corners(Rml::Span<const Rml::Vertex> vertices,
                         const mesh_shape& first,
                         const mesh_shape& second,
                         std::array<point_key, STRIP_SHARED_CORNERS>& shared) -> bool
{
    size_t count = 0;
    for(size_t i = 0; i < TRIANGLE_INDICES; ++i)
    {
        const point_key key = make_point_key(vertices[first.indices[i]].position);
        const bool is_shared = std::any_of(second.indices.begin(),
                                           second.indices.begin() + TRIANGLE_INDICES,
                                           [&](int index) { return make_point_key(vertices[index].position) == key; });
        if(is_shared)
        {
            if(count == STRIP_SHARED_CORNERS)
            {
                return false;
            }
            shared[count++] = key;
        }
    }
    return count == STRIP_SHARED_CORNERS;
}

/// Whether an edge of one triangle and an edge of another meet at a corner.
auto share_an_end(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& first, size_t first_edge, const mesh_shape& second, size_t second_edge)
    -> bool
{
    const std::array<point_key, 2> first_ends = {make_point_key(vertices[first.indices[first_edge]].position),
                                                 make_point_key(vertices[first.indices[(first_edge + 1) % TRIANGLE_INDICES]].position)};
    const std::array<point_key, 2> second_ends = {make_point_key(vertices[second.indices[second_edge]].position),
                                                  make_point_key(vertices[second.indices[(second_edge + 1) % TRIANGLE_INDICES]].position)};
    return std::any_of(first_ends.begin(),
                       first_ends.end(),
                       [&](const point_key& end) { return end == second_ends[0] || end == second_ends[1]; });
}

/// Makes two consecutive triangles a strip when they fade across two opposite sides of the quad they make.
auto try_make_strip(Rml::Span<const Rml::Vertex> vertices, mesh_shape& first, mesh_shape& second) -> bool
{
    std::array<point_key, STRIP_SHARED_CORNERS> shared{};
    if(!find_shared_corners(vertices, first, second, shared))
    {
        return false;
    }
    const size_t first_diagonal = find_diagonal(first, shared, vertices);
    const size_t second_diagonal = find_diagonal(second, shared, vertices);
    if(first_diagonal == TRIANGLE_INDICES || second_diagonal == TRIANGLE_INDICES)
    {
        return false;
    }
    // The quad's opposite sides are one edge of each triangle besides the diagonal that share no corner.
    for(size_t first_edge = 0; first_edge < TRIANGLE_INDICES; ++first_edge)
    {
        for(size_t second_edge = 0; second_edge < TRIANGLE_INDICES; ++second_edge)
        {
            if(first_edge == first_diagonal || second_edge == second_diagonal || !is_fading(first.roles[first_edge]) ||
               !is_fading(second.roles[second_edge]) || share_an_end(vertices, first, first_edge, second, second_edge))
            {
                continue;
            }
            std::array<float, 4> first_line = get_inward_line(vertices, first, first_edge);
            std::array<float, 4> second_line = get_inward_line(vertices, second, second_edge);
            if(get_line_normal(first_line).DotProduct(get_line_normal(second_line)) > STRIP_FACING_COSINE ||
               !is_inside_line(vertices, first, second_line) || !is_inside_line(vertices, second, first_line))
            {
                continue;
            }
            first_line[3] = FADING_SIDE;
            second_line[3] = FADING_SIDE;
            first.is_strip = true;
            first.strip_edge = first_edge;
            first.strip_diagonal = first_diagonal;
            first.strip_partner_line = second_line;
            second.is_strip = true;
            second.strip_edge = second_edge;
            second.strip_diagonal = second_diagonal;
            second.strip_partner_line = first_line;
            return true;
        }
    }
    return false;
}

/**
 * @brief Marks the pairs of consecutive lone triangles that make a thin quad between two opposite edges they
 * fade across, as RmlUi builds the pieces of a border. Each fades by both edges as the sum of the two ramps
 * less one, which is exact for parallel edges: the product of the two, or a ramp across one of them alone,
 * draws a border thinner than a pixel too heavy.
 */
void mark_strips(Rml::Span<const Rml::Vertex> vertices, std::vector<mesh_shape>& shapes)
{
    for(size_t first = 0; first + 1 < shapes.size(); ++first)
    {
        if(shapes[first].is_rectangle || shapes[first + 1].is_rectangle)
        {
            continue;
        }
        if(try_make_strip(vertices, shapes[first], shapes[first + 1]))
        {
            ++first;
        }
    }
}

/// The texture coordinates of a whole mesh: min u, min v, max u, max v.
auto get_tex_coord_bounds(Rml::Span<const Rml::Vertex> vertices) -> std::array<float, 4>
{
    Rml::Vector2f low = vertices[0].tex_coord;
    Rml::Vector2f high = low;
    for(const Rml::Vertex& vertex : vertices)
    {
        low = Rml::Math::Min(low, vertex.tex_coord);
        high = Rml::Math::Max(high, vertex.tex_coord);
    }
    return {low.x, low.y, high.x, high.y};
}

/**
 * @brief Opens the texture bounds of a rectangle across each side the mesh continues over, out to the
 * mesh's bounds: the pieces of a nine-patch sit side by side in their sprite, and a piece thinner than
 * a pixel has to blend with its neighbours there. Across a side that fades the rectangle's own bound stays.
 */
void open_shared_sides(RmlUi_WorldShapeVertex& rectangle, const std::array<float, 4>& mesh_bounds)
{
    // The texture bound each side (left, top, right, bottom) faces, by the direction u and v run.
    const bool u_runs_right = rectangle.tex_coord_gradient_or_edge2[0] >= 0.0f;
    const bool v_runs_down = rectangle.tex_coord_gradient_or_edge2[3] >= 0.0f;
    const std::array<size_t, 4> facing_bound = {u_runs_right ? 0u : 2u,
                                                v_runs_down ? 1u : 3u,
                                                u_runs_right ? 2u : 0u,
                                                v_runs_down ? 3u : 1u};
    for(size_t side = 0; side < facing_bound.size(); ++side)
    {
        if(rectangle.outline_sides_or_edge1[side] != FADING_SIDE)
        {
            rectangle.tex_coord_bounds[facing_bound[side]] = mesh_bounds[facing_bound[side]];
        }
    }
}

auto get_side_value(edge_role role) -> float
{
    if(is_fading(role))
    {
        return FADING_SIDE;
    }
    if(is_underlay(role))
    {
        return UNDERLAY_SIDE;
    }
    return is_covered(role) ? PULLED_SIDE : 0.0f;
}

auto describe_rectangle(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape) -> RmlUi_WorldShapeVertex
{
    RmlUi_WorldShapeVertex rectangle;
    rectangle.kind = {RECTANGLE_KIND, 0.0f, 0.0f, 0.0f};
    rectangle.bounds_or_edge0 = {shape.min.x, shape.min.y, shape.max.x, shape.max.y};
    rectangle.outline_sides_or_edge1 = {get_side_value(shape.roles[0]),
                                        get_side_value(shape.roles[1]),
                                        get_side_value(shape.roles[2]),
                                        get_side_value(shape.roles[3])};
    rectangle.outline_normals_or_underlay_limits = shape.underlay_limits;
    // The texture coordinates at the left / top and right / bottom sides, read off the corners.
    Rml::Vector2f tex_coord_min_side;
    Rml::Vector2f tex_coord_max_side;
    Rml::Vector2f tex_coord_low = vertices[shape.corners[0]].tex_coord;
    Rml::Vector2f tex_coord_high = tex_coord_low;
    for(const int corner : shape.corners)
    {
        const Rml::Vertex& vertex = vertices[corner];
        (is_same(vertex.position.x, shape.min.x) ? tex_coord_min_side.x : tex_coord_max_side.x) = vertex.tex_coord.x;
        (is_same(vertex.position.y, shape.min.y) ? tex_coord_min_side.y : tex_coord_max_side.y) = vertex.tex_coord.y;
        tex_coord_low = Rml::Math::Min(tex_coord_low, vertex.tex_coord);
        tex_coord_high = Rml::Math::Max(tex_coord_high, vertex.tex_coord);
    }
    const Rml::Vector2f size = shape.max - shape.min;
    rectangle.tex_coord_gradient_or_edge2 = {(tex_coord_max_side.x - tex_coord_min_side.x) / size.x,
                                             0.0f,
                                             0.0f,
                                             (tex_coord_max_side.y - tex_coord_min_side.y) / size.y};
    rectangle.tex_coord_bounds = {tex_coord_low.x, tex_coord_low.y, tex_coord_high.x, tex_coord_high.y};
    return rectangle;
}

/// A lone triangle's three edges (from corner 0 to 1, 1 to 2 and 2 to 0), each marked when it fades.
auto describe_triangle(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape, const std::array<float, 4>& mesh_bounds)
    -> RmlUi_WorldShapeVertex
{
    std::array<Rml::Vector2f, TRIANGLE_INDICES> corners;
    for(size_t corner = 0; corner < TRIANGLE_INDICES; ++corner)
    {
        corners[corner] = vertices[shape.indices[corner]].position;
    }
    const auto edge = [&](size_t from)
    {
        const Rml::Vector2f a = corners[from];
        const Rml::Vector2f b = corners[(from + 1) % TRIANGLE_INDICES];
        const Rml::Vector2f opposite = corners[(from + 2) % TRIANGLE_INDICES];
        return make_edge_line(a, b, opposite, is_fading(shape.roles[from]));
    };
    RmlUi_WorldShapeVertex triangle;
    triangle.kind = {TRIANGLE_KIND, 0.0f, 0.0f, 0.0f};
    triangle.bounds_or_edge0 = edge(0);
    triangle.outline_sides_or_edge1 = edge(1);
    triangle.tex_coord_gradient_or_edge2 = edge(2);
    triangle.tex_coord_bounds = mesh_bounds;
    return triangle;
}

/// A lone triangle's edges as describe_triangle stores them: from corner 0 to 1, 1 to 2 and 2 to 0.
auto get_triangle_edges(const RmlUi_WorldShapeVertex& triangle) -> std::array<const std::array<float, 4>*, TRIANGLE_INDICES>
{
    return {&triangle.bounds_or_edge0, &triangle.outline_sides_or_edge1, &triangle.tex_coord_gradient_or_edge2};
}

/**
 * @brief Turns a strip triangle's edges into its own long edge, its partner's long edge and its edge across
 * the strip: the quad's diagonal fades nowhere, and the vertex shader reads the first two as the strip.
 */
void make_strip(RmlUi_WorldShapeVertex& triangle, const mesh_shape& shape)
{
    const std::array<std::array<float, 4>, TRIANGLE_INDICES> edges = {triangle.bounds_or_edge0,
                                                                      triangle.outline_sides_or_edge1,
                                                                      triangle.tex_coord_gradient_or_edge2};
    const size_t across = TRIANGLE_INDICES - shape.strip_edge - shape.strip_diagonal;
    triangle.kind[0] = STRIP_KIND;
    triangle.bounds_or_edge0 = edges[shape.strip_edge];
    triangle.outline_sides_or_edge1 = shape.strip_partner_line;
    triangle.tex_coord_gradient_or_edge2 = edges[across];
}

auto is_same_line(const std::array<float, 4>& a, const std::array<float, 4>& b) -> bool
{
    return std::abs(a[0] - b[0]) <= SAME_DIRECTION_EPSILON && std::abs(a[1] - b[1]) <= SAME_DIRECTION_EPSILON &&
           is_same(a[2], b[2]);
}

/// Which edges a corner map holds: those a shape fades across, reaches under a later shape by, or pulls in by.
using role_filter = bool (*)(edge_role);

/// Adds the sides of a rectangle that pass @p wanted to the corners they end at, where a lone triangle's
/// edges of the same kind already meet them.
void add_rectangle_corner_edges(point_edges_map& corner_edges, const mesh_shape& rectangle, role_filter wanted)
{
    struct rectangle_side
    {
        Rml::Vector2f first_end;
        Rml::Vector2f second_end;
    };
    const Rml::Vector2f low = rectangle.min;
    const Rml::Vector2f high = rectangle.max;
    const std::array<std::array<float, 4>, RECTANGLE_SIDES> lines = get_rectangle_side_lines(rectangle);
    const std::array<rectangle_side, RECTANGLE_SIDES> sides = {rectangle_side{low, {low.x, high.y}},
                                                               rectangle_side{low, {high.x, low.y}},
                                                               rectangle_side{{high.x, low.y}, high},
                                                               rectangle_side{{low.x, high.y}, high}};
    const auto add_end = [&](const std::array<float, 4>& line, Rml::Vector2f end, Rml::Vector2f other_end)
    {
        const auto found = corner_edges.find(make_point_key(end));
        if(found != corner_edges.end())
        {
            found->second.push_back({line, other_end});
        }
    };
    for(size_t side = 0; side < sides.size(); ++side)
    {
        if(wanted(rectangle.roles[side]))
        {
            add_end(lines[side], sides[side].first_end, sides[side].second_end);
            add_end(lines[side], sides[side].second_end, sides[side].first_end);
        }
    }
}

/// The edges of lone triangles that pass @p wanted by the corners they run between, plus the rectangle
/// sides of the same kind that end at those corners.
auto map_corner_edges(Rml::Span<const Rml::Vertex> vertices,
                      const std::vector<mesh_shape>& shapes,
                      const std::vector<RmlUi_WorldShapeVertex>& shape_vertices,
                      role_filter wanted) -> point_edges_map
{
    point_edges_map corner_edges;
    for(size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
    {
        const mesh_shape& shape = shapes[shape_index];
        if(shape.is_rectangle)
        {
            continue;
        }
        const auto edges = get_triangle_edges(shape_vertices[shape_index]);
        for(size_t from = 0; from < TRIANGLE_INDICES; ++from)
        {
            if(!wanted(shape.roles[from]))
            {
                continue;
            }
            const Rml::Vector2f a = vertices[shape.indices[from]].position;
            const Rml::Vector2f b = vertices[shape.indices[(from + 1) % TRIANGLE_INDICES]].position;
            corner_edges[make_point_key(a)].push_back({*edges[from], b});
            corner_edges[make_point_key(b)].push_back({*edges[from], a});
        }
    }
    for(size_t shape_index = 0; shape_index < shapes.size() && !corner_edges.empty(); ++shape_index)
    {
        if(shapes[shape_index].is_rectangle)
        {
            add_rectangle_corner_edges(corner_edges, shapes[shape_index], wanted);
        }
    }
    return corner_edges;
}

/// The two edges that meet at @p position, or none where there are not exactly two.
auto find_corner_edges(const point_edges_map& corner_edges, Rml::Vector2f position) -> const std::vector<corner_edge>*
{
    const auto found = corner_edges.find(make_point_key(position));
    return found == corner_edges.end() || found->second.size() != OUTLINE_EDGES_PER_CORNER ? nullptr : &found->second;
}

/// The normals a fringe's outer corner moves by: the two edges of its kind that meet at it, or @p fallback
/// where there are not exactly two.
auto get_corner_normals(const point_edges_map& corner_edges, Rml::Vector2f position, const std::array<float, 4>& fallback)
    -> std::array<float, 4>
{
    const std::vector<corner_edge>* edges = find_corner_edges(corner_edges, position);
    if(edges == nullptr)
    {
        return fallback;
    }
    return {(*edges)[0].line[0], (*edges)[0].line[1], (*edges)[1].line[0], (*edges)[1].line[1]};
}

/// The normals a triangle corner pulls in by: the covered edges that meet at it (one counts twice), or none.
auto get_inset_normals(const point_edges_map& covered_edges, Rml::Vector2f position) -> std::array<float, 4>
{
    const auto found = covered_edges.find(make_point_key(position));
    if(found == covered_edges.end() || found->second.empty() || found->second.size() > OUTLINE_EDGES_PER_CORNER)
    {
        return {};
    }
    const std::array<float, 4>& first = found->second.front().line;
    const std::array<float, 4>& last = found->second.back().line;
    return {first[0], first[1], last[0], last[1]};
}

/// Whether the edges turn inward at a corner, so the shape there lies inside both of them.
auto is_convex_corner(const std::vector<corner_edge>& edges) -> bool
{
    return get_line_distance(edges[0].line, edges[1].other_end) >= -SAME_COORDINATE_EPSILON;
}

/// The sine of the narrowest angle at @p corner between @p line and the triangle, or a negative value
/// when every other corner lies on the line.
auto get_line_spread(const std::array<float, 4>& line, Rml::Vector2f corner, const std::array<Rml::Vector2f, TRIANGLE_INDICES>& corners)
    -> float
{
    float spread = -1.0f;
    for(const Rml::Vector2f other : corners)
    {
        const float length = (other - corner).Magnitude();
        const float distance = std::abs(get_line_distance(line, other));
        if(length > SAME_COORDINATE_EPSILON && distance > SAME_COORDINATE_EPSILON)
        {
            const float sine = distance / length;
            spread = spread < 0.0f ? sine : Rml::Math::Min(spread, sine);
        }
    }
    return spread;
}

/// The fading edges of the mesh that meet a lone triangle at its convex corners, that it does not own and
/// that it lies inside of, those it runs closest along first.
auto collect_corner_lines(Rml::Span<const Rml::Vertex> vertices,
                          const mesh_shape& shape,
                          const RmlUi_WorldShapeVertex& triangle,
                          const point_edges_map& fading_edges) -> std::vector<corner_line>
{
    std::array<Rml::Vector2f, TRIANGLE_INDICES> corners;
    for(size_t corner = 0; corner < TRIANGLE_INDICES; ++corner)
    {
        corners[corner] = vertices[shape.indices[corner]].position;
    }
    const auto edges = get_triangle_edges(triangle);
    const auto is_known = [&](const std::array<float, 4>& line, const std::vector<corner_line>& lines)
    {
        return std::any_of(edges.begin(), edges.end(), [&](const auto* edge) { return (*edge)[3] != 0.0f && is_same_line(*edge, line); }) ||
               std::any_of(lines.begin(), lines.end(), [&](const corner_line& known) { return is_same_line(known.line, line); });
    };
    std::vector<corner_line> lines;
    for(const Rml::Vector2f corner : corners)
    {
        const std::vector<corner_edge>* meeting = find_corner_edges(fading_edges, corner);
        if(meeting == nullptr || !is_convex_corner(*meeting))
        {
            continue;
        }
        for(const corner_edge& edge : *meeting)
        {
            const float spread = get_line_spread(edge.line, corner, corners);
            if(spread >= 0.0f && is_inside_line(vertices, shape, edge.line) && !is_known(edge.line, lines))
            {
                lines.push_back({edge.line, spread});
            }
        }
    }
    std::sort(lines.begin(), lines.end(), [](const corner_line& a, const corner_line& b) { return a.spread < b.spread; });
    return lines;
}

/**
 * @brief Gives a lone triangle's interior edge slots to the fading edges of the mesh that meet it at its
 * convex corners. A triangle can run within a pixel of such an edge without owning it (the narrow
 * triangles of a fan next to its first corner): it then fades along the edge as the owning triangle does.
 * At a convex corner the whole triangle lies inside the edge, so the fade only reaches the pixels by it.
 */
void add_corner_lines(RmlUi_WorldShapeVertex& triangle,
                      Rml::Span<const Rml::Vertex> vertices,
                      const mesh_shape& shape,
                      const point_edges_map& fading_edges)
{
    const std::vector<corner_line> lines = collect_corner_lines(vertices, shape, triangle, fading_edges);
    std::array<std::array<float, 4>*, TRIANGLE_INDICES> slots = {&triangle.bounds_or_edge0,
                                                                 &triangle.outline_sides_or_edge1,
                                                                 &triangle.tex_coord_gradient_or_edge2};
    auto next_line = lines.begin();
    for(std::array<float, 4>* slot : slots)
    {
        if((*slot)[3] == 0.0f && next_line != lines.end())
        {
            *slot = (next_line++)->line;
        }
    }
}

/// A lone triangle's texture coordinates per mesh pixel (d(u, v)/dx, d(u, v)/dy), zero when it has no area.
auto get_tex_coord_gradient(Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape) -> std::array<float, 4>
{
    const Rml::Vertex& origin = vertices[shape.indices[0]];
    const Rml::Vector2f edge_a = vertices[shape.indices[1]].position - origin.position;
    const Rml::Vector2f edge_b = vertices[shape.indices[2]].position - origin.position;
    const float determinant = edge_a.x * edge_b.y - edge_a.y * edge_b.x;
    if(std::abs(determinant) <= MIN_TRIANGLE_DETERMINANT)
    {
        return {};
    }
    const Rml::Vector2f tex_coord_a = vertices[shape.indices[1]].tex_coord - origin.tex_coord;
    const Rml::Vector2f tex_coord_b = vertices[shape.indices[2]].tex_coord - origin.tex_coord;
    const Rml::Vector2f along_x = (tex_coord_a * edge_b.y - tex_coord_b * edge_a.y) / determinant;
    const Rml::Vector2f along_y = (tex_coord_b * edge_a.x - tex_coord_a * edge_b.x) / determinant;
    return {along_x.x, along_x.y, along_y.x, along_y.y};
}

/**
 * @brief Appends the fringe across a lone triangle's edge from @p a to @p b: the edge, and a copy of it
 * whose corners the vertex shader moves out by the normals of the edges of the same kind there. The
 * triangle keeps its corners, so the mesh still tiles exactly, and only the fringe reaches across.
 */
void append_fringe(RmlUi_WorldMesh& mesh,
                   const Rml::Vertex& a,
                   const Rml::Vertex& b,
                   const RmlUi_WorldShapeVertex& fringe,
                   const point_edges_map& corner_edges)
{
    const std::array<float, 4>& edge = fringe.bounds_or_edge0;
    const std::array<float, 4> edge_normals = {edge[0], edge[1], edge[0], edge[1]};
    const std::array<const Rml::Vertex*, 4> corners = {&a, &b, &b, &a};
    const std::array<std::array<float, 4>, 4> corner_normals = {std::array<float, 4>{},
                                                                std::array<float, 4>{},
                                                                get_corner_normals(corner_edges, b.position, edge_normals),
                                                                get_corner_normals(corner_edges, a.position, edge_normals)};
    const int first = static_cast<int>(mesh.vertices.size());
    for(size_t corner = 0; corner < corners.size(); ++corner)
    {
        mesh.vertices.push_back(*corners[corner]);
        mesh.shapes.push_back(fringe);
        mesh.shapes.back().outline_normals_or_underlay_limits = corner_normals[corner];
    }
    for(const int index : FRINGE_INDICES)
    {
        mesh.indices.push_back(first + index);
    }
}

/// Appends a fringe across each edge of a lone triangle that fades or reaches under a later shape. The
/// fringe across a strip's long edge fades by the strip's other long edge too.
void append_fringes(RmlUi_WorldMesh& mesh,
                    Rml::Span<const Rml::Vertex> vertices,
                    const mesh_shape& shape,
                    const RmlUi_WorldShapeVertex& triangle,
                    const point_edges_map& fading_edges,
                    const point_edges_map& underlay_edges)
{
    RmlUi_WorldShapeVertex fringe;
    fringe.tex_coord_gradient_or_edge2 = get_tex_coord_gradient(vertices, shape);
    fringe.tex_coord_bounds = triangle.tex_coord_bounds;
    const auto edges = get_triangle_edges(triangle);
    for(size_t from = 0; from < TRIANGLE_INDICES; ++from)
    {
        const edge_role role = shape.roles[from];
        if(!is_fading(role) && !is_underlay(role))
        {
            continue;
        }
        fringe.kind = {FRINGE_KIND, is_underlay(role) ? UNDERLAY_FRINGE : 0.0f, shape.underlay_limits[from], 0.0f};
        fringe.bounds_or_edge0 = *edges[from];
        const bool is_strip_edge = shape.is_strip && from == shape.strip_edge;
        fringe.outline_sides_or_edge1 = is_strip_edge ? shape.strip_partner_line : std::array<float, 4>{};
        append_fringe(mesh,
                      vertices[shape.indices[from]],
                      vertices[shape.indices[(from + 1) % TRIANGLE_INDICES]],
                      fringe,
                      is_underlay(role) ? underlay_edges : fading_edges);
    }
}

/// Appends a shape with corners of its own: its vertices copied once each, its indices pointing at the copies.
void append_shape(RmlUi_WorldMesh& mesh,
                  Rml::Span<const Rml::Vertex> vertices,
                  const mesh_shape& shape,
                  const RmlUi_WorldShapeVertex& shape_vertex)
{
    const int first_copy = static_cast<int>(mesh.vertices.size());
    std::array<int, RECTANGLE_CORNERS> copied_from{};
    size_t copied_count = 0;
    for(size_t i = 0; i < shape.index_count; ++i)
    {
        const int source = shape.indices[i];
        const auto copied_end = copied_from.begin() + static_cast<std::ptrdiff_t>(copied_count);
        const auto copy = std::find(copied_from.begin(), copied_end, source);
        if(copy == copied_end)
        {
            copied_from[copied_count++] = source;
            mesh.vertices.push_back(vertices[source]);
            mesh.shapes.push_back(shape_vertex);
        }
        mesh.indices.push_back(first_copy + static_cast<int>(copy - copied_from.begin()));
    }
}

/// Gives the triangle corners appended from @p first_vertex on the normals of the covered edges they pull in by.
void pull_in_corners(RmlUi_WorldMesh& mesh, size_t first_vertex, const point_edges_map& covered_edges)
{
    if(covered_edges.empty())
    {
        return;
    }
    for(size_t vertex = first_vertex; vertex < mesh.vertices.size(); ++vertex)
    {
        mesh.shapes[vertex].outline_normals_or_underlay_limits = get_inset_normals(covered_edges, mesh.vertices[vertex].position);
    }
}

/// Records the outline edges of a shape for a draw before it to compare its own outline with.
void record_outline(RmlUi_WorldMesh& mesh, Rml::Span<const Rml::Vertex> vertices, const mesh_shape& shape)
{
    const auto edges = get_shape_edges(vertices, shape);
    for(size_t edge = 0; edge < get_edge_count(shape); ++edge)
    {
        if(shape.roles[edge] != edge_role::outline)
        {
            continue;
        }
        const Rml::Vector2f a = edges[edge].from->position;
        const Rml::Vector2f b = edges[edge].to->position;
        const std::array<float, 4> line = get_inward_line(vertices, shape, edge);
        const auto index = static_cast<uint32_t>(mesh.outline.edges.size());
        mesh.outline.edges.push_back({a, b, get_line_normal(line), get_depth_inside(vertices, shape, line)});
        if(is_axis_aligned(a, b))
        {
            mesh.outline.axis_aligned_edges.push_back({make_outline_line_key(a, b), index});
        }
        else
        {
            mesh.outline.slanted_edges.push_back({make_outline_edge_key(a, b), index});
        }
    }
}
/// Sorts an outline's keys and sets the box around its edges.
void finish_outline(RmlUi_WorldOutline& outline)
{
    const auto by_key = [](const RmlUi_WorldOutlineKey& a, const RmlUi_WorldOutlineKey& b) { return a.key < b.key; };
    std::sort(outline.axis_aligned_edges.begin(), outline.axis_aligned_edges.end(), by_key);
    std::sort(outline.slanted_edges.begin(), outline.slanted_edges.end(), by_key);
    if(outline.edges.empty())
    {
        return;
    }
    outline.min = outline.edges.front().from;
    outline.max = outline.min;
    for(const RmlUi_WorldOutlineEdge& edge : outline.edges)
    {
        outline.min = Rml::Math::Min(outline.min, Rml::Math::Min(edge.from, edge.to));
        outline.max = Rml::Math::Max(outline.max, Rml::Math::Max(edge.from, edge.to));
    }
}
} // namespace

auto operator==(const RmlUi_WorldOutlineEdge& a, const RmlUi_WorldOutlineEdge& b) -> bool
{
    return a.from == b.from && a.to == b.to && a.inward_normal == b.inward_normal && a.depth == b.depth;
}

auto build_world_mesh(Rml::Span<const Rml::Vertex> vertices,
                      Rml::Span<const int> indices,
                      Rml::Span<const RmlUi_WorldOutlineEdge> later_edges) -> RmlUi_WorldMesh
{
    RmlUi_WorldMesh mesh;
    if(vertices.empty() || indices.empty())
    {
        return mesh;
    }
    std::vector<mesh_shape> shapes = collect_shapes(vertices, indices);
    assign_roles(vertices, shapes, map_edges(vertices, shapes), index_later_edges(later_edges));
    mark_strips(vertices, shapes);
    const std::array<float, 4> mesh_bounds = get_tex_coord_bounds(vertices);

    std::vector<RmlUi_WorldShapeVertex> shape_vertices;
    shape_vertices.reserve(shapes.size());
    for(const mesh_shape& shape : shapes)
    {
        if(!shape.is_rectangle)
        {
            shape_vertices.push_back(describe_triangle(vertices, shape, mesh_bounds));
            continue;
        }
        RmlUi_WorldShapeVertex rectangle = describe_rectangle(vertices, shape);
        open_shared_sides(rectangle, mesh_bounds);
        shape_vertices.push_back(rectangle);
    }
    const point_edges_map fading_edges = map_corner_edges(vertices, shapes, shape_vertices, is_fading);
    const point_edges_map underlay_edges = map_corner_edges(vertices, shapes, shape_vertices, is_underlay);
    const point_edges_map covered_edges = map_corner_edges(vertices, shapes, shape_vertices, is_covered);

    mesh.vertices.reserve(indices.size());
    mesh.shapes.reserve(indices.size());
    mesh.indices.reserve(indices.size());
    for(size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
    {
        const mesh_shape& shape = shapes[shape_index];
        record_outline(mesh, vertices, shape);
        if(shape.is_rectangle)
        {
            append_shape(mesh, vertices, shape, shape_vertices[shape_index]);
            continue;
        }
        RmlUi_WorldShapeVertex triangle = shape_vertices[shape_index];
        if(shape.is_strip)
        {
            make_strip(triangle, shape);
        }
        add_corner_lines(triangle, vertices, shape, fading_edges);
        const size_t first_vertex = mesh.vertices.size();
        append_shape(mesh, vertices, shape, triangle);
        pull_in_corners(mesh, first_vertex, covered_edges);
        append_fringes(mesh, vertices, shape, shape_vertices[shape_index], fading_edges, underlay_edges);
    }
    finish_outline(mesh.outline);
    return mesh;
}

void append_touching_edges(const RmlUi_WorldOutline& outline,
                           const RmlUi_WorldOutline& later,
                           Rml::Vector2f offset,
                           std::vector<RmlUi_WorldOutlineEdge>& touching)
{
    const Rml::Vector2f later_min = later.min + offset;
    const Rml::Vector2f later_max = later.max + offset;
    if(later.edges.empty() || later_min.x > outline.max.x + SAME_COORDINATE_EPSILON ||
       later_min.y > outline.max.y + SAME_COORDINATE_EPSILON || later_max.x < outline.min.x - SAME_COORDINATE_EPSILON ||
       later_max.y < outline.min.y - SAME_COORDINATE_EPSILON)
    {
        return;
    }
    // Searches the longer of the two key lists for the entries of the shorter, so a box laid over a block
    // of text costs a few searches rather than a pass over every glyph's edges.
    std::vector<uint32_t> matched;
    const auto match = [&](const std::vector<RmlUi_WorldOutlineKey>& own_keys,
                           const std::vector<RmlUi_WorldOutlineKey>& later_keys,
                           auto make_key,
                           auto touches)
    {
        const bool searches_own = later_keys.size() <= own_keys.size();
        const std::vector<RmlUi_WorldOutlineKey>& walked = searches_own ? later_keys : own_keys;
        const std::vector<RmlUi_WorldOutlineKey>& searched = searches_own ? own_keys : later_keys;
        const Rml::Vector2f shift = searches_own ? offset : -offset;
        for(const RmlUi_WorldOutlineKey& walked_key : walked)
        {
            const RmlUi_WorldOutlineEdge& walked_edge = (searches_own ? later : outline).edges[walked_key.edge];
            const auto [first, last] =
                find_key_range(searched, make_key(walked_edge.from + shift, walked_edge.to + shift), [](const RmlUi_WorldOutlineKey& entry) { return entry.key; });
            for(auto found = first; found != last; ++found)
            {
                const RmlUi_WorldOutlineEdge& own = searches_own ? outline.edges[found->edge] : outline.edges[walked_key.edge];
                const RmlUi_WorldOutlineEdge& candidate = searches_own ? later.edges[walked_key.edge] : later.edges[found->edge];
                if(touches(own, candidate.from + offset, candidate.to + offset))
                {
                    matched.push_back(searches_own ? walked_key.edge : found->edge);
                }
            }
        }
    };
    match(outline.axis_aligned_edges,
          later.axis_aligned_edges,
          make_outline_line_key,
          [](const RmlUi_WorldOutlineEdge& own, Rml::Vector2f from, Rml::Vector2f to)
          {
              const auto [own_from, own_to] = get_span(own.from, own.to);
              const auto [candidate_from, candidate_to] = get_span(from, to);
              return make_line_key(own.from, own.to) == make_line_key(from, to) &&
                     Rml::Math::Min(own_to, candidate_to) - Rml::Math::Max(own_from, candidate_from) > SAME_COORDINATE_EPSILON;
          });
    match(outline.slanted_edges,
          later.slanted_edges,
          make_outline_edge_key,
          [](const RmlUi_WorldOutlineEdge& own, Rml::Vector2f from, Rml::Vector2f to) { return has_same_ends(own.from, own.to, from, to); });
    std::sort(matched.begin(), matched.end());
    matched.erase(std::unique(matched.begin(), matched.end()), matched.end());
    for(const uint32_t index : matched)
    {
        RmlUi_WorldOutlineEdge edge = later.edges[index];
        edge.from += offset;
        edge.to += offset;
        touching.push_back(edge);
    }
}

} // namespace unravel
