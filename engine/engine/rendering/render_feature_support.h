#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/feature_limits.h>

#include <base/platform/config.hpp>
#include <graphics/device_caps.h>
#include <hpp/string_view.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace unravel
{

/// Samples per pixel of the UI layers where the device multisamples them (render_feature::ui_msaa): smoother edges,
/// especially under transforms. bgfx stores every sample of a multisampled target and resolves it in a separate copy,
/// so on a phone the full-screen layer costs more memory traffic than the smoother edges are worth.
#if UNRAVEL_PLATFORM_ANDROID
constexpr std::uint8_t UI_LAYER_MSAA_SAMPLES = 1;
#else
constexpr std::uint8_t UI_LAYER_MSAA_SAMPLES = 2;
#endif

/// A rendering feature whose GPU needs the device may not meet. Listed after the features they depend on.
enum class render_feature : std::uint8_t
{
    global_illumination,
    gi_page_feedback,
    hiz,
    screen_space_reflections,
    screen_space_indirect_lighting,
    gtao,
    assao,
    auto_exposure,
    local_exposure,
    gpu_pre_exposure,
    irradiance_sh,
    reflection_probe_prefilter,
    volumetric_fog,
    cloud_shadow,
    vertex_storage_buffers,
    particle_storage_buffers,
    gpu_particles_indirect,
    particle_reactive_mask,
    soft_shadows,
    ui_msaa,
    temporal_probe,

    count
};

/// How a compute shader of a feature uses a storage image (a read-only image declared IMAGE*_RO is a texture
/// binding on D3D and needs nothing).
enum class image_access : std::uint8_t
{
    /// Written only.
    write,
    /// Loaded only, through a read-write binding: a typed UAV load on D3D.
    read,
    /// Read and written through one binding.
    read_write,
};

/// A storage image format a feature's compute shaders bind.
struct image_need
{
    bgfx::TextureFormat::Enum format{};
    gfx::texture_dimension dimension{gfx::texture_dimension::two_d};
    image_access access{image_access::write};
    /// The format the feature binds under WebGPU instead, whose shaders declare another; Count: @ref format.
    bgfx::TextureFormat::Enum webgpu_format{bgfx::TextureFormat::Count};

    /// The format the feature binds on a device with @p renderer.
    constexpr auto get_format(bgfx::RendererType::Enum renderer) const -> bgfx::TextureFormat::Enum
    {
        return renderer == bgfx::RendererType::WebGPU && webgpu_format != bgfx::TextureFormat::Count ? webgpu_format
                                                                                                    : format;
    }
};

/// A render target format a feature's passes render to.
struct render_target_need
{
    bgfx::TextureFormat::Enum format{};
    std::uint8_t samples{1};
    bool needs_mips{};
    /// Whether the feature samples the target with linear filtering.
    bool needs_filtering{};
};

/**
 * @brief Everything a rendering feature needs from the device, the one place that says so.
 *
 * The compiled-shader test checks the compute shaders named by @ref shader_prefixes against @ref images and
 * @ref compute_bindings, so the table follows the shaders.
 */
struct render_feature_requirements
{
    /// The feature's name in logs and the editor, capitalized.
    const char* name{};
    /// What renders instead when the feature is unavailable, as a sentence.
    const char* fallback{};
    /// The feature @ref fallback names, if any (render_feature::count for none): while it is unavailable too, its
    /// own fallback is what renders.
    render_feature fallback_feature{render_feature::count};
    bool needs_compute{};
    bool needs_draw_indirect{};
    bool needs_dispatch_indirect{};
    bool needs_blend_independent{};
    bool needs_vertex_storage_buffers{};
    /// The highest binding stage of the feature's compute shaders, plus one.
    std::uint8_t compute_bindings{};
    /// The most storage images one of the feature's shaders binds; 0 for a feature that fits WebGPU's core limit
    /// (the "eshader files" suite checks both against the compiled WGSL).
    std::uint8_t storage_textures_per_stage{};
    std::span<const image_need> images;
    std::span<const render_target_need> render_targets;
    std::span<const render_feature> depends_on;
    /// The compiled compute shaders that belong to the feature, as paths under the shader folder without the
    /// extension; the longest matching prefix decides.
    std::span<const char* const> shader_prefixes;
};

/// The device needs of @p feature.
ENGINE_EXPORT auto get_render_feature_requirements(render_feature feature) -> const render_feature_requirements&;

/**
 * @brief The feature an engine compute shader belongs to, or render_feature::count for one of no feature.
 * @param shader The shader's path under the shader folder without the extension (gi/cs_gi_card_copy): the longest
 * matching prefix of render_feature_requirements::shader_prefixes decides.
 */
ENGINE_EXPORT auto find_shader_render_feature(hpp::string_view shader) -> render_feature;

/// Whether a feature runs on the device, and the reason when it does not.
struct render_feature_status
{
    bool is_available{true};
    /// The first unmet need, as sentences; empty when available.
    std::string reason;
};

/**
 * @brief Which rendering features the device runs, resolved once from its capabilities.
 *
 * A default-constructed instance has every feature available: the state for a run without a renderer.
 */
class ENGINE_EXPORT render_feature_support
{
public:
    /// The support of every feature on a device with @p caps.
    static auto resolve(const gfx::device_caps& caps) -> render_feature_support;

    auto is_available(render_feature feature) const -> bool;
    auto get_status(render_feature feature) const -> const render_feature_status&;

    /**
     * @brief Whether the shader asset @p key is for something the device runs: false for an engine compute shader
     * of a feature it does not run, and for every engine compute shader on a device without compute; true for every
     * other shader.
     *
     * Such a shader may declare storage formats the device lacks, which WebGPU rejects as soon as the shader is
     * created: the shader loader leaves it empty, as a program without compute is.
     */
    auto runs_shader(hpp::string_view key) const -> bool;

    /// The feature limits the device allows: what the project's limits are ANDed with.
    auto get_device_limits() const -> feature_limits;

    /// The features the device does not run, in table order.
    auto get_unavailable_features() const -> std::vector<render_feature>;

    /// One line per unavailable feature: its name, the reason and its fallback.
    auto describe_unavailable() const -> std::string;

    /// What the editor shows about @p feature on this device: its name, the reason and its fallback as sentences,
    /// or empty when the device runs it.
    auto get_hint(render_feature feature) const -> std::string;

    /// What renders instead of @p feature on this device, as a sentence.
    auto get_fallback(render_feature feature) const -> const char*;

private:
    std::array<render_feature_status, static_cast<std::size_t>(render_feature::count)> statuses_{};
    bool has_compute_{true};
};

/// The support the renderer resolved for the running device.
ENGINE_EXPORT auto get_render_feature_support() -> const render_feature_support&;

/// Sets what @ref get_render_feature_support returns. Called by the renderer once its backend started.
ENGINE_EXPORT void set_render_feature_support(const render_feature_support& support);

/// The stricter of two limits: a feature both allow, the lower of two caps.
ENGINE_EXPORT auto intersect_feature_limits(const feature_limits& lhs, const feature_limits& rhs) -> feature_limits;

} // namespace unravel
