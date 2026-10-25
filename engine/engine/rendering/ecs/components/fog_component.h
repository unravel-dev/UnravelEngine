#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/fog_pass.h>
#include <cmath>

namespace unravel
{

/**
 * @brief Height fog volume / camera component (fog_pass::settings).
 *
 * On a camera it fogs that camera's view; on a volume entity it blends with the other volumes at the camera's
 * position like the post-processing effects, so a local volume can thicken, thin or recolor the fog in a zone.
 */
class fog_component : public component_crtp<fog_component>
{
public:
    bool enabled = true;
    fog_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(fog_pass::settings& result, const fog_pass::settings& from, float contribution, bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }
        result.attenuation_distance = std::lerp(result.attenuation_distance, from.attenuation_distance, contribution);
        result.base_height = std::lerp(result.base_height, from.base_height, contribution);
        result.maximum_height = std::lerp(result.maximum_height, from.maximum_height, contribution);
        result.albedo.value.x = std::lerp(result.albedo.value.x, from.albedo.value.x, contribution);
        result.albedo.value.y = std::lerp(result.albedo.value.y, from.albedo.value.y, contribution);
        result.albedo.value.z = std::lerp(result.albedo.value.z, from.albedo.value.z, contribution);
        result.albedo.value.w = std::lerp(result.albedo.value.w, from.albedo.value.w, contribution);
        result.anisotropy = std::lerp(result.anisotropy, from.anisotropy, contribution);
        result.max_opacity = std::lerp(result.max_opacity, from.max_opacity, contribution);
        result.start_distance = std::lerp(result.start_distance, from.start_distance, contribution);
        result.sun_scattering = std::lerp(result.sun_scattering, from.sun_scattering, contribution);
        result.ambient_scattering = std::lerp(result.ambient_scattering, from.ambient_scattering, contribution);
        result.sky_affect = std::lerp(result.sky_affect, from.sky_affect, contribution);
        result.sky_distance = std::lerp(result.sky_distance, from.sky_distance, contribution);
        result.volumetric = contribution >= 0.5f ? from.volumetric : result.volumetric;
        result.volumetric_distance = std::lerp(result.volumetric_distance, from.volumetric_distance, contribution);
        result.volumetric_quality = contribution >= 0.5f ? from.volumetric_quality : result.volumetric_quality;
        result.volumetric_slice_uniformity =
            std::lerp(result.volumetric_slice_uniformity, from.volumetric_slice_uniformity, contribution);
        result.volumetric_history = std::lerp(result.volumetric_history, from.volumetric_history, contribution);
    }
};

} // namespace unravel
