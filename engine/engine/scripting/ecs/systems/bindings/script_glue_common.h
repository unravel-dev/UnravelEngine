#pragma once

#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <dotnetpp/dotnetpp.h>
#include <logging/logging.h>
#include <hpp/type_name.hpp>

#include <thread>
#include <type_traits>
#include <utility>

namespace unravel
{

/// The thread scripts update on (set by script_system::init): engine calls that reach the scene, the renderer
/// or other engine state belong on it. Script code on worker threads may only do pure C# work.
inline auto get_script_thread_id() -> std::thread::id&
{
    static std::thread::id id;
    return id;
}

/// True on the script thread; otherwise raises a C# InvalidOperationException and returns false.
inline auto require_script_thread() -> bool
{
    if(std::this_thread::get_id() == get_script_thread_id())
    {
        return true;
    }
    dotnet::raise_exception("System",
                            "InvalidOperationException",
                            "Engine calls must be made from the main thread; worker threads may only run pure C# code.");
    return false;
}

namespace detail
{
/// What a call refused by require_script_thread() returns; C# throws the raised exception instead of reading it.
template<typename R>
auto make_refused_result() -> R
{
    if constexpr(std::is_void_v<R>)
    {
        return;
    }
    else if constexpr(std::is_reference_v<R>)
    {
        static const std::decay_t<R> refused{};
        return refused;
    }
    else if constexpr(std::is_default_constructible_v<R>)
    {
        return R();
    }
    else
    {
        // A managed array: a null one.
        return R(dotnet::object{});
    }
}

template<typename Signature, Signature& func>
struct script_thread_call;

/// @p func behind require_script_thread(): on another thread it raises in C# and returns a default value.
template<typename R, typename... Args, R (&func)(Args...)>
struct script_thread_call<R(Args...), func>
{
    static auto call(Args... args) -> R
    {
        if(!require_script_thread())
        {
            return make_refused_result<R>();
        }
        return func(std::forward<Args>(args)...);
    }
};
} // namespace detail

/**
 * @brief dotnet_internal_call for a call that reaches engine state (every binding but logging): from a thread other
 * than the script thread it raises InvalidOperationException in C# instead. Calls safe on any thread register with
 * dotnet_internal_call itself.
 */
#if DOTNETPP_BACKEND_MONO
#define script_internal_call(func)                                                                                     \
    &mono::mono_jit_internal_call_wrapper<decltype(func),                                                              \
                                          ::unravel::detail::script_thread_call<decltype(func), func>::call>::wrapper
#elif DOTNETPP_BACKEND_CORECLR
#define script_internal_call(func)                                                                                     \
    reinterpret_cast<void*>(                                                                                           \
        &clr::detail::clr_internal_call_wrapper<decltype(func),                                                        \
                                                ::unravel::detail::script_thread_call<decltype(func), func>::call>::wrapper)
#endif

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
