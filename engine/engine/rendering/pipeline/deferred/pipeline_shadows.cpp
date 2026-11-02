#include "pipeline_internal.h"

namespace unravel
{
namespace
{
namespace ANONYMOUS
{
using namespace rendering::deferred_detail;

} // namespace ANONYMOUS
} // namespace

namespace rendering
{

namespace
{
/// Copies into @p light_casters the casters whose world bounds reach into a local light's
/// bounding sphere (world space). A caster wholly outside it is beyond the light's reach:
/// every ray from the light to a lit receiver stays inside that volume, so such a caster has
/// nothing to shadow. Both lists are pipeline-owned scratch whose clear() keeps the storage -
/// once warmed up this allocates nothing, and it reads nothing but the contiguous list.
void cull_shadow_casters(const shadow::shadow_map_models_t& static_casters,
                         const shadow::shadow_map_models_t& dynamic_casters,
                         const shadow::shadow_map_models_t& instanced_casters,
                         const math::bsphere& light_sphere,
                         shadow::shadow_map_models_t& light_casters)
{
    APP_SCOPE_PERF("Rendering/Shadow Caster Cull Per Light");
    light_casters.clear();
    const float radius_squared = light_sphere.radius * light_sphere.radius;
    const auto keep_casters_in_range = [&](const shadow::shadow_map_models_t& casters)
    {
        for(const auto& caster : casters)
        {
            const math::vec3 to_closest =
                caster.world_bounds.closest_point(light_sphere.position) - light_sphere.position;
            if(math::dot(to_closest, to_closest) <= radius_squared)
            {
                light_casters.emplace_back(caster);
            }
        }
    };
    keep_casters_in_range(static_casters);
    keep_casters_in_range(dynamic_casters);
    keep_casters_in_range(instanced_casters);
}
} // namespace

void deferred::refresh_shadow_casters(scene& scn, const camera& camera, delta_t dt, layer_mask render_mask)
{
    const auto* revision = scn.registry->ctx().find<shadow_caster_revision>();
    const uint64_t current_revision = revision != nullptr ? revision->value : 0ULL;
    // The revision counts per registry: another scene's lists can sit at the same number with other entities.
    const bool lists_retired = scn.registry.get() != shadow_casters_registry_ ||
                               current_revision != shadow_casters_revision_ ||
                               render_mask.mask != shadow_casters_mask_.mask;

    if(lists_retired)
    {
        APP_SCOPE_PERF("Rendering/Shadow Casters Rebuild");
        // The ONE walk over every model in the scene. Everything the split depends on -
        // membership, static-ness, a static caster's bounds - is behind the revision, so in a
        // steady scene this does not run again.
        shadow_static_casters_.clear();
        shadow_dynamic_casters_.clear();
        const auto query = visibility_flags{visibility_query::is_shadow_caster};
        // No LOD reference camera: the shadow submit resolves LOD itself, per caster it draws.
        gather_visible_models(scn, nullptr, query, render_mask, dt,
            [&](entt::handle entity, const lod_data& /*lod_data*/)
            {
                const auto& model_comp = entity.get<model_component>();
                auto& casters = model_comp.is_static() ? shadow_static_casters_ : shadow_dynamic_casters_;
                casters.emplace_back(shadow::shadow_visibility_data{entity, model_comp.get_world_bounds()});
            }, nullptr);
        shadow_casters_registry_ = scn.registry.get();
        shadow_casters_revision_ = current_revision;
        shadow_casters_mask_ = render_mask;
        shadow_casters_frame_ = gfx::get_render_frame();
        return;
    }

    const uint64_t frame = gfx::get_render_frame();
    if(shadow_casters_frame_ == frame)
    {
        return;
    }
    shadow_casters_frame_ = frame;

    APP_SCOPE_PERF("Rendering/Shadow Casters Refresh Movers");
    // Movers only: their bounds changed because they moved, which is not a membership change.
    // Static casters keep the bounds captured at the rebuild - a change there bumped the
    // revision and took the branch above.
    for(auto& caster : shadow_dynamic_casters_)
    {
        caster.world_bounds = caster.entity.get<model_component>().get_world_bounds();
    }
}

namespace
{
/// How much of @p camera's view a light reaching @p reach covers: its angular radius, up to 1 from inside it. The
/// shadow atlas keeps the resolution of the lights that cover more the longest.
auto get_shadow_importance(const camera& camera, const math::bsphere& reach) -> float
{
    const float distance = math::distance(camera.get_position(), reach.position);
    return reach.radius / math::max(distance, math::max(reach.radius, 1e-4f));
}
} // namespace

void deferred::reserve_shadow_atlas_tiles(scene& scn, const camera& camera, const feature_limits& limits)
{
    shadow_atlas_requests_.clear();
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            const auto light = limit_light(light_comp.get_light(), limits);
            if(!shadow::shadowmap_generator::uses_shadow_atlas(light) || !light_comp.is_enabled() ||
               !ANONYMOUS::is_light_needed(light, light_comp.get_linear_color()))
            {
                return;
            }
            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();
            const auto light_sphere = light_comp.get_world_bounds_sphere(world_transform);
            if(!camera.get_frustum().test_sphere(light_sphere))
            {
                return;
            }
            const float importance = get_shadow_importance(camera, light_sphere);
            shadow_atlas_requests_.push_back(light_comp.get_shadowmap_generator().make_atlas_request(light, importance));
        });
    // Every view reserves, with or without lights: a new frame first takes back the tiles no view asked for.
    shadow::shadow_atlas::get().reserve(shadow_atlas_requests_);
}

void deferred::build_shadows(scene& scn,
                             const camera& camera,
                             delta_t dt,
                             const feature_limits& limits,
                             layer_mask render_mask)
{
    APP_SCOPE_PERF("Rendering/Shadow Generation Pass");

    // Before any light renders: reserving can grow the atlas or step lights down, which moves tiles.
    reserve_shadow_atlas_tiles(scn, camera, limits);

    bool queried = false;

    scn.registry->view<transform_component, light_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp)
        {
            const auto light = limit_light(light_comp.get_light(), limits);

            bool is_directional = light.type == light_type::directional;
            bool has_render_mask = render_mask.mask != layer_reserved::everything_layer;
            bool camera_dependant = is_directional || has_render_mask;
            bool is_active = scn.registry->all_of<active_component>(e);

            // A point / spot light's shadow maps are the same for every view of the frame, so
            // the first view that resolves them wins and the rest skip the light (see
            // shadowmap_generator::already_resolved). A directional light re-fits its cascades
            // to the view being rendered, and a render mask makes even a local light's caster
            // set view specific, so neither is ever skipped.
            auto& generator = light_comp.get_shadowmap_generator();
            if(!camera_dependant && generator.already_resolved())
            {
                return;
            }

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light");

            auto world_transform = transform_comp.get_transform_global();
            world_transform.reset_scale();

            generator.update(camera, light, world_transform, is_active);

            // Camera independent: no view of this frame wants shadow maps from this light. A disabled
            // light, or one that adds no light, keeps its maps allocated so switching it back on is cheap.
            if(!is_active || !light_comp.is_enabled() || !light.casts_shadows ||
               !ANONYMOUS::is_light_needed(light, light_comp.get_linear_color()))
            {
                generator.mark_resolved();
                return;
            }

            // Camera DEPENDENT, so not a resolution: a later view of this frame (the camera
            // after a probe face capture) may still see this light and have to generate.
            const auto light_sphere = light_comp.get_world_bounds_sphere(world_transform);
            if(!camera.get_frustum().test_sphere(light_sphere))
            {
                return;
            }

            if(!queried)
            {
                refresh_shadow_casters(scn, camera, dt, render_mask);
                queried = true;
            }

            // A directional light reaches every caster (its generator culls them against the
            // view); a local light only gets the casters that reach into its range.
            const shadow::shadow_map_models_t* light_casters = nullptr;
            if(is_directional)
            {
                // One list for the generator: the movers appended to the static ones. Only
                // the tail is rewritten per light, so the static part is not copied again.
                shadow_light_casters_.assign(shadow_static_casters_.begin(), shadow_static_casters_.end());
                shadow_light_casters_.insert(shadow_light_casters_.end(),
                                             shadow_dynamic_casters_.begin(),
                                             shadow_dynamic_casters_.end());
                // A probe's cascades are its own, and a capture does not keep one moment of the sources' motion.
                if(current_run_->is_camera_run())
                {
                    shadow_light_casters_.insert(shadow_light_casters_.end(),
                                                 instance_source_casters_.begin(),
                                                 instance_source_casters_.end());
                }
                light_casters = &shadow_light_casters_;
            }
            else
            {
                cull_shadow_casters(shadow_static_casters_,
                                    shadow_dynamic_casters_,
                                    instance_source_casters_,
                                    light_sphere,
                                    shadow_light_casters_);
                light_casters = &shadow_light_casters_;
            }

            // Nothing in reach: skip, unless the maps still show casters that have since left.
            if(light_casters->empty() && !generator.needs_clear())
            {
                generator.mark_resolved();
                return;
            }

            APP_SCOPE_PERF("Rendering/Shadow Generation Pass Per Light After Cull");

            generator.generate_shadowmaps(*light_casters, camera, &stats_);
        });
}

void deferred::gather_instance_sources(scene& scn, const camera& camera, layer_mask render_mask)
{
    instance_sources_.clear();
    instance_source_casters_.clear();
    APP_SCOPE_PERF("Rendering/Gather Instance Sources");
    const math::vec3 eye = camera.get_position();
    const ps_soa::particle_quality& quality = ps_soa::get_quality();
    scn.registry->view<transform_component, particle_emitter_component, active_component>().each(
        [&](auto entity, auto&& transform_comp, auto&& emitter, auto&& active)
        {
            if(!emitter.is_enabled() || !emitter.is_mesh_render() || !is_on_rendered_layer(*scn.registry, entity, render_mask))
            {
                return;
            }
            const math::bbox& bounds = emitter.get_mesh_bounds();
            const math::bbox emitter_bounds = emitter.get_world_bounds();
            // The same gates as the billboards: beyond the draw distance or smaller on screen than the project
            // allows, the emitter is not drawn (and renderer-based culling freezes it).
            const float max_draw_distance = emitter.get_max_draw_distance();
            if(max_draw_distance > 0.0f &&
               math::distance(eye, math::clamp(eye, emitter_bounds.min, emitter_bounds.max)) > max_draw_distance)
            {
                return;
            }
            if(quality.min_screen_size > 0.0f && get_particle_screen_size(camera, emitter_bounds, eye) < quality.min_screen_size)
            {
                return;
            }
            const bool has_particles = bounds.is_populated() && !emitter.get_mesh_transforms().empty();
            const bool is_visible = has_particles && camera.test_aabb(bounds);
            if(is_visible || camera.test_aabb(emitter_bounds))
            {
                emitter.set_last_render_frame(uint64_t(gfx::get_render_frame()));
            }
            if(is_visible)
            {
                instance_sources_.push_back(&emitter);
            }
            if(has_particles && emitter.get_mesh_cast_shadows())
            {
                instance_source_casters_.push_back(
                    shadow::shadow_visibility_data{entt::handle(*scn.registry, entity), bounds, &emitter});
            }
        });
    // Scripted instance draws are culled tile by tile: each cluster is a source of its own for the camera and the
    // shadow cascades. They belong to no entity, but to the scene scripts run in: other scenes (thumbnails, prefab
    // editing) do not draw them. Their instances are packed once for every pass of the frame.
    if(&scn != &engine::context().get_cached<ecs>().get_scene())
    {
        return;
    }
    auto& instanced_draws = engine::context().get_cached<rendering_system>().get_instanced_draws();
    const auto& clusters = instanced_draws.get_clusters();
    if(!clusters.empty())
    {
        instanced_draws.prepare(instanced_layout::base, is_baked_animation_active());
    }
    for(const auto& cluster : clusters)
    {
        if((cluster.get_layer_mask() & render_mask.mask) == 0)
        {
            continue;
        }
        const math::bbox& bounds = cluster.get_mesh_bounds();
        if(camera.test_aabb(bounds))
        {
            instance_sources_.push_back(&cluster);
        }
        if(cluster.casts_shadow())
        {
            instance_source_casters_.push_back(shadow::shadow_visibility_data{entt::handle{}, bounds, &cluster});
        }
    }
}

} // namespace rendering
} // namespace unravel
