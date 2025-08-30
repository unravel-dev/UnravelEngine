#include "property_actions.h"

namespace unravel
{


// Property Action Implementation
property_action_t::property_action_t(meta_any_proxy inst, const entt::meta_any& old_val, const entt::meta_any& new_val, const entt::meta_custom& custom, const std::function<void()>& on_success)
    : instance(inst), old_value(old_val), new_value(new_val), custom(custom), on_success(on_success)
{
    name = "Property Edit";
    
    if(inst.impl->get_name)
    {
        name += " " + inst.impl->get_name();
    }
}

void property_action_t::do_action()
{
    if(instance.impl->setter(instance, new_value))
    {
        if(on_success)
        {
            on_success();
        }
    }
}

void property_action_t::undo_action()
{
    if(instance.impl->setter(instance, old_value))
    {
        if(on_success)
        {
            on_success();
        }
    }
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
