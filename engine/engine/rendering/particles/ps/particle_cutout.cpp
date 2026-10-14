#include "particle_cutout.h"

#include <algorithm>
#include <limits>

namespace unravel
{
namespace ps_soa
{
namespace
{

/// Lengths and areas below this are degenerate (frame units).
constexpr float k_cutout_epsilon = 1.0e-6f;
constexpr uint32_t k_rgba_channels = 4;
constexpr float k_alpha_max = 255.0f;

auto cross(const math::vec2& a, const math::vec2& b) -> float
{
    return a.x * b.y - a.y * b.x;
}

/// The whole frame, as a cutout polygon (y up): its corners and the middles of its edges, counter-clockwise.
auto get_frame_polygon() -> std::vector<math::vec2>
{
    return {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.5f}};
}

/// Adds the outer corners of each texel row's opaque span of @p frame (y up, frame units) to @p points.
void add_opaque_corners(const uint8_t* rgba,
                        uint32_t width,
                        uint32_t frame,
                        uint32_t frame_width,
                        uint32_t frame_height,
                        const cutout_sheet& sheet,
                        std::vector<math::vec2>& points)
{
    const uint32_t origin_x = (frame % sheet.tiles_x) * frame_width;
    const uint32_t origin_y = (frame / sheet.tiles_x) * frame_height;
    const float threshold = math::clamp(sheet.alpha_threshold, 0.0f, 1.0f) * k_alpha_max;
    for(uint32_t y = 0; y < frame_height; ++y)
    {
        const uint8_t* row = rgba + (size_t(origin_y + y) * width + origin_x) * k_rgba_channels;
        uint32_t first = frame_width;
        uint32_t last = 0;
        for(uint32_t x = 0; x < frame_width; ++x)
        {
            if(float(row[x * k_rgba_channels + 3]) > threshold)
            {
                first = math::min(first, x);
                last = x;
            }
        }
        if(first == frame_width)
        {
            continue;
        }
        // Texel corners, flipped so y runs up the frame.
        const float top = 1.0f - float(y) / float(frame_height);
        const float bottom = 1.0f - float(y + 1) / float(frame_height);
        const float left = float(first) / float(frame_width);
        const float right = float(last + 1) / float(frame_width);
        points.insert(points.end(), {{left, top}, {left, bottom}, {right, top}, {right, bottom}});
    }
}

/// @p polygon (y up, at most cutout_vertex_count points) as a cutout: in texture coordinates (v down), its last
/// point repeated to fill the fan.
auto to_cutout(const std::vector<math::vec2>& polygon) -> cutout_polygon
{
    cutout_polygon cutout;
    for(uint32_t i = 0; i < cutout_vertex_count; ++i)
    {
        const math::vec2 point = polygon[math::min(size_t(i), polygon.size() - 1)];
        cutout[i] = math::vec2(point.x, 1.0f - point.y);
    }
    return cutout;
}

} // namespace

auto get_convex_hull(std::vector<math::vec2> points) -> std::vector<math::vec2>
{
    std::sort(points.begin(),
              points.end(),
              [](const math::vec2& a, const math::vec2& b)
              {
                  return a.x < b.x || (a.x == b.x && a.y < b.y);
              });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if(points.size() < 3)
    {
        return points;
    }
    // Monotone chain: the lower hull left to right, then the upper one right to left.
    std::vector<math::vec2> hull(points.size() * 2);
    size_t count = 0;
    const auto add = [&](const math::vec2& point, size_t floor)
    {
        while(count >= floor && cross(hull[count - 1] - hull[count - 2], point - hull[count - 2]) <= 0.0f)
        {
            --count;
        }
        hull[count++] = point;
    };
    for(const auto& point : points)
    {
        add(point, 2);
    }
    const size_t lower = count + 1;
    for(size_t i = points.size() - 1; i-- > 0;)
    {
        add(points[i], lower);
    }
    hull.resize(count - 1);
    return hull;
}

auto fit_polygon(std::vector<math::vec2> hull, uint32_t vertex_count, const math::vec2& low, const math::vec2& high)
    -> std::vector<math::vec2>
{
    while(hull.size() > vertex_count)
    {
        const size_t count = hull.size();
        size_t best = count;
        float best_area = std::numeric_limits<float>::max();
        math::vec2 best_point{0.0f};
        for(size_t i = 0; i < count; ++i)
        {
            // Folding away edge b -> c: the edges a -> b and d -> c, extended past b and c, meet at the new vertex.
            const math::vec2& a = hull[(i + count - 1) % count];
            const math::vec2& b = hull[i];
            const math::vec2& c = hull[(i + 1) % count];
            const math::vec2& d = hull[(i + 2) % count];
            const math::vec2 along_ab = b - a;
            const math::vec2 along_dc = c - d;
            const float denominator = cross(along_ab, along_dc);
            if(math::abs(denominator) < k_cutout_epsilon)
            {
                continue;
            }
            const math::vec2 to_c = c - b;
            const float t = cross(to_c, along_dc) / denominator;
            const float s = cross(to_c, along_ab) / denominator;
            if(t < 0.0f || s < 0.0f)
            {
                continue;
            }
            const math::vec2 point = b + along_ab * t;
            if(point.x < low.x - k_cutout_epsilon || point.y < low.y - k_cutout_epsilon || point.x > high.x + k_cutout_epsilon ||
               point.y > high.y + k_cutout_epsilon)
            {
                continue;
            }
            const float area = 0.5f * math::abs(cross(to_c, point - b));
            if(area < best_area)
            {
                best_area = area;
                best = i;
                best_point = math::clamp(point, low, high);
            }
        }
        if(best == count)
        {
            return {};
        }
        hull[best] = best_point;
        hull.erase(hull.begin() + std::ptrdiff_t((best + 1) % count));
    }
    return hull;
}

auto get_next_cutout_frame(uint32_t frame, const cutout_sheet& sheet) -> uint32_t
{
    if(!sheet.is_blended)
    {
        return frame;
    }
    const uint32_t tiles_x = math::max(sheet.tiles_x, 1u);
    const uint32_t total = tiles_x * math::max(sheet.tiles_y, 1u);
    const uint32_t length = sheet.is_random_row ? tiles_x : total;
    const uint32_t start = sheet.is_random_row ? (frame / tiles_x) * tiles_x : 0u;
    const uint32_t local = frame - start;
    const uint32_t next = sheet.is_single_pass ? math::min(local + 1, length - 1) : (local + 1) % length;
    return start + next;
}

auto bake_cutouts(const uint8_t* rgba, uint32_t width, uint32_t height, const cutout_sheet& sheet)
    -> std::vector<cutout_polygon>
{
    cutout_sheet frames = sheet;
    frames.tiles_x = math::max(sheet.tiles_x, 1u);
    frames.tiles_y = math::max(sheet.tiles_y, 1u);
    const uint32_t frame_width = width / frames.tiles_x;
    const uint32_t frame_height = height / frames.tiles_y;
    const uint32_t total = frames.tiles_x * frames.tiles_y;
    std::vector<cutout_polygon> cutouts(total);
    if(rgba == nullptr || frame_width == 0 || frame_height == 0)
    {
        std::fill(cutouts.begin(), cutouts.end(), to_cutout(get_frame_polygon()));
        return cutouts;
    }
    std::vector<math::vec2> points;
    for(uint32_t frame = 0; frame < total; ++frame)
    {
        points.clear();
        add_opaque_corners(rgba, width, frame, frame_width, frame_height, frames, points);
        const uint32_t next = get_next_cutout_frame(frame, frames);
        if(next != frame)
        {
            add_opaque_corners(rgba, width, next, frame_width, frame_height, frames, points);
        }
        const std::vector<math::vec2> hull = get_convex_hull(points);
        if(hull.size() < 3)
        {
            // Nothing (or a sliver) to show: the polygon collapses and the frame draws no pixels.
            const math::vec2 centre = hull.empty() ? math::vec2(0.5f) : hull.front();
            cutouts[frame] = to_cutout({centre});
            continue;
        }
        const std::vector<math::vec2> fitted =
            fit_polygon(hull, cutout_vertex_count, math::vec2(0.0f), math::vec2(1.0f));
        cutouts[frame] = to_cutout(fitted.empty() ? get_frame_polygon() : fitted);
    }
    return cutouts;
}

} // namespace ps_soa
} // namespace unravel
