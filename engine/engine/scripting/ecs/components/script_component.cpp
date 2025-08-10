#include "script_component.h"
#include <monopp/mono_property.h>
#include <monopp/mono_property_invoker.h>

#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/scripting/ecs/systems/script_system.h>
namespace unravel
{

namespace
{

struct managed_vector3
{
    float x, y, z;
};

struct managed_contact_point
{
    managed_vector3 point{};
    managed_vector3 normal{};
    float distance{};
    float impulse{};
};

} // namespace

void script_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);

    auto& component = entity.get<script_component>();
    component.set_owner(entity);
}

void script_component::on_destroy_component(entt::registry& r, entt::entity e)
{
}

void script_component::create()
{
    process_pending_creates();
}
void script_component::start()
{
    process_pending_starts();

    process_pending_deletions();
}

void script_component::destroy()
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     remove_script_component(obj);
                 });

    process_pending_deletions();
}

void script_component::enable()
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     enable(script, true);
                 });
}

void script_component::disable()
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     disable(script, true);
                 });
}

void script_component::on_sensor_enter(entt::handle other)
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     on_sensor_enter(obj, other);
                 });
}

void script_component::on_sensor_exit(entt::handle other)
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     on_sensor_exit(obj, other);
                 });
}

void script_component::on_collision_enter(entt::handle b, const std::vector<manifold_point>& manifolds, bool use_b)
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     on_collision_enter(obj, b, manifolds, use_b);
                 });
}

void script_component::on_collision_exit(entt::handle b, const std::vector<manifold_point>& manifolds, bool use_b)
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto& obj = script.scoped->object;
                     on_collision_exit(obj, b, manifolds, use_b);
                 });
}

void script_component::enable(script_object& script_obj, bool check_order)
{
    if(script_obj.is_enabled() || script_obj.is_marked_for_destroy())
    {
        return;
    }

    script_obj.state->active = true;

    if(check_order)
    {
        if(!script_obj.is_create_called())
        {
            return;
        }
    }

    auto& obj = script_obj.scoped->object;

    auto method = mono::make_method_invoker<void()>(obj, "internal_n2m_on_enable");
    method(obj);
}

void script_component::disable(script_object& script_obj, bool check_order)
{
    if(script_obj.is_disabled() || script_obj.is_marked_for_destroy())
    {
        return;
    }

    script_obj.state->active = 0;

    if(check_order)
    {
        if(!script_obj.is_create_called())
        {
            return;
        }
    }
    auto& obj = script_obj.scoped->object;

    try
    {
        auto method = mono::make_method_invoker<void()>(obj, "internal_n2m_on_disable");
        method(obj);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::create(script_object& script_obj)
{
    if(script_obj.is_create_called())
    {
        return;
    }

    script_obj.state->create_called = true;
    auto& obj = script_obj.scoped->object;

    try
    {
        auto method = mono::make_method_invoker<void()>(obj, "internal_n2m_on_create");
        method(obj);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}
void script_component::start(script_object& script_obj)
{
    if(script_obj.is_start_called())
    {
        return;
    }
    script_obj.state->start_called = true;
    auto& obj = script_obj.scoped->object;

    try
    {
        auto method = mono::make_method_invoker<void()>(obj, "internal_n2m_on_start");
        method(obj);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::destroy(script_object& script_obj)
{
    auto& obj = script_obj.scoped->object;
    try
    {
        auto method = mono::make_method_invoker<void()>(obj, "internal_n2m_on_destroy");
        method(obj);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::set_entity(const mono::mono_object& obj, entt::handle e)
{
    try
    {
        auto method = mono::make_method_invoker<void(entt::entity)>(obj, "internal_n2m_set_entity");
        method(obj, e.entity());
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_sensor_enter(const mono::mono_object& obj, entt::handle other)
{
    try
    {
        auto method = mono::make_method_invoker<void(entt::entity)>(obj, "internal_n2m_on_sensor_enter");
        method(obj, other.entity());
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_sensor_exit(const mono::mono_object& obj, entt::handle other)
{
    try
    {
        auto method = mono::make_method_invoker<void(entt::entity)>(obj, "internal_n2m_on_sensor_exit");
        method(obj, other.entity());
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_collision_enter(const mono::mono_object& obj,
                                          entt::handle b,
                                          const std::vector<manifold_point>& manifolds,
                                          bool use_b)
{
    std::vector<managed_contact_point> points;
    points.reserve(manifolds.size());
    for(const auto& manifold : manifolds)
    {
        auto& point = points.emplace_back();
        if(use_b)
        {
            point.point = {manifold.b.x, manifold.b.y, manifold.b.z};
            point.normal = {manifold.normal_on_b.x, manifold.normal_on_b.y, manifold.normal_on_b.z};
        }
        else
        {
            point.point = {manifold.a.x, manifold.a.y, manifold.a.z};
            point.normal = {manifold.normal_on_a.x, manifold.normal_on_a.y, manifold.normal_on_a.z};
        }
        point.distance = manifold.distance;
        point.impulse = manifold.impulse;
    }

    try
    {
        auto method = mono::make_method_invoker<void(entt::entity, const std::vector<managed_contact_point>&)>(
            obj,
            "internal_n2m_on_collision_enter");
        method(obj, b.entity(), points);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_collision_exit(const mono::mono_object& obj,
                                         entt::handle b,
                                         const std::vector<manifold_point>& manifolds,
                                         bool use_b)
{
    std::vector<managed_contact_point> points;
    points.reserve(manifolds.size());
    for(const auto& manifold : manifolds)
    {
        auto& point = points.emplace_back();
        if(use_b)
        {
            point.point = {manifold.b.x, manifold.b.y, manifold.b.z};
            point.normal = {manifold.normal_on_b.x, manifold.normal_on_b.y, manifold.normal_on_b.z};
        }
        else
        {
            point.point = {manifold.a.x, manifold.a.y, manifold.a.z};
            point.normal = {manifold.normal_on_a.x, manifold.normal_on_a.y, manifold.normal_on_a.z};
        }
        point.distance = manifold.distance;
        point.impulse = manifold.impulse;
    }

    try
    {
        auto method = mono::make_method_invoker<void(entt::entity, const std::vector<managed_contact_point>&)>(
            obj,
            "internal_n2m_on_collision_exit");
        method(obj, b.entity(), points);
    }
    catch(const mono::mono_exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::process_pending_deletions()
{
    auto& ctx = engine::context();
    auto& ev = ctx.get_cached<events>();

    size_t erased = std::erase_if(script_components_,
                                  [&](auto& rhs)
                                  {
                                      bool marked = rhs.is_marked_for_destroy();
                                      if(marked && ev.is_playing)
                                      {
                                          destroy(rhs);
                                      }

                                      return marked;
                                  });

    erased += std::erase_if(native_components_,
                            [&](const auto& rhs)
                            {
                                return rhs.is_marked_for_destroy();
                            });
}

void script_component::process_pending_creates()
{
    while(!script_components_to_create_.empty())
    {
        auto comps = std::move(script_components_to_create_);
        script_components_to_create_.clear();

        for(auto& script : comps)
        {
            if(!script.is_marked_for_destroy())
            {
                create(script);
            }
        }
    }
}

void script_component::process_pending_starts()
{
    while(!script_components_to_start_.empty())
    {
        auto comps = std::move(script_components_to_start_);
        script_components_to_start_.clear();

        for(auto& script : comps)
        {
            if(!script.is_marked_for_destroy())
            {
                start(script);
            }
        }
    }
}

void script_component::process_pending_actions(script_object script_obj)
{
    process_pending_actions_create(script_obj);
}

void script_component::process_pending_actions_create(script_object script_obj)
{
    auto& ctx = engine::context();
    auto& sys = ctx.get_cached<script_system>();
    auto& ev = ctx.get_cached<events>();

    if(ev.is_playing && sys.is_create_called())
    {
        process_pending_creates();

        if(get_owner().all_of<active_component>())
        {
            enable(script_obj, false);
        }
        else
        {
            disable(script_obj, false);
        }
    }
}


auto script_component::add_script_component(const mono::mono_type& type) -> script_object
{
    auto obj = type.new_instance();
    return add_script_component(obj);
}

auto script_component::add_script_component(const mono::mono_object& obj) -> script_object
{
    script_object script_obj(obj);
    return add_script_component(script_obj);
}

auto script_component::add_script_component(const script_object& script_obj, bool process_callbacks) -> script_object
{
    script_components_.emplace_back(script_obj);
    script_components_to_create_.emplace_back(script_obj);
    script_components_to_start_.emplace_back(script_obj);

    auto& obj = script_obj.scoped->object;

    set_entity(obj, get_owner());

    if(process_callbacks)
    {
        process_pending_actions(script_obj);
    }

    return script_obj;
}

void script_component::add_script_components(const script_components_t& comps)
{
    for(auto& comp : comps)
    {
        if(comp.scoped)
        {
            add_script_component(comp, false);
        }
    }
}

void script_component::add_missing_script_components(const script_components_t& comps)
{
    for(auto& comp : comps)
    {
        if(comp.scoped)
        {
            auto& obj = comp.scoped->object;
            const auto& type = obj.get_type();
            if(get_script_component(type).scoped)
            {
                continue;
            }

            add_script_component(obj);
        }
    }
}

auto script_component::add_native_component(const mono::mono_type& type) -> script_object
{
    auto obj = type.new_instance();
    auto& script_obj = native_components_.emplace_back(obj);

    set_entity(obj, get_owner());

    return script_obj;
}

auto script_component::get_script_components(const mono::mono_type& type) -> std::vector<mono::mono_object>
{
    std::vector<mono::mono_object> result;
    for(const auto& component : script_components_)
    {
        const auto& comp_type = component.scoped->object.get_type();

        if(comp_type.get_internal_ptr() == type.get_internal_ptr() || comp_type.is_derived_from(type))
        {
            result.emplace_back(static_cast<mono::mono_object&>(component.scoped->object));
        }
    }

    return result;
}

auto script_component::get_script_component(const mono::mono_type& type) -> script_object
{
    auto it = std::find_if(std::begin(script_components_),
                           std::end(script_components_),
                           [&](const auto& component)
                           {
                               const auto& comp_type = component.scoped->object.get_type();
                               return comp_type.get_internal_ptr() == type.get_internal_ptr() ||
                                      comp_type.is_derived_from(type);
                           });

    if(it != std::end(script_components_))
    {
        return *it;
    }

    return {};
}

auto script_component::get_native_component(const mono::mono_type& type) -> script_object
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& component)
                           {
                               const auto& comp_type = component.scoped->object.get_type();
                               return comp_type.get_internal_ptr() == type.get_internal_ptr() ||
                                      comp_type.is_derived_from(type);
                           });

    if(it != std::end(native_components_))
    {
        return *it;
    }

    return {};
}

auto script_component::remove_script_component(const mono::mono_object& obj) -> bool
{
    auto checker = [&](const auto& rhs)
    {
        return rhs.scoped->object.get_internal_ptr() == obj.get_internal_ptr();
    };
    std::erase_if(script_components_to_create_, checker);

    std::erase_if(script_components_to_start_, checker);

    auto it = std::find_if(std::begin(script_components_), std::end(script_components_), checker);

    if(it != std::end(script_components_))
    {
        auto& script_obj = *it;

        set_entity(obj, {});

        script_obj.state->marked_for_destroy = true;
        return true;
    }

    return false;
}

auto script_component::remove_script_component(const mono::mono_type& type) -> bool
{
    auto checker = [&](const auto& rhs)
    {
        return rhs.scoped->object.get_type().get_internal_ptr() == type.get_internal_ptr();
    };
    std::erase_if(script_components_to_create_, checker);

    std::erase_if(script_components_to_start_, checker);

    auto it = std::find_if(std::begin(script_components_), std::end(script_components_), checker);

    if(it != std::end(script_components_))
    {
        auto& script_obj = *it;
        auto& obj = script_obj.scoped->object;
        set_entity(obj, {});

        script_obj.state->marked_for_destroy = true;
        return true;
    }

    return false;
}

auto script_component::remove_native_component(const mono::mono_object& obj) -> bool
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& rhs)
                           {
                               return rhs.scoped->object.get_internal_ptr() == obj.get_internal_ptr();
                           });

    if(it != std::end(native_components_))
    {
        auto& script_obj = *it;

        set_entity(obj, {});

        script_obj.state->marked_for_destroy = true;
        return true;
    }

    return false;
}

auto script_component::remove_native_component(const mono::mono_type& type) -> bool
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& rhs)
                           {
                               return rhs.scoped->object.get_type().get_internal_ptr() == type.get_internal_ptr();
                           });

    if(it != std::end(native_components_))
    {
        auto& script_obj = *it;

        auto& obj = script_obj.scoped->object;
        set_entity(obj, {});

        script_obj.state->marked_for_destroy = true;
        return true;
    }

    return false;
}

auto script_component::get_script_components() const -> const script_components_t&
{
    return script_components_;
}

auto script_component::has_script_components() const -> bool
{
    return !script_components_.empty();
}

auto script_component::has_script_components(const std::string& type_name) const -> bool
{
    return std::any_of(std::begin(script_components_), std::end(script_components_), [&](const auto& component)
    {
        return component.scoped->object.get_type().get_name() == type_name;
    });
}

auto script_component::get_script_source_location(const script_object& obj) const -> std::string
{
    if(!obj.scoped)
    {
        return {};
    }

    const auto& object = obj.scoped->object;
    const auto& type = object.get_type();
    try
    {
        auto prop = type.get_property("SourceFilePath");
        auto invoker = mono::make_property_invoker<std::string>(prop);
        return invoker.get_value(object);
    }
    catch(const mono::mono_exception& e)
    {
        return {};
    }
}
} // namespace unravel
