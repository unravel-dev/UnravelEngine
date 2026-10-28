#pragma once

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <dotnetpp/dotnetpp.h>
#include <logging/logging.h>
#include <hpp/type_name.hpp>

#include <thread>

namespace unravel
{

/// The thread scripts update on (set by script_system::init): engine calls that reach the scene, the renderer
/// or other engine state belong on it. Script code on worker threads may only do pure C# work.
inline auto get_script_thread_id() -> std::thread::id&
{
    static std::thread::id id;
    return id;
}

/// True on the script thread; otherwise raises a C# InvalidOperationException naming @p api and returns false.
inline auto require_script_thread(const char* api) -> bool
{
    if(std::this_thread::get_id() == get_script_thread_id())
    {
        return true;
    }
    dotnet::raise_exception("System",
                            "InvalidOperationException",
                            fmt::format("{} must be called from the main thread; worker threads may only run pure C# code.", api));
    return false;
}

inline auto get_entity_from_id(entt::entity id) -> entt::handle
{
    if(!require_script_thread("Entity access"))
    {
        return {};
    }
    if(id == entt::entity(0))
    {
        return {};
    }
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    return ec.get_scene().create_handle(id);
}

inline void raise_invalid_entity_exception()
{
    dotnet::raise_exception("System", "Exception", "Entity is invalid.");
}

template<typename T>
inline void raise_missing_component_exception()
{
    dotnet::raise_exception("System",
                            "Exception",
                            fmt::format("Entity does not have component of type {}.", hpp::type_name_str<T>()));
}

template<typename T>
inline auto safe_get_component(entt::entity id) -> T*
{
    if(!require_script_thread("Component access"))
    {
        return nullptr;
    }
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return nullptr;
    }
    auto* comp = e.try_get<T>();
    if(!comp)
    {
        raise_missing_component_exception<T>();
        return nullptr;
    }
    return comp;
}

} // namespace unravel
