#include "../tests.h"

#include <math/math.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace unravel::gi_screen_sampling_tests
{
namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr int EDGE = 8;
constexpr int ANGULAR_STEPS = 64;
constexpr int SAMPLE_STEPS = 256;
constexpr int CELL_RAYS = 4;
int g_checks = 0;
int g_failures = 0;

struct oct_cell
{
    int x;
    int y;
    int span;
};

struct cone
{
    math::dvec3 axis;
    double cosine;
};

struct angular_integral
{
    double area = 0.0;
    double probability = 0.0;
    double radiance = 0.0;
};

void check_near(double actual, double expected, double tolerance, const std::string& label)
{
    ++g_checks;
    if(!(std::abs(actual - expected) <= tolerance))
    {
        ++g_failures;
        std::printf("  FAIL: %s (got %.8f, expected %.8f +/- %.8f)\n",
                    label.c_str(), actual, expected, tolerance);
    }
}

auto decode_oct(double u, double v) -> math::dvec3
{
    math::dvec3 direction(2.0 * u - 1.0, 2.0 * v - 1.0, 0.0);
    direction.z = 1.0 - std::abs(direction.x) - std::abs(direction.y);
    const double fold = std::max(-direction.z, 0.0);
    direction.x += direction.x >= 0.0 ? -fold : fold;
    direction.y += direction.y >= 0.0 ? -fold : fold;
    return math::normalize(direction);
}

auto sample_cell(const oct_cell& cell, double u, double v) -> math::dvec3
{
    return decode_oct((cell.x + cell.span * u) / EDGE, (cell.y + cell.span * v) / EDGE);
}

auto contains_direction(const oct_cell& cell, const math::dvec3& direction) -> bool
{
    const math::dvec3 oct = direction / (std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z));
    const double x = oct.z >= 0.0 ? oct.x : (1.0 - std::abs(oct.y)) * (oct.x >= 0.0 ? 1.0 : -1.0);
    const double y = oct.z >= 0.0 ? oct.y : (1.0 - std::abs(oct.x)) * (oct.y >= 0.0 ? 1.0 : -1.0);
    const double texel_x = (x * 0.5 + 0.5) * EDGE;
    const double texel_y = (y * 0.5 + 0.5) * EDGE;
    return texel_x >= cell.x && texel_x < cell.x + cell.span &&
           texel_y >= cell.y && texel_y < cell.y + cell.span;
}

// CPU transcription of GiOctCellDirectionalPdf. The independent oracle below obtains
// angular measure from spherical triangles, without using this Jacobian.
auto evaluate_cell_pdf(const oct_cell& cell, const math::dvec3& direction) -> double
{
    const double l1 = std::abs(direction.x) + std::abs(direction.y) + std::abs(direction.z);
    return double(EDGE * EDGE) / (4.0 * cell.span * cell.span * l1 * l1 * l1);
}

auto evaluate_radiance(const math::dvec3& direction) -> double
{
    return 0.25 + std::exp(4.0 * direction.z) + 2.0 * direction.x * direction.x + 0.5 * direction.y;
}

auto evaluate_cone_pdf(const cone& proposal, const math::dvec3& direction) -> double
{
    return math::dot(proposal.axis, direction) >= proposal.cosine ? 1.0 / (2.0 * PI * (1.0 - proposal.cosine)) : 0.0;
}

void accumulate_triangle(angular_integral& integral, const oct_cell& cell,
                         const math::dvec3& a, const math::dvec3& b, const math::dvec3& c)
{
    const double determinant = std::abs(math::dot(a, math::cross(b, c)));
    const double area = 2.0 * std::atan2(determinant, 1.0 + math::dot(a, b) + math::dot(b, c) + math::dot(c, a));
    const math::dvec3 centroid = math::normalize(a + b + c);
    integral.area += area;
    integral.probability += area * evaluate_cell_pdf(cell, centroid);
    integral.radiance += area * evaluate_radiance(centroid);
}

auto integrate_angular_reference(const oct_cell& cell) -> angular_integral
{
    angular_integral integral;
    for(int y = 0; y < ANGULAR_STEPS; ++y)
    {
        for(int x = 0; x < ANGULAR_STEPS; ++x)
        {
            const math::dvec3 a = sample_cell(cell, double(x) / ANGULAR_STEPS, double(y) / ANGULAR_STEPS);
            const math::dvec3 b = sample_cell(cell, double(x + 1) / ANGULAR_STEPS, double(y) / ANGULAR_STEPS);
            const math::dvec3 c = sample_cell(cell, double(x + 1) / ANGULAR_STEPS, double(y + 1) / ANGULAR_STEPS);
            const math::dvec3 d = sample_cell(cell, double(x) / ANGULAR_STEPS, double(y + 1) / ANGULAR_STEPS);
            // Octahedral folds follow either diagonal. Split on the fold so each spherical
            // triangle is the image of one planar octahedron face, including equator cells.
            const bool split_other_diagonal = (cell.x + cell.span * (double(x) + 0.5) / ANGULAR_STEPS < EDGE / 2.0) ==
                                              (cell.y + cell.span * (double(y) + 0.5) / ANGULAR_STEPS < EDGE / 2.0);
            if(split_other_diagonal)
            {
                accumulate_triangle(integral, cell, a, b, d);
                accumulate_triangle(integral, cell, b, c, d);
            }
            else
            {
                accumulate_triangle(integral, cell, a, b, c);
                accumulate_triangle(integral, cell, a, c, d);
            }
        }
    }
    return integral;
}

auto sample_cone(const cone& proposal, double u, double v) -> math::dvec3
{
    const math::dvec3 tangent = math::normalize(math::cross(proposal.axis, math::dvec3(0.0, 1.0, 0.0)));
    const math::dvec3 bitangent = math::cross(proposal.axis, tangent);
    const double cosine = 1.0 - u * (1.0 - proposal.cosine);
    const double sine = std::sqrt(1.0 - cosine * cosine);
    const double azimuth = 2.0 * PI * v;
    return proposal.axis * cosine + sine * (tangent * std::cos(azimuth) + bitangent * std::sin(azimuth));
}

auto evaluate_mis_sample(const oct_cell& cell, const std::array<cone, 2>& emitters,
                         const math::dvec3& direction, double area, bool has_emitters, bool use_legacy_pdf) -> double
{
    if(!contains_direction(cell, direction))
    {
        return 0.0;
    }
    double denominator = CELL_RAYS * (use_legacy_pdf ? 1.0 / area : evaluate_cell_pdf(cell, direction));
    if(has_emitters)
    {
        denominator += evaluate_cone_pdf(emitters[0], direction) + evaluate_cone_pdf(emitters[1], direction);
    }
    return evaluate_radiance(direction) / denominator;
}

auto integrate_sampled_estimator(const oct_cell& cell, double area, bool has_emitters, bool use_legacy_pdf) -> double
{
    const std::array<cone, 2> emitters{{{sample_cell(cell, 0.3, 0.4), 0.90},
                                      {sample_cell(cell, 0.65, 0.6), 0.85}}};
    double integral = 0.0;
    for(int y = 0; y < SAMPLE_STEPS; ++y)
    {
        for(int x = 0; x < SAMPLE_STEPS; ++x)
        {
            const double u = (x + 0.5) / SAMPLE_STEPS;
            const double v = (y + 0.5) / SAMPLE_STEPS;
            integral += CELL_RAYS * evaluate_mis_sample(cell, emitters, sample_cell(cell, u, v), area,
                                                        has_emitters, use_legacy_pdf);
            if(has_emitters)
            {
                for(const cone& emitter : emitters)
                {
                    integral += evaluate_mis_sample(cell, emitters, sample_cone(emitter, u, v), area,
                                                    has_emitters, use_legacy_pdf);
                }
            }
        }
    }
    return integral / double(SAMPLE_STEPS * SAMPLE_STEPS);
}

void test_pdf_normalization()
{
    for(const int span : {1, 2})
    {
        double sphere_area = 0.0;
        for(int y = 0; y < EDGE; y += span)
        {
            for(int x = 0; x < EDGE; x += span)
            {
                const angular_integral integral = integrate_angular_reference({x, y, span});
                const std::string label = "span " + std::to_string(span) + " cell " + std::to_string(x) + "," + std::to_string(y);
                check_near(integral.probability, 1.0, 0.0001, label + " integrates p(d) dOmega to one");
                sphere_area += integral.area;
            }
        }
        check_near(sphere_area, 4.0 * PI, 1e-10, "angular reference tiles the entire sphere");
    }
}

void test_variable_radiance_estimators()
{
    for(const int span : {1, 2})
    {
        for(const bool has_emitters : {false, true})
        {
            double largest_legacy_error = 0.0;
            for(const oct_cell cell : {oct_cell{0, 0, span}, oct_cell{2, 2, span}, oct_cell{6, 2, span}})
            {
                const angular_integral reference = integrate_angular_reference(cell);
                const double actual = integrate_sampled_estimator(cell, reference.area, has_emitters, false);
                const double legacy = integrate_sampled_estimator(cell, reference.area, has_emitters, true);
                const std::string label = "span " + std::to_string(span) + (has_emitters ? " overlapping emitter MIS" : " no emitters") +
                                          " cell " + std::to_string(cell.x) + "," + std::to_string(cell.y);
                check_near(actual, reference.radiance, 0.001 * reference.radiance,
                           label + " matches independent angular radiance integral");
                largest_legacy_error = std::max(largest_legacy_error, std::abs(legacy / reference.radiance - 1.0));
                std::printf("  %s: relative error %.6f, legacy %.6f\n", label.c_str(),
                            actual / reference.radiance - 1.0, legacy / reference.radiance - 1.0);
            }
            // A constant-radiance furnace hides the old UV mean bias. This variable field
            // must reject it by at least 1%, with and without overlapping light proposals.
            check_near(largest_legacy_error > 0.01 ? 1.0 : 0.0, 1.0, 0.0,
                       "variable-radiance cases detect the legacy constant 1/Omega PDF");
        }
    }
}
} // namespace

auto run_gi_screen_sampling_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_pdf_normalization();
    test_variable_radiance_estimators();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

REGISTER_TEST_SUITE("gi screen sampling / directional PDF", run_gi_screen_sampling_suite)
} // namespace unravel::gi_screen_sampling_tests
