#include "render_feature_support.h"

#include <bimg/bimg.h>

#include <algorithm>

namespace unravel
{
namespace
{

constexpr std::size_t RENDER_FEATURE_COUNT = static_cast<std::size_t>(render_feature::count);

constexpr image_need GI_IMAGES[] = {
    {bgfx::TextureFormat::RGBA8},
    {bgfx::TextureFormat::RG8},
    {bgfx::TextureFormat::R8},
    {bgfx::TextureFormat::R16},
    {bgfx::TextureFormat::R32F},
    {bgfx::TextureFormat::R32U},
    {bgfx::TextureFormat::RGBA16F},
    {bgfx::TextureFormat::RGBA32F},
    {bgfx::TextureFormat::RG11B10F},
    {bgfx::TextureFormat::RG11B10F, gfx::texture_dimension::two_d, image_access::read_write},
    {bgfx::TextureFormat::R32F, gfx::texture_dimension::two_d, image_access::read_write},
    {bgfx::TextureFormat::R8, gfx::texture_dimension::three_d},
    {bgfx::TextureFormat::RGBA16, gfx::texture_dimension::three_d},
};
constexpr render_target_need GI_TARGETS[] = {{bgfx::TextureFormat::RGBA8},
                                             {bgfx::TextureFormat::RGBA16F},
                                             {bgfx::TextureFormat::D32F}};
constexpr const char* GI_SHADERS[] = {"gi/"};

constexpr image_need GI_PAGE_FEEDBACK_IMAGES[] = {{bgfx::TextureFormat::RG32U}};
constexpr render_feature GI_PAGE_FEEDBACK_DEPENDS[] = {render_feature::global_illumination};
constexpr const char* GI_PAGE_FEEDBACK_SHADERS[] = {"gi/cs_gi_feedback_copy"};

constexpr image_need HIZ_IMAGES[] = {
    {bgfx::TextureFormat::R32F},
    {bgfx::TextureFormat::R32F, gfx::texture_dimension::two_d, image_access::read_write},
};
constexpr const char* HIZ_SHADERS[] = {"ssr/cs_hiz_"};

constexpr image_need SSR_IMAGES[] = {{bgfx::TextureFormat::RGBA16F}};
constexpr render_target_need SSR_TARGETS[] = {{bgfx::TextureFormat::RGBA16F}, {bgfx::TextureFormat::R16F}};
constexpr render_feature SCREEN_TRACE_DEPENDS[] = {render_feature::hiz};
constexpr const char* SSR_SHADERS[] = {"ssr/"};

constexpr image_need SSIL_IMAGES[] = {{bgfx::TextureFormat::RGBA16F}, {bgfx::TextureFormat::R16F}};
constexpr render_target_need SSIL_TARGETS[] = {{bgfx::TextureFormat::RGBA16F}, {bgfx::TextureFormat::R16F}};
constexpr const char* SSIL_SHADERS[] = {"ssil/"};

constexpr image_need GTAO_IMAGES[] = {{bgfx::TextureFormat::R32F}, {bgfx::TextureFormat::RGBA8}};
constexpr const char* GTAO_SHADERS[] = {"gtao/"};

constexpr image_need ASSAO_IMAGES[] = {{bgfx::TextureFormat::R16F},
                                       {bgfx::TextureFormat::R8},
                                       {bgfx::TextureFormat::RG8},
                                       {bgfx::TextureFormat::RGBA8}};
constexpr const char* ASSAO_SHADERS[] = {"assao/"};

constexpr image_need AUTO_EXPOSURE_IMAGES[] = {
    {bgfx::TextureFormat::R16F},
    {bgfx::TextureFormat::R32F},
    {bgfx::TextureFormat::RGBA32F},
};
constexpr const char* AUTO_EXPOSURE_SHADERS[] = {"exposure/cs_luminance_histogram", "exposure/cs_histogram_average"};

constexpr image_need LOCAL_EXPOSURE_IMAGES[] = {{bgfx::TextureFormat::R16F}, {bgfx::TextureFormat::RGBA32F}};
constexpr const char* LOCAL_EXPOSURE_SHADERS[] = {"exposure/cs_local_exposure_"};

constexpr image_need PRE_EXPOSURE_IMAGES[] = {{bgfx::TextureFormat::RGBA32F}};
constexpr const char* PRE_EXPOSURE_SHADERS[] = {"exposure/cs_pre_exposure"};

constexpr image_need IRRADIANCE_IMAGES[] = {{bgfx::TextureFormat::RGBA32F}};
constexpr const char* IRRADIANCE_SHADERS[] = {"irradiance/"};

constexpr image_need PREFILTER_IMAGES[] = {{bgfx::TextureFormat::RGBA16F, gfx::texture_dimension::cube}};
constexpr const char* PREFILTER_SHADERS[] = {"prefilter/"};

constexpr image_need FOG_IMAGES[] = {{bgfx::TextureFormat::RGBA16F, gfx::texture_dimension::three_d}};
constexpr const char* FOG_SHADERS[] = {"fog/"};

constexpr render_target_need CLOUD_SHADOW_TARGETS[] = {{bgfx::TextureFormat::R8, 1, true}};

constexpr const char* PARTICLE_SHADERS[] = {"particles/"};
constexpr render_feature PARTICLES_INDIRECT_DEPENDS[] = {render_feature::gpu_particles};

constexpr render_target_need SOFT_SHADOW_TARGETS[] = {{bgfx::TextureFormat::RG32F}, {bgfx::TextureFormat::R32F}};

/// RmlUi layers render with two samples per pixel on desktop.
constexpr std::uint8_t UI_MSAA_SAMPLES = 2;
constexpr render_target_need UI_MSAA_TARGETS[] = {{bgfx::TextureFormat::RGBA8, UI_MSAA_SAMPLES},
                                                  {bgfx::TextureFormat::D24S8, UI_MSAA_SAMPLES}};

constexpr image_need TEMPORAL_PROBE_IMAGES[] = {
    {bgfx::TextureFormat::R32F},
    {bgfx::TextureFormat::RGBA32F, gfx::texture_dimension::two_d, image_access::read_write},
};
constexpr const char* TEMPORAL_PROBE_SHADERS[] = {"taa/cs_temporal_probe"};

/// Every compute binding stage a backend offers (D3D11 at feature level 11_1, D3D12, Vulkan, GL, Metal).
constexpr std::uint8_t ALL_COMPUTE_BINDINGS = 16;

constexpr std::array<render_feature_requirements, RENDER_FEATURE_COUNT> RENDER_FEATURE_REQUIREMENTS = {{
    {.name = "Global illumination",
     .fallback = "Scenes light with their reflection probes and sky only.",
     .needs_compute = true,
     .needs_dispatch_indirect = true,
     .compute_bindings = ALL_COMPUTE_BINDINGS,
     .images = GI_IMAGES,
     .render_targets = GI_TARGETS,
     .shader_prefixes = GI_SHADERS},
    {.name = "GI reflection page feedback",
     .fallback = "GI reflections keep their default page resolution.",
     .needs_compute = true,
     .compute_bindings = 3,
     .images = GI_PAGE_FEEDBACK_IMAGES,
     .depends_on = GI_PAGE_FEEDBACK_DEPENDS,
     .shader_prefixes = GI_PAGE_FEEDBACK_SHADERS},
    {.name = "Hierarchical depth",
     .fallback = "Screen space reflections and indirect lighting turn off.",
     .needs_compute = true,
     .compute_bindings = 2,
     .images = HIZ_IMAGES,
     .shader_prefixes = HIZ_SHADERS},
    {.name = "Screen space reflections",
     .fallback = "Reflections come from reflection probes only.",
     .needs_compute = true,
     .compute_bindings = 4,
     .images = SSR_IMAGES,
     .render_targets = SSR_TARGETS,
     .depends_on = SCREEN_TRACE_DEPENDS,
     .shader_prefixes = SSR_SHADERS},
    {.name = "Screen space indirect lighting",
     .fallback = "Indirect lighting comes from probes only.",
     .needs_compute = true,
     .compute_bindings = 7,
     .images = SSIL_IMAGES,
     .render_targets = SSIL_TARGETS,
     .depends_on = SCREEN_TRACE_DEPENDS,
     .shader_prefixes = SSIL_SHADERS},
    {.name = "Ground truth ambient occlusion",
     .fallback = "ASSAO renders the ambient occlusion instead.",
     .needs_compute = true,
     .compute_bindings = 6,
     .images = GTAO_IMAGES,
     .shader_prefixes = GTAO_SHADERS},
    {.name = "Adaptive screen space ambient occlusion",
     .fallback = "Scenes render without screen space ambient occlusion.",
     .needs_compute = true,
     .compute_bindings = 8,
     .images = ASSAO_IMAGES,
     .shader_prefixes = ASSAO_SHADERS},
    {.name = "Auto exposure",
     .fallback = "Cameras keep a fixed exposure.",
     .needs_compute = true,
     .compute_bindings = 6,
     .images = AUTO_EXPOSURE_IMAGES,
     .shader_prefixes = AUTO_EXPOSURE_SHADERS},
    {.name = "Local exposure",
     .fallback = "Cameras expose the whole image alike.",
     .needs_compute = true,
     .compute_bindings = 2,
     .images = LOCAL_EXPOSURE_IMAGES,
     .shader_prefixes = LOCAL_EXPOSURE_SHADERS},
    {.name = "GPU pre-exposure",
     .fallback = "Lighting renders at an exposure of one before the tone mapping.",
     .needs_compute = true,
     .compute_bindings = 3,
     .images = PRE_EXPOSURE_IMAGES,
     .shader_prefixes = PRE_EXPOSURE_SHADERS},
    {.name = "Sky irradiance",
     .fallback = "The sky lights scenes with a flat ambient color.",
     .needs_compute = true,
     .compute_bindings = 3,
     .images = IRRADIANCE_IMAGES,
     .shader_prefixes = IRRADIANCE_SHADERS},
    {.name = "Reflection probe filtering",
     .fallback = "Reflection probes keep sharp, unfiltered mips.",
     .needs_compute = true,
     .compute_bindings = 2,
     .images = PREFILTER_IMAGES,
     .shader_prefixes = PREFILTER_SHADERS},
    {.name = "Volumetric fog",
     .fallback = "Fog renders without volumetric lighting.",
     .needs_compute = true,
     .compute_bindings = 13,
     .images = FOG_IMAGES,
     .shader_prefixes = FOG_SHADERS},
    {.name = "Cloud shadows", .fallback = "Clouds cast no shadows.", .render_targets = CLOUD_SHADOW_TARGETS},
    {.name = "GPU particles",
     .fallback = "Particles do not render.",
     .needs_compute = true,
     .compute_bindings = 8,
     .shader_prefixes = PARTICLE_SHADERS},
    {.name = "GPU particle culling and sorting",
     .fallback = "Particles are culled and sorted on the CPU.",
     .needs_compute = true,
     .needs_draw_indirect = true,
     .depends_on = PARTICLES_INDIRECT_DEPENDS},
    {.name = "Particle reactive mask",
     .fallback = "Temporal anti-aliasing treats particles like the rest of the scene.",
     .needs_blend_independent = true},
    {.name = "Skinned mesh batching", .fallback = "Skinned meshes draw one by one.", .needs_compute = true},
    {.name = "Soft shadows", .fallback = "Soft shadows filter with PCF.", .render_targets = SOFT_SHADOW_TARGETS},
    {.name = "UI multisampling",
     .fallback = "UI layers render without multisampling.",
     .render_targets = UI_MSAA_TARGETS},
    {.name = "Temporal stability probe",
     .fallback = "The temporal stability measurement is not available.",
     .needs_compute = true,
     .compute_bindings = 9,
     .images = TEMPORAL_PROBE_IMAGES,
     .shader_prefixes = TEMPORAL_PROBE_SHADERS},
}};

constexpr auto get_render_feature_index(render_feature feature) -> std::size_t
{
    return static_cast<std::size_t>(feature);
}

constexpr auto are_dependencies_listed_first() -> bool
{
    for(std::size_t index = 0; index < RENDER_FEATURE_COUNT; ++index)
    {
        for(const auto dependency : RENDER_FEATURE_REQUIREMENTS[index].depends_on)
        {
            if(get_render_feature_index(dependency) >= index)
            {
                return false;
            }
        }
    }
    return true;
}
static_assert(are_dependencies_listed_first(), "A rendering feature must be listed after the features it depends on.");

auto get_texture_format_name(bgfx::TextureFormat::Enum format) -> std::string
{
    return bimg::getName(static_cast<bimg::TextureFormat::Enum>(format));
}

auto get_texture_dimension_text(gfx::texture_dimension dimension) -> const char*
{
    switch(dimension)
    {
        case gfx::texture_dimension::three_d:
            return "3D ";
        case gfx::texture_dimension::cube:
            return "cube ";
        case gfx::texture_dimension::two_d:
        default:
            return "";
    }
}

auto find_missing_image(const gfx::device_caps& caps, const render_feature_requirements& needs) -> std::string
{
    for(const auto& image : needs.images)
    {
        if(image.access == image_access::read_write && !caps.can_read_write_image(image.format))
        {
            return "This GPU cannot read and write " + get_texture_format_name(image.format) +
                   " images through one compute binding.";
        }
        if(!caps.can_write_image(image.format, image.dimension))
        {
            return "This GPU cannot write " + get_texture_format_name(image.format) + " " +
                   get_texture_dimension_text(image.dimension) + "images from compute shaders.";
        }
    }
    return {};
}

auto find_missing_render_target(const gfx::device_caps& caps, const render_feature_requirements& needs) -> std::string
{
    for(const auto& target : needs.render_targets)
    {
        if(!caps.can_render_to(target.format, target.samples))
        {
            return "This GPU cannot render to " + get_texture_format_name(target.format) + " targets" +
                   (target.samples > 1 ? " with multisampling." : ".");
        }
        if(target.needs_mips && !caps.can_generate_mips(target.format))
        {
            return "This GPU cannot build the mips of " + get_texture_format_name(target.format) + " render targets.";
        }
    }
    return {};
}

auto find_missing_capability(const gfx::device_caps& caps, const render_feature_requirements& needs) -> std::string
{
    if(needs.needs_compute && !caps.has_compute)
    {
        return "This GPU has no compute shaders.";
    }
    if(needs.needs_dispatch_indirect && !caps.has_dispatch_indirect)
    {
        return "This GPU cannot dispatch compute work indirectly.";
    }
    if(needs.needs_draw_indirect && !caps.has_draw_indirect)
    {
        return "This GPU cannot draw indirectly.";
    }
    if(needs.needs_blend_independent && !caps.has_blend_independent)
    {
        return "This GPU cannot blend each render target on its own.";
    }
    if(needs.compute_bindings > caps.max_compute_bindings)
    {
        return "This GPU binds " + std::to_string(caps.max_compute_bindings) +
               " resources per compute dispatch, and this feature needs " + std::to_string(needs.compute_bindings) +
               ".";
    }
    std::string missing = find_missing_image(caps, needs);
    if(missing.empty())
    {
        missing = find_missing_render_target(caps, needs);
    }
    return missing;
}

render_feature_support resolved_render_feature_support;
} // namespace

auto get_render_feature_requirements(render_feature feature) -> const render_feature_requirements&
{
    return RENDER_FEATURE_REQUIREMENTS[get_render_feature_index(feature)];
}

auto render_feature_support::resolve(const gfx::device_caps& caps) -> render_feature_support
{
    render_feature_support support;
    for(std::size_t index = 0; index < RENDER_FEATURE_COUNT; ++index)
    {
        const auto& needs = RENDER_FEATURE_REQUIREMENTS[index];
        auto& status = support.statuses_[index];
        const auto unavailable_dependency = std::find_if(needs.depends_on.begin(),
                                                         needs.depends_on.end(),
                                                         [&](render_feature dependency)
                                                         {
                                                             return !support.is_available(dependency);
                                                         });
        if(unavailable_dependency != needs.depends_on.end())
        {
            const auto& dependency = get_render_feature_requirements(*unavailable_dependency);
            status.reason = std::string("It depends on ") + dependency.name + ". " +
                            support.get_status(*unavailable_dependency).reason;
        }
        else
        {
            status.reason = find_missing_capability(caps, needs);
        }
        status.is_available = status.reason.empty();
    }
    return support;
}

auto render_feature_support::is_available(render_feature feature) const -> bool
{
    return statuses_[get_render_feature_index(feature)].is_available;
}

auto render_feature_support::get_status(render_feature feature) const -> const render_feature_status&
{
    return statuses_[get_render_feature_index(feature)];
}

auto render_feature_support::get_device_limits() const -> feature_limits
{
    feature_limits limits;
    limits.global_illumination = is_available(render_feature::global_illumination);
    limits.screen_space_reflections = is_available(render_feature::screen_space_reflections);
    limits.screen_space_indirect_lighting = is_available(render_feature::screen_space_indirect_lighting);
    limits.ambient_occlusion = is_available(render_feature::gtao) || is_available(render_feature::assao);
    limits.volumetric_fog = is_available(render_feature::volumetric_fog);
    limits.soft_shadows = is_available(render_feature::soft_shadows);
    return limits;
}

auto render_feature_support::describe_unavailable() const -> std::string
{
    std::string text;
    for(std::size_t index = 0; index < RENDER_FEATURE_COUNT; ++index)
    {
        const std::string hint = get_hint(static_cast<render_feature>(index));
        if(!hint.empty())
        {
            text += hint + "\n";
        }
    }
    return text;
}

auto render_feature_support::get_hint(render_feature feature) const -> std::string
{
    const auto& status = get_status(feature);
    if(status.is_available)
    {
        return {};
    }
    const auto& needs = get_render_feature_requirements(feature);
    return std::string(needs.name) + " is unavailable on this device. " + status.reason + " " + needs.fallback;
}

auto get_render_feature_support() -> const render_feature_support&
{
    return resolved_render_feature_support;
}

void set_render_feature_support(const render_feature_support& support)
{
    resolved_render_feature_support = support;
}

auto intersect_feature_limits(const feature_limits& lhs, const feature_limits& rhs) -> feature_limits
{
    feature_limits limits;
    limits.global_illumination = lhs.global_illumination && rhs.global_illumination;
    limits.screen_space_reflections = lhs.screen_space_reflections && rhs.screen_space_reflections;
    limits.screen_space_indirect_lighting = lhs.screen_space_indirect_lighting && rhs.screen_space_indirect_lighting;
    limits.ambient_occlusion = lhs.ambient_occlusion && rhs.ambient_occlusion;
    limits.bloom = lhs.bloom && rhs.bloom;
    limits.volumetric_fog = lhs.volumetric_fog && rhs.volumetric_fog;
    limits.max_anti_aliasing = std::min(lhs.max_anti_aliasing, rhs.max_anti_aliasing);
    limits.max_shadow_resolution = std::min(lhs.max_shadow_resolution, rhs.max_shadow_resolution);
    limits.soft_shadows = lhs.soft_shadows && rhs.soft_shadows;
    limits.contact_shadows = lhs.contact_shadows && rhs.contact_shadows;
    return limits;
}

} // namespace unravel
