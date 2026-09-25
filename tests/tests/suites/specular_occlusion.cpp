#include "../tests.h"

#include <engine/rendering/specular_occlusion_lut.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace unravel::specular_occlusion_tests
{
namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr uint32_t EDGE = specular_occlusion_lut::resolution;
/// Angular grid of the reference integral over the reflected directions' hemisphere.
constexpr int THETA_STEPS = 2048;
constexpr int PHI_STEPS = 512;
/// The table against the reference: Hammersley sampling of a cone boundary plus UNORM8
/// quantisation stay within 0.005 (measured); this leaves headroom without hiding a wrong model.
constexpr double REFERENCE_TOLERANCE = 0.01;
/// One UNORM8 step: the resolution of every exact limit below.
constexpr double UNORM8_STEP = 1.0 / 255.0;
/// Mirror-row texels this close to the cone boundary are skipped (the boundary is a step).
constexpr double MIRROR_BOUNDARY_MARGIN = 0.02;
int g_checks = 0;
int g_failures = 0;

void check_near(double actual, double expected, double tolerance, const std::string& label)
{
    ++g_checks;
    if(!(std::abs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.6f, expected %.6f +/- %.6f)\n", label.c_str(), actual, expected, tolerance);
    }
}

auto column_cos_beta(uint32_t column) -> double
{
    return 2.0 * double(column) / double(EDGE - 1) - 1.0;
}

auto row_roughness(uint32_t row) -> double
{
    return double(row) / double(EDGE - 1);
}

auto slice_visibility(uint32_t slice) -> double
{
    return double(slice) / double(EDGE - 1);
}

auto texel(const std::vector<uint8_t>& table, uint32_t column, uint32_t row, uint32_t slice) -> double
{
    return double(table[(size_t(slice) * EDGE + row) * EDGE + column]) / 255.0;
}

auto texel_label(uint32_t column, uint32_t row, uint32_t slice) -> std::string
{
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "cos_beta %.3f roughness %.3f visibility %.3f",
                  column_cos_beta(column), row_roughness(row), slice_visibility(slice));
    return buffer;
}

auto smith_lambda(double cos_theta, double alpha) -> double
{
    const double cos_squared = std::max(cos_theta * cos_theta, 1.0e-12);
    return 0.5 * (std::sqrt(1.0 + alpha * alpha * (1.0 - cos_squared) / cos_squared) - 1.0);
}

/// The table's model integrated on an angular grid instead of by lobe samples: at normal
/// incidence the GGX specular lobe weights a reflected direction l, at angle theta from the
/// normal, by D(h) G2(v, l) with n.h = cos(theta / 2) and no masking of the view. Returns, per
/// requested visibility, the weight inside the cone around an axis at angle beta from the
/// normal over the weight above that axis's horizon.
auto integrate_reference(double cos_beta, double roughness, const std::vector<uint32_t>& slices) -> std::vector<double>
{
    const double alpha = roughness * roughness;
    const double alpha_squared = alpha * alpha;
    const double sin_beta = std::sqrt(std::max(0.0, 1.0 - cos_beta * cos_beta));
    std::vector<double> cos_apertures;
    for(const uint32_t slice : slices)
    {
        cos_apertures.push_back(std::sqrt(std::max(0.0, 1.0 - slice_visibility(slice))));
    }
    std::array<double, PHI_STEPS> cos_phi{};
    for(int p = 0; p < PHI_STEPS; ++p)
    {
        cos_phi[size_t(p)] = std::cos((double(p) + 0.5) * 2.0 * PI / double(PHI_STEPS));
    }
    std::vector<double> inside(slices.size(), 0.0);
    double above = 0.0;
    for(int t = 0; t < THETA_STEPS; ++t)
    {
        const double theta = (double(t) + 0.5) * 0.5 * PI / double(THETA_STEPS);
        const double cos_half = std::cos(0.5 * theta);
        const double denominator = cos_half * cos_half * (alpha_squared - 1.0) + 1.0;
        const double distribution = alpha_squared / (PI * denominator * denominator);
        const double weight = distribution / (1.0 + smith_lambda(std::cos(theta), alpha)) * std::sin(theta);
        for(int p = 0; p < PHI_STEPS; ++p)
        {
            const double axis_cosine = std::sin(theta) * cos_phi[size_t(p)] * sin_beta + std::cos(theta) * cos_beta;
            if(axis_cosine <= 0.0)
            {
                continue;
            }
            above += weight;
            for(size_t k = 0; k < slices.size(); ++k)
            {
                inside[k] += axis_cosine >= cos_apertures[k] ? weight : 0.0;
            }
        }
    }
    for(auto& value : inside)
    {
        value = above > 0.0 ? value / above : 0.0;
    }
    return inside;
}

void test_against_reference(const std::vector<uint8_t>& table)
{
    const std::vector<uint32_t> slices = {4, 8, 16, 24, 28};
    for(const uint32_t row : {10u, 16u, 24u, 31u})
    {
        for(const uint32_t column : {31u, 27u, 24u, 20u, 18u})
        {
            const auto reference = integrate_reference(column_cos_beta(column), row_roughness(row), slices);
            for(size_t k = 0; k < slices.size(); ++k)
            {
                check_near(texel(table, column, row, slices[k]), reference[k], REFERENCE_TOLERANCE,
                           texel_label(column, row, slices[k]) + " matches the angular reference integral");
            }
        }
    }
}

void test_limits(const std::vector<uint8_t>& table)
{
    for(uint32_t row = 0; row < EDGE; ++row)
    {
        for(uint32_t column = 0; column < EDGE; ++column)
        {
            // Full visibility: every direction above the cone axis's horizon is inside the cone.
            if(column_cos_beta(column) > 0.0)
            {
                check_near(texel(table, column, row, EDGE - 1), 1.0, UNORM8_STEP,
                           texel_label(column, row, EDGE - 1) + " is unoccluded at full visibility");
            }
            // A zero-aperture cone holds no measure of a lobe with any width.
            if(row > 0)
            {
                check_near(texel(table, column, row, 0), 0.0, UNORM8_STEP,
                           texel_label(column, row, 0) + " is occluded at zero visibility");
            }
            // More visibility never occludes more.
            for(uint32_t slice = 1; slice < EDGE; ++slice)
            {
                const double step = texel(table, column, row, slice) - texel(table, column, row, slice - 1);
                check_near(std::min(step, 0.0), 0.0, 0.0,
                           texel_label(column, row, slice) + " does not fall below the previous visibility");
            }
        }
    }
    // A mirror reflects one direction: visible exactly when it lies inside the cone.
    for(uint32_t column = 0; column < EDGE; ++column)
    {
        const double cos_beta = column_cos_beta(column);
        for(uint32_t slice = 0; slice < EDGE; ++slice)
        {
            const double cos_aperture = std::sqrt(1.0 - slice_visibility(slice));
            if(cos_beta <= 0.0 || std::abs(cos_beta - cos_aperture) < MIRROR_BOUNDARY_MARGIN)
            {
                continue;
            }
            check_near(texel(table, column, 0, slice), cos_beta >= cos_aperture ? 1.0 : 0.0, UNORM8_STEP,
                       texel_label(column, 0, slice) + " is the mirror's inside-the-cone step");
        }
    }
}
} // namespace

auto run_specular_occlusion_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto table = specular_occlusion_lut::build();
    check_near(double(table.size()), double(EDGE) * EDGE * EDGE, 0.0, "table holds resolution^3 texels");
    if(table.size() == size_t(EDGE) * EDGE * EDGE)
    {
        test_against_reference(table);
        test_limits(table);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("specular occlusion / GTSO table", run_specular_occlusion_suite)
} // namespace unravel::specular_occlusion_tests
