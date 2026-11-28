#include "particle_system.h"
#include "threadpp/thread.h"
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/play_mode.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/particles/ps/particle_system_soa.h>
#include <engine/settings/settings.h>
#include <graphics/graphics.h>
#include <logging/logging.h>


#include <concurrency/parallel.h>


namespace unravel
{
namespace
{
/// Below this radius, metres, a particle is swept as a ray: a sphere that small finds what a ray does.
constexpr float k_min_particle_sphere_radius = 0.01f;

/// Whether @p scn is the project's scene, the one the game plays, and not a scene a tool keeps for itself.
auto is_project_scene(scene& scn) -> bool
{
    return &scn == &engine::context().get_cached<ecs>().get_scene();
}

/// Whether @p scn is the scene the game plays right now: its scripts run and entities may be made in it.
auto is_played_scene(scene& scn) -> bool
{
    return engine::context().get_cached<play_mode>().is_active() && is_project_scene(scn);
}

/// Most events that wait for scripts: past it no more are kept until the scripts took them (the game shows its
/// splash, or its scripts do not run).
constexpr size_t k_max_waiting_script_events = 65536;

/// Whether the project draws decals (Graphics > Allowed Features > Decals).
auto are_decals_allowed() -> bool
{
    auto& ctx = engine::context();
    return !ctx.has<settings>() || ctx.get<settings>().graphics.limits.decals;
}

/// How many events wait in @p batches.
auto count_particle_script_events(const std::vector<particle_script_events>& batches) -> size_t
{
    size_t count = 0;
    for(const auto& batch : batches)
    {
        count += batch.events.size();
    }
    return count;
}

/// A cast that travels less than this, in metres, before it touches started in contact with what it touched.
constexpr float k_particle_touching_distance = 1.0e-4f;

/// What a cast of the physics world touched, as a particle sweep reads it.
void write_particle_sweep_hit(const raycast_hit& touched, ps_soa::particle_sweep_hit& hit)
{
    hit.distance = touched.distance;
    hit.point = touched.point;
    hit.normal = touched.normal;
    hit.entity = uint32_t(entt::to_integral(touched.entity));
}

/// The sweep the particles of @p scn make against its physics world. Empty where it has none: one exists while the
/// game plays.
auto make_particle_scene_sweep(scene& scn) -> ps_soa::particle_sweep
{
    physics_system** found = scn.registry->ctx().find<physics_system*>();
    if(!found || !*found)
    {
        return {};
    }
    const physics_system* physics = *found;
    return [physics](const math::vec3& from, const math::vec3& to, float radius, int layers, ps_soa::particle_sweep_hit& hit)
    {
        const math::vec3 travel = to - from;
        const float length = math::length(travel);
        const bool is_ray = radius < k_min_particle_sphere_radius;
        const auto nearest = is_ray ? physics->ray_cast(from, travel, length, layers, false)
                                    : physics->sphere_cast(from, travel, radius, length, layers, false);
        if(!nearest)
        {
            return false;
        }
        if(nearest->distance > k_particle_touching_distance)
        {
            write_particle_sweep_hit(*nearest, hit);
            return true;
        }
        // The nearest collider is one the sphere starts in contact with, and it hides the ones past it: of every
        // collider on the way, the first one past the start counts.
        hit.starts_touching = true;
        const auto all = is_ray ? physics->ray_cast_all(from, travel, length, layers, false)
                                : physics->sphere_cast_all(from, travel, radius, length, layers, false);
        const raycast_hit* first = nullptr;
        for(const auto& touched : all)
        {
            if(touched.distance > k_particle_touching_distance && (first == nullptr || touched.distance < first->distance))
            {
                first = &touched;
            }
        }
        if(first == nullptr)
        {
            return false;
        }
        write_particle_sweep_hit(*first, hit);
        return true;
    };
}
} // namespace

particle_system::particle_system() = default;
particle_system::~particle_system() = default;

auto particle_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Initialize the soa particle system (large emitter pool for editor/scenes).
    constexpr uint16_t EMITTER_POOL_SIZE = 4096 * 4;
    ps_soa::init(EMITTER_POOL_SIZE);
    ps_soa::init_gpu(ctx);
    

    initialized_ = true;

    return true;
}

auto particle_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(initialized_)
    {
        ps_soa::shutdown();
        initialized_ = false;
    }
    
    return true;
}

void particle_system::on_frame_before_render(scene& scn, const frame_times& times)
{
    if(!initialized_)
    {
        return;
    }

    APP_SCOPE_PERF("Particles/System Update");

    auto& registry = *scn.registry;
    
    // Update all particle emitter components that have both transform and particle emitter components
    auto view = registry.view<transform_component, particle_emitter_component, active_component>();
    
    // Emitters that share an ancestor (several under one moving parent) would resolve it lazily
    // from several tasks at once; resolve those chains first.
    for(const auto entity : view)
    {
        view.get<transform_component>(entity).resolve_shared_ancestors();
    }
    static const std::string thread_name = "Particle/Pool Thread";
    // Each task works with one emitter and only reads the ancestors resolved above.
    // Over the view's leading pool, not its iterator: an entt view iterator is forward-only and
    // poolstl re-walks it once per chunk (see for_each_entity_par).
    poolstl::for_each_entity_par(true,
        view,
        [&](entt::entity entity)
    {
        APP_SCOPE_PERF_THREAD("Particle/Update Emitter","Pool Thread");
        // The emitter reads its material's asset below.
        tpp::this_thread::register_this_thread(thread_name, true);
        auto& transform_comp = view.get<transform_component>(entity);
        auto& emitter_comp = view.get<particle_emitter_component>(entity);
        // The overrides set since the last frame reach the material this emitter draws with. Here, in the emitter's
        // own task, so the passes below only read it.
        emitter_comp.refresh_material();
        // Same grace as model_system: leave newly created as "used" for one frame.
        if(emitter_comp.is_newly_created())
        {
            emitter_comp.set_last_render_frame(uint64_t(gfx::get_render_frame()));
        }
        const auto& world_transform = transform_comp.get_transform_global();
        emitter_comp.update_emitter(world_transform, times.scaled);
    });

    // Physics casts run on one thread, while the physics world is at rest.
    const particle_collision_stats swept = resolve_collisions(scn);
    if(is_project_scene(scn))
    {
        collision_stats_ = swept;
    }

    hand_out_events(scn);

    // Mesh emitters resolve their mesh's materials: one thread, after every emitter updated.
    view.each(
        [](auto entity, auto&& transform_comp, auto&& emitter_comp, auto&& active)
        {
            emitter_comp.refresh_emission_mesh();
            emitter_comp.refresh_render_mesh();
            emitter_comp.refresh_cutout();
        });

    // Every emitter uploads the particles it spawned this frame (and the table rows that changed) to the GPU pool.
    ps_soa::sync_gpu_simulation();
}

auto particle_system::resolve_collisions(scene& scn) -> particle_collision_stats
{
    sweeping_emitters_.clear();
    auto view = scn.registry->view<particle_emitter_component, active_component>();
    for(const auto entity : view)
    {
        auto& emitter = view.get<particle_emitter_component>(entity);
        if(emitter.sweeps_colliders())
        {
            sweeping_emitters_.push_back(&emitter);
        }
    }
    if(sweeping_emitters_.empty())
    {
        return {};
    }
    APP_SCOPE_PERF("Particles/Collision Sweeps");
    // Without a physics world the emitters forget the paths that wait: there is nothing to sweep them against.
    const ps_soa::particle_sweep sweep = make_particle_scene_sweep(scn);
    uint32_t casts_left = ps_soa::get_quality().max_collision_casts;
    particle_collision_stats stats;
    const auto resolve = [&](particle_emitter_component& emitter, uint32_t share)
    {
        const ps_soa::collision_resolve_stats resolved = emitter.resolve_collisions(sweep, share);
        casts_left -= math::min(casts_left, resolved.casts);
        stats.casts += resolved.casts;
        stats.hits += resolved.hits;
        return resolved.waiting;
    };
    // The emitters take turns at going first, so a short limit starves none of them. The ones with paths that wait
    // share the casts evenly, and what one does not use goes to the ones after it; the others are told only that
    // sweeps run (or do not).
    const size_t count = sweeping_emitters_.size();
    const size_t first = sweep_turn_++ % count;
    uint32_t busy = 0;
    for(const particle_emitter_component* emitter : sweeping_emitters_)
    {
        busy += emitter->get_waiting_sweeps() > 0 ? 1u : 0u;
    }
    still_waiting_.clear();
    for(size_t index = 0; index < count; ++index)
    {
        particle_emitter_component& emitter = *sweeping_emitters_[(first + index) % count];
        const bool is_busy = emitter.get_waiting_sweeps() > 0;
        const uint32_t share = is_busy && casts_left > 0 ? math::max(casts_left / math::max(busy, 1u), 1u) : 0u;
        busy -= is_busy && busy > 0 ? 1u : 0u;
        if(resolve(emitter, share) > 0)
        {
            still_waiting_.push_back(&emitter);
        }
    }
    // The casts the turn left over go to the emitters it kept waiting.
    for(particle_emitter_component* emitter : still_waiting_)
    {
        stats.waiting += casts_left > 0 ? resolve(*emitter, casts_left) : emitter->get_waiting_sweeps();
    }
    // Without a physics world nothing was cast: the emitters forgot their paths.
    return sweep ? stats : particle_collision_stats{};
}

void particle_system::hand_out_events(scene& scn)
{
    // Scripts run, and decals are made, only in the scene the game plays.
    const bool is_playing = is_played_scene(scn);
    if(!is_playing && is_project_scene(scn))
    {
        // What a game that ended left for its scripts names entities that are gone.
        script_events_.clear();
    }
    const bool keeps_script_events = is_playing && count_particle_script_events(script_events_) < k_max_waiting_script_events;
    particle_event_outputs outputs;
    outputs.script_events = keeps_script_events ? &script_events_ : nullptr;
    outputs.decals = is_playing && are_decals_allowed() ? &collision_decals_ : nullptr;
    collision_decals_.clear();
    // Sub-emitters queue bursts on other emitters: one thread, after every emitter updated.
    auto view = scn.registry->view<particle_emitter_component, active_component>();
    for(const auto entity : view)
    {
        view.get<particle_emitter_component>(entity).dispatch_events(outputs);
    }
    // After the walk over the emitters: a decal is a new entity.
    spawn_collision_decals(scn);
}

void particle_system::spawn_collision_decals(scene& scn)
{
    if(collision_decals_.empty())
    {
        return;
    }
    APP_SCOPE_PERF("Particles/Collision Decals");
    auto& ctx = engine::context();
    for(const particle_decal_spawn& spawn : collision_decals_)
    {
        // The entity a decal follows may have gone since its particle's path was swept.
        const entt::handle parent = scn.registry->valid(spawn.attach_to) ? entt::handle(*scn.registry, spawn.attach_to)
                                                                         : entt::handle{};
        defaults::spawn_transient_decal(ctx,
                                        scn,
                                        spawn.material,
                                        spawn.point,
                                        spawn.normal,
                                        math::vec2(spawn.size),
                                        spawn.roll_degrees,
                                        spawn.lifetime,
                                        spawn.fade_out,
                                        parent);
    }
    collision_decals_.clear();
}

auto particle_system::get_collision_stats() const -> const particle_collision_stats&
{
    return collision_stats_;
}

void particle_system::take_script_events(std::vector<particle_script_events>& out_events)
{
    out_events.insert(out_events.end(),
                      std::make_move_iterator(script_events_.begin()),
                      std::make_move_iterator(script_events_.end()));
    script_events_.clear();
}

} // namespace unravel
