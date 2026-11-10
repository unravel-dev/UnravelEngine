#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/light.h>

#include <cstdint>

namespace unravel
{

/// The anti-aliasing a view may use, from the cheapest.
enum class anti_aliasing_limit : std::uint8_t
{
    /// No anti-aliasing.
    none,
    /// FXAA, one pass over the finished image: a view asking for temporal AA gets FXAA instead.
    fxaa,
    /// Temporal AA, which keeps a history and a velocity buffer per view.
    temporal,
};

/// The widest shadow filter a light may use, from the cheapest. Each filter allowed is one more lighting program per
/// light type and shadow depth format, which every device builds when the pipeline starts; WebGL 2 spends seconds on
/// each of the wider ones there.
enum class shadow_filtering_limit : std::uint8_t
{
    /// One depth comparison per pixel: every shadowed light renders hard shadows.
    hard,
    /// PCF with the 3x3 tent: wider PCF and soft shadows (PCSS, VSM, ESM) filter with it.
    pcf_3x3,
    /// PCF of every width, and soft shadows where soft_shadows allows them.
    pcf,
};

/**
 * @brief The most expensive rendering features a project's scenes may use.
 *
 * A camera's effect components and the volumes it stands in ask for effects. The limits turn off what they
 * do not allow and lower what they cap, so a target with lower limits renders every scene with less work
 * and no scene changes.
 */
struct feature_limits
{
    /// Global illumination; off, scenes light with their probes and sky only.
    bool global_illumination{true};
    /// Screen space reflections.
    bool screen_space_reflections{true};
    /// Screen space indirect lighting.
    bool screen_space_indirect_lighting{true};
    /// Screen space ambient occlusion (GTAO and ASSAO).
    bool ambient_occlusion{true};
    /// Bloom.
    bool bloom{true};
    /// Volumetric fog (fog_pass): off, fog components that ask for it render the analytic fog alone.
    bool volumetric_fog{true};
    /// The most expensive anti-aliasing a view may use.
    anti_aliasing_limit max_anti_aliasing{anti_aliasing_limit::temporal};
    /// The largest shadow map a light may use; larger ones render at this resolution.
    sm_resolution max_shadow_resolution{sm_resolution::very_high};
    /// Soft shadows (PCSS, VSM and ESM, with their blur passes); off, those lights filter their shadows with PCF.
    bool soft_shadows{true};
    /// The widest shadow filter a light may use; lights asking for a wider one filter with this one.
    shadow_filtering_limit max_shadow_filtering{shadow_filtering_limit::pcf};
    /// Contact shadows.
    bool contact_shadows{true};
    /// Subsurface scattering: off, the G-buffer holds every material's subsurface color as black, and the lighting
    /// programs leave the subsurface code out.
    bool subsurface{true};
    /// Volumetric clouds: off, a sky that asks for them renders flat clouds (one sample of the same layer in the sky
    /// pass) instead of the half-resolution raymarch.
    bool volumetric_clouds{true};
    /// The finest detail of the indirect lighting: off, a view without screen space AO, GI, SSIL or traced reflections
    /// reads two of the environment's three SH bands and skips background pixels, in a cheaper program. A view with any
    /// of those keeps the full program.
    bool detailed_indirect_lighting{true};
    /// Full precision for the targets that hold light: off, a view without temporal AA, SSR, SSIL or GI keeps its
    /// emissive, light buffer and bloom in RG11B10F. That is half the size of RGBA16F and what tile-based GPUs
    /// compress where they store RGBA16F raw, at the cost of a slight tint in dark gradients.
    bool full_precision_light_targets{true};
    /// The most mips bloom's pyramid may use: each one is two render passes, and fewer make the glow tighter.
    int max_bloom_mips{10};
    /// Auto exposure metered every frame: off, a view runs its histogram and local exposure every few frames
    /// (auto_exposure_pass) and keeps adapting every frame from the last histogram.
    bool per_frame_exposure_metering{true};
    /// The most cascades a directional light may split its shadow map into. Fewer cascades cover the same distance
    /// with less resolution, and every caster draws into fewer of them.
    int max_shadow_cascades{4};

    friend auto operator==(const feature_limits& lhs, const feature_limits& rhs) -> bool = default;
};

/**
 * @brief The light as `limits` let it render: its shadow map no larger than the limit, soft shadows (PCSS,
 * VSM, ESM) filtered with PCF without soft shadows, its filter no wider than max_shadow_filtering, no contact shadows
 * without them, and no more cascades than allowed.
 *
 * The shadow pass and the lighting pass must both render the limited light, so the maps and the program
 * reading them agree.
 */
ENGINE_EXPORT auto limit_light(const light& source, const feature_limits& limits) -> light;

} // namespace unravel
