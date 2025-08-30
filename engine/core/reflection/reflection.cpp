#include "reflection.h"

namespace entt
{
namespace
{
auto get_derived(const entt::meta_type& base) -> std::vector<entt::meta_type>
{
    std::vector<entt::meta_type> result;

    auto types = entt::resolve();
    for (auto mt : types)
    {
        for (auto b : mt.second.base())
        {
            if (b.second == base) 
            {
                result.push_back(mt.second);
            }
        }
    }
    return result;
}

}

auto copy_meta_any(const entt::meta_any& src) -> entt::meta_any
{
    if (!src)
    {
        return {};
    }

    const entt::meta_type mt = src.type();

    // 2) T() + operator=
    if (auto owned = mt.construct(); owned)        // default-construct
    {
        if (owned.assign(src))                       // in-place copy
        {
            return owned;                            // still owned
        }
    }

    return {};
}

auto get_derived_types(const meta_type& t) -> std::vector<meta_type>
{
    return get_derived(t);
}

auto as_derived(meta_any& obj) -> bool
{
    auto type = obj.type();
    auto as_derived = type.func("as_derived"_hs);
    if(as_derived)
    {
        obj = as_derived.invoke(obj);
        return true;
    }
    return false;
}

auto get_derived_type(meta_any& obj) -> meta_type
{
    auto type = obj.type();

    auto as_derived = type.func("as_derived"_hs);
    if(as_derived)
    {
        return as_derived.invoke(obj).type();
    }

    auto func = type.func("get_meta_type"_hs);
    if(func)
    {
        auto result = func.invoke(obj); 
        return result.cast<meta_type>();
    }
    return type;
}

auto get_attribute(const meta_custom& custom, const char* name) -> const meta_any&
{
    const attributes* attrs = custom;

    if(attrs)
    {
        auto it = attrs->find(name);
        if(it != attrs->end())
        {
            return it->second;
        }
    }

    static const meta_any any;
    return any;
}

auto get_attribute(const meta_type& t, const char* name) -> const meta_any&
{
    return get_attribute(t.custom(), name);
}

auto get_attribute(const meta_data& prop, const char* name) -> const meta_any&
{
    return get_attribute(prop.custom(), name);
}

auto get_pretty_name(const meta_type& t) -> std::string
{
    auto name = get_attribute_as<std::string>(t, "pretty_name");
    if(name.empty())
    {
        return get_name(t);
    }
    return name;
}

auto get_name(const meta_type& t) -> std::string
{
    auto name = get_attribute_as<std::string>(t, "name");
    if(name.empty())
    {
        return std::string(t.info().name());
    }
    return name;
}


auto get_pretty_name(const meta_custom& t) -> std::string
{
    auto name = get_attribute_as<std::string>(t, "pretty_name");
    if(name.empty())
    {
        return get_name(t);
    }
    return name;
}

auto get_name(const meta_custom& t) -> std::string
{
    auto name = get_attribute_as<std::string>(t, "name");
    if(name.empty())
    {
        return std::string();
    }
    return name;
}

auto get_name(const meta_data& prop) -> std::string
{
    return get_attribute_as<std::string>(prop, "name");
}

auto get_pretty_name(const meta_data& prop) -> std::string
{
    auto name = get_attribute_as<std::string>(prop, "pretty_name");
    if(name.empty())
    {
        return get_name(prop);
    }
    return name;
}

auto property_predicate(property_predicate_t predicate) -> property_predicate_t
{
    return std::move(predicate);
}

}

auto register_type_helper(const char* name) -> int
{
      return 0;
}
