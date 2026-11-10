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
 *   - WebGPU reads and writes only texture-formats-tier2's list (never rg11b10f), and GI asks for its WebGPU format;
 *   - soft shadows need 32-bit float filtering: never on WebGPU or OpenGL ES, on Vulkan where the format builds mips;
 *   - GI needs more storage images per shader stage than WebGPU's core 4;
 *   - the engine compute shaders of a feature the device does not run are left empty (the longest shader prefix
 *     decides the feature), every other shader is created;
 *   - a device without compute loses every compute feature and fails the minimum requirements;
 *   - a device without vertex-stage storage buffers (OpenGL ES) reads bone palettes and particle data from textures,
 *     its particles culled and sorted on the CPU;
 *   - where the particle storage buffers are unavailable (no compute, or no vertex storage buffers) the particle
 *     programs draw with the data texture variants vs_particle.eshader lists;
 *   - a WebGL 2 device (OpenGL ES 3.0) meets the minimum requirements without compute, loses every compute feature
 *     and creates no engine compute shader, not even one of no feature;
 *   - a feature whose dependency is unavailable is unavailable, and says why;
 *   - the --gfx-disable names parse into the mask they describe;
 *   - every requirement entry has a name and a fallback sentence.
 */

#include "../tests.h"

#include <asset_compiler/eshader_file.h>
#include <engine/rendering/particles/ps/particle_gpu.h>
#include <engine/rendering/shader_buffer.h>
#include <engine/rendering/render_feature_support.h>
#include <filesystem/filesystem.h>
#include <graphics/device_caps.h>
#include <graphics/shader.h>

#include <engine/rendering/renderer.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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
/// The storage images per shader stage every WebGPU device grants.
constexpr std::uint8_t WEBGPU_CORE_STORAGE_TEXTURES = 4;

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
    caps.has_depth_blit = true;
    caps.max_compute_bindings = ALL_COMPUTE_BINDINGS;
    caps.max_storage_textures_per_stage = ALL_COMPUTE_BINDINGS;
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
    check_available(support, render_feature::particle_storage_buffers, true, device);
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

void test_webgpu_read_write_formats()
{
    std::printf("WebGPU reads and writes only its own list of formats through one binding\n");
    auto caps = make_desktop_caps();
    caps.renderer = bgfx::RendererType::WebGPU;
    check(!caps.can_read_write_image(bgfx::TextureFormat::RG11B10F), "WebGPU: rg11b10f is never read-write");
    check(caps.can_read_write_image(bgfx::TextureFormat::RGBA16F) && caps.can_read_write_image(bgfx::TextureFormat::R32F),
          "WebGPU with texture-formats-tier2: rgba16f and r32f are read-write");
    const auto support = render_feature_support::resolve(caps);
    check_available(support, render_feature::global_illumination, true, "WebGPU with tier2");
    // Without texture-formats-tier2 bgfx reports image reads for the 32-bit single-channel formats only.
    gfx::device_caps_mask core_mask;
    core_mask.drops_typed_image_loads = true;
    caps = gfx::apply_device_caps_mask(caps, core_mask);
    check(!caps.can_read_write_image(bgfx::TextureFormat::RGBA16F) && caps.can_read_write_image(bgfx::TextureFormat::R32F),
          "WebGPU without tier2: only the 32-bit single-channel formats are read-write");
    const auto core_support = render_feature_support::resolve(caps);
    check_available(core_support, render_feature::global_illumination, false, "WebGPU without tier2");
    check(core_support.get_status(render_feature::global_illumination).reason.find("RGBA16F") != std::string::npos,
          "WebGPU without tier2: GI names the format it binds there");
    check(make_desktop_caps().can_read_write_image(bgfx::TextureFormat::RG11B10F), "Direct3D 12: rg11b10f is read-write");
}

void test_float_filtering()
{
    std::printf("soft shadows run only where 32-bit floats filter\n");
    auto caps = make_desktop_caps();
    check(caps.can_filter(bgfx::TextureFormat::RG32F), "Direct3D 12 filters rg32f");
    check_available(render_feature_support::resolve(caps), render_feature::soft_shadows, true, "Direct3D 12");
    caps.renderer = bgfx::RendererType::WebGPU;
    check(!caps.can_filter(bgfx::TextureFormat::R32F) && caps.can_filter(bgfx::TextureFormat::RGBA16F),
          "WebGPU filters no 32-bit float format, every other one");
    const auto webgpu_support = render_feature_support::resolve(caps);
    check_available(webgpu_support, render_feature::soft_shadows, false, "WebGPU");
    caps.renderer = bgfx::RendererType::OpenGLES;
    check(!caps.can_filter(bgfx::TextureFormat::RG32F), "OpenGL ES: rg32f filters only with an extension bgfx does not report");
    check(webgpu_support.get_status(render_feature::soft_shadows).reason.find("filter") != std::string::npos,
          "WebGPU: soft shadows say they need filtering");
    auto vulkan = make_s21_caps();
    check_available(render_feature_support::resolve(vulkan), render_feature::soft_shadows, true, "Vulkan with filtering");
    gfx::device_caps_mask mask;
    mask.dropped_format_caps.emplace_back(bgfx::TextureFormat::RG32F, BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN);
    vulkan = gfx::apply_device_caps_mask(vulkan, mask);
    check_available(render_feature_support::resolve(vulkan), render_feature::soft_shadows, false, "Vulkan without rg32f filtering");
}

void test_storage_textures_per_stage()
{
    std::printf("GI runs only where a shader stage binds its storage images\n");
    auto caps = make_desktop_caps();
    caps.renderer = bgfx::RendererType::WebGPU;
    caps.max_storage_textures_per_stage = WEBGPU_CORE_STORAGE_TEXTURES;
    const auto support = render_feature_support::resolve(caps);
    check_available(support, render_feature::global_illumination, false, "WebGPU with the core 4 storage images");
    check(support.get_status(render_feature::global_illumination).reason.find("storage images") != std::string::npos,
          "WebGPU with 4: GI says it needs more storage images per stage");
    caps.max_storage_textures_per_stage = get_render_feature_requirements(render_feature::global_illumination).storage_textures_per_stage;
    check_available(render_feature_support::resolve(caps), render_feature::global_illumination, true, "WebGPU with GI's storage images");
    gfx::device_caps_mask mask;
    mask.max_compute_bindings = FEATURE_LEVEL_11_0_BINDINGS;
    check(gfx::apply_device_caps_mask(make_desktop_caps(), mask).max_storage_textures_per_stage == FEATURE_LEVEL_11_0_BINDINGS,
          "fewer simulated compute bindings bind fewer storage images too");
}

void test_shaders_of_unavailable_features()
{
    std::printf("the compute shaders of a feature the device does not run are left empty, and only those\n");
    check(find_shader_render_feature("gi/cs_gi_card_copy") == render_feature::global_illumination,
          "gi/cs_gi_card_copy belongs to GI");
    check(find_shader_render_feature("gi/cs_gi_feedback_copy") == render_feature::gi_page_feedback,
          "the longest prefix decides: gi/cs_gi_feedback_copy belongs to the page feedback");
    check(find_shader_render_feature("ssr/cs_hiz_downsample") == render_feature::hiz, "ssr/cs_hiz_downsample belongs to Hi-Z");
    check(find_shader_render_feature("cs_clear_uints") == render_feature::count, "cs_clear_uints belongs to no feature");
    // A WebGPU device without texture-formats-tier1 has no 16-bit normalized formats, which GI writes.
    auto caps = make_desktop_caps();
    caps.renderer = bgfx::RendererType::WebGPU;
    caps.formats[bgfx::TextureFormat::R16] = 0;
    caps.formats[bgfx::TextureFormat::RGBA16] = 0;
    const auto support = render_feature_support::resolve(caps);
    const char* device = "WebGPU without tier1";
    check_available(support, render_feature::global_illumination, false, device);
    check(!support.runs_shader("engine:/data/shaders/gi/cs_gi_card_copy.eshader"),
          std::string(device) + ": GI's compute shaders stay empty");
    check(!support.runs_shader("engine:/data/shaders/gi/cs_gi_feedback_copy.eshader"),
          std::string(device) + ": so do those of the features that depend on GI");
    check(support.runs_shader("engine:/data/shaders/gi/fs_gi_scene_debug.eshader"),
          std::string(device) + ": GI's fragment shaders are created");
    check(support.runs_shader("engine:/data/shaders/ssr/cs_hiz_downsample.eshader"),
          std::string(device) + ": the features it runs keep their compute shaders");
    check(support.runs_shader("engine:/data/shaders/cs_clear_uints.eshader"),
          std::string(device) + ": a compute shader of no feature is created");
    check(support.runs_shader("app:/data/shaders/gi/cs_gi_card_copy.eshader"),
          std::string(device) + ": a project's shaders are created");
    check(render_feature_support{}.runs_shader("engine:/data/shaders/gi/cs_gi_card_copy.eshader"),
          "without a renderer every shader is created");
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
    std::printf("a device without vertex storage buffers reads bone palettes and particles from textures\n");
    gfx::device_caps_mask mask;
    mask.drops_vertex_storage_buffers = true;
    const auto support = render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask));
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        const bool needs_vertex_storage = feature == render_feature::vertex_storage_buffers ||
                                          feature == render_feature::particle_storage_buffers ||
                                          feature == render_feature::gpu_particles_indirect;
        check_available(support, feature, !needs_vertex_storage, "no vertex storage");
    }
    check(support.get_status(render_feature::particle_storage_buffers).reason.rfind("It depends on Vertex storage buffers.", 0) == 0,
          "no vertex storage: the particle storage buffers name the vertex storage buffers they depend on");
    check(support.get_status(render_feature::gpu_particles_indirect).reason.rfind("It depends on Particle storage buffers.", 0) == 0,
          "no vertex storage: the GPU particle culling names the storage buffers it depends on");
}

/// A WebGL 2 device: OpenGL ES 3.0, so no compute, no indirect draws, no images, no vertex storage buffers and no
/// depth blits.
auto make_webgl2_caps() -> gfx::device_caps
{
    auto caps = make_desktop_caps();
    caps.renderer = bgfx::RendererType::OpenGLES;
    caps.has_compute = false;
    caps.has_draw_indirect = false;
    caps.has_dispatch_indirect = false;
    caps.has_image_read_write = false;
    caps.has_vertex_storage_buffers = false;
    caps.has_depth_blit = false;
    caps.max_compute_bindings = 0;
    caps.max_storage_textures_per_stage = 0;
    return caps;
}

void test_webgl2_device()
{
    std::printf("a WebGL 2 device starts without compute and runs no compute shader\n");
    const auto caps = make_webgl2_caps();
    check(gfx::get_missing_minimum_requirements(caps).empty(), "WebGL 2: the minimum requirements are met without compute");
    check(gfx::is_compute_required(bgfx::RendererType::WebGPU) && !gfx::is_compute_required(bgfx::RendererType::OpenGLES),
          "compute is a minimum on WebGPU, not on OpenGL ES");
    const auto support = render_feature_support::resolve(caps);
    for(std::size_t index = 0; index < static_cast<std::size_t>(render_feature::count); ++index)
    {
        const auto feature = static_cast<render_feature>(index);
        if(get_render_feature_requirements(feature).needs_compute)
        {
            check_available(support, feature, false, "WebGL 2");
        }
    }
    check(!support.runs_shader("engine:/data/shaders/cs_clear_uints.eshader"),
          "WebGL 2: a compute shader of no feature is left empty too");
    check(!support.runs_shader("engine:/data/shaders/particles/cs_particle_scatter.eshader"),
          "WebGL 2: the particle scatter is left empty");
    check(support.runs_shader("engine:/data/shaders/particles/vs_particle.eshader"),
          "WebGL 2: the particle vertex shader is created");
    check(render_feature_support::resolve(make_desktop_caps()).runs_shader("engine:/data/shaders/cs_clear_uints.eshader"),
          "with compute, a compute shader of no feature is created");
    bgfx::Caps backend{};
    backend.rendererType = bgfx::RendererType::OpenGLES;
    check(!gfx::make_device_caps(backend).has_depth_blit, "OpenGL ES copies depth with a draw, not a blit");
    backend.rendererType = bgfx::RendererType::WebGPU;
    check(gfx::make_device_caps(backend).has_depth_blit, "WebGPU blits depth");
}

/// shader_buffer.sh counts the texture rows as shader_buffer.h does.
void check_texture_width()
{
#if defined(ENGINE_DATA_DIR)
    std::ifstream stream(fs::path(ENGINE_DATA_DIR) / "data" / "shaders" / "shader_buffer.sh");
    std::stringstream text;
    text << stream.rdbuf();
    const std::string source = text.str();
    std::smatch width;
    const bool is_found = std::regex_search(source, width, std::regex(R"(#define SHADER_BUFFER_TEXTURE_WIDTH (\d+)u)"));
    check(is_found && std::stoul(width[1].str()) == SHADER_BUFFER_TEXTURE_WIDTH,
          "shader_buffer.sh has SHADER_BUFFER_TEXTURE_WIDTH texels a row, as shader_buffer.h");
#endif
}

void test_shader_buffer_storage()
{
    std::printf("bone palettes and particle data live in textures where vertex shaders read no storage buffers\n");
    const render_feature_support saved = get_render_feature_support();
    set_render_feature_support(render_feature_support::resolve(make_desktop_caps()));
    check(get_vertex_shader_buffer_storage() == shader_buffer_storage::buffer, "desktop: palettes live in buffers");
    check(ps_soa::get_particle_data_storage() == shader_buffer_storage::buffer, "desktop: particles read storage buffers");
    gfx::device_caps_mask mask;
    mask.drops_compute = true;
    set_render_feature_support(render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask)));
    check(get_vertex_shader_buffer_storage() == shader_buffer_storage::texture, "no compute: palettes live in textures");
    check(ps_soa::get_particle_data_storage() == shader_buffer_storage::texture, "no compute: particles read textures");
    mask = {};
    mask.drops_vertex_storage_buffers = true;
    set_render_feature_support(render_feature_support::resolve(gfx::apply_device_caps_mask(make_desktop_caps(), mask)));
    check(get_vertex_shader_buffer_storage() == shader_buffer_storage::texture,
          "no vertex storage: palettes live in textures");
    check(ps_soa::get_particle_data_storage() == shader_buffer_storage::texture,
          "no vertex storage: particles read textures");
    check(get_shader_buffer_variant("skinned", shader_buffer_storage::texture) == "skinned_textures" &&
              get_shader_buffer_variant(gfx::shader::DEFAULT_VARIANT, shader_buffer_storage::texture) == "textures" &&
              get_shader_buffer_variant("lit", shader_buffer_storage::buffer) == "lit",
          "a variant's texture twin is named after it");
    set_render_feature_support(saved);
    check_texture_width();
}

/// The script names of render_feature's values (RenderFeature in Graphics.cs), by the engine's names.
constexpr std::array<std::pair<render_feature, const char*>, static_cast<std::size_t>(render_feature::count)>
    SCRIPT_FEATURE_NAMES = {{
        {render_feature::global_illumination, "GlobalIllumination"},
        {render_feature::gi_page_feedback, "GlobalIlluminationPageFeedback"},
        {render_feature::hiz, "HierarchicalDepth"},
        {render_feature::screen_space_reflections, "ScreenSpaceReflections"},
        {render_feature::screen_space_indirect_lighting, "ScreenSpaceIndirectLighting"},
        {render_feature::gtao, "GroundTruthAmbientOcclusion"},
        {render_feature::assao, "AdaptiveScreenSpaceAmbientOcclusion"},
        {render_feature::auto_exposure, "AutoExposure"},
        {render_feature::local_exposure, "LocalExposure"},
        {render_feature::gpu_pre_exposure, "GpuPreExposure"},
        {render_feature::irradiance_sh, "SkyIrradiance"},
        {render_feature::reflection_probe_prefilter, "ReflectionProbeFiltering"},
        {render_feature::volumetric_fog, "VolumetricFog"},
        {render_feature::cloud_shadow, "CloudShadows"},
        {render_feature::vertex_storage_buffers, "VertexStorageBuffers"},
        {render_feature::particle_storage_buffers, "ParticleStorageBuffers"},
        {render_feature::gpu_particles_indirect, "GpuParticleCulling"},
        {render_feature::particle_reactive_mask, "ParticleReactiveMask"},
        {render_feature::soft_shadows, "SoftShadows"},
        {render_feature::ui_msaa, "UIMultisampling"},
        {render_feature::temporal_probe, "TemporalStabilityProbe"},
    }};

/// The members of C# enum @p name in @p source by their values.
auto read_script_enum(const std::string& source, const std::string& name) -> std::map<std::string, int>
{
    std::map<std::string, int> members;
    std::smatch body;
    if(!std::regex_search(source, body, std::regex("enum " + name + R"(\s*\{([^}]*)\})")))
    {
        return members;
    }
    // The members, the doc comments left out: they may hold "name=value" text.
    const std::string code = std::regex_replace(body[1].str(), std::regex(R"(///[^\n]*)"), "");
    const std::regex member(R"((\w+)\s*=\s*(\d+))");
    for(std::sregex_iterator it(code.begin(), code.end(), member), end; it != end; ++it)
    {
        members[(*it)[1].str()] = std::stoi((*it)[2].str());
    }
    return members;
}

/// "InsecurePage" for "insecure_page": the script name of a page's fallback reason.
auto make_script_name(const std::string& page_name) -> std::string
{
    std::string name;
    bool is_word_start = true;
    for(const char letter : page_name)
    {
        if(letter == '_')
        {
            is_word_start = true;
            continue;
        }
        name += is_word_start ? static_cast<char>(std::toupper(static_cast<unsigned char>(letter))) : letter;
        is_word_start = false;
    }
    return name;
}

void test_script_enums_mirror_the_engine()
{
    std::printf("Graphics.cs mirrors render_feature and graphics_fallback_reason value by value\n");
#if defined(ENGINE_DATA_DIR)
    std::ifstream stream(fs::path(ENGINE_DATA_DIR) / "data" / "scripts" / "rendering" / "Graphics.cs");
    std::stringstream text;
    text << stream.rdbuf();
    const std::string source = text.str();
    std::map<std::string, int> expected_features;
    for(const auto& [feature, name] : SCRIPT_FEATURE_NAMES)
    {
        expected_features[name] = static_cast<int>(feature);
    }
    check(read_script_enum(source, "RenderFeature") == expected_features,
          "RenderFeature in Graphics.cs holds every render_feature at its value");
    const auto reasons = read_script_enum(source, "GraphicsFallbackReason");
    bool is_mirrored = reasons.size() == static_cast<std::size_t>(graphics_fallback_reason::count) &&
                       reasons.contains("None") && reasons.at("None") == static_cast<int>(graphics_fallback_reason::none);
    for(const auto& [name, value] : reasons)
    {
        bool is_found = name == "None";
        for(int index = 1; index < static_cast<int>(graphics_fallback_reason::count) && !is_found; ++index)
        {
            const auto reason = static_cast<graphics_fallback_reason>(index);
            const std::string page_name = get_graphics_fallback_reason_name(reason);
            is_found = make_script_name(page_name) == name && value == index;
        }
        is_mirrored = is_mirrored && is_found;
    }
    check(is_mirrored, "GraphicsFallbackReason in Graphics.cs holds every graphics_fallback_reason at its value");
#endif
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

void test_render_target_sample_counts()
{
    auto caps = make_desktop_caps();
    caps.formats[bgfx::TextureFormat::RGBA8] &= ~BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA_X16;
    check(caps.can_render_to(bgfx::TextureFormat::RGBA8), "no 16 samples: a plain target renders");
    check(caps.can_render_to(bgfx::TextureFormat::RGBA8, 4), "no 16 samples: 4 samples render");
    check(caps.can_render_to(bgfx::TextureFormat::RGBA8, 8), "no 16 samples: 8 samples render");
    check(!caps.can_render_to(bgfx::TextureFormat::RGBA8, 16), "no 16 samples: 16 samples do not render");
}

void test_mask_parsing()
{
    std::printf("the --gfx-disable names parse into their mask\n");
    std::vector<std::string> unknown;
    const auto mask = gfx::parse_device_caps_mask(
        "Compute, draw_indirect,bindings8,format:rg11b10f:write,extended_storage_formats,vertex_storage_buffers,"
        "depth_blit,bogus",
        unknown);
    check(mask.drops_compute && mask.drops_draw_indirect && mask.drops_extended_storage_formats &&
              mask.drops_vertex_storage_buffers && mask.drops_depth_blit,
          "the flag names set their drops");
    check(!gfx::apply_device_caps_mask(make_desktop_caps(), mask).has_depth_blit, "depth_blit drops depth blits");
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
    project.full_precision_light_targets = false;
    device.per_frame_exposure_metering = false;
    project.max_bloom_mips = 5;
    device.max_bloom_mips = 7;
    project.max_shadow_cascades = 3;
    device.max_shadow_cascades = 2;
    const auto limits = intersect_feature_limits(project, device);
    check(!limits.bloom && !limits.global_illumination, "a feature either side turns off stays off");
    check(!limits.full_precision_light_targets && !limits.per_frame_exposure_metering, "so does full precision and per frame metering");
    check(limits.max_anti_aliasing == anti_aliasing_limit::fxaa, "the lower anti-aliasing cap wins");
    check(limits.max_shadow_resolution == sm_resolution::medium, "the lower shadow resolution wins");
    check(limits.max_bloom_mips == 5 && limits.max_shadow_cascades == 2, "the lower bloom mip and cascade caps win");
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
    test_webgpu_read_write_formats();
    test_float_filtering();
    test_storage_textures_per_stage();
    test_shaders_of_unavailable_features();
    test_no_compute();
    test_no_vertex_storage_buffers();
    test_shader_buffer_storage();
    test_script_enums_mirror_the_engine();
    test_webgl2_device();
    test_dependency_reason();
    test_render_target_sample_counts();
    test_mask_parsing();
    test_limits_intersection();
    test_requirement_entries();
    std::printf("\nrender feature support: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("render feature support", run_render_feature_support_suite)
