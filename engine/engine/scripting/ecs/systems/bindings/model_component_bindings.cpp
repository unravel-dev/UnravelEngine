#include "script_glue_common.h"
#include "script_bindings.h"
#include "material_script_helpers.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/rendering/ecs/components/model_component.h>

namespace unravel
{
namespace
{

auto internal_m2n_model_get_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->is_enabled();
    }

    return false;
}

void internal_m2n_model_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_enabled(enabled);
    }
}

auto internal_m2n_model_get_shared_material(entt::entity id, uint32_t index) -> hpp::uuid
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_material(index).uid();
    }

    return {};
}

auto internal_m2n_model_get_shared_material_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_materials().size();
    }

    return {};
}

auto internal_m2n_model_get_material_instance(entt::entity id, uint32_t index)
    -> dotnetpp_backend::managed_interface::material_properties
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto instance = comp->get_model().get_material_instance(index);
        return get_material_properties(instance);
    }

    return {};
}

void internal_m2n_model_set_shared_material(entt::entity id, const hpp::uuid& uid, uint32_t index)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto asset = am.get_asset<material>(uid);

        auto model = comp->get_model();
        model.set_material(asset, index);
        comp->set_model(model);
    }
}

void internal_m2n_model_set_material_instance(entt::entity id,
                                              const dotnetpp_backend::managed_interface::material_properties& props,
                                              uint32_t index)
{
    using converter = dotnet::managed_interface::converter;

    if(auto comp = safe_get_component<model_component>(id))
    {
        auto model = comp->get_model();

        if(props.valid)
        {
            auto material = model.get_or_emplace_material_instance(index);
            set_material_properties(material, props);
            model.set_material_instance(material, index);
        }
        else
        {
            model.set_material_instance(nullptr, index);
        }
        comp->set_model(model);
    }
}

auto internal_m2n_model_get_material_instance_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_material_instances().size();
    }

    return {};
}

//------------------------------

} // namespace

void register_model_component_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

    auto reg = dotnet::internal_call_registry("Unravel.Core.ModelComponent");
    reg.add_internal_call("internal_m2n_model_get_enabled", dotnet_internal_call(internal_m2n_model_get_enabled));
    reg.add_internal_call("internal_m2n_model_set_enabled", dotnet_internal_call(internal_m2n_model_set_enabled));
    reg.add_internal_call("internal_m2n_model_get_shared_material",
                            dotnet_internal_call(internal_m2n_model_get_shared_material));
    reg.add_internal_call("internal_m2n_model_get_shared_material_count",
                            dotnet_internal_call(internal_m2n_model_get_shared_material_count));
    reg.add_internal_call("internal_m2n_model_set_shared_material",
                            dotnet_internal_call(internal_m2n_model_set_shared_material));
    reg.add_internal_call("internal_m2n_model_set_material_instance",
                            dotnet_internal_call(internal_m2n_model_set_material_instance));
    reg.add_internal_call("internal_m2n_model_get_material_instance",
                            dotnet_internal_call(internal_m2n_model_get_material_instance));
    reg.add_internal_call("internal_m2n_model_get_material_instance_count",
                            dotnet_internal_call(internal_m2n_model_get_material_instance_count));
}

} // namespace unravel
