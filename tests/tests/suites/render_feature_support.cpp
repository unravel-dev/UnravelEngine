/*
 * Validation suite for GPU feature support: which rendering features a device runs, from its capabilities.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "render feature support"
 *
 * What it pins, on synthetic devices:
 *   - a device with every capability runs every feature, and its limits are the defaults;
 *   - a D3D11 feature level 11_0 device (8 compute bindings, no typed loads) loses the features that bind more;
 *   - a Vulkan device without multi-draw indirect (the S21) loses only the GPU particle culling;
 *   - a device without Vulkan's extended storage formats loses the features that write them;
 *   - a device without compute loses every compute feature and fails the minimum requirements;
 *   - a device without vertex-stage storage buffers (OpenGL ES) draws skinned meshes one by one;
 *   - a feature whose dependency is unavailable is unavailable, and says why;
 *   - the --gfx-disable names parse into the mask they describe;
 *   - every requirement entry has a name and a fallback sentence.
 */

#include "../tests.h"

#include <engine/rendering/render_feature_support.h>
#include <graphics/device_caps.h>

#include <cstdio>
#include <string>

using namespace unravel;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

constexpr std::uint32_t ALL_FORMAT_CAPS =
    BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_3D | BGFX_CAPS_FORMAT_TEXTURE_CUBE |
    BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ | BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE | BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER |
    BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA | BGFX_CAPS_FORMAT_TEXTURE_MSAA | BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN;

constexpr std::uint8_t ALL_COMPUTE_BINDINGS = 16;
constexpr std::uint8_t FEATURE_LEVEL_11_0_BINDINGS = 8;
constexpr std::uint8_t DESKTOP_ATTACHMENTS = 8;

auto make_desktop_caps() -> gfx::device_caps
{
    gfx::device_caps caps;
    caps.renderer = bgfx::RendererType::Direct3D12;
    caps.has_compute = true;
    caps.has_draw_indirect = true;
    caps.has_dispatch_indirect = true;
    caps.has_image_read_write = true;
    caps.has_blend_independent = true;
    caps.has_vertex_storage_buffers = true;
    caps.max_compute_bindings = ALL_COMPUTE_BINDINGS;
    caps.max_texture_size = 16384;
    caps.max_frame_buffer_attachments = DESKTOP_ATTACHMENTS;
    caps.formats.fill(ALL_FORMAT_CAPS);
    return caps;
}

auto make_feature_level_11_0_caps() -> gfx::device_caps
{
    gfx::device_caps_mask mask;
    mask.drops_image_read_write = true;
    mask.drops_typed_image_loads = true;
    mask.max_compute_bindings = FEATURE_LEVEL_11_0_BINDINGS;
    auto caps = gfx::apply_device_caps_mask(make_desktop_caps(), mask);
    caps.renderer = bgfx::RendererType::Direct3D11;
    return caps;
}

auto make_s21_caps() -> gfx::device_caps
{
    auto caps = make_desktop_caps();
    caps.renderer = bgfx::RendererType::Vulkan;
    caps.has_draw_indirect = false;
    return caps;
}

auto describe(render_feature feature) -> std::string
{
    return get_render_feature_requirements(feature).name;
}

void check_available(const render_feature_support& support, render_feature feature, bool expected, const char* device)
{
    const auto& status = support.get_status(feature);
    check(status.is_available == expected,
          std::string(device) + ": " + describe(feature) + (expected ? " runs" : " does not run") +
              (status.reason.empty() ? std::string() : " (" + status.reason + ")"));
}

void test_desktop_runs_everything()
{
    std::printf("a device with every capability runs every feature\n");
    const auto caps = make_desktop_caps();
    const auto support = render_feature_support::resolve(caps);
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        check_available(support, static_cast<render_feature>(index), true, "desktop");
    }
    check(support.get_device_limits() == feature_limits{}, "desktop: the device limits are the defaults");
    check(support.describe_unavailable().empty(), "desktop: nothing is reported unavailable");
    check(gfx::get_missing_minimum_requirements(caps).empty(), "desktop: the minimum requirements are met");
}

void test_feature_level_11_0()
{
    std::printf("a D3D11 feature level 11_0 device loses what binds more than 8 or reads typed images\n");
    const auto support = render_feature_support::resolve(make_feature_level_11_0_caps());
    const char* device = "feature level 11_0";
    check_available(support, render_feature::global_illumination, false, device);
    check_available(support, render_feature::gi_page_feedback, false, device);
    check_available(support, render_feature::volumetric_fog, false, device);
    check_available(support, render_feature::temporal_probe, false, device);
    check_available(support, render_feature::auto_exposure, true, device);
    check_available(support, render_feature::gpu_pre_exposure, true, device);
    check_available(support, render_feature::hiz, true, device);
    check_available(support, render_feature::screen_space_reflections, true, device);
    check_available(support, render_feature::screen_space_indirect_lighting, true, device);
    check_available(support, render_feature::gtao, true, device);
    check_available(support, render_feature::assao, true, device);
    check_available(support, render_feature::local_exposure, true, device);
    check_available(support, render_feature::irradiance_sh, true, device);
    check_available(support, render_feature::reflection_probe_prefilter, true, device);
    check_available(support, render_feature::gpu_particles, true, device);
    check_available(support, render_feature::gpu_particles_indirect, true, device);
    const auto limits = support.get_device_limits();
    check(!limits.global_illumination && !limits.volumetric_fog, "feature level 11_0: GI and volumetric fog are off");
    check(limits.ambient_occlusion && limits.screen_space_reflections, "feature level 11_0: AO and SSR stay on");
    const auto& gi = support.get_status(render_feature::global_illumination);
    check(gi.reason.find("binds 8 resources") != std::string::npos, "feature level 11_0: GI names the binding count");
}

void test_vulkan_without_multi_draw()
{
    std::printf("a Vulkan device without multi-draw indirect loses only the GPU particle culling\n");
    const auto support = render_feature_support::resolve(make_s21_caps());
    const char* device = "S21";
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        check_available(support, feature, feature != render_feature::gpu_particles_indirect, device);
    }
}

void test_extended_storage_formats()
{
    std::printf("a device without extended storage formats loses the features that write them\n");
    gfx::device_caps_mask mask;
    mask.drops_extended_storage_formats = true;
    const auto support = render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask));
    const char* device = "no extended formats";
    check_available(support, render_feature::global_illumination, false, device);
    check_available(support, render_feature::assao, false, device);
    check_available(support, render_feature::screen_space_indirect_lighting, false, device);
    check_available(support, render_feature::auto_exposure, false, device);
    check_available(support, render_feature::local_exposure, false, device);
    check_available(support, render_feature::gtao, true, device);
    check_available(support, render_feature::screen_space_reflections, true, device);
    check_available(support, render_feature::volumetric_fog, true, device);
    check_available(support, render_feature::gpu_pre_exposure, true, device);
    check(support.get_device_limits().ambient_occlusion, "no extended formats: GTAO keeps the ambient occlusion on");
}

void test_no_compute()
{
    std::printf("a device without compute loses every compute feature\n");
    gfx::device_caps_mask mask;
    mask.drops_compute = true;
    const auto caps = gfx::apply_device_caps_mask(make_desktop_caps(), mask);
    const auto support = render_feature_support::resolve(caps);
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        const bool needs_compute = get_render_feature_requirements(feature).needs_compute;
        check_available(support, feature, !needs_compute, "no compute");
    }
    check(!caps.has_dispatch_indirect, "no compute: no indirect dispatch either");
    const auto limits = support.get_device_limits();
    check(!limits.global_illumination && !limits.screen_space_reflections && !limits.ambient_occlusion,
          "no compute: GI, SSR and AO are off");
    check(!gfx::get_missing_minimum_requirements(caps).empty(), "no compute: the minimum requirements fail");
    check(hpp::string_view(support.get_fallback(render_feature::gtao)) ==
              get_render_feature_requirements(render_feature::assao).fallback,
          "no compute: GTAO's fallback is ASSAO's, which does not run either");
}

void test_no_vertex_storage_buffers()
{
    std::printf("a device without vertex storage buffers loses only the skinned mesh batching\n");
    gfx::device_caps_mask mask;
    mask.drops_vertex_storage_buffers = true;
    const auto support = render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask));
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        check_available(support, feature, feature != render_feature::skinned_mesh_batching, "no vertex storage");
    }
}

void test_dependency_reason()
{
    std::printf("a feature whose dependency is unavailable says why\n");
    gfx::device_caps_mask mask;
    mask.dropped_format_caps.emplace_back(bgfx::TextureFormat::R32F, BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE);
    const auto support = render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask));
    check_available(support, render_feature::hiz, false, "no R32F images");
    check_available(support, render_feature::screen_space_reflections, false, "no R32F images");
    const auto& reason = support.get_status(render_feature::screen_space_reflections).reason;
    check(reason.rfind("It depends on Hierarchical depth.", 0) == 0, "no R32F images: SSR names hierarchical depth");
    check(reason.find("R32F") != std::string::npos, "no R32F images: SSR carries the format the dependency lacks");
}

void test_mask_parsing()
{
    std::printf("the --gfx-disable names parse into their mask\n");
    std::vector<std::string> unknown;
    const auto mask = gfx::parse_device_caps_mask(
        "Compute, draw_indirect,bindings8,format:rg11b10f:write,extended_storage_formats,vertex_storage_buffers,bogus",
        unknown);
    check(mask.drops_compute && mask.drops_draw_indirect && mask.drops_extended_storage_formats &&
              mask.drops_vertex_storage_buffers,
          "the flag names set their drops");
    check(mask.max_compute_bindings == FEATURE_LEVEL_11_0_BINDINGS, "bindings8 sets eight compute bindings");
    check(mask.dropped_format_caps.size() == 1 && mask.dropped_format_caps[0].first == bgfx::TextureFormat::RG11B10F &&
              mask.dropped_format_caps[0].second == BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE,
          "format:rg11b10f:write drops RG11B10F image writes");
    check(unknown.size() == 1 && unknown[0] == "bogus", "an unknown name is reported");
    check(gfx::parse_device_caps_mask("", unknown).is_empty(), "an empty value masks nothing");
}

void test_limits_intersection()
{
    std::printf("two limits intersect into the stricter one\n");
    feature_limits project;
    project.bloom = false;
    project.max_anti_aliasing = anti_aliasing_limit::fxaa;
    feature_limits device;
    device.global_illumination = false;
    device.max_shadow_resolution = sm_resolution::medium;
    const auto limits = intersect_feature_limits(project, device);
    check(!limits.bloom && !limits.global_illumination, "a feature either side turns off stays off");
    check(limits.max_anti_aliasing == anti_aliasing_limit::fxaa, "the lower anti-aliasing cap wins");
    check(limits.max_shadow_resolution == sm_resolution::medium, "the lower shadow resolution wins");
    check(limits.screen_space_reflections && limits.soft_shadows, "a feature both allow stays on");
}

void test_requirement_entries()
{
    std::printf("every requirement entry has a name and a fallback sentence\n");
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto& needs = get_render_feature_requirements(static_cast<render_feature>(index));
        const std::string name = needs.name != nullptr ? needs.name : "";
        const std::string fallback = needs.fallback != nullptr ? needs.fallback : "";
        check(!name.empty(), "feature " + std::to_string(index) + " has a name");
        check(!fallback.empty() && fallback.back() == '.', name + " has a fallback sentence");
        check(needs.compute_bindings <= ALL_COMPUTE_BINDINGS, name + " binds no more than any backend offers");
    }
}

auto run_render_feature_support_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_desktop_runs_everything();
    test_feature_level_11_0();
    test_vulkan_without_multi_draw();
    test_extended_storage_formats();
    test_no_compute();
    test_no_vertex_storage_buffers();
    test_dependency_reason();
    test_mask_parsing();
    test_limits_intersection();
    test_requirement_entries();
    std::printf("\nrender feature support: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("render feature support", run_render_feature_support_suite)
