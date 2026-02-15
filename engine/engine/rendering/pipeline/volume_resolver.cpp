#include "volume_resolver.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace unravel
{

namespace
{

struct volume_contribution
{
    entt::entity entity;
    float contribution;
    int priority;
    bool is_global;
};

auto compute_blend_factor(const math::bbox& world_bounds,
                          const math::vec3& camera_pos,
                          float blend_distance) -> float
{
    if(world_bounds.contains_point(camera_pos))
    {
        return 1.0f;
    }
    const math::vec3 closest = world_bounds.closest_point(camera_pos);
    const float dist_to_boundary = glm::length(camera_pos - closest);
    if(blend_distance <= 0.0f)
    {
        return dist_to_boundary <= 0.0f ? 1.0f : 0.0f;
    }
    if(dist_to_boundary >= blend_distance)
    {
        return 0.0f;
    }
    return 1.0f - (dist_to_boundary / blend_distance);
}

void merge_bloom(bloom_pass::settings& result,
                 const bloom_pass::settings& from,
                 float contribution)
{
    result.threshold = std::lerp(result.threshold, from.threshold, contribution);
    result.soft_knee = std::lerp(result.soft_knee, from.soft_knee, contribution);
    result.clamp = std::lerp(result.clamp, from.clamp, contribution);
    result.intensity = std::lerp(result.intensity, from.intensity, contribution);
    result.mip_count = static_cast<int>(std::lround(std::lerp(float(result.mip_count), float(from.mip_count), contribution)));
}

void merge_tonemapping(tonemapping_pass::settings& result,
                      const tonemapping_pass::settings& from,
                      float contribution)
{
    result.exposure = std::lerp(result.exposure, from.exposure, contribution);
    if(contribution >= 0.5f)
    {
        result.method = from.method;
    }
}

void merge_assao(assao_pass::settings& result,
                 const assao_pass::settings& from,
                 float contribution)
{
    result.radius = std::lerp(result.radius, from.radius, contribution);
    result.shadow_multiplier = std::lerp(result.shadow_multiplier, from.shadow_multiplier, contribution);
    result.shadow_power = std::lerp(result.shadow_power, from.shadow_power, contribution);
    result.shadow_clamp = std::lerp(result.shadow_clamp, from.shadow_clamp, contribution);
    result.horizon_angle_threshold = std::lerp(result.horizon_angle_threshold, from.horizon_angle_threshold, contribution);
    result.fade_out_from = std::lerp(result.fade_out_from, from.fade_out_from, contribution);
    result.fade_out_to = std::lerp(result.fade_out_to, from.fade_out_to, contribution);
    result.quality_level = contribution >= 0.5f ? from.quality_level : result.quality_level;
    result.adaptive_quality_limit = std::lerp(result.adaptive_quality_limit, from.adaptive_quality_limit, contribution);
    result.blur_pass_count = contribution >= 0.5f ? from.blur_pass_count : result.blur_pass_count;
    result.sharpness = std::lerp(result.sharpness, from.sharpness, contribution);
    result.temporal_supersampling_angle_offset = std::lerp(result.temporal_supersampling_angle_offset, from.temporal_supersampling_angle_offset, contribution);
    result.temporal_supersampling_radius_offset = std::lerp(result.temporal_supersampling_radius_offset, from.temporal_supersampling_radius_offset, contribution);
    result.detail_shadow_strength = std::lerp(result.detail_shadow_strength, from.detail_shadow_strength, contribution);
    result.generate_normals = contribution >= 0.5f ? from.generate_normals : result.generate_normals;
}

} // namespace

auto resolve_post_process_volumes(scene& scn,
                                 const math::vec3& camera_pos,
                                 entt::handle camera_ent) -> resolved_post_process_settings
{
    resolved_post_process_settings result;

    std::vector<volume_contribution> contributions;

    scn.registry->view<transform_component, volume_component>().each(
        [&](entt::entity e, const transform_component& transform_comp, const volume_component& volume_comp)
        {
            float contribution = 0.0f;
            if(volume_comp.mode == volume_mode::global)
            {
                contribution = volume_comp.weight;
            }
            else
            {
                const math::bbox local_bounds = volume_comp.get_local_bounds();
                const math::transform& world_transform = transform_comp.get_transform_global();
                const math::bbox world_bounds = math::bbox::mul(local_bounds, world_transform);
                const float blend_factor = compute_blend_factor(world_bounds, camera_pos, volume_comp.blend_distance);
                contribution = volume_comp.weight * blend_factor;
            }
            if(contribution > 0.0f)
            {
                contributions.push_back({e, contribution, volume_comp.priority, volume_comp.mode == volume_mode::global});
            }
        });

    std::sort(contributions.begin(),
              contributions.end(),
              [](const volume_contribution& a, const volume_contribution& b)
              {
                  if(a.priority != b.priority)
                  {
                      return a.priority > b.priority;
                  }
                  return a.is_global && !b.is_global;
              });

    bool first_bloom = true;
    bool first_tonemapping = true;
    bool first_assao = true;
    bool first_ssr = true;

    for(const auto& c : contributions)
    {
        auto handle = scn.create_handle(c.entity);
        const float contrib = c.contribution;

        if(auto* bloom = handle.try_get<bloom_component>(); bloom && bloom->enabled && contrib > 0.0f)
        {
            if(first_bloom)
            {
                result.has_bloom = true;
                result.bloom = bloom->settings;
                first_bloom = false;
            }
            else
            {
                merge_bloom(result.bloom, bloom->settings, contrib);
            }
        }

        if(auto* tonemapping = handle.try_get<tonemapping_component>(); tonemapping && tonemapping->enabled && contrib > 0.0f)
        {
            if(first_tonemapping)
            {
                result.has_tonemapping = true;
                result.tonemapping = tonemapping->settings;
                first_tonemapping = false;
            }
            else
            {
                merge_tonemapping(result.tonemapping, tonemapping->settings, contrib);
            }
        }

        if(auto* fxaa = handle.try_get<fxaa_component>(); fxaa && fxaa->enabled && contrib > 0.0f)
        {
            result.has_fxaa = true;
        }

        if(auto* ssr = handle.try_get<ssr_component>(); ssr && ssr->enabled && contrib > 0.0f)
        {
            if(first_ssr)
            {
                result.has_ssr = true;
                result.ssr = ssr->settings;
                first_ssr = false;
            }
        }

        if(auto* assao = handle.try_get<assao_component>(); assao && assao->enabled && contrib > 0.0f)
        {
            if(first_assao)
            {
                result.has_assao = true;
                result.assao = assao->settings;
                first_assao = false;
            }
            else
            {
                merge_assao(result.assao, assao->settings, contrib);
            }
        }
    }

    return result;
}

} // namespace unravel
