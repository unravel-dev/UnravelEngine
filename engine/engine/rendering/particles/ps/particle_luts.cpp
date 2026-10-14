#include "particle_luts.h"
#include "particle_scale_curve.h"

#include <cmath>
#include <cstring>

namespace unravel
{
namespace ps_soa
{
namespace
{

constexpr uint64_t k_fnv_offset = 14695981039346656037ull;
constexpr uint64_t k_fnv_prime = 1099511628211ull;
/// Step of the central difference that gives the easing's rate.
constexpr float k_ease_rate_step = 1.0e-3f;
/// Integration steps per curve sample of the speed and spin integrals.
constexpr uint32_t k_speed_integral_substeps = 8;

class key_hasher
{
public:
    void add_bytes(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; ++i)
        {
            hash_ = (hash_ ^ bytes[i]) * k_fnv_prime;
        }
    }

    template<typename T>
    void add(const T& value)
    {
        add_bytes(&value, sizeof(T));
    }

    template<typename T>
    void add_curve(const math::gradient<T>& curve)
    {
        add(curve.is_linked());
        const auto& channels = curve.get_channels();
        add(channels.size());
        for(const auto& channel : channels)
        {
            const auto& keys = channel.get_keys();
            add(keys.size());
            for(const auto& key : keys)
            {
                add(key.position);
                add(key.value);
                add(key.in_tangent);
                add(key.out_tangent);
                add(key.interpolation);
                add(key.tangent_mode);
            }
        }
    }

    auto value() const -> uint64_t
    {
        return hash_;
    }

private:
    uint64_t hash_ = k_fnv_offset;
};

auto mean_lifetime(const emitter_desc& desc) -> float
{
    return math::max((desc.motion.lifetime.min + desc.motion.lifetime.max) * 0.5f, 1.0e-4f);
}

auto ease(bx::EaseFn function, float t) -> float
{
    return function ? function(t) : t;
}

void bake_color_row(const math::gradient<math::color>& gradient, lut_set& luts, lut_row row)
{
    const uint32_t base = uint32_t(row) * lut_size;
    for(uint32_t i = 0; i < lut_size; ++i)
    {
        const float t = float(i) / float(lut_size - 1);
        // Gradient keys are picker (sRGB) colors; particles light the linear HDR scene.
        luts.data[base + i] = gradient.sample(t).to_linear().value;
    }
}

void bake_life_row(const emitter_desc& desc, emitter_feature features, lut_set& luts, lut_ranges& ranges)
{
    const bool has_ease = has_feature(features, emitter_feature::non_linear_ease);
    const bx::EaseFn function = has_ease ? bx::getEaseFunc(desc.motion.motion_easing) : nullptr;
    const uint32_t base = uint32_t(lut_row_life) * lut_size;
    ranges.ease_min = 0.0f;
    ranges.ease_max = 1.0f;
    ranges.ease_rate_max = has_ease ? 0.0f : 1.0f;
    for(uint32_t i = 0; i < lut_size; ++i)
    {
        const float t = float(i) / float(lut_size - 1);
        const float eased = ease(function, t);
        const float before = math::max(t - k_ease_rate_step, 0.0f);
        const float after = math::min(t + k_ease_rate_step, 1.0f);
        const float rate = (ease(function, after) - ease(function, before)) / (after - before);
        const frange_t size = desc.appearance.size_over_life.sample(t);
        luts.data[base + i] = math::vec4(eased, rate, size.min, size.max);
        ranges.ease_min = math::min(ranges.ease_min, eased);
        ranges.ease_max = math::max(ranges.ease_max, eased);
        ranges.ease_rate_max = math::max(ranges.ease_rate_max, math::abs(rate));
    }
}

void bake_speed_row(const emitter_desc& desc, emitter_feature features, lut_set& luts, lut_ranges& ranges)
{
    const uint32_t base = uint32_t(lut_row_speed) * lut_size;
    ranges.speed_integral_max = 1.0f;
    ranges.speed_factor_max = 1.0f;
    if(!has_feature(features, emitter_feature::speed_curve))
    {
        for(uint32_t i = 0; i < lut_size; ++i)
        {
            const float u = float(i) / float(lut_size - 1);
            luts.data[base + i] = math::vec4(u, 1.0f, 0.0f, 0.0f);
        }
        return;
    }
    // Displacement = launch velocity x lifetime x integral of (speed factor x drag decay) over the
    // eased life; the drag decay is taken at the mean lifetime (exact when every particle lives as
    // long).
    const float drag_per_life = desc.motion.drag * mean_lifetime(desc);
    const auto factor = [&](float u)
    {
        return desc.motion.speed_over_life.sample(u) * std::exp(-drag_per_life * u);
    };
    float integral = 0.0f;
    float previous_value = factor(0.0f);
    ranges.speed_integral_max = 0.0f;
    ranges.speed_factor_max = math::abs(previous_value);
    luts.data[base] = math::vec4(0.0f, previous_value, 0.0f, 0.0f);
    const float sample_step = 1.0f / float(lut_size - 1);
    const float substep = sample_step / float(k_speed_integral_substeps);
    for(uint32_t i = 1; i < lut_size; ++i)
    {
        const float start = float(i - 1) * sample_step;
        for(uint32_t s = 1; s <= k_speed_integral_substeps; ++s)
        {
            const float value = factor(start + float(s) * substep);
            integral += (previous_value + value) * 0.5f * substep;
            previous_value = value;
            ranges.speed_factor_max = math::max(ranges.speed_factor_max, math::abs(value));
        }
        luts.data[base + i] = math::vec4(integral, previous_value, 0.0f, 0.0f);
        ranges.speed_integral_max = math::max(ranges.speed_integral_max, math::abs(integral));
    }
}

} // namespace

auto compute_lut_key(const emitter_desc& desc, emitter_feature features) -> uint64_t
{
    key_hasher hasher;
    const uint32_t lut_features = static_cast<uint32_t>(
        features & (emitter_feature::non_linear_ease | emitter_feature::speed_curve | emitter_feature::color_by_speed |
                    emitter_feature::erosion | emitter_feature::size_axis_curve | emitter_feature::spin_curve |
                    emitter_feature::intensity_curve | emitter_feature::two_gradients |
                    emitter_feature::texsheet_frame_curve | emitter_feature::turbulence));
    hasher.add(lut_features);
    hasher.add_curve(desc.appearance.color_gradient);
    hasher.add_curve(desc.appearance.size_over_life);
    if(has_feature(features, emitter_feature::non_linear_ease))
    {
        hasher.add(desc.motion.motion_easing);
    }
    if(has_feature(features, emitter_feature::color_by_speed))
    {
        hasher.add_curve(desc.appearance.color_by_speed_gradient);
    }
    if(has_feature(features, emitter_feature::speed_curve))
    {
        hasher.add_curve(desc.motion.speed_over_life);
        hasher.add(desc.motion.drag);
        hasher.add(mean_lifetime(desc));
    }
    if(has_feature(features, emitter_feature::erosion))
    {
        hasher.add_curve(desc.render.erosion_over_life);
    }
    if(has_feature(features, emitter_feature::size_axis_curve))
    {
        hasher.add_curve(desc.appearance.size_axes_over_life);
    }
    if(has_feature(features, emitter_feature::spin_curve))
    {
        hasher.add_curve(desc.render.spin_over_life);
    }
    if(has_feature(features, emitter_feature::intensity_curve))
    {
        hasher.add_curve(desc.appearance.intensity_over_life);
    }
    if(has_feature(features, emitter_feature::two_gradients))
    {
        hasher.add_curve(desc.appearance.color_gradient_b);
    }
    if(has_feature(features, emitter_feature::texsheet_frame_curve))
    {
        hasher.add_curve(desc.render.tex_sheet_frame_curve);
    }
    if(has_feature(features, emitter_feature::turbulence))
    {
        hasher.add_curve(desc.motion.turbulence_over_life);
    }
    return hasher.value();
}

/// The erosion threshold and the sheet frame over life, in the speed row's third and fourth channels.
void bake_erosion_and_frame(const emitter_desc& desc, emitter_feature features, lut_set& luts)
{
    const uint32_t base = uint32_t(lut_row_speed) * lut_size;
    const bool has_erosion = has_feature(features, emitter_feature::erosion);
    const bool has_frame_curve = has_feature(features, emitter_feature::texsheet_frame_curve);
    for(uint32_t i = 0; i < lut_size; ++i)
    {
        const float t = float(i) / float(lut_size - 1);
        luts.data[base + i].z = has_erosion ? math::clamp(desc.render.erosion_over_life.sample(t), 0.0f, 1.0f) : 0.0f;
        luts.data[base + i].w = has_frame_curve ? math::clamp(desc.render.tex_sheet_frame_curve.sample(t), 0.0f, 1.0f) : t;
    }
}

/// Per-axis size (x and y; z rides in the turbulence row), the spin's integral over life (in lifetimes: the angle is
/// spin x lifetime x it) and the intensity; neutral (1, 1, life, 1) for the features that are off.
void bake_shape_row(const emitter_desc& desc, emitter_feature features, lut_set& luts, lut_ranges& ranges)
{
    const uint32_t base = uint32_t(lut_row_shape) * lut_size;
    const bool has_axes = has_feature(features, emitter_feature::size_axis_curve);
    // Only meshes have a depth: a quad's z multiplier never reaches its bounds.
    const bool has_depth = desc.render.render_mode == render_mode::mesh;
    const bool has_spin = has_feature(features, emitter_feature::spin_curve);
    const bool has_intensity = has_feature(features, emitter_feature::intensity_curve);
    const auto spin = [&](float u)
    {
        return has_spin ? desc.render.spin_over_life.sample(u) : 1.0f;
    };
    ranges.size_axis_peak = 0.0f;
    float spin_integral = 0.0f;
    float previous_spin = spin(0.0f);
    const float sample_step = 1.0f / float(lut_size - 1);
    const float substep = sample_step / float(k_speed_integral_substeps);
    for(uint32_t i = 0; i < lut_size; ++i)
    {
        const float t = float(i) * sample_step;
        if(i > 0)
        {
            for(uint32_t s = 1; s <= k_speed_integral_substeps; ++s)
            {
                const float value = spin(t - sample_step + float(s) * substep);
                spin_integral += (previous_spin + value) * 0.5f * substep;
                previous_spin = value;
            }
        }
        const math::vec3 axes = has_axes ? desc.appearance.size_axes_over_life.sample(t) : math::vec3(1.0f);
        const float intensity = has_intensity ? desc.appearance.intensity_over_life.sample(t) : 1.0f;
        luts.data[base + i] = math::vec4(axes.x, axes.y, spin_integral, intensity);
        const math::vec3 reach = math::abs(axes);
        ranges.size_axis_peak = math::max(ranges.size_axis_peak, math::max(math::max(reach.x, reach.y), has_depth ? reach.z : 0.0f));
    }
}

void bake_lut_set(const emitter_desc& desc, emitter_feature features, lut_set& out_luts)
{
    lut_ranges& out_ranges = out_luts.ranges;
    out_ranges.size_peak = get_scale_curve_peak(desc.appearance.size_over_life);
    bake_color_row(desc.appearance.color_gradient, out_luts, lut_row_color);
    bake_life_row(desc, features, out_luts, out_ranges);
    if(has_feature(features, emitter_feature::color_by_speed))
    {
        bake_color_row(desc.appearance.color_by_speed_gradient, out_luts, lut_row_color_by_speed);
    }
    else
    {
        const uint32_t base = uint32_t(lut_row_color_by_speed) * lut_size;
        for(uint32_t i = 0; i < lut_size; ++i)
        {
            out_luts.data[base + i] = math::vec4(1.0f);
        }
    }
    bake_speed_row(desc, features, out_luts, out_ranges);
    bake_erosion_and_frame(desc, features, out_luts);
    bake_shape_row(desc, features, out_luts, out_ranges);
    const bool has_two_gradients = has_feature(features, emitter_feature::two_gradients);
    bake_color_row(has_two_gradients ? desc.appearance.color_gradient_b : desc.appearance.color_gradient,
                   out_luts,
                   lut_row_color_b);
    const bool has_turbulence = has_feature(features, emitter_feature::turbulence);
    const bool has_axes = has_feature(features, emitter_feature::size_axis_curve);
    const uint32_t turbulence_base = uint32_t(lut_row_turbulence_size_z) * lut_size;
    out_ranges.turbulence_peak = 0.0f;
    for(uint32_t i = 0; i < lut_size; ++i)
    {
        const float t = float(i) / float(lut_size - 1);
        const float strength = has_turbulence ? desc.motion.turbulence_over_life.sample(t) : 1.0f;
        const float size_z = has_axes ? desc.appearance.size_axes_over_life.sample(t).z : 1.0f;
        out_luts.data[turbulence_base + i] = math::vec4(strength, size_z, 0.0f, 0.0f);
        out_ranges.turbulence_peak = math::max(out_ranges.turbulence_peak, math::abs(strength));
    }
}

} // namespace ps_soa
} // namespace unravel
