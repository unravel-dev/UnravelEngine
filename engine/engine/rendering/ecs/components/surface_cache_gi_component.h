#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/surface_cache_gi_pass.h>

namespace unravel
{

/**
 * @brief Volume / camera settings for world-space surface-cache GI.
 *
 * Production path: mesh cards + card lighting + cascade probe final gather.
 * Static meshes only (skinned deferred). HW-RT optional later (prefer_hardware_rt).
 * Hybrid compose: probe irradiance + SSIL near-field + skylight SH miss.
 */
class surface_cache_gi_component : public component_crtp<surface_cache_gi_component>
{
public:
    bool enabled = false;
    surface_cache_gi_pass::surface_cache_gi_settings settings{};

    static void merge_into(surface_cache_gi_pass::surface_cache_gi_settings& result,
                           const surface_cache_gi_pass::surface_cache_gi_settings& from,
                           float contribution,
                           bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.cache_blend = std::lerp(result.cache_blend, from.cache_blend, contribution);
        result.ssil_near_field_weight =
            std::lerp(result.ssil_near_field_weight, from.ssil_near_field_weight, contribution);
        result.max_card_distance = std::lerp(result.max_card_distance, from.max_card_distance, contribution);
        result.card_thickness = std::lerp(result.card_thickness, from.card_thickness, contribution);
        result.project_history = std::lerp(result.project_history, from.project_history, contribution);
        result.stale_frames =
            int(std::lerp(float(result.stale_frames), float(from.stale_frames), contribution));
        result.pages_per_frame =
            int(std::lerp(float(result.pages_per_frame), float(from.pages_per_frame), contribution));
        result.min_face_area = std::lerp(result.min_face_area, from.min_face_area, contribution);
        result.seed_with_skylight =
            contribution >= 0.5f ? from.seed_with_skylight : result.seed_with_skylight;
        result.max_gather_distance =
            std::lerp(result.max_gather_distance, from.max_gather_distance, contribution);
        result.gather_intensity =
            std::lerp(result.gather_intensity, from.gather_intensity, contribution);
        result.max_card_extent =
            std::lerp(result.max_card_extent, from.max_card_extent, contribution);
        result.sticky_distance = std::lerp(result.sticky_distance, from.sticky_distance, contribution);
        result.probe_near_extent =
            std::lerp(result.probe_near_extent, from.probe_near_extent, contribution);
        result.probe_far_extent =
            std::lerp(result.probe_far_extent, from.probe_far_extent, contribution);
        result.probes_per_frame =
            int(std::lerp(float(result.probes_per_frame), float(from.probes_per_frame), contribution));
        result.probe_history = std::lerp(result.probe_history, from.probe_history, contribution);
        result.bounce_strength = std::lerp(result.bounce_strength, from.bounce_strength, contribution);
        result.enable_screen_project =
            contribution >= 0.5f ? from.enable_screen_project : result.enable_screen_project;
    }
};

} // namespace unravel
