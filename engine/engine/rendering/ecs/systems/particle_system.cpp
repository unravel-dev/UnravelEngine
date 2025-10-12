#include "particle_system.h"
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/particles/ps/particle_system.h>
#include <engine/profiler/profiler.h>
#include <logging/logging.h>

namespace unravel
{

auto particle_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Initialize the particle system with default parameters
    // 64 max emitters, using default allocator
    psInit(4096 * 4, nullptr);
    
    initialized_ = true;
    
    APPLOG_INFO("Particle system initialized successfully");
    return true;
}

auto particle_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(initialized_)
    {
        // Shutdown the particle system
        psShutdown();
        initialized_ = false;
    }
    
    APPLOG_INFO("Particle system deinitialized successfully");
    return true;
}

void particle_system::on_frame_update(scene& scn, delta_t dt)
{
    if(!initialized_)
    {
        return;
    }

    APP_SCOPE_PERF("Particles/System Update");

    auto& registry = *scn.registry;
    
    // Update all particle emitter components that have both transform and particle emitter components
    auto view = registry.view<transform_component, particle_emitter_component>();
    
    for(auto entity : view)
    {
        auto& transform_comp = view.get<transform_component>(entity);
        auto& emitter_comp = view.get<particle_emitter_component>(entity);
        
        // Get the world transform and pass it to the emitter
        const auto& world_transform = transform_comp.get_transform_global();
        emitter_comp.update_emitter(world_transform, dt);
    }
    
}

} // namespace unravel
