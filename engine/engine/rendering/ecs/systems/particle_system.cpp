#include "particle_system.h"
#include "threadpp/thread.h"
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/particles/ps/particle_system.h>
#include <engine/profiler/profiler.h>
#include <logging/logging.h>


#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>


namespace unravel
{

auto particle_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    // Initialize the particle system with default parameters
    // 64 max emitters, using default allocator
    psInit(4096 * 4, nullptr);
    
    initialized_ = true;
    
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
    
    return true;
}

void particle_system::on_frame_before_render(scene& scn, delta_t dt)
{
    if(!initialized_)
    {
        return;
    }

    APP_SCOPE_PERF("Particles/System Update");

    auto& registry = *scn.registry;
    
    // Update all particle emitter components that have both transform and particle emitter components
    auto view = registry.view<transform_component, particle_emitter_component, active_component>();
    
       // this code should be thread safe as each task works with a whole hierarchy and
    // there is no interleaving between tasks.
    std::for_each(std::execution::par,
        view.begin(),
        view.end(),
        [&](entt::entity entity)
    {
        // This is not needed as we dont cal .get on any assets here
        // tpp::this_thread::register_this_thread();

        auto& transform_comp = view.get<transform_component>(entity);
        auto& emitter_comp = view.get<particle_emitter_component>(entity);
        
        // Get the world transform and pass it to the emitter
        const auto& world_transform = transform_comp.get_transform_global();
        emitter_comp.update_emitter(world_transform, dt);
    });
    
}

} // namespace unravel
