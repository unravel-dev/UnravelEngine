#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/gi/gi_settings.h>

#include <cmath>

namespace unravel
{

/**
 * @brief Volume component exposing global illumination (Lumen), blending across post-process volumes like
 *        ssil_component.
 */
class gi_component : public component_crtp<gi_component>
{
public:
    bool enabled = true;
    gi_settings settings{};

    /// Continuous quantities interpolate; switches and counts take the dominant volume's value (a blended count is
    /// no volume's intent).
    static void merge_into(gi_settings& result, const gi_settings& from, float contribution, bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        const bool dominant = contribution >= 0.5f;
        merge_diffuse(result.diffuse, from.diffuse, contribution, dominant);
        merge_reflections(result.reflections, from.reflections, contribution, dominant);
        merge_ambient_occlusion(result.ambient_occlusion, from.ambient_occlusion, contribution, dominant);
        merge_scene(result.scene, from.scene, contribution);
        merge_distance_field(result.distance_field, from.distance_field, contribution, dominant);
    }

private:
    static void merge_diffuse(gi_settings::diffuse_settings& result,
                              const gi_settings::diffuse_settings& from,
                              float contribution,
                              bool dominant)
    {
        result.intensity = std::lerp(result.intensity, from.intensity, contribution);
        result.quality = std::lerp(result.quality, from.quality, contribution);
        result.screen_traces = dominant ? from.screen_traces : result.screen_traces;
        result.update_speed = std::lerp(result.update_speed, from.update_speed, contribution);
        result.max_trace_distance = std::lerp(result.max_trace_distance, from.max_trace_distance, contribution);
    }

    static void merge_reflections(gi_settings::reflection_settings& result,
                                  const gi_settings::reflection_settings& from,
                                  float contribution,
                                  bool dominant)
    {
        result.enabled = dominant ? from.enabled : result.enabled;
        result.quality = std::lerp(result.quality, from.quality, contribution);
        result.screen_traces = dominant ? from.screen_traces : result.screen_traces;
        result.max_roughness_to_trace =
            std::lerp(result.max_roughness_to_trace, from.max_roughness_to_trace, contribution);
    }

    static void merge_ambient_occlusion(gi_settings::ambient_occlusion_settings& result,
                                        const gi_settings::ambient_occlusion_settings& from,
                                        float contribution,
                                        bool dominant)
    {
        result.enabled = dominant ? from.enabled : result.enabled;
        result.intensity = std::lerp(result.intensity, from.intensity, contribution);
    }

    static void merge_scene(gi_settings::scene_settings& result,
                            const gi_settings::scene_settings& from,
                            float contribution)
    {
        result.lighting_quality = std::lerp(result.lighting_quality, from.lighting_quality, contribution);
        result.detail = std::lerp(result.detail, from.detail, contribution);
        result.view_distance = std::lerp(result.view_distance, from.view_distance, contribution);
        result.lighting_update_speed =
            std::lerp(result.lighting_update_speed, from.lighting_update_speed, contribution);
        result.surface_cache_resolution =
            std::lerp(result.surface_cache_resolution, from.surface_cache_resolution, contribution);
    }

    static void merge_distance_field(gi_settings::distance_field_settings& result,
                                     const gi_settings::distance_field_settings& from,
                                     float contribution,
                                     bool dominant)
    {
        result.levels_per_update = dominant ? from.levels_per_update : result.levels_per_update;
        result.level_blend_band = std::lerp(result.level_blend_band, from.level_blend_band, contribution);
    }
};

} // namespace unravel
