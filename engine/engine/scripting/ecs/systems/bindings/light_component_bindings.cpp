#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/rendering/ecs/components/light_component.h>

namespace unravel
{
namespace
{

//------------------------------

void internal_m2n_light_set_color(entt::entity id, const math::color& color)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.color = color;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().color;
    }

    return math::color::white();
}

} // namespace

void register_light_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.LightComponent");
    reg.add_internal_call("internal_m2n_light_get_color", dotnet_internal_call(internal_m2n_light_get_color));
    reg.add_internal_call("internal_m2n_light_set_color", dotnet_internal_call(internal_m2n_light_set_color));
}

} // namespace unravel
