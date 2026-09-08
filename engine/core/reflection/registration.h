#ifndef REFLECTION_REGISTRATION_H
#define REFLECTION_REGISTRATION_H

#include <entt/meta/resolve.hpp>


template<typename T, typename... Args>
struct crtp_meta_type :  public Args...
{
    virtual auto get_meta_type() const -> entt::meta_type
    {
        return entt::resolve<T>();
    }

    static auto get_static_meta_type() -> entt::meta_type
    {
        return entt::resolve<T>();
    }

    template<typename U>
    auto is() const -> bool
    {
        return get_meta_type() == entt::resolve<U>();
    }

    template<typename U>
    auto safe_cast() -> U*
    {
        if(!is<U>())
        {
            return nullptr;
        }

        return static_cast<U*>(this);
    }

    template<typename U>
    auto safe_cast() const -> const U*
    {
        if(!is<U>())
        {
            return nullptr;
        }

        return static_cast<const U*>(this);
    }

    auto is(const entt::meta_type& type) const -> bool
    {
        return get_meta_type() == type;
    }

    // virtual auto as_derived(const entt::meta_type& type) const -> entt::meta_any
    // {
    //     auto type = get_meta_type();
    //     return type.from_void(static_cast<const T*>(this));
    // }

    virtual auto as_derived() -> entt::meta_any
    {
        auto type = get_meta_type();
        return type.from_void(static_cast<T*>(this));
    }
};

#endif // REFLECTION_REGISTRATION_H
