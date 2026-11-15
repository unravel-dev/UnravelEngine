#ifndef REFLECTION_REGISTRATION_H
#define REFLECTION_REGISTRATION_H

#include <entt/meta/resolve.hpp>

#include <memory>


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

/**
 * @brief @p ptr as a shared pointer to @p U when the object is exactly a U (crtp_meta_type::is), else null.
 *
 * std::dynamic_pointer_cast for reflected types by one virtual call and a type compare, without the RTTI walk. U must
 * be the object's most derived type: is<U> compares exact types, so a cast to an intermediate base gives null.
 */
template<typename U, typename T>
auto safe_pointer_cast(const std::shared_ptr<T>& ptr) -> std::shared_ptr<U>
{
    return ptr && ptr->template is<U>() ? std::static_pointer_cast<U>(ptr) : nullptr;
}

#endif // REFLECTION_REGISTRATION_H
