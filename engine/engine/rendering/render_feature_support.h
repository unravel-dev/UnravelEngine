#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/feature_limits.h>

#include <graphics/device_caps.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace unravel
{

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
    gpu_particles,
    gpu_particles_indirect,
    particle_reactive_mask,
    skinned_mesh_batching,
    soft_shadows,
    ui_msaa,
    temporal_probe,

    count
};

/// How a compute shader of a feature uses a storage image.
enum class image_access : std::uint8_t
{
    /// Written only (or read only, which needs the same format support).
    write,
    /// Read and written through one binding.
    read_write,
};

/// A storage image format a feature's compute shaders bind.
struct image_need
{
    bgfx::TextureFormat::Enum format{};
    gfx::texture_dimension dimension{gfx::texture_dimension::two_d};
    image_access access{image_access::write};
};

/// A render target format a feature's passes render to.
struct render_target_need
{
    bgfx::TextureFormat::Enum format{};
    std::uint8_t samples{1};
    bool needs_mips{};
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
    bool needs_compute{};
    bool needs_draw_indirect{};
    bool needs_dispatch_indirect{};
    bool needs_blend_independent{};
    /// The highest binding stage of the feature's compute shaders, plus one.
    std::uint8_t compute_bindings{};
    std::span<const image_need> images;
    std::span<const render_target_need> render_targets;
    std::span<const render_feature> depends_on;
    /// The compiled compute shaders that belong to the feature, as paths under the shader folder without the
    /// extension; the longest matching prefix decides.
    std::span<const char* const> shader_prefixes;
};

/// The device needs of @p feature.
ENGINE_EXPORT auto get_render_feature_requirements(render_feature feature) -> const render_feature_requirements&;

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

    /// The feature limits the device allows: what the project's limits are ANDed with.
    auto get_device_limits() const -> feature_limits;

    /// One line per unavailable feature: its name, the reason and its fallback.
    auto describe_unavailable() const -> std::string;

    /// What the editor shows about @p feature on this device: its name, the reason and its fallback as sentences,
    /// or empty when the device runs it.
    auto get_hint(render_feature feature) const -> std::string;

private:
    std::array<render_feature_status, static_cast<std::size_t>(render_feature::count)> statuses_{};
};

/// The support the renderer resolved for the running device.
ENGINE_EXPORT auto get_render_feature_support() -> const render_feature_support&;

/// Sets what @ref get_render_feature_support returns. Called by the renderer once its backend started.
ENGINE_EXPORT void set_render_feature_support(const render_feature_support& support);

/// The stricter of two limits: a feature both allow, the lower of two caps.
ENGINE_EXPORT auto intersect_feature_limits(const feature_limits& lhs, const feature_limits& rhs) -> feature_limits;

} // namespace unravel
