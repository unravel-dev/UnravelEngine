#include "volume_resolver.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
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

    bool first_auto_exposure = true;
    bool first_bloom = true;
    bool first_tonemapping = true;
    bool first_assao = true;
    bool first_ssr = true;
    bool first_ssil = true;
    float auto_exposure_enabled_sum = 0.0f;
    float auto_exposure_contrib_sum = 0.0f;
    float bloom_enabled_sum = 0.0f;
    float bloom_contrib_sum = 0.0f;
    float tonemapping_enabled_sum = 0.0f;
    float tonemapping_contrib_sum = 0.0f;
    float fxaa_enabled_sum = 0.0f;
    float fxaa_contrib_sum = 0.0f;
    float ssr_enabled_sum = 0.0f;
    float ssr_contrib_sum = 0.0f;
    float assao_enabled_sum = 0.0f;
    float assao_contrib_sum = 0.0f;
    float ssil_enabled_sum = 0.0f;
    float ssil_contrib_sum = 0.0f;

    for(const auto& c : contributions)
    {
        auto handle = scn.create_handle(c.entity);
        const float contrib = c.contribution;

        if(auto* ae = handle.try_get<auto_exposure_component>(); ae && contrib > 0.0f)
        {
            auto_exposure_enabled_sum += (ae->enabled ? 1.0f : 0.0f) * contrib;
            auto_exposure_contrib_sum += contrib;
            if(ae->enabled)
            {
                auto_exposure_component::merge_into(result.auto_exposure, ae->settings, contrib, first_auto_exposure);
                first_auto_exposure = false;
            }
        }

        if(auto* bloom = handle.try_get<bloom_component>(); bloom && contrib > 0.0f)
        {
            bloom_enabled_sum += (bloom->enabled ? 1.0f : 0.0f) * contrib;
            bloom_contrib_sum += contrib;
            if(bloom->enabled)
            {
                bloom_component::merge_into(result.bloom, bloom->settings, contrib, first_bloom);
                first_bloom = false;
            }
        }

        if(auto* tonemapping = handle.try_get<tonemapping_component>(); tonemapping && contrib > 0.0f)
        {
            tonemapping_enabled_sum += (tonemapping->enabled ? 1.0f : 0.0f) * contrib;
            tonemapping_contrib_sum += contrib;
            if(tonemapping->enabled)
            {
                tonemapping_component::merge_into(result.tonemapping, tonemapping->settings, contrib, first_tonemapping);
                first_tonemapping = false;
            }
        }

        if(auto* fxaa = handle.try_get<fxaa_component>(); fxaa && contrib > 0.0f)
        {
            fxaa_enabled_sum += (fxaa->enabled ? 1.0f : 0.0f) * contrib;
            fxaa_contrib_sum += contrib;
        }

        if(auto* ssr = handle.try_get<ssr_component>(); ssr && contrib > 0.0f)
        {
            ssr_enabled_sum += (ssr->enabled ? 1.0f : 0.0f) * contrib;
            ssr_contrib_sum += contrib;
            if(ssr->enabled)
            {
                ssr_component::merge_into(result.ssr, ssr->settings, contrib, first_ssr);
                first_ssr = false;
            }
        }

        if(auto* assao = handle.try_get<assao_component>(); assao && contrib > 0.0f)
        {
            assao_enabled_sum += (assao->enabled ? 1.0f : 0.0f) * contrib;
            assao_contrib_sum += contrib;
            if(assao->enabled)
            {
                assao_component::merge_into(result.assao, assao->settings, contrib, first_assao);
                first_assao = false;
            }
        }

        if(auto* ssil = handle.try_get<ssil_component>(); ssil && contrib > 0.0f)
        {
            ssil_enabled_sum += (ssil->enabled ? 1.0f : 0.0f) * contrib;
            ssil_contrib_sum += contrib;
            if(ssil->enabled)
            {
                ssil_component::merge_into(result.ssil, ssil->settings, contrib, first_ssil);
                first_ssil = false;
            }
        }
    }

    result.has_auto_exposure = auto_exposure_contrib_sum > 0.0f && (auto_exposure_enabled_sum / auto_exposure_contrib_sum) > 0.5f;
    result.has_bloom = bloom_contrib_sum > 0.0f && (bloom_enabled_sum / bloom_contrib_sum) > 0.5f;
    result.has_tonemapping = tonemapping_contrib_sum > 0.0f && (tonemapping_enabled_sum / tonemapping_contrib_sum) > 0.5f;
    result.has_fxaa = fxaa_contrib_sum > 0.0f && (fxaa_enabled_sum / fxaa_contrib_sum) > 0.5f;
    result.has_ssr = ssr_contrib_sum > 0.0f && (ssr_enabled_sum / ssr_contrib_sum) > 0.5f;
    result.has_assao = assao_contrib_sum > 0.0f && (assao_enabled_sum / assao_contrib_sum) > 0.5f;
    result.has_ssil = ssil_contrib_sum > 0.0f && (ssil_enabled_sum / ssil_contrib_sum) > 0.5f;

    return result;
}

} // namespace unravel
