#include "property_actions.h"

namespace unravel
{


// Property Action Implementation
property_action_t::property_action_t(meta_any_proxy inst, const entt::meta_any& old_val, const entt::meta_any& new_val, const entt::meta_custom& custom, const std::function<void()>& on_success, const std::function<void()>& on_undo)
    : instance(inst), old_value(old_val), new_value(new_val), custom(custom), on_success(on_success), on_undo(on_undo)
{
    
    if(!inst.impl->name.empty())
    {
        name = inst.impl->name;
    }
    else
    {
        name = "Property Edit";
    }
}

void property_action_t::do_action()
{
    if(instance.impl->setter(instance, new_value, execution_count))
    {
        if(on_success)
        {
            on_success();
        }
    }
    // After first execution, switch to resolver-based access.
    // The original references will go out of scope after this frame.
    if(execution_count <= 1)
    {
        detach();
    }
}

void property_action_t::undo_action()
{
    if(instance.impl->setter(instance, old_value, execution_count))
    {
        if(on_undo)
        {
            on_undo();
        }
    }
}

void property_action_t::detach()
{
    instance.detach();
}

auto property_action_t::is_mergeable(const editing_action_t& previous) const -> bool
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    entt::meta_any inst;
    instance.impl->getter(inst);
    entt::meta_any prev_inst;
    prev.instance.impl->getter(prev_inst);
    return inst == prev_inst;
}

void property_action_t::merge_with(const editing_action_t& previous)
{
    const auto& prev = static_cast<const property_action_t&>(previous);
    old_value.assign(prev.old_value);
}

auto property_action_t::is_valid() const -> bool
{
    entt::meta_any inst;
    instance.impl->getter(inst);

    return !!inst;
}

void property_action_t::draw_in_inspector(rtti::context& ctx)
{
    draw_in_inspector_impl(ctx, old_value, new_value, custom);
}

} // namespace unravel
