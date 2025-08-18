#ifndef REFLECTION_H
#define REFLECTION_H

#include <entt/core/hashed_string.hpp>
#include <entt/meta/container.hpp>
#include <entt/meta/factory.hpp>
#include <entt/meta/meta.hpp>
#include <reflection/reflection_export.h>


using namespace entt::literals;

namespace entt
{
using attributes = std::map<std::string, meta_any>;
using attribute = attributes::value_type;

REFLECTION_EXPORT auto get_attribute(const meta_custom& custom, const char* name) -> const meta_any&;

template<typename T>
auto get_attribute_as(const meta_custom& custom, const char* name) -> T
{
    auto attr = get_attribute(custom, name);
    if(attr.allow_cast<T>())
    {
        return attr.cast<T>();
    }
    return T();
}

template<>
inline auto get_attribute_as<std::string>(const meta_custom& custom, const char* name) -> std::string
{
    auto attr = get_attribute(custom, name);
    if(attr.allow_cast<std::string>())
    {
        return attr.cast<std::string>();
    }

    if(attr.allow_cast<const char*>())
    {
        return std::string(attr.cast<const char*>());
    }
    return std::string();
}

REFLECTION_EXPORT auto get_attribute(const meta_type& t, const char* name) -> const meta_any&;

template<typename T>
auto get_attribute_as(const meta_type& t, const char* name) -> T
{
    return get_attribute_as<T>(t.custom(), name);
}

REFLECTION_EXPORT auto get_attribute(const meta_data& prop, const char* name) -> const meta_any&;

template<typename T>
auto get_attribute_as(const meta_data& prop, const char* name) -> T
{
    return get_attribute_as<T>(prop.custom(), name);
}

REFLECTION_EXPORT auto get_derived_types(const meta_type& t) -> std::vector<meta_type>;

REFLECTION_EXPORT auto as_derived(meta_any& obj) -> bool;

REFLECTION_EXPORT auto get_derived_type(meta_any& obj) -> meta_type;

REFLECTION_EXPORT auto get_pretty_name(const meta_type& t) -> std::string;
REFLECTION_EXPORT auto get_name(const meta_type& t) -> std::string;
REFLECTION_EXPORT auto get_pretty_name(const meta_data& prop) -> std::string;
REFLECTION_EXPORT auto get_name(const meta_data& prop) -> std::string;

using property_predicate_t = std::function<bool(const meta_any&)>;
REFLECTION_EXPORT auto property_predicate(property_predicate_t predicate)
    -> property_predicate_t;

    
template<typename Value, typename... Args>
auto make_custom(Args &&...args) -> entt::meta_custom
{
    return {entt::internal::meta_custom_node{type_id<Value>().hash(), std::make_shared<Value>(std::forward<Args>(args)...)}};
}

} // namespace entt

#define CAT_IMPL_(a, b) a##b
#define CAT_(a, b)      CAT_IMPL_(a, b)
#ifdef __COUNTER__
#define ANONYMOUS_VARIABLE(str) CAT_(str, CAT_(__COUNTER__, CAT_(__LINE__, __COUNTER__)))
#else
#define ANONYMOUS_VARIABLE(str) CAT_(str, __LINE__)
#endif

namespace refl_detail
{
template<typename T>
inline int get_reg(void (*f)())
{
    static const int s = [&f]()
    {
        f();
        return 0;
    }();
    return s;
}
} // namespace refl_detail

#define REFLECT_EXTERN(cls)                                                                                            \
    template<typename T>                                                                                               \
    extern void reflection_auto_register_reflection_function_t();                                                            \
    template<>                                                                                                         \
    void reflection_auto_register_reflection_function_t<cls>();                                                              \
    static const int ANONYMOUS_VARIABLE(auto_register__) =                                                             \
        refl_detail::get_reg<cls>(&reflection_auto_register_reflection_function_t<cls>)

#define REFLECT_INLINE(cls)                                                                                            \
    REFLECT_EXTERN(cls);                                                                                               \
    template<>                                                                                                         \
    inline void reflection_auto_register_reflection_function_t<cls>()

#define REFLECT(cls)                                                                                                   \
    template<>                                                                                                         \
    void reflection_auto_register_reflection_function_t<cls>()

#define REFLECTION_REGISTRATION                                                                                        \
    static void reflection_auto_register_reflection_function_();                                                       \
    namespace                                                                                                          \
    {                                                                                                                  \
    struct reflection__auto__register__                                                                                \
    {                                                                                                                  \
        reflection__auto__register__()                                                                                 \
        {                                                                                                              \
            reflection_auto_register_reflection_function_();                                                           \
        }                                                                                                              \
    };                                                                                                                 \
    }                                                                                                                  \
    static const reflection__auto__register__ ANONYMOUS_VARIABLE(auto_register__);                                     \
    static void reflection_auto_register_reflection_function_()

#endif // RTTR_REFLECTION_H_
