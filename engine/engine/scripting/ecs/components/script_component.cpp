#include "script_component.h"
#include <dotnetpp/dotnetpp.h>

#include <engine/ecs/components/transform_component.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/scripting/ecs/systems/script_system.h>
namespace unravel
{

namespace
{


auto to_managed_contact_point(const manifold_point& manifold, bool use_b = false) -> dotnetpp_backend::managed_interface::manifold_point
{
    dotnetpp_backend::managed_interface::manifold_point result;
    if(use_b)
    {
        result.point = {manifold.b.x, manifold.b.y, manifold.b.z};
        result.normal = {manifold.normal_on_b.x, manifold.normal_on_b.y, manifold.normal_on_b.z};
        result.distance = manifold.distance;
        result.impulse = manifold.impulse;
    }
    else
    {
        result.point = {manifold.a.x, manifold.a.y, manifold.a.z};
        result.normal = {manifold.normal_on_a.x, manifold.normal_on_a.y, manifold.normal_on_a.z};
        result.distance = manifold.distance;
        result.impulse = manifold.impulse;
    }
    return result;
}

auto to_managed_contact_points(const std::vector<manifold_point>& manifolds, bool use_b = false) -> std::vector<dotnetpp_backend::managed_interface::manifold_point>
{
    std::vector<dotnetpp_backend::managed_interface::manifold_point> result;
    result.reserve(manifolds.size());
    for(const auto& manifold : manifolds)
    {
        result.push_back(to_managed_contact_point(manifold, use_b));
    }
    return result;
}

auto engine_script_cache() -> const script_system::engine_script_cache&
{
    const auto& ctx = engine::context();
    return ctx.get_cached<script_system>().get_cache();
}
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
                     if(script.pinned)
                     {
                         auto obj = script.pinned->get_object();
                         remove_script_component(obj);
                     }
                 });

    process_pending_deletions();
}

void script_component::enable()
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     enable(script, true);
                 });
}

void script_component::disable()
{
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     disable(script, true);
                 });
}

void script_component::on_sensor_enter(entt::handle other, const std::vector<manifold_point>& manifolds)
{
    auto managed_manifolds = to_managed_contact_points(manifolds, true);
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     on_sensor_enter(obj, other, managed_manifolds);
                 });
}

void script_component::on_sensor_exit(entt::handle other, const std::vector<manifold_point>& manifolds)
{
    auto managed_manifolds = to_managed_contact_points(manifolds, true);
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     on_sensor_exit(obj, other, managed_manifolds);
                 });
}

void script_component::on_collision_enter(entt::handle b, const std::vector<manifold_point>& manifolds, bool use_b)
{
    auto managed_manifolds = to_managed_contact_points(manifolds, use_b);
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     on_collision_enter(obj, b, managed_manifolds);
                 });
}

void script_component::on_collision_exit(entt::handle b, const std::vector<manifold_point>& manifolds, bool use_b)
{
    auto managed_manifolds = to_managed_contact_points(manifolds, use_b);
    safe_foreach(script_components_,
                 [&](auto& script)
                 {
                     auto obj = script.pinned->get_object();
                     on_collision_exit(obj, b, managed_manifolds);
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

    auto obj = script_obj.pinned->get_object();
    if(!obj.valid())
    {
        APPLOG_WARNING("Script component already destroyed");
        return;
    }

    try
    {
        auto method = dotnet::make_method_invoker<void()>(engine_script_cache().on_enable_method, false);
        method(obj);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
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
    auto obj = script_obj.pinned->get_object();
    if(!obj.valid())
    {
        APPLOG_WARNING("Script component already destroyed");
        return;
    }
    try
    {
        auto method = dotnet::make_method_invoker<void()>(engine_script_cache().on_disable_method, false);
        method(obj);
    }
    catch(const dotnet::exception& e)
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
    auto obj = script_obj.pinned->get_object();
    if(!obj.valid())
    {
        APPLOG_WARNING("Script component already destroyed");
        return;
    }

    try
    {
        auto method = dotnet::make_method_invoker<void()>(engine_script_cache().on_create_method, false);
        method(obj);
    }
    catch(const dotnet::exception& e)
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
    auto obj = script_obj.pinned->get_object();
    if(!obj.valid())
    {
        APPLOG_WARNING("Script component already destroyed");
        return;
    }
    try
    {
        auto method = dotnet::make_method_invoker<void()>(engine_script_cache().on_start_method, false);
        method(obj);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::destroy(script_object& script_obj)
{
    auto obj = script_obj.pinned->get_object();
    if(!obj.valid())
    {
        APPLOG_WARNING("Script component already destroyed");
        return;
    }
    try
    {
        auto method = dotnet::make_method_invoker<void()>(engine_script_cache().on_destroy_method, false);
        method(obj);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::set_entity(const dotnet::object& obj, entt::handle e)
{
    if(!obj.valid())
    {
        return;
    }
    try
    {
        auto method =
            dotnet::make_method_invoker<void(entt::entity)>(engine_script_cache().set_entity_method, false);
        method(obj, e.entity());
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_sensor_enter(const dotnet::object& obj, entt::handle other, const std::vector<dotnetpp_backend::managed_interface::manifold_point>& manifolds)
{
    try
    {
        auto method = dotnet::make_method_invoker<void(
            entt::entity, const std::vector<dotnetpp_backend::managed_interface::manifold_point>&)>(
            engine_script_cache().on_sensor_enter_method, false);
        method(obj, other.entity(), manifolds);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_sensor_exit(const dotnet::object& obj, entt::handle other, const std::vector<dotnetpp_backend::managed_interface::manifold_point>& manifolds)
{
    try
    {
        auto method = dotnet::make_method_invoker<void(
            entt::entity, const std::vector<dotnetpp_backend::managed_interface::manifold_point>&)>(
            engine_script_cache().on_sensor_exit_method, false);
        method(obj, other.entity(), manifolds);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_collision_enter(const dotnet::object& obj,
                                          entt::handle other,
                                          const std::vector<dotnetpp_backend::managed_interface::manifold_point>& manifolds)
{
    try
    {
        auto method = dotnet::make_method_invoker<void(
            entt::entity, const std::vector<dotnetpp_backend::managed_interface::manifold_point>&)>(
            engine_script_cache().on_collision_enter_method, false);
        method(obj, other.entity(), manifolds);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::on_collision_exit(const dotnet::object& obj,
                                         entt::handle other,
                                         const std::vector<dotnetpp_backend::managed_interface::manifold_point>& manifolds)
{
    try
    {
        auto method = dotnet::make_method_invoker<void(
            entt::entity, const std::vector<dotnetpp_backend::managed_interface::manifold_point>&)>(
            engine_script_cache().on_collision_exit_method, false);
        method(obj, other.entity(), manifolds);
    }
    catch(const dotnet::exception& e)
    {
        script_system::log_exception(e);
    }
}

void script_component::process_pending_deletions()
{
    auto& ctx = engine::context();
    auto& play = ctx.get_cached<play_mode>();

    // Call destroy on marked script components before erasing
    if(play.is_simulation_running())
    {
        for(auto& script : script_components_)
        {
            if(script.is_marked_for_destroy())
            {
                destroy(script);
            }
        }
    }

    // Now erase the marked components
    size_t erased = std::erase_if(script_components_,
                                  [](const auto& rhs)
                                  {
                                      return rhs.is_marked_for_destroy();
                                  });

    erased += std::erase_if(native_components_,
                            [](const auto& rhs)
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
    auto& play = ctx.get_cached<play_mode>();

    if(play.is_simulation_running() && sys.is_create_called())
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


auto script_component::add_script_component(const dotnet::type& type) -> script_object
{
    auto obj = type.new_instance();
    return add_script_component(obj);
}

auto script_component::add_script_component(const dotnet::object& obj) -> script_object
{
    script_object script_obj(obj);
    return add_script_component(script_obj);
}

auto script_component::add_script_component(const script_object& script_obj, bool process_callbacks) -> script_object
{
    script_components_.emplace_back(script_obj);
    script_components_to_create_.emplace_back(script_obj);
    script_components_to_start_.emplace_back(script_obj);

    auto obj = script_obj.pinned->get_object();

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
        if(comp.pinned)
        {
            add_script_component(comp, false);
        }
    }
}

void script_component::add_missing_script_components(const script_components_t& comps)
{
    for(auto& comp : comps)
    {
        if(comp.pinned)
        {
            auto obj = comp.pinned->get_object();
            const auto& type = obj.get_type();
            if(get_script_component(type).pinned)
            {
                continue;
            }

            add_script_component(obj);
        }
    }
}

auto script_component::add_native_component(const dotnet::type& type) -> script_object
{
    auto obj = type.new_instance();
    auto& script_obj = native_components_.emplace_back(obj);

    set_entity(obj, get_owner());

    return script_obj;
}

auto script_component::get_script_components(const dotnet::type& type) -> std::vector<dotnet::object>
{
    std::vector<dotnet::object> result;
    for(const auto& component : script_components_)
    {
        auto obj = component.pinned->get_object();
        const auto& comp_type = obj.get_type();

        if(comp_type.equals(type) || comp_type.is_derived_from(type))
        {
            // Validate the object is still valid before adding
            if(!component.is_marked_for_destroy())
            {
                // Since the handle is pinned, the object pointer remains valid
                result.emplace_back(obj);
            }
        }
    }

    return result;
}

auto script_component::get_script_component(const dotnet::type& type) -> script_object
{
    auto it = std::find_if(std::begin(script_components_),
                           std::end(script_components_),
                           [&](const auto& component)
                           {
                                auto obj = component.pinned->get_object();
                                const auto& comp_type = obj.get_type();
                                return comp_type.equals(type) || comp_type.is_derived_from(type);
                           });

    if(it != std::end(script_components_))
    {
        return *it;
    }

    return {};
}

auto script_component::get_native_component(const dotnet::type& type) -> script_object
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& component)
                           {
                                auto obj = component.pinned->get_object();
                                const auto& comp_type = obj.get_type();
                                return comp_type.equals(type) || comp_type.is_derived_from(type);
                           });

    if(it != std::end(native_components_))
    {
        return *it;
    }

    return {};
}

auto script_component::remove_script_component(const dotnet::object& obj) -> bool
{
    auto checker = [&](const auto& rhs)
    {
        return rhs.pinned->get_object().equals(obj);
    };
    std::erase_if(script_components_to_create_, checker);

    std::erase_if(script_components_to_start_, checker);

    auto it = std::find_if(std::begin(script_components_), std::end(script_components_), checker);

    if(it != std::end(script_components_))
    {
        auto& script_obj = *it;

        if(script_obj.state)
        {
            
            // already queued for destruction
            if(script_obj.state->marked_for_destroy)
            {
                APPLOG_WARNING("Script component already queued for destruction");
                return true;
            }
            set_entity(obj, {});
            
            script_obj.state->marked_for_destroy = true;
            return true;
        }
        return false;
    }

    return false;
}

auto script_component::remove_script_component(const dotnet::type& type) -> bool
{
    auto checker = [&](const auto& rhs)
    {
        return rhs.pinned->get_object().get_type().equals(type);
    };
    std::erase_if(script_components_to_create_, checker);

    std::erase_if(script_components_to_start_, checker);

    auto it = std::find_if(std::begin(script_components_), std::end(script_components_), checker);

    if(it != std::end(script_components_))
    {
        auto& script_obj = *it;

        if(script_obj.state)
        {
            // already queued for destruction
            if(script_obj.state->marked_for_destroy)
            {
                APPLOG_WARNING("Script component already queued for destruction");
                return true;
            }
            auto obj = script_obj.pinned->get_object();
            set_entity(obj, {});

            script_obj.state->marked_for_destroy = true;
            return true;
        }

        return false;
       
    }

    return false;
}

auto script_component::remove_native_component(const dotnet::object& obj) -> bool
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& rhs)
                           {
                               return rhs.pinned->get_object().equals(obj);
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

auto script_component::remove_native_component(const dotnet::type& type) -> bool
{
    auto it = std::find_if(std::begin(native_components_),
                           std::end(native_components_),
                           [&](const auto& rhs)
                           {
                               return rhs.pinned->get_object().get_type().equals(type);
                           });

    if(it != std::end(native_components_))
    {
        auto& script_obj = *it;

        auto obj = script_obj.pinned->get_object();
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
        return component.pinned->get_object().get_type().get_name() == type_name;
    });
}

auto script_component::get_script_source_location(const script_object& obj) const -> std::string
{
    if(!obj.pinned)
    {
        return {};
    }

    auto object = obj.pinned->get_object();
    const auto& type = object.get_type();
    try
    {
        auto attrs = type.get_attributes();
        for(auto& attr : attrs)
        {
            if(attr.get_type().get_fullname() == "Unravel.Core.ScriptSourceFileAttribute")
            {
                auto invoker = dotnet::make_property_invoker<std::string>(attr.get_type(), "Path");
                return invoker.get_value(attr);
            }
        }
        auto prop = type.get_property("SourceFilePath");
        auto invoker = dotnet::make_property_invoker<std::string>(prop);
        return invoker.get_value(object);
    }
    catch(const dotnet::exception& e)
    {
        return {};
    }
}
} // namespace unravel
