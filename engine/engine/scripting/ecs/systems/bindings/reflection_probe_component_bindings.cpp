#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/reflection_probe.h>

namespace unravel
{
namespace
{

auto internal_m2n_probe_get_type(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return static_cast<uint8_t>(comp->get_probe().type);
    }
    return 0;
}

void internal_m2n_probe_set_type(entt::entity id, uint8_t type)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        auto probe = comp->get_probe();
        probe.type = static_cast<probe_type>(type);
        comp->set_probe(probe);
    }
}

auto internal_m2n_probe_get_method(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return static_cast<uint8_t>(comp->get_probe().method);
    }
    return 0;
}

void internal_m2n_probe_set_method(entt::entity id, uint8_t method)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        auto probe = comp->get_probe();
        probe.method = static_cast<reflect_method>(method);
        comp->set_probe(probe);
    }
}

auto internal_m2n_probe_get_intensity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_probe().intensity;
    }
    return 1.0f;
}

void internal_m2n_probe_set_intensity(entt::entity id, float intensity)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        auto probe = comp->get_probe();
        probe.intensity = intensity;
        comp->set_probe(probe);
    }
}

auto internal_m2n_probe_get_update_mode(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return static_cast<uint8_t>(comp->get_update_mode());
    }
    return 0;
}

void internal_m2n_probe_set_update_mode(entt::entity id, uint8_t mode)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->set_update_mode(static_cast<probe_update_mode>(mode));
    }
}

auto internal_m2n_probe_get_update_interval(entt::entity id) -> float
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_update_interval();
    }
    return 0.0f;
}

void internal_m2n_probe_set_update_interval(entt::entity id, float seconds)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->set_update_interval(seconds);
    }
}

auto internal_m2n_probe_get_resolution(entt::entity id) -> uint8_t
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return static_cast<uint8_t>(comp->get_resolution());
    }
    return 0;
}

void internal_m2n_probe_set_resolution(entt::entity id, uint8_t resolution)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->set_resolution(static_cast<probe_resolution>(resolution));
    }
}

auto internal_m2n_probe_get_box_extents(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_probe().box_data.extents;
    }
    return {};
}

void internal_m2n_probe_set_box_extents(entt::entity id, const math::vec3& extents)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        auto probe = comp->get_probe();
        probe.box_data.extents = extents;
        comp->set_probe(probe);
    }
}

auto internal_m2n_probe_get_sphere_range(entt::entity id) -> float
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_probe().sphere_data.range;
    }
    return 0.0f;
}

void internal_m2n_probe_set_sphere_range(entt::entity id, float range)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        auto probe = comp->get_probe();
        probe.sphere_data.range = range;
        comp->set_probe(probe);
    }
}

auto internal_m2n_probe_get_capture_sky(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_capture_sky();
    }
    return true;
}

void internal_m2n_probe_set_capture_sky(entt::entity id, bool capture)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->set_capture_sky(capture);
    }
}

auto internal_m2n_probe_get_capture_shadows(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->get_capture_shadows();
    }
    return true;
}

void internal_m2n_probe_set_capture_shadows(entt::entity id, bool capture)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->set_capture_shadows(capture);
    }
}

auto internal_m2n_probe_is_dirty(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        return comp->is_dirty();
    }
    return false;
}

void internal_m2n_probe_mark_dirty(entt::entity id, bool force_full_first_frame)
{
    if(auto comp = safe_get_component<reflection_probe_component>(id))
    {
        comp->mark_dirty(force_full_first_frame);
    }
}

} // namespace

void register_reflection_probe_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);
    auto reg = dotnet::internal_call_registry("Unravel.Core.ReflectionProbeComponent");
    reg.add_internal_call("internal_m2n_probe_get_type", dotnet_internal_call(internal_m2n_probe_get_type));
    reg.add_internal_call("internal_m2n_probe_set_type", dotnet_internal_call(internal_m2n_probe_set_type));
    reg.add_internal_call("internal_m2n_probe_get_method", dotnet_internal_call(internal_m2n_probe_get_method));
    reg.add_internal_call("internal_m2n_probe_set_method", dotnet_internal_call(internal_m2n_probe_set_method));
    reg.add_internal_call("internal_m2n_probe_get_intensity",
                          dotnet_internal_call(internal_m2n_probe_get_intensity));
    reg.add_internal_call("internal_m2n_probe_set_intensity",
                          dotnet_internal_call(internal_m2n_probe_set_intensity));
    reg.add_internal_call("internal_m2n_probe_get_update_mode",
                          dotnet_internal_call(internal_m2n_probe_get_update_mode));
    reg.add_internal_call("internal_m2n_probe_set_update_mode",
                          dotnet_internal_call(internal_m2n_probe_set_update_mode));
    reg.add_internal_call("internal_m2n_probe_get_update_interval",
                          dotnet_internal_call(internal_m2n_probe_get_update_interval));
    reg.add_internal_call("internal_m2n_probe_set_update_interval",
                          dotnet_internal_call(internal_m2n_probe_set_update_interval));
    reg.add_internal_call("internal_m2n_probe_get_resolution",
                          dotnet_internal_call(internal_m2n_probe_get_resolution));
    reg.add_internal_call("internal_m2n_probe_set_resolution",
                          dotnet_internal_call(internal_m2n_probe_set_resolution));
    reg.add_internal_call("internal_m2n_probe_get_box_extents",
                          dotnet_internal_call(internal_m2n_probe_get_box_extents));
    reg.add_internal_call("internal_m2n_probe_set_box_extents",
                          dotnet_internal_call(internal_m2n_probe_set_box_extents));
    reg.add_internal_call("internal_m2n_probe_get_sphere_range",
                          dotnet_internal_call(internal_m2n_probe_get_sphere_range));
    reg.add_internal_call("internal_m2n_probe_set_sphere_range",
                          dotnet_internal_call(internal_m2n_probe_set_sphere_range));
    reg.add_internal_call("internal_m2n_probe_get_capture_sky",
                          dotnet_internal_call(internal_m2n_probe_get_capture_sky));
    reg.add_internal_call("internal_m2n_probe_set_capture_sky",
                          dotnet_internal_call(internal_m2n_probe_set_capture_sky));
    reg.add_internal_call("internal_m2n_probe_get_capture_shadows",
                          dotnet_internal_call(internal_m2n_probe_get_capture_shadows));
    reg.add_internal_call("internal_m2n_probe_set_capture_shadows",
                          dotnet_internal_call(internal_m2n_probe_set_capture_shadows));
    reg.add_internal_call("internal_m2n_probe_is_dirty", dotnet_internal_call(internal_m2n_probe_is_dirty));
    reg.add_internal_call("internal_m2n_probe_mark_dirty", dotnet_internal_call(internal_m2n_probe_mark_dirty));
}

} // namespace unravel
