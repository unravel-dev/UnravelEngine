#include "reflection_probe_system.h"
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>

#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/reflection_probe.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/profiler/profiler.h>
#include <logging/logging.h>

#include <hpp/optional.hpp>

namespace unravel
{

namespace
{

/// Whether the view uses @p probe, its bake needs faces this frame, and it is the turn of probes of @p rank to take
/// them.
auto needs_faces_at_rank(const reflection_probe_component& probe, probe_bake_rank rank, bool gi_reflections_own_view)
    -> bool
{
    return reflection_probe_system::is_used_in_view(probe.get_probe(), gi_reflections_own_view) &&
           !probe.already_generated() && reflection_probe_system::get_bake_rank(probe) == rank;
}

/// The next face @p probe's bake needs this frame, taken from @p budget and recorded as generated on @p frame: none
/// when every face is rendered, the probe's own slice for the frame is spent, or the budget is.
auto take_next_face(reflection_probe_component& probe, probe_face_budget& budget, std::uint64_t frame)
    -> hpp::optional<std::uint32_t>
{
    for(std::uint32_t face = 0; face < 6; ++face)
    {
        // A face already rendered this cycle, or past the probe's own slice for this frame.
        if(probe.already_generated(face))
        {
            continue;
        }
        if(!budget.try_take())
        {
            return hpp::nullopt;
        }
        probe.set_generation_frame(face, frame);
        return face;
    }
    return hpp::nullopt;
}

/// Renders the faces of one probe the view sees that its slice, @p budget and the frame's render passes allow.
void bake_probe_faces(const probe_bake_candidate& candidate,
                      probe_face_budget& budget,
                      std::uint64_t frame,
                      const probe_bake_steps& steps)
{
    auto& probe = *candidate.probe;
    steps.begin_probe(candidate);
    bool rendered = false;
    while(steps.has_room_for_face())
    {
        const auto face = take_next_face(probe, budget, frame);
        if(!face)
        {
            break;
        }
        steps.render_face(candidate, *face);
        rendered = true;
    }
    steps.end_probe(candidate, rendered && probe.is_bake_complete());
}

} // namespace

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

void reflection_probe_system::on_frame_update(scene& scn, const frame_times& times)
{
    APP_SCOPE_PERF("Reflection Probe/System Update");
    const float seconds = times.scaled.count();
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
    // Ensure play mode starts with up-to-date reflections: flag every probe for a whole bake, which the frames'
    // face budget spreads over the first frames of play. Probes in mode 'on_demand' or 'once' will bake exactly
    // once here; realtime probes will resume their interval afterwards.
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

void reflection_probe_system::on_create_active_component(entt::registry& r, entt::entity e)
{
    auto* probe = r.try_get<reflection_probe_component>(e);
    if(!probe)
    {
        return;
    }
    // In play mode, on_demand probes keep their manual contract: gameplay activating
    // pooled/streamed objects must not pay surprise full-frame bakes - the owning script
    // schedules the rebake explicitly via MarkDirty. In edit mode every mode refreshes:
    // the product cubemaps were released while inactive, and the person flipping the
    // active toggle expects a valid capture, not an uninitialized cubemap.
    if(probe->get_update_mode() == probe_update_mode::on_demand)
    {
        if(engine::context().get_cached<play_mode>().is_active())
        {
            return;
        }
    }
    probe->mark_dirty(true);
}

auto reflection_probe_system::get_face_budget() -> probe_face_budget&
{
    static probe_face_budget budget;
    return budget;
}

auto reflection_probe_system::get_bake_rank(const reflection_probe_component& probe) -> probe_bake_rank
{
    if(!probe.has_capture())
    {
        return probe_bake_rank::first_capture;
    }
    if(probe.is_whole_bake())
    {
        return probe_bake_rank::whole_bake;
    }
    return probe_bake_rank::refresh;
}

auto reflection_probe_system::is_used_in_view(const reflection_probe& probe, bool gi_reflections_own_view) -> bool
{
    return !gi_reflections_own_view || probe.method == reflect_method::environment;
}

void reflection_probe_system::bake_due_probes(hpp::span<const probe_bake_candidate> candidates,
                                              probe_face_budget& budget,
                                              bool gi_reflections_own_view,
                                              std::uint64_t frame,
                                              const probe_bake_steps& steps)
{
    for(const probe_bake_rank rank : {probe_bake_rank::first_capture, probe_bake_rank::whole_bake, probe_bake_rank::refresh})
    {
        for(const auto& candidate : candidates)
        {
            if(!budget.has_faces_left() || !steps.has_room_for_face())
            {
                return;
            }
            if(needs_faces_at_rank(*candidate.probe, rank, gi_reflections_own_view) && steps.sees(candidate))
            {
                bake_probe_faces(candidate, budget, frame, steps);
            }
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
