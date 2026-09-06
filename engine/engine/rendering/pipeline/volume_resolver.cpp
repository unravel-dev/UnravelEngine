#include "volume_resolver.h"
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/volume_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/gi_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/gtao_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <algorithm>
#include <cmath>
#include <limits>
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
    /// World-space volume of the bounds (product of dimensions). Globals use the
    /// maximum value so a smaller, more specific local always wins a tie.
    float size;
};

/// Resolves an effect's on/off state across overlapping volumes using the same
/// override model as the settings blend (see *_component::merge_into): the first
/// contributing volume seeds the state, and any later volume whose contribution
/// dominates (>= 0.5, e.g. the camera is inside a local volume) overrides it.
/// This lets a local volume turn an effect on even when a global volume that has
/// it disabled also contributes, which a contribution-weighted average could not.
struct enabled_resolver
{
    bool seen = false;
    bool enabled = false;

    void accumulate(bool component_enabled, float contribution)
    {
        if(!seen)
        {
            enabled = component_enabled;
            seen = true;
            return;
        }
        if(contribution >= 0.5f)
        {
            enabled = component_enabled;
        }
    }

    auto resolve() const -> bool
    {
        return seen && enabled;
    }
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

    scn.registry->view<transform_component, volume_component, active_component>().each(
        [&](entt::entity e, const transform_component& transform_comp, const volume_component& volume_comp, const active_component& active_comp)
        {
            float contribution = 0.0f;
            float size = std::numeric_limits<float>::max();
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
                const math::vec3 dims = world_bounds.get_dimensions();
                size = dims.x * dims.y * dims.z;
            }
            if(contribution > 0.0f)
            {
                contributions.push_back({e, contribution, volume_comp.priority, volume_comp.mode == volume_mode::global, size});
            }
        });

    // Order volumes so the most authoritative one is applied last: the merge
    // model seeds the result with the first volume and lets later volumes
    // override it (see *_component::merge_into and enabled_resolver). Sorting
    // ascending by priority makes a higher-priority volume win; the
    // global-before-local tiebreak lets a local volume override a global at
    // equal priority; for the same priority and type the larger contribution is
    // applied last; and when contributions tie (e.g. the camera is fully inside
    // two overlapping locals) the smaller, more specific volume is applied last
    // so it wins.
    std::sort(contributions.begin(),
              contributions.end(),
              [](const volume_contribution& a, const volume_contribution& b)
              {
                  if(a.priority != b.priority)
                  {
                      return a.priority < b.priority;
                  }
                  if(a.is_global != b.is_global)
                  {
                      return a.is_global && !b.is_global;
                  }
                  if(a.contribution != b.contribution)
                  {
                      return a.contribution < b.contribution;
                  }
                  return a.size > b.size;
              });

    bool first_auto_exposure = true;
    bool first_bloom = true;
    bool first_tonemapping = true;
    bool first_assao = true;
    bool first_ssr = true;
    bool first_ssil = true;
    bool first_gtao = true;
    bool first_gi = true;
    bool first_taa = true;
    enabled_resolver auto_exposure_enabled;
    enabled_resolver bloom_enabled;
    enabled_resolver tonemapping_enabled;
    enabled_resolver fxaa_enabled;
    enabled_resolver ssr_enabled;
    enabled_resolver assao_enabled;
    enabled_resolver ssil_enabled;
    enabled_resolver gtao_enabled;
    enabled_resolver gi_enabled;
    enabled_resolver taa_enabled;

    for(const auto& c : contributions)
    {
        auto handle = scn.create_handle(c.entity);
        const float contrib = c.contribution;

        if(auto* ae = handle.try_get<auto_exposure_component>(); ae && contrib > 0.0f)
        {
            auto_exposure_enabled.accumulate(ae->enabled, contrib);
            if(ae->enabled)
            {
                auto_exposure_component::merge_into(result.auto_exposure, ae->settings, contrib, first_auto_exposure);
                first_auto_exposure = false;
            }
        }

        if(auto* bloom = handle.try_get<bloom_component>(); bloom && contrib > 0.0f)
        {
            bloom_enabled.accumulate(bloom->enabled, contrib);
            if(bloom->enabled)
            {
                bloom_component::merge_into(result.bloom, bloom->settings, contrib, first_bloom);
                first_bloom = false;
            }
        }

        if(auto* tonemapping = handle.try_get<tonemapping_component>(); tonemapping && contrib > 0.0f)
        {
            tonemapping_enabled.accumulate(tonemapping->enabled, contrib);
            if(tonemapping->enabled)
            {
                tonemapping_component::merge_into(result.tonemapping, tonemapping->settings, contrib, first_tonemapping);
                first_tonemapping = false;
            }
        }

        if(auto* fxaa = handle.try_get<fxaa_component>(); fxaa && contrib > 0.0f)
        {
            fxaa_enabled.accumulate(fxaa->enabled, contrib);
        }

        if(auto* taa = handle.try_get<taa_component>(); taa && contrib > 0.0f)
        {
            taa_enabled.accumulate(taa->enabled, contrib);
            if(taa->enabled)
            {
                taa_component::merge_into(result.taa, taa->settings, contrib, first_taa);
                first_taa = false;
            }
        }

        if(auto* ssr = handle.try_get<ssr_component>(); ssr && contrib > 0.0f)
        {
            ssr_enabled.accumulate(ssr->enabled, contrib);
            if(ssr->enabled)
            {
                ssr_component::merge_into(result.ssr, ssr->settings, contrib, first_ssr);
                first_ssr = false;
            }
        }

        if(auto* assao = handle.try_get<assao_component>(); assao && contrib > 0.0f)
        {
            assao_enabled.accumulate(assao->enabled, contrib);
            if(assao->enabled)
            {
                assao_component::merge_into(result.assao, assao->settings, contrib, first_assao);
                first_assao = false;
            }
        }

        if(auto* ssil = handle.try_get<ssil_component>(); ssil && contrib > 0.0f)
        {
            ssil_enabled.accumulate(ssil->enabled, contrib);
            if(ssil->enabled)
            {
                ssil_component::merge_into(result.ssil, ssil->settings, contrib, first_ssil);
                first_ssil = false;
            }
        }

        if(auto* gtao = handle.try_get<gtao_component>(); gtao && contrib > 0.0f)
        {
            gtao_enabled.accumulate(gtao->enabled, contrib);
            if(gtao->enabled)
            {
                gtao_component::merge_into(result.gtao, gtao->settings, contrib, first_gtao);
                first_gtao = false;
            }
        }

        if(auto* gi = handle.try_get<gi_component>(); gi && contrib > 0.0f)
        {
            gi_enabled.accumulate(gi->enabled, contrib);
            if(gi->enabled)
            {
                gi_component::merge_into(result.gi, gi->settings, contrib, first_gi);
                first_gi = false;
            }
        }
    }

    result.has_auto_exposure = auto_exposure_enabled.resolve();
    result.has_bloom = bloom_enabled.resolve();
    result.has_tonemapping = tonemapping_enabled.resolve();
    result.has_fxaa = fxaa_enabled.resolve();
    result.has_taa = taa_enabled.resolve();
    result.has_ssr = ssr_enabled.resolve();
    result.has_assao = assao_enabled.resolve();
    result.has_ssil = ssil_enabled.resolve();
    result.has_gtao = gtao_enabled.resolve();
    result.has_gi = gi_enabled.resolve();

    return result;
}

} // namespace unravel
