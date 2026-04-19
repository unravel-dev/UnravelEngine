#include "reflection_probe_system.h"
#include <engine/events.h>

#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <logging/logging.h>

namespace unravel
{

auto reflection_probe_system::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

auto reflection_probe_system::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void reflection_probe_system::on_frame_update(scene& scn, delta_t dt)
{
    APP_SCOPE_PERF("Reflection Probe/System Update");
    const float seconds = dt.count();
    scn.registry->view<transform_component, reflection_probe_component>().each(
        [&](auto e, auto&& transform, auto&& probe)
        {
            if(!scn.registry->all_of<active_component>(e))
            {
                probe.release_resources();
            }

            probe.update(seconds);
        });
}

void reflection_probe_system::on_play_begin(hpp::span<const entt::handle> entities, delta_t dt)
{
    // Ensure play mode starts with up-to-date reflections: flag every probe for a full one-frame bake.
    // Probes in mode 'on_demand' or 'once' will bake exactly once here; realtime probes will resume their interval afterwards.
    for(const auto& entity : entities)
    {
        if(!entity || !entity.valid())
        {
            continue;
        }

        if(auto* probe = entity.try_get<reflection_probe_component>())
        {
            probe->mark_dirty(true);
        }
    }
}

auto reflection_probe_system::mark_all_dirty(scene& scn, bool force_full_first_frame) -> size_t
{
    size_t count = 0;
    scn.registry->view<reflection_probe_component>().each(
        [&](auto /*e*/, auto&& probe)
        {
            probe.mark_dirty(force_full_first_frame);
            ++count;
        });
    return count;
}

} // namespace unravel
