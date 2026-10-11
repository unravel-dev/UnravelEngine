/*
 * Validation suite for math::curve / math::gradient.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite curve
 *
 * Written against the key-list API that survives the curve rework, so it runs both before and
 * after it and says whether the rework changed an answer it should not have.
 *
 * What it pins:
 *   - an empty curve samples the value type's zero, and a curve holds its end values outside
 *     the key range rather than extrapolating;
 *   - keys sort on insertion whatever order they arrive in, and a sample landing exactly on a
 *     key returns that key's value;
 *   - linear interpolation, and constant interpolation stepping from the left key;
 *   - reverse() mirroring positions about the middle;
 *   - every value type the engine stores in a curve interpolates per channel: float, color
 *     (one key, four channels) and frange_t (min and max moving independently);
 *   - a baked curve answering what the unbaked one answers. The two paths used to disagree:
 *     the lookup table always lerped, so a constant curve stepped in 1/255-wide ramps, and it
 *     clamped the position to [0,1], so keys outside that range were unreachable. Those two
 *     are asserted here, so the LUT cannot silently change a curve's meaning again.
 */

#include "../tests.h"

#include <base/basetypes.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <math/math.h>
#include <serialization/associative_archive.h>

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

using namespace unravel;

namespace
{

int g_curve_checks = 0;
int g_curve_failures = 0;

void curve_check(bool condition, const std::string& what)
{
    ++g_curve_checks;
    if(!condition)
    {
        ++g_curve_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

/// Tolerance of a sampled scalar. Loose enough for a lookup table to answer like a direct
/// evaluation at the table's own resolution, tight enough to catch a wrong key or a wrong lerp.
constexpr float CURVE_SAMPLE_EPSILON = 1e-3f;

void curve_check_near(float actual, float expected, const std::string& what)
{
    curve_check(std::fabs(actual - expected) <= CURVE_SAMPLE_EPSILON,
                what + " (expected " + std::to_string(expected) + ", got " + std::to_string(actual) + ")");
}

auto make_ramp() -> math::gradient<float>
{
    math::gradient<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 1.0f);
    return curve;
}

// ---------------------------------------------------------------------------------

void test_empty_curve_samples_zero()
{
    const math::gradient<float> curve;
    curve_check(!curve.is_valid(), "an empty curve is not valid");
    curve_check_near(curve.sample(0.5f), 0.0f, "an empty curve samples zero");
}

void test_keys_sort_on_insertion()
{
    math::gradient<float> curve;
    curve.add_point(1.0f, 1.0f);
    curve.add_point(0.0f, 0.0f);
    curve.add_point(0.5f, 0.5f);
    const auto points = curve.get_points();
    curve_check(points.size() == 3, "three keys inserted out of order are all kept");
    curve_check(points[0].progress <= points[1].progress && points[1].progress <= points[2].progress,
                "keys are sorted by position whatever order they arrive in");
}

void test_sample_on_a_key_returns_that_key()
{
    math::gradient<float> curve;
    curve.add_point(2.0f, 0.0f);
    curve.add_point(7.0f, 0.5f);
    curve.add_point(3.0f, 1.0f);
    curve_check_near(curve.sample(0.0f), 2.0f, "sampling the first key returns its value");
    curve_check_near(curve.sample(0.5f), 7.0f, "sampling a middle key returns its value");
    curve_check_near(curve.sample(1.0f), 3.0f, "sampling the last key returns its value");
}

void test_ends_hold_outside_the_key_range()
{
    const auto curve = make_ramp();
    curve_check_near(curve.sample(-1.0f), 0.0f, "before the first key the first value holds");
    curve_check_near(curve.sample(2.0f), 1.0f, "after the last key the last value holds");
}

void test_linear_interpolation()
{
    const auto curve = make_ramp();
    curve_check_near(curve.sample(0.25f), 0.25f, "a 0..1 ramp is linear at a quarter");
    curve_check_near(curve.sample(0.5f), 0.5f, "a 0..1 ramp is linear at the middle");
    curve_check_near(curve.sample(0.75f), 0.75f, "a 0..1 ramp is linear at three quarters");
}

void test_constant_interpolation_steps_from_the_left_key()
{
    auto curve = make_ramp();
    curve.set_interpolation(math::curve_interpolation::constant);
    curve_check_near(curve.sample(0.25f), 0.0f, "constant holds the left key just after it");
    curve_check_near(curve.sample(0.99f), 0.0f, "constant holds the left key up to the next one");
    curve_check_near(curve.sample(1.0f), 1.0f, "constant takes the next key's value at the key");
}

void test_reverse_mirrors_positions()
{
    // Mirrored about the middle of the curve's own key range, which is what a curve can know
    // without being told the range it will be sampled over.
    math::gradient<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 0.25f);
    curve.add_point(2.0f, 1.0f);
    curve.reverse();
    curve_check_near(curve.sample(0.0f), 2.0f, "reverse moves the last key to the start");
    curve_check_near(curve.sample(0.75f), 1.0f, "reverse mirrors a middle key about the domain");
    curve_check_near(curve.sample(1.0f), 0.0f, "reverse moves the first key to the end");
}

void test_color_channels_interpolate_independently()
{
    math::gradient<math::color> curve;
    curve.add_point(math::color(0.0f, 1.0f, 0.0f, 0.0f), 0.0f);
    curve.add_point(math::color(1.0f, 1.0f, 0.0f, 1.0f), 1.0f);
    const math::color middle = curve.sample(0.5f);
    curve_check_near(middle.value.r, 0.5f, "a color curve interpolates red");
    curve_check_near(middle.value.g, 1.0f, "a color curve holds a channel that does not change");
    curve_check_near(middle.value.b, 0.0f, "a color curve holds a channel that stays at zero");
    curve_check_near(middle.value.a, 0.5f, "a color curve interpolates alpha");
}

void test_range_channels_interpolate_independently()
{
    math::gradient<frange_t> curve;
    curve.add_point(frange_t(0.0f, 10.0f), 0.0f);
    curve.add_point(frange_t(1.0f, 20.0f), 1.0f);
    const frange_t middle = curve.sample(0.5f);
    curve_check_near(middle.min, 0.5f, "a range curve interpolates its minimum");
    curve_check_near(middle.max, 15.0f, "a range curve interpolates its maximum");
}

void test_baked_curve_matches_unbaked_when_linear()
{
    const auto reference = make_ramp();
    auto baked = make_ramp();
    baked.bake(256);
    for(int step = 0; step <= 20; ++step)
    {
        const float position = float(step) / 20.0f;
        curve_check_near(baked.sample(position),
                         reference.sample(position),
                         "a baked linear curve answers like an unbaked one at " + std::to_string(position));
    }
}

void test_baked_curve_keeps_constant_steps()
{
    auto curve = make_ramp();
    curve.set_interpolation(math::curve_interpolation::constant);
    curve.bake(256);
    // The lookup table used to lerp between its entries unconditionally, which turned every step
    // into a ramp one table entry wide - visible only within that last entry, which is why this
    // samples hard against the next key rather than at a round position.
    curve_check_near(curve.sample(0.5f), 0.0f, "a baked constant curve still steps at the middle");
    curve_check_near(curve.sample(0.999f), 0.0f, "a baked constant curve does not ramp inside its last table cell");
}

void test_baked_curve_reaches_keys_outside_zero_to_one()
{
    math::gradient<float> curve;
    curve.add_point(10.0f, -5.0f);
    curve.add_point(20.0f, 5.0f);
    const float unbaked_middle = curve.sample(0.0f);
    curve.bake(256);
    // The lookup table used to clamp the position to [0,1] and to bake only that span, so a
    // curve authored outside it sampled as a flat line.
    curve_check_near(curve.sample(0.0f), unbaked_middle, "a baked curve keeps the middle of a -5..5 domain");
    curve_check_near(curve.sample(-5.0f), 10.0f, "a baked curve holds the first value below its domain");
    curve_check_near(curve.sample(5.0f), 20.0f, "a baked curve holds the last value above its domain");
}

// ---------------------------------------------------------------------------------
// What the curve model adds over the gradient it replaces.

void test_smooth_interpolation_passes_through_its_keys()
{
    math::curve<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 0.5f);
    curve.add_point(0.0f, 1.0f);
    curve.set_interpolation(math::curve_interpolation::smooth);
    curve_check_near(curve.sample(0.0f), 0.0f, "a smooth curve still passes through its first key");
    curve_check_near(curve.sample(0.5f), 1.0f, "a smooth curve still passes through a middle key");
    curve_check_near(curve.sample(1.0f), 0.0f, "a smooth curve still passes through its last key");
}

void test_flat_tangents_level_off_at_the_peak()
{
    math::curve<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 0.5f);
    curve.add_point(0.0f, 1.0f);
    curve.set_interpolation(math::curve_interpolation::smooth);
    auto& channel = curve.get_channel(0);
    auto keys = channel.get_keys();
    for(auto& key : keys)
    {
        key.tangent_mode = math::curve_tangent_mode::flat;
    }
    channel.set_keys(std::move(keys));
    channel.refresh_automatic_tangents();
    // Level at the peak means the curve approaches it from below on both sides instead of
    // overshooting past it.
    curve_check(curve.sample(0.45f) < 1.0f, "a flat peak is not overshot from the left");
    curve_check(curve.sample(0.55f) < 1.0f, "a flat peak is not overshot from the right");
    curve_check(curve.sample(0.25f) > 0.0f && curve.sample(0.25f) < 1.0f, "a flat curve still rises between keys");
}

void test_automatic_tangents_level_off_at_the_ends()
{
    // Two keys are both ends of the curve, so a smooth segment between them eases out of the
    // first and into the second. Taking the slope of that one segment instead would reproduce
    // the straight line the curve was asked to round off.
    math::curve<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 1.0f);
    curve.set_interpolation(math::curve_interpolation::smooth);
    const auto keys = curve.get_channel(0).get_keys();
    curve_check_near(keys.front().out_tangent, 0.0f, "a smooth curve leaves its first key level");
    curve_check_near(keys.back().in_tangent, 0.0f, "a smooth curve arrives at its last key level");
    curve_check_near(curve.sample(0.5f), 0.5f, "an eased curve still crosses the middle halfway");
    curve_check(curve.sample(0.25f) < 0.25f, "an eased curve starts slower than a straight line");
    curve_check(curve.sample(0.75f) > 0.75f, "an eased curve finishes faster than a straight line");
}

void test_automatic_tangents_follow_the_neighbours()
{
    // A key with a neighbour either side takes the slope through them, so the curve flows through
    // it rather than easing at every key it passes.
    math::curve<float> curve;
    curve.add_point(0.0f, 0.0f);
    curve.add_point(1.0f, 1.0f);
    curve.add_point(2.0f, 2.0f);
    curve.set_interpolation(math::curve_interpolation::smooth);
    const auto keys = curve.get_channel(0).get_keys();
    curve_check_near(keys[1].out_tangent, 1.0f, "a middle key takes the slope through its neighbours");
    curve_check_near(curve.sample(1.0f), 1.0f, "a smooth curve still passes through that middle key");
}

void test_channels_can_be_keyed_independently()
{
    math::curve<frange_t> curve;
    curve.add_point(frange_t(0.0f, 10.0f), 0.0f);
    curve.add_point(frange_t(1.0f, 20.0f), 1.0f);
    curve.set_linked(false);
    // A key on the maximum alone must not disturb the minimum's straight ramp.
    curve.get_channel(1).add_key(0.5f, 100.0f);
    curve.get_channel(1).refresh_automatic_tangents();
    curve_check_near(curve.sample(0.5f).min, 0.5f, "an unlinked minimum keeps its own ramp");
    curve_check_near(curve.sample(0.5f).max, 100.0f, "an unlinked maximum takes its own key");
    curve_check(curve.get_channel(0).get_key_count() == 2, "an unlinked channel keeps its own key count");
    curve_check(curve.get_channel(1).get_key_count() == 3, "the keyed channel gained a key");
}

void test_linking_keeps_the_shape()
{
    math::curve<frange_t> curve;
    curve.add_point(frange_t(0.0f, 10.0f), 0.0f);
    curve.add_point(frange_t(1.0f, 20.0f), 1.0f);
    curve.set_linked(false);
    curve.get_channel(1).add_key(0.5f, 100.0f);
    const float min_before = curve.sample(0.25f).min;
    const float max_before = curve.sample(0.25f).max;
    curve.set_linked(true);
    curve_check(curve.get_channel(0).get_key_count() == 3, "linking gives every channel the same key count");
    curve_check_near(curve.sample(0.25f).min, min_before, "linking does not move the minimum");
    curve_check_near(curve.sample(0.25f).max, max_before, "linking does not move the maximum");
}

void test_domain_is_not_limited_to_zero_to_one()
{
    // The exposure compensation curve is authored over EV, not over a lifetime.
    math::curve<float> curve;
    curve.add_point(-2.0f, -10.0f);
    curve.add_point(2.0f, 20.0f);
    const frange_t domain = curve.get_domain();
    curve_check_near(domain.min, -10.0f, "the domain starts at the first key");
    curve_check_near(domain.max, 20.0f, "the domain ends at the last key");
    curve_check_near(curve.sample(5.0f), 0.0f, "a curve interpolates across a wide domain");
    curve_check_near(curve.sample(-50.0f), -2.0f, "a curve holds its first value far below its domain");
}

void test_editing_drops_the_bake()
{
    auto curve = make_ramp();
    curve.bake(256);
    curve_check(curve.is_baked(), "baking leaves a table behind");
    curve.add_point(0.5f, 0.5f);
    curve_check(!curve.is_baked(), "editing a curve drops its table rather than leaving it stale");
    curve_check_near(curve.sample(0.5f), 0.5f, "a curve edited after baking samples its new key");
}

// ---------------------------------------------------------------------------------
// Serialization. Scenes and particle prefabs on disk predate channels, so the load path has to
// keep reading them.

template<typename T>
auto save_curve(const math::curve<T>& curve) -> std::string
{
    std::stringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        ar(ser20::make_nvp("curve", curve));
    }
    return stream.str();
}

template<typename T>
auto load_curve(const std::string& text) -> math::curve<T>
{
    std::stringstream stream(text);
    math::curve<T> curve;
    auto ar = ser20::create_iarchive_associative(stream);
    ar(ser20::make_nvp("curve", curve));
    return curve;
}

void test_curve_round_trips_through_an_archive()
{
    math::curve<frange_t> written;
    written.add_point(frange_t(0.0f, 10.0f), 0.0f);
    written.add_point(frange_t(1.0f, 20.0f), 1.0f);
    written.set_interpolation(math::curve_interpolation::smooth);
    written.set_linked(false);
    written.get_channel(1).add_key(0.5f, 100.0f);

    const auto reloaded = load_curve<frange_t>(save_curve(written));
    curve_check(reloaded.is_linked() == written.is_linked(), "a round trip keeps the linked flag");
    curve_check(reloaded.get_channel(1).get_key_count() == 3, "a round trip keeps an unlinked channel's keys");
    curve_check(reloaded.get_interpolation() == math::curve_interpolation::smooth,
                "a round trip keeps the interpolation, which the gradient format never stored");
    for(int step = 0; step <= 10; ++step)
    {
        const float position = float(step) / 10.0f;
        curve_check_near(reloaded.sample(position).min,
                         written.sample(position).min,
                         "a round trip samples the minimum the same at " + std::to_string(position));
        curve_check_near(reloaded.sample(position).max,
                         written.sample(position).max,
                         "a round trip samples the maximum the same at " + std::to_string(position));
    }
}

void test_legacy_point_list_still_loads()
{
    // What a .spfb written before this rework holds for a color gradient.
    const std::string legacy =
        R"({"curve":{"points":[)"
        R"({"progress":0.0,"element":{"r":1.0,"g":0.0,"b":0.0,"a":0.0}},)"
        R"({"progress":1.0,"element":{"r":0.0,"g":0.0,"b":1.0,"a":1.0}}]}})";

    const auto curve = load_curve<math::color>(legacy);
    curve_check(curve.is_valid(), "a legacy point list loads into a curve");
    curve_check(curve.get_points().size() == 2, "a legacy point list keeps both of its keys");
    curve_check(curve.is_linked(), "a legacy point list loads as a linked curve");

    const math::color start = curve.sample(0.0f);
    const math::color middle = curve.sample(0.5f);
    const math::color end = curve.sample(1.0f);
    curve_check_near(start.value.r, 1.0f, "a legacy curve keeps its first key");
    curve_check_near(end.value.b, 1.0f, "a legacy curve keeps its last key");
    curve_check_near(middle.value.r, 0.5f, "a legacy curve interpolates red as it always did");
    curve_check_near(middle.value.b, 0.5f, "a legacy curve interpolates blue as it always did");
    curve_check_near(middle.value.a, 0.5f, "a legacy curve interpolates alpha as it always did");
}

void test_legacy_load_matches_an_equivalent_curve()
{
    const std::string legacy =
        R"({"curve":{"points":[)"
        R"({"progress":0.0,"element":0.0},{"progress":0.25,"element":1.0},{"progress":1.0,"element":0.0}]}})";

    const auto loaded = load_curve<float>(legacy);
    math::curve<float> expected;
    expected.add_point(0.0f, 0.0f);
    expected.add_point(1.0f, 0.25f);
    expected.add_point(0.0f, 1.0f);
    for(int step = 0; step <= 20; ++step)
    {
        const float position = float(step) / 20.0f;
        curve_check_near(loaded.sample(position),
                         expected.sample(position),
                         "a legacy curve samples like the curve it describes at " + std::to_string(position));
    }
}

} // namespace

// ---------------------------------------------------------------------------------

auto run_curve_suite(rtti::context& /*ctx*/) -> int
{
    test_empty_curve_samples_zero();
    test_keys_sort_on_insertion();
    test_sample_on_a_key_returns_that_key();
    test_ends_hold_outside_the_key_range();
    test_linear_interpolation();
    test_constant_interpolation_steps_from_the_left_key();
    test_reverse_mirrors_positions();
    test_color_channels_interpolate_independently();
    test_range_channels_interpolate_independently();
    test_baked_curve_matches_unbaked_when_linear();
    test_baked_curve_keeps_constant_steps();
    test_baked_curve_reaches_keys_outside_zero_to_one();

    test_smooth_interpolation_passes_through_its_keys();
    test_flat_tangents_level_off_at_the_peak();
    test_automatic_tangents_level_off_at_the_ends();
    test_automatic_tangents_follow_the_neighbours();
    test_channels_can_be_keyed_independently();
    test_linking_keeps_the_shape();
    test_domain_is_not_limited_to_zero_to_one();
    test_editing_drops_the_bake();

    test_curve_round_trips_through_an_archive();
    test_legacy_point_list_still_loads();
    test_legacy_load_matches_an_equivalent_curve();

    std::printf("\n%d checks, %d failures\n", g_curve_checks, g_curve_failures);
    return g_curve_failures;
}

REGISTER_TEST_SUITE("curve", run_curve_suite)
