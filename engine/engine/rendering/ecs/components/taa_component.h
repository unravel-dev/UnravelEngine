#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/taa_pass.h>

#include <cmath>

namespace unravel
{

/**
 * @brief Temporal anti-aliasing (HDR, before tonemap). Mutually exclusive with FXAA when enabled.
 */
class taa_component : public component_crtp<taa_component>
{
public:
    bool enabled = false;
    taa_pass::settings settings{};

    static void merge_into(taa_pass::settings& result,
                           const taa_pass::settings& from,
                           float contribution,
                           bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.history_blend = std::lerp(result.history_blend, from.history_blend, contribution);
        result.sharpen = std::lerp(result.sharpen, from.sharpen, contribution);
        result.depth_reject_scale = std::lerp(result.depth_reject_scale, from.depth_reject_scale, contribution);
        result.variance_clip_scale = std::lerp(result.variance_clip_scale, from.variance_clip_scale, contribution);
        result.jitter_amplitude = std::lerp(result.jitter_amplitude, from.jitter_amplitude, contribution);
        result.jitter_temporal_phase_scale =
            std::lerp(result.jitter_temporal_phase_scale, from.jitter_temporal_phase_scale, contribution);
        result.temporal_sample_count =
            static_cast<std::uint32_t>(std::lround(std::lerp(static_cast<float>(result.temporal_sample_count),
                                                            static_cast<float>(from.temporal_sample_count),
                                                            contribution)));
        if(contribution > 0.5f)
        {
            result.jitter_mode = from.jitter_mode;
        }
    }
};

} // namespace unravel
