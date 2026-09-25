#include "specular_occlusion_lut.h"
#include <concurrency/parallel.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

namespace unravel
{

// File-local helpers live in their own namespace: the non-Debug unity build merges every
// source's unravel::(anonymous) (tasks/lessons.md).
namespace gtso
{
namespace
{

constexpr uint32_t table_edge = specular_occlusion_lut::resolution;
constexpr double two_pi = 6.283185307179586;
/// 2^-32: maps a 32-bit reversed integer onto [0, 1).
constexpr double radical_inverse_scale = 1.0 / 4294967296.0;
/// Floor on cos^2 in the Smith term, so a direction on the horizon stays finite.
constexpr double min_cos_squared = 1.0e-12;
constexpr double unorm8_max = 255.0;

/// One reflected direction of the lobe: its components across the lobe axis (in the plane the
/// cone axis tilts in) and along it, and its weight. The third component never enters the dot
/// product with a cone axis in that plane.
struct lobe_sample
{
    double across;
    double along;
    double weight;
};

/// Van der Corput radical inverse in base 2: the second Hammersley coordinate.
auto radical_inverse(uint32_t bits) -> double
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return double(bits) * radical_inverse_scale;
}

/// Smith Lambda of GGX for a direction at cos_theta from the surface normal.
auto smith_lambda(double cos_theta, double alpha) -> double
{
    const double cos_squared = std::max(cos_theta * cos_theta, min_cos_squared);
    const double tan_squared = (1.0 - cos_squared) / cos_squared;
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * tan_squared) - 1.0);
}

/// The GGX lobe viewed along its normal: visible-normal samples (Heitz 2018; at normal
/// incidence the visible normals are distributed as D(h) (n.h)), each reflected view weighted by
/// the height-correlated masking-shadowing over the view's own masking, which is 1 there.
/// Reflections below the surface carry no weight and are dropped.
auto sample_lobe(double alpha) -> std::vector<lobe_sample>
{
    std::vector<lobe_sample> samples;
    samples.reserve(specular_occlusion_lut::lobe_samples);
    for(uint32_t i = 0; i < specular_occlusion_lut::lobe_samples; ++i)
    {
        const double radius = std::sqrt((double(i) + 0.5) / double(specular_occlusion_lut::lobe_samples));
        const double phi = two_pi * radical_inverse(i);
        const double disk_x = radius * std::cos(phi);
        const double disk_y = radius * std::sin(phi);
        const double disk_z = std::sqrt(std::max(0.0, 1.0 - disk_x * disk_x - disk_y * disk_y));
        const double half_x = alpha * disk_x;
        const double half_y = alpha * disk_y;
        const double half_length = std::sqrt(half_x * half_x + half_y * half_y + disk_z * disk_z);
        const double half_across = half_x / half_length;
        const double half_along = disk_z / half_length;
        const double along = 2.0 * half_along * half_along - 1.0;
        if(along <= 0.0)
        {
            continue;
        }
        samples.push_back({2.0 * half_along * half_across, along, 1.0 / (1.0 + smith_lambda(along, alpha))});
    }
    return samples;
}

auto to_unorm8(double value) -> uint8_t
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * unorm8_max));
}

/// One roughness row: for each angle between the cone axis and the lobe, the lobe weight
/// inside the cone of each visibility over the weight above the cone axis's horizon. A direction
/// d from the axis lies inside the cone of visibility v when d >= sqrt(1 - v), so it counts from
/// the first slice at or above v = 1 - d^2 on, and a prefix sum over the slices finishes the row.
void fill_roughness_row(uint32_t row, std::vector<uint8_t>& table)
{
    const double roughness = double(row) / double(table_edge - 1);
    const auto samples = sample_lobe(roughness * roughness);
    std::array<double, table_edge> entering{};
    for(uint32_t column = 0; column < table_edge; ++column)
    {
        const double cos_beta = 2.0 * double(column) / double(table_edge - 1) - 1.0;
        const double sin_beta = std::sqrt(std::max(0.0, 1.0 - cos_beta * cos_beta));
        entering.fill(0.0);
        double above_horizon = 0.0;
        for(const auto& sample : samples)
        {
            const double axis_cosine = sample.across * sin_beta + sample.along * cos_beta;
            if(axis_cosine <= 0.0)
            {
                continue;
            }
            above_horizon += sample.weight;
            const double first_slice = std::ceil((1.0 - axis_cosine * axis_cosine) * double(table_edge - 1));
            entering[size_t(std::clamp(first_slice, 0.0, double(table_edge - 1)))] += sample.weight;
        }
        double inside = 0.0;
        for(uint32_t slice = 0; slice < table_edge; ++slice)
        {
            inside += entering[slice];
            const double occlusion = above_horizon > 0.0 ? inside / above_horizon : 0.0;
            table[(size_t(slice) * table_edge + row) * table_edge + column] = to_unorm8(occlusion);
        }
    }
}

} // namespace
} // namespace gtso

auto specular_occlusion_lut::build() -> std::vector<uint8_t>
{
    std::vector<uint8_t> table(size_t(resolution) * resolution * resolution);
    std::vector<uint32_t> rows(resolution);
    std::iota(rows.begin(), rows.end(), 0u);
    // Rows write disjoint texels.
    poolstl::for_each_par_if(true,
                             rows.begin(),
                             rows.end(),
                             [&](uint32_t row)
                             {
                                 gtso::fill_roughness_row(row, table);
                             });
    return table;
}

void specular_occlusion_lut::generate()
{
    const auto start = std::chrono::steady_clock::now();
    const auto table = build();
    texture = std::make_unique<gfx::texture>(resolution,
                                             resolution,
                                             resolution,
                                             false,
                                             bgfx::TextureFormat::R8,
                                             BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP,
                                             bgfx::copy(table.data(), static_cast<uint32_t>(table.size())));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    APPLOG_TRACE("[SpecularOcclusion] {0}x{0}x{0} GTSO table ready ({1} ms).", resolution, ms);
}

void specular_occlusion_lut::clear()
{
    texture.reset();
}

} // namespace unravel
