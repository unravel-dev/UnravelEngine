#pragma once

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <dotnetpp/dotnetpp.h>
#include <logging/logging.h>
#include <hpp/type_name.hpp>

namespace unravel
{

inline auto get_entity_from_id(entt::entity id) -> entt::handle
{
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
