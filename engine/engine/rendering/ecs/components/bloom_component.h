#pragma once

#include <base/basetypes.hpp>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/pipeline/passes/bloom_pass.h>
#include <cmath>

namespace unravel
{

class bloom_component : public component_crtp<bloom_component>
{
public:
    bool enabled = true;
    bloom_pass::settings settings{};

    /// Merges this component's settings into result. For first contribution, copies; otherwise lerps.
    static void merge_into(bloom_pass::settings& result,
                          const bloom_pass::settings& from,
                          float contribution,
                          bool is_first)
    {
        if(is_first)
        {
            result = from;
            return;
        }

        auto lerp_color = [](math::color& dst, const math::color& src, float t) -> void {
            dst.value.x = std::lerp(dst.value.x, src.value.x, t);
            dst.value.y = std::lerp(dst.value.y, src.value.y, t);
            dst.value.z = std::lerp(dst.value.z, src.value.z, t);
            dst.value.w = std::lerp(dst.value.w, src.value.w, t);
        };

        result.threshold = std::lerp(result.threshold, from.threshold, contribution);
        result.soft_knee = std::lerp(result.soft_knee, from.soft_knee, contribution);
        result.clamp = std::lerp(result.clamp, from.clamp, contribution);
        result.intensity = std::lerp(result.intensity, from.intensity, contribution);
        result.scatter = std::lerp(result.scatter, from.scatter, contribution);
        result.mip_count = static_cast<int>(std::lround(std::lerp(float(result.mip_count), float(from.mip_count), contribution)));

        lerp_color(result.mip0_tint, from.mip0_tint, contribution);
        lerp_color(result.mip1_tint, from.mip1_tint, contribution);
        lerp_color(result.mip2_tint, from.mip2_tint, contribution);
        lerp_color(result.mip3_tint, from.mip3_tint, contribution);
        lerp_color(result.mip4_tint, from.mip4_tint, contribution);
        lerp_color(result.mip5_tint, from.mip5_tint, contribution);

        result.dirt_intensity = std::lerp(result.dirt_intensity, from.dirt_intensity, contribution);
        // Asset references cannot lerp; the dominant contributor wins, like the tonemap method.
        if(contribution >= 0.5f)
        {
            result.dirt_texture = from.dirt_texture;
        }
    }
};

} // namespace unravel
