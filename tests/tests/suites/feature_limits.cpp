/*
 * Validation suite for the project's rendering feature limits and render scale.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "feature limits"
 *
 * What it pins:
 *   - pipeline::apply_feature_limits leaves a run as it is under the default limits, clears the hooks of
 *     the effects a limit turns off (global illumination, SSR, SSIL, both ambient occlusion passes, bloom),
 *     gives a view asking for temporal AA FXAA under an FXAA limit and no AA under none, and records the
 *     limits for the passes that read them;
 *   - limit_light caps a light's shadow map resolution and cascades, filters PCSS with PCF without soft shadows and
 *     drops contact shadows without them, and leaves an allowed light as it is;
 *   - max_shadow_filtering: hard renders every light hard, PCF 3x3 narrows wider PCF (point light faces too) and soft
 *     shadows to the 3x3 tent, PCF keeps every width, and two limits keep the narrower;
 *   - the graphics settings' render scale and limits round trip through a settings document;
 *   - get_render_size scales a view's size from MIN_RENDER_SCALE, keeps it at 1, for a scale that is not a
 *     number and for an empty view, and never goes below a texel;
 *   - get_view_render_size renders a view's own resolution, larger or smaller than its viewport, at the scale,
 *     and the viewport's size without one.
 */

#include "../tests.h"

#include <engine/meta/settings/settings.hpp>
#include <engine/rendering/feature_limits.h>
#include <engine/rendering/render_feature_support.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/render_scale.h>
#include <engine/settings/settings.h>
#include <serialization/sparse_document.h>

#include <cstdio>
#include <limits>
#include <string>

using namespace unravel::test;

using namespace unravel;

namespace
{

/// The root a settings document is saved under.
constexpr const char* LIMITS_DOCUMENT_ROOT = "settings";

using run_params = rendering::pipeline::run_params;

/// A run whose camera asks for every effect a limit bounds, with temporal AA.
auto make_run_asking_for_everything() -> run_params
{
    run_params params;
    params.fill_gi_params = [](gi_settings&) {};
    params.fill_ssr_params = [](ssr_pass::run_params&) {};
    params.fill_ssil_params = [](ssil_pass::run_params&) {};
    params.fill_gtao_params = [](gtao_pass::run_params&) {};
    params.fill_assao_params = [](assao_pass::run_params&) {};
    params.fill_bloom_params = [](bloom_pass::run_params&) {};
    params.fill_taa_params = [](taa_pass::run_params&) {};
    params.apply_taa_params = [](camera&, const usize32_t&) {};
    return params;
}

void test_default_limits()
{
    std::printf("test_default_limits\n");
    auto params = make_run_asking_for_everything();
    rendering::pipeline::apply_feature_limits(params, feature_limits{});
    check(params.fill_gi_params && params.fill_ssr_params && params.fill_ssil_params && params.fill_gtao_params &&
                     params.fill_assao_params && params.fill_bloom_params && params.fill_taa_params &&
                     params.apply_taa_params && !params.fill_fxaa_params,
                 "the default limits allow everything a run asks for");
}

void test_effects_turned_off()
{
    std::printf("test_effects_turned_off\n");
    feature_limits limits;
    limits.global_illumination = false;
    limits.screen_space_reflections = false;
    limits.screen_space_indirect_lighting = false;
    limits.ambient_occlusion = false;
    limits.bloom = false;
    auto params = make_run_asking_for_everything();
    rendering::pipeline::apply_feature_limits(params, limits);
    check(!params.fill_gi_params && !params.fill_ssr_params && !params.fill_ssil_params, "GI, SSR and SSIL are turned off");
    check(!params.fill_gtao_params && !params.fill_assao_params, "both ambient occlusion passes are turned off");
    check(!params.fill_bloom_params, "bloom is turned off");
    check(params.fill_taa_params && params.apply_taa_params, "and anti-aliasing is left alone");
    check(params.limits == limits, "the run records the limits");
}

void test_anti_aliasing_limits()
{
    std::printf("test_anti_aliasing_limits\n");
    feature_limits limits;
    limits.max_anti_aliasing = anti_aliasing_limit::fxaa;
    auto params = make_run_asking_for_everything();
    rendering::pipeline::apply_feature_limits(params, limits);
    check(!params.fill_taa_params && !params.apply_taa_params && params.fill_fxaa_params,
                 "under an FXAA limit a view asking for temporal AA gets FXAA");
    params = {};
    params.fill_fxaa_params = [](fxaa_pass::run_params&) {};
    rendering::pipeline::apply_feature_limits(params, limits);
    check(static_cast<bool>(params.fill_fxaa_params), "and a view asking for FXAA keeps it");
    params = {};
    rendering::pipeline::apply_feature_limits(params, limits);
    check(!params.fill_fxaa_params && !params.fill_taa_params, "a view asking for no anti-aliasing gets none");
    limits.max_anti_aliasing = anti_aliasing_limit::none;
    params = make_run_asking_for_everything();
    rendering::pipeline::apply_feature_limits(params, limits);
    check(!params.fill_taa_params && !params.apply_taa_params && !params.fill_fxaa_params,
                 "under no anti-aliasing neither temporal AA nor FXAA runs");
}

void test_limited_lights()
{
    std::printf("test_limited_lights\n");
    light source;
    source.shadow_params.resolution = sm_resolution::very_high;
    source.shadow_params.type = sm_impl::pcss;
    source.contact_shadow.enabled = true;
    const auto allowed = limit_light(source, feature_limits{});
    check(allowed.shadow_params.resolution == sm_resolution::very_high && allowed.shadow_params.type == sm_impl::pcss &&
                     allowed.contact_shadow.enabled,
                 "the default limits leave a light as it is");
    feature_limits limits;
    limits.max_shadow_resolution = sm_resolution::medium;
    limits.soft_shadows = false;
    limits.contact_shadows = false;
    const auto limited = limit_light(source, limits);
    check(limited.shadow_params.resolution == sm_resolution::medium, "a larger shadow map is capped");
    check(limited.shadow_params.type == sm_impl::pcf, "PCSS filters with PCF without soft shadows");
    check(!limited.contact_shadow.enabled, "contact shadows are dropped without them");
    source.shadow_params.resolution = sm_resolution::low;
    source.shadow_params.type = sm_impl::vsm;
    check(limit_light(source, limits).shadow_params.type == sm_impl::pcf, "VSM filters with PCF without soft shadows");
    source.shadow_params.type = sm_impl::hard;
    const auto kept = limit_light(source, limits);
    check(kept.shadow_params.resolution == sm_resolution::low && kept.shadow_params.type == sm_impl::hard,
                 "a smaller shadow map and hard shadows are kept");
    source.directional_shadow_params.num_splits = 3;
    check(limit_light(source, feature_limits{}).directional_shadow_params.num_splits == 3, "the default limits keep a light's cascades");
    limits.max_shadow_cascades = 2;
    check(limit_light(source, limits).directional_shadow_params.num_splits == 2, "more cascades than allowed are capped");
    limits.max_shadow_cascades = 4;
    check(limit_light(source, limits).directional_shadow_params.num_splits == 3, "fewer cascades than allowed are kept");
}

void test_shadow_filtering_limit()
{
    std::printf("test_shadow_filtering_limit\n");
    light source;
    source.type = light_type::spot;
    source.shadow_params.type = sm_impl::pcf;
    source.shadow_params.pcf.x_offset = 2.0f;
    source.shadow_params.pcf.y_offset = 0.5f;
    check(get_pcf_kernel(source) == pcf_kernel::tent7, "a PCF offset of 2 filters a spot light with the 7x7 tent");
    check(limit_light(source, feature_limits{}).shadow_params.pcf.x_offset == 2.0f,
                 "the default limits keep every PCF width");
    feature_limits limits;
    limits.max_shadow_filtering = shadow_filtering_limit::pcf_3x3;
    const auto narrow = limit_light(source, limits);
    check(narrow.shadow_params.type == sm_impl::pcf && get_pcf_kernel(narrow) == pcf_kernel::tent3,
                 "under PCF 3x3 a wider PCF light filters with the 3x3 tent");
    check(narrow.shadow_params.pcf.y_offset == 0.5f, "a PCF offset already narrow enough is kept");
    source.type = light_type::point;
    source.shadow_params.pcf.x_offset = 8.0f;
    check(get_pcf_kernel(limit_light(source, limits)) == pcf_kernel::tent3,
                 "a point light's faces narrow to the 3x3 tent too");
    source.shadow_params.type = sm_impl::pcss;
    check(limit_light(source, limits).shadow_params.type == sm_impl::pcf, "under PCF 3x3 soft shadows filter with PCF");
    limits.max_shadow_filtering = shadow_filtering_limit::hard;
    for(const auto filter : {sm_impl::pcf, sm_impl::pcss, sm_impl::vsm, sm_impl::esm, sm_impl::hard})
    {
        source.shadow_params.type = filter;
        check(limit_light(source, limits).shadow_params.type == sm_impl::hard, "under hard every light renders hard shadows");
    }
    feature_limits other;
    other.max_shadow_filtering = shadow_filtering_limit::pcf_3x3;
    check(intersect_feature_limits(feature_limits{}, other).max_shadow_filtering == shadow_filtering_limit::pcf_3x3,
                 "two limits keep the narrower shadow filtering");
}

void test_settings_round_trip()
{
    std::printf("test_settings_round_trip\n");
    settings saved;
    saved.graphics.render_scale = 0.75f;
    saved.graphics.limits.bloom = false;
    saved.graphics.limits.volumetric_fog = false;
    saved.graphics.limits.max_anti_aliasing = anti_aliasing_limit::fxaa;
    saved.graphics.limits.max_shadow_resolution = sm_resolution::medium;
    saved.graphics.limits.max_shadow_filtering = shadow_filtering_limit::pcf_3x3;
    saved.graphics.limits.subsurface = false;
    saved.graphics.limits.volumetric_clouds = false;
    saved.graphics.limits.detailed_indirect_lighting = false;
    saved.graphics.limits.full_precision_light_targets = false;
    saved.graphics.limits.max_bloom_mips = 5;
    saved.graphics.limits.per_frame_exposure_metering = false;
    saved.graphics.limits.max_shadow_cascades = 2;
    const auto document = serialization::save_to_document(saved, LIMITS_DOCUMENT_ROOT);
    settings loaded;
    check(serialization::load_from_document(document, LIMITS_DOCUMENT_ROOT, loaded), "a settings document loads");
    check(loaded.graphics.render_scale == 0.75f && loaded.graphics.limits == saved.graphics.limits,
                 "the render scale and the limits round trip");
}

void test_render_size()
{
    std::printf("test_render_size\n");
    const usize32_t view{1920, 1080};
    check(get_render_size(view, 1.0f) == view && get_render_size(view, 2.0f) == view, "a scale of 1 or more keeps the size");
    check(get_render_size(view, 0.5f) == usize32_t(960, 540), "half the scale is half the size");
    check(get_render_size(view, 0.1f) == get_render_size(view, MIN_RENDER_SCALE), "the scale stops at its minimum");
    check(get_render_size(view, std::numeric_limits<float>::quiet_NaN()) == view, "a scale that is not a number keeps the size");
    check(get_render_size({0, 0}, 0.5f) == usize32_t(0, 0), "an empty view stays empty");
    check(get_render_size({1, 3}, MIN_RENDER_SCALE) == usize32_t(1, 1), "a side never goes below one texel");
    check(get_render_size({1279, 719}, 0.5f) == usize32_t(640, 360), "odd sizes round to the nearest texel");
}

void test_view_render_size()
{
    std::printf("test_view_render_size\n");
    const usize32_t viewport{900, 506};
    const usize32_t full_hd{1920, 1080};
    check(get_view_render_size(viewport, full_hd, 1.0f) == full_hd, "a resolution larger than the viewport renders whole");
    check(get_view_render_size(viewport, full_hd, 0.5f) == usize32_t(960, 540), "the scale applies to the resolution");
    check(get_view_render_size(viewport, {}, 0.5f) == usize32_t(450, 253), "without a resolution the viewport is scaled");
    check(get_view_render_size(viewport, {1920, 0}, 1.0f) == viewport, "a resolution with an empty side is none");
    check(get_view_render_size(viewport, {640, 360}, 1.0f) == usize32_t(640, 360), "a smaller resolution renders whole");
}

void run_feature_limits_suite(rtti::context& /*ctx*/)
{
    RUN_CASE(test_default_limits());
    RUN_CASE(test_effects_turned_off());
    RUN_CASE(test_anti_aliasing_limits());
    RUN_CASE(test_limited_lights());
    RUN_CASE(test_shadow_filtering_limit());
    RUN_CASE(test_settings_round_trip());
    RUN_CASE(test_render_size());
    RUN_CASE(test_view_render_size());
}

} // namespace

REGISTER_TEST_SUITE("feature limits", run_feature_limits_suite)
