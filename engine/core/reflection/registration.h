#ifndef REFLECTION_REGISTRATION_H
#define REFLECTION_REGISTRATION_H

#include <rttr/registration_friend.h>
#include <rttr/rttr_enable.h>
#include <entt/meta/resolve.hpp>

#define RTTR_REGISTRATION_FRIEND_NON_INTRUSIVE(cls)                                                                    \
    RTTR_REGISTRATION_FRIEND

#define REFLECTABLE(cls)                                                                                               \
    RTTR_REGISTRATION_FRIEND_NON_INTRUSIVE(cls)                                                                        \
    RTTR_ENABLE()                                                                                                      \
public:

#define REFLECTABLEV(cls, ...)                                                                                         \
    RTTR_REGISTRATION_FRIEND_NON_INTRUSIVE(cls)                                                                        \
    RTTR_ENABLE(__VA_ARGS__)                                                                                           \
public:


template<typename T, typename... Args>
struct crtp_meta_type :  public Args...
{
    virtual auto type_id() const -> entt::meta_type
    {
        return entt::resolve<T>();
    }

    static auto static_type_id() -> entt::meta_type
    {
        return entt::resolve<T>();
    }
};

#endif // REFLECTION_REGISTRATION_H
