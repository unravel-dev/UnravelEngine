#pragma once

#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_settings.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/render_view.h>
#include <graphics/texture.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @file lumen_pass_common.h
 * @brief Dispatch and target helpers shared by the Lumen compute passes (the gather, the reflections), and the
 *        values the passes derive from the view's GI settings.
 */

namespace unravel::lumen_pass
{

/// Threads per group edge of the per-pixel and per-probe dispatches (NUM_THREADS(8, 8, 1)).
inline constexpr uint32_t group_edge = 8;
/// The final gather update speed's range for the temporal and for the radiance cache budget
/// (R/LumenScreenProbeGather.cpp:572, 741).
inline constexpr float min_update_speed = 0.5f;
inline constexpr float max_temporal_update_speed = 8.0f;
inline constexpr float max_budget_update_speed = 4.0f;
/// The maximum trace distance's range in metres: UE's 0.01 cm floor and Lumen::MaxTraceDistance
/// (half UE_OLD_WORLD_MAX, R/LumenDiffuseIndirect.cpp:230).
inline constexpr float min_trace_distance = 0.0001f;
inline constexpr float max_trace_distance = 10485.76f;
/// The rays per axis a screen probe traces (R/LumenScreenProbeGather.cpp:452-458): the resolutions the gather's
/// programs are compiled for (lumen_gather_pass) span this range in powers of two.
inline constexpr uint32_t min_probe_trace_resolution = 4;
inline constexpr uint32_t max_probe_trace_resolution = 16;
/// The final gather qualities from which the full-resolution jitter halves (R/LumenScreenProbeGather.cpp:546) and the
/// screen probes are twice as dense (:486).
inline constexpr float half_jitter_final_gather_quality = 4.0f;
inline constexpr float dense_probes_final_gather_quality = 6.0f;
/// The reflection quality at or below which one pixel of each 2 x 2 block traces (R/LumenReflections.cpp:1271), and the
/// most neighbouring rays the resolve reuses per pixel (:1502).
inline constexpr float downsampled_reflection_quality = 0.25f;
inline constexpr uint32_t max_reflection_reconstruction_samples = 64;
/// The radiosity probes at Epic (BaseScalability.ini: r.LumenScene.Radiosity.ProbeSpacing 4, HemisphereProbeResolution
/// 4), the surface cache lighting quality from which they are twice as dense and its range for the rays
/// (R/LumenRadiosity.cpp:208-224).
inline constexpr uint32_t radiosity_probe_spacing = 4;
inline constexpr uint32_t radiosity_hemisphere_resolution = 4;
inline constexpr float dense_radiosity_lighting_quality = 6.0f;
inline constexpr float min_radiosity_lighting_quality = 0.5f;
inline constexpr float max_radiosity_lighting_quality = 4.0f;
inline constexpr uint32_t max_radiosity_hemisphere_resolution = 16;

/// The farthest a Lumen ray travels under the setting @p setting (UE Lumen::GetMaxTraceDistance).
inline auto get_max_trace_distance(float setting) -> float
{
    return std::clamp(setting, min_trace_distance, max_trace_distance);
}

/// The frames the gather's temporal accumulates at most at the final gather update speed @p update_speed: the
/// default over the square root of the speed, rounded (UE LumenScreenProbeGather GetMaxFramesAccumulated, without
/// its editing scale).
inline auto get_temporal_max_frames(float update_speed) -> float
{
    const float speed = std::clamp(update_speed, min_update_speed, max_temporal_update_speed);
    return std::round(float(gi::lumen::LUMEN_TEMPORAL_MAX_FRAMES) / std::sqrt(speed));
}

/// The radiance cache probes re-traced per frame beyond the new ones at the final gather update speed
/// @p update_speed (UE NumProbesToTraceBudget, without its editing scale).
inline auto get_radiance_cache_trace_budget(float update_speed) -> uint32_t
{
    const float speed = std::clamp(update_speed, min_update_speed, max_budget_update_speed);
    return uint32_t(std::lround(float(gi::lumen::LUMEN_RADIANCE_CACHE_TRACE_BUDGET) * speed));
}

/// The roughness below which pixels trace reflection rays under @p settings (UE LumenMaxRoughnessToTraceReflections,
/// in [0, 1]).
inline auto get_max_roughness_to_trace(const gi_settings::reflection_settings& settings) -> float
{
    return std::clamp(settings.max_roughness_to_trace, 0.0f, 1.0f);
}

/// The reflection traces' downsample factor under @p settings (UE LumenReflections' UserDownsampleFactor): 2, one
/// traced pixel per 2 x 2 block, at or below downsampled_reflection_quality, else 1.
inline auto get_reflection_downsample_factor(const gi_settings::reflection_settings& settings) -> uint32_t
{
    return settings.quality <= downsampled_reflection_quality ? 2u : 1u;
}

/// The neighbouring rays the reflection resolve reuses per pixel under @p settings (UE NumReconstructionSamples): the
/// Epic count scaled by the quality and rounded, never fewer than it nor more than
/// max_reflection_reconstruction_samples.
inline auto get_reflection_reconstruction_samples(const gi_settings::reflection_settings& settings) -> uint32_t
{
    const auto samples = uint32_t(gi::lumen::LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES);
    // Bounded before the conversion (any quality from here on lands on the maximum); not-a-number counts as 0.
    const float max_quality = float(max_reflection_reconstruction_samples);
    const float quality = settings.quality > 0.0f ? std::min(settings.quality, max_quality) : 0.0f;
    // UE FMath::RoundToInt: floor(x + 0.5).
    const auto scaled = uint32_t(std::floor(quality * float(samples) + 0.5f));
    return std::clamp(scaled, samples, max_reflection_reconstruction_samples);
}

/// The rays per axis each screen probe traces at the final gather quality @p quality (UE
/// LumenScreenProbeGather::GetTracingOctahedronResolution): sqrt(quality) x 8, truncated, rounded up to a power of two
/// and clamped - 4 below 0.390625, 8 below 1.265625, 16 beyond.
inline auto get_probe_trace_resolution(float quality) -> uint32_t
{
    const float scaled = std::sqrt(std::max(quality, 0.0f)) * float(gi::lumen::LUMEN_PROBE_TRACE_RES);
    // UE converts to uint32 before rounding up; every value from the maximum on lands on the maximum.
    const uint32_t truncated =
        scaled < float(max_probe_trace_resolution) ? uint32_t(scaled) : max_probe_trace_resolution;
    return std::clamp(std::bit_ceil(truncated), min_probe_trace_resolution, max_probe_trace_resolution);
}

/// The screen probe spacing in pixels at the final gather quality @p quality (UE GetScreenDownsampleFactor before its
/// texture-size clamp, which lumen_gather_pass applies).
inline auto get_probe_downsample_factor(float quality) -> uint32_t
{
    const auto spacing = uint32_t(gi::lumen::LUMEN_PROBE_DOWNSAMPLE_FACTOR);
    return quality >= dense_probes_final_gather_quality ? spacing / 2u : spacing;
}

/// The integrate's full-resolution jitter in probe tiles at the final gather quality @p quality (UE
/// GetScreenProbeFullResolutionJitterWidth).
inline auto get_full_res_jitter_width(float quality) -> float
{
    const float scale = quality >= half_jitter_final_gather_quality ? 0.5f : 1.0f;
    return float(gi::lumen::LUMEN_FULL_RES_JITTER_WIDTH) * scale;
}

/// The surface cache radiosity's probes (lumen_radiosity_common.sh).
struct radiosity_layout
{
    /// Card texels between probes.
    uint32_t probe_spacing = radiosity_probe_spacing;
    /// Rays per axis of a probe's hemisphere.
    uint32_t hemisphere_resolution = radiosity_hemisphere_resolution;

    auto operator==(const radiosity_layout&) const -> bool = default;
};

/// The radiosity probes at the surface cache lighting quality @p lighting_quality (UE LumenRadiosity
/// GetRadiosityProbeSpacing, GetHemisphereProbeResolution): half the spacing from dense_radiosity_lighting_quality,
/// and 4 x sqrt(quality) rays per axis, the quality clamped to its range and the product truncated - 2 x 2 at 0.5,
/// 8 x 8 at 4.
inline auto get_radiosity_layout(float lighting_quality) -> radiosity_layout
{
    radiosity_layout layout;
    layout.probe_spacing = lighting_quality >= dense_radiosity_lighting_quality ? radiosity_probe_spacing / 2u
                                                                                : radiosity_probe_spacing;
    // Not-a-number takes the minimum.
    const float quality = lighting_quality > min_radiosity_lighting_quality
                              ? std::min(lighting_quality, max_radiosity_lighting_quality)
                              : min_radiosity_lighting_quality;
    const auto resolution = uint32_t(float(radiosity_hemisphere_resolution) * std::sqrt(quality));
    layout.hemisphere_resolution = std::clamp(resolution, 1u, max_radiosity_hemisphere_resolution);
    return layout;
}

/// The u_lumen_settings values of @p settings (lumen_common.sh): x = the maximum trace distance, y = the temporal's
/// maximum frame count, z = the roughness below which pixels trace reflection rays, w = the integrate's
/// full-resolution jitter.
inline auto make_settings_uniform(const gi_settings& settings) -> std::array<float, 4>
{
    return {get_max_trace_distance(settings.diffuse.max_trace_distance),
            get_temporal_max_frames(settings.diffuse.update_speed),
            get_max_roughness_to_trace(settings.reflections),
            get_full_res_jitter_width(settings.diffuse.quality)};
}
/// The layout of the vec4 tables the Lumen passes upload as buffers (a vertex buffer is read as vec4 by the shaders).
inline auto get_vec4_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return decl;
    }();
    return layout;
}

/// Uploads a vec4 table, growing the buffer when needed; an empty table keeps one zero element.
inline void upload_vec4_table(bgfx::DynamicVertexBufferHandle& buffer, const std::vector<math::vec4>& data)
{
    const uint32_t count = std::max(uint32_t(data.size()), 1u);
    if(!bgfx::isValid(buffer))
    {
        buffer = bgfx::createDynamicVertexBuffer(count,
                                                 get_vec4_layout(),
                                                 BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE);
    }
    if(data.empty())
    {
        const math::vec4 zero(0.0f);
        bgfx::update(buffer, 0, bgfx::copy(&zero, sizeof(zero)));
        return;
    }
    bgfx::update(buffer, 0, bgfx::copy(data.data(), uint32_t(data.size() * sizeof(math::vec4))));
}

/// A Lumen texture read with texelFetch and written as an image.
inline constexpr uint64_t compute_texture_flags = BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP |
                                                  BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT |
                                                  BGFX_SAMPLER_MAG_POINT;

inline auto divide_round_up(uint32_t value, uint32_t divisor) -> uint32_t
{
    return (value + divisor - 1u) / divisor;
}

inline void bind_image(uint8_t stage,
                       const gfx::texture::ptr& tex,
                       bgfx::Access::Enum access,
                       bgfx::TextureFormat::Enum format)
{
    bgfx::setImage(stage, tex->native_handle(), 0, access, format);
}

/// Experiment toggle (gi_set_experiment_flags) every Lumen tracer honours: the global SDF reads as covered everywhere,
/// two-sided meshes as opaque as one-sided ones (the A/B of UE's coverage).
inline constexpr uint32_t experiment_no_sdf_coverage = 1u << 25u;

/// The global SDF coverage a Lumen tracer binds at its SDF_CLIPMAP_COVERAGE_STAGE (gi/sdf_clipmap.sh): the clipmap's,
/// or everything covered when it has none or under experiment_no_sdf_coverage.
inline auto get_sdf_coverage(const global_sdf_clipmap_gpu& clipmap, uint32_t experiments) -> gfx::texture::ptr
{
    const auto& coverage = clipmap.get_coverage_texture();
    const bool use_coverage = coverage && (experiments & experiment_no_sdf_coverage) == 0u;
    return use_coverage ? coverage : default_textures::get().white_texture_3d();
}

/// True when @p tex exists at exactly @p size.
inline auto has_view_size(const gfx::texture::ptr& tex, const usize32_t& size) -> bool
{
    return tex && tex->get_size().width == size.width && tex->get_size().height == size.height;
}

/// A uint buffer compute passes read and write (BUFFER_RW(name, uint, stage)).
inline auto make_uint_buffer(uint32_t count) -> bgfx::DynamicIndexBufferHandle
{
    return bgfx::createDynamicIndexBuffer(count, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
}

/// Destroys a bgfx handle when it is valid and invalidates it.
template<typename Handle>
void destroy_handle(Handle& handle)
{
    if(bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = {bgfx::kInvalidHandle};
    }
}

/// The render view's texture @p name, recreated at @p size when it is missing or sized differently.
inline auto ensure_texture(gfx::render_view& rview,
                           const std::string& name,
                           const usize32_t& size,
                           bgfx::TextureFormat::Enum format,
                           uint64_t flags = compute_texture_flags) -> gfx::texture::ptr
{
    auto& tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(tex, size))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(size.width, size.height, false, 1, format, flags);
    }
    return tex;
}

} // namespace unravel::lumen_pass
