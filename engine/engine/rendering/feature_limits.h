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
    /// The most expensive anti-aliasing a view may use.
    anti_aliasing_limit max_anti_aliasing{anti_aliasing_limit::temporal};
    /// The largest shadow map a light may use; larger ones render at this resolution.
    sm_resolution max_shadow_resolution{sm_resolution::very_high};
    /// Soft shadows (PCSS, VSM and ESM, with their blur passes); off, those lights filter their shadows with PCF.
    bool soft_shadows{true};
    /// Contact shadows.
    bool contact_shadows{true};

    friend auto operator==(const feature_limits& lhs, const feature_limits& rhs) -> bool = default;
};

/**
 * @brief The light as `limits` let it render: its shadow map no larger than the limit, soft shadows (PCSS,
 * VSM, ESM) filtered with PCF without soft shadows, and no contact shadows without them.
 *
 * The shadow pass and the lighting pass must both render the limited light, so the maps and the program
 * reading them agree.
 */
ENGINE_EXPORT auto limit_light(const light& source, const feature_limits& limits) -> light;

} // namespace unravel
