#include "strip_style.h"

#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>

#include <algorithm>

namespace unravel
{

namespace
{
/// Whether a key of @p curve holds its value to the next one: its samples then take the nearer value, not a mix.
template<typename T>
auto has_held_key(const math::gradient<T>& curve) -> bool
{
    const auto& channels = curve.get_channels();
    return std::any_of(channels.begin(),
                       channels.end(),
                       [](const auto& channel)
                       {
                           return channel.has_constant_key();
                       });
}
} // namespace

strip_style::strip_style()
{
    width_over_length.add_point(1.0f, 0.0f);
    width_over_length.add_point(1.0f, 1.0f);
    color_over_length.add_point(math::color::white(), 0.0f);
    color_over_length.add_point(math::color::white(), 1.0f);
}

auto strip_style::faces_camera() const -> bool
{
    return shape != strip_shape::tube && alignment == strip_alignment::camera;
}

auto strip_style::get_curve_reach(const math::bbox& points_box) const -> float
{
    // A Catmull-Rom segment leaves the line between its two points by at most 4/27 of each of its tangents, which
    // are half the step between a point's two neighbours: no longer than the box's diagonal together.
    constexpr float CURVE_REACH = 4.0f / 27.0f;
    return subdivisions > 0 ? CURVE_REACH * math::length(points_box.max - points_box.min) : 0.0f;
}

auto strip_style::get_width_reach(const strip_samples& curves) const -> float
{
    const float half_width = std::max(width, 0.0f) * curves.widest_scale * 0.5f;
    const bool has_arcs = corner_vertices > 0 && shape != strip_shape::tube;
    return has_arcs ? half_width * STRIP_MAX_MITRE : half_width;
}

auto strip_style::make_options(const strip_samples& curves, const math::vec3& camera_position, const math::quat& rotation) const
    -> strip_options
{
    strip_options options;
    options.shape = shape;
    options.facing = alignment == strip_alignment::camera ? strip_facing::camera : strip_facing::normal;
    const math::vec3 direction = alignment == strip_alignment::local_axis ? rotation * axis : axis;
    options.normal = math::dot(direction, direction) > 0.0f ? math::normalize(direction) : math::vec3(0.0f, 1.0f, 0.0f);
    options.camera_position = camera_position;
    options.tube_sides = tube_sides;
    options.uv_mode = uv_mode;
    options.tile_length = tile_length;
    options.subdivisions = std::min(subdivisions, STRIP_MAX_ADDED_VERTICES);
    options.corner_vertices = std::min(corner_vertices, STRIP_MAX_ADDED_VERTICES);
    options.cap_vertices = std::min(cap_vertices, STRIP_MAX_ADDED_VERTICES);
    options.curves = &curves;
    options.width = width;
    return options;
}

auto strip_sampler::get(const strip_style& style) -> const strip_samples&
{
    if(has_samples_ && widths_ == style.width_over_length && colors_ == style.color_over_length)
    {
        return samples_;
    }
    samples_.widest_scale = 0.0f;
    for(size_t i = 0; i < strip_samples::COUNT; ++i)
    {
        const float along = float(i) / float(strip_samples::COUNT - 1);
        samples_.width_scales[i] = std::max(style.width_over_length.sample(along), 0.0f);
        samples_.colors[i] = style.color_over_length.sample(along).to_linear().value;
        samples_.widest_scale = std::max(samples_.widest_scale, samples_.width_scales[i]);
    }
    samples_.interpolates_width = !has_held_key(style.width_over_length);
    samples_.interpolates_color = !has_held_key(style.color_over_length);
    samples_.key = XXH3_64bits(samples_.width_scales.data(), sizeof(float) * strip_samples::COUNT);
    samples_.key = XXH3_64bits_withSeed(samples_.colors.data(), sizeof(math::vec4) * strip_samples::COUNT, samples_.key);
    samples_.key ^= (samples_.interpolates_width ? 0u : 1u) | (samples_.interpolates_color ? 0u : 2u);
    widths_ = style.width_over_length;
    colors_ = style.color_over_length;
    has_samples_ = true;
    return samples_;
}

} // namespace unravel
