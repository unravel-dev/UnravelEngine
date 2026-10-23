#include "material_instance.hpp"
#include "material.hpp"
#include "standard_material.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>

#include <stdexcept>

namespace unravel
{
namespace
{

/// The key every saved instance has: an older save wrote a whole material per slot instead (see the model's LOAD).
constexpr const char* MATERIAL_TYPE_KEY = "material_type";

template<typename T, typename Archive>
auto save_value_as(Archive& ar, const char* name, const entt::meta_any& value) -> bool
{
    const auto* typed = value.try_cast<T>();
    if(typed == nullptr)
    {
        return false;
    }
    T copy = *typed;
    try_save(ar, ser20::make_nvp(name, copy));
    return true;
}

/// Saves one override with the serializer of its type. The set covers the property types of the material types.
template<typename Archive>
void save_value(Archive& ar, const char* name, const entt::meta_any& value)
{
    const bool saved = save_value_as<math::color>(ar, name, value) || save_value_as<float>(ar, name, value) ||
                       save_value_as<math::vec2>(ar, name, value) ||
                       save_value_as<asset_handle<gfx::texture>>(ar, name, value) ||
                       save_value_as<alpha_mode>(ar, name, value) || save_value_as<cull_type>(ar, name, value);
    if(!saved)
    {
        throw std::runtime_error(std::string("A material instance cannot save the type of property ") + name + ".");
    }
}

template<typename T, typename Archive>
auto load_value_as(Archive& ar, const char* name, const entt::meta_type& type, entt::meta_any& value) -> bool
{
    if(type != entt::resolve<T>())
    {
        return false;
    }
    T loaded{};
    if(try_load(ar, ser20::make_nvp(name, loaded)))
    {
        value = std::move(loaded);
    }
    return true;
}

/// Loads one override of a property of type @p type; empty when it is not saved.
template<typename Archive>
auto load_value(Archive& ar, const char* name, const entt::meta_type& type) -> entt::meta_any
{
    entt::meta_any value;
    const bool known = load_value_as<math::color>(ar, name, type, value) ||
                       load_value_as<float>(ar, name, type, value) ||
                       load_value_as<math::vec2>(ar, name, type, value) ||
                       load_value_as<asset_handle<gfx::texture>>(ar, name, type, value) ||
                       load_value_as<alpha_mode>(ar, name, type, value) ||
                       load_value_as<cull_type>(ar, name, type, value);
    if(!known)
    {
        throw std::runtime_error(std::string("A material instance cannot load the type of property ") + name + ".");
    }
    return value;
}

auto get_property_name(const entt::meta_type& type, entt::id_type property) -> std::string
{
    const auto data = type.data(property);
    return data ? entt::get_attribute_as<std::string>(data, "name") : std::string{};
}

} // namespace

REFLECT(material_instance)
{
    entt::meta_factory<material_instance>{}
        .type("material_instance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_instance"},
            entt::attribute{"pretty_name", "Material Instance"},
        });
}

SAVE(material_instance)
{
    const auto type = obj.get_material_type();
    const auto overrides = obj.collect_overrides();
    std::string type_name = type ? entt::get_name(type) : std::string{};
    try_save(ar, ser20::make_nvp(MATERIAL_TYPE_KEY, type_name));
    if(!type)
    {
        return;
    }
    if constexpr(is_binary_archive<Archive>())
    {
        auto count = static_cast<uint32_t>(overrides.size());
        try_save(ar, ser20::make_nvp("count", count));
        for(const auto& entry : overrides)
        {
            auto name = get_property_name(type, entry.property);
            try_save(ar, ser20::make_nvp("property", name));
            save_value(ar, name.c_str(), entry.value);
        }
    }
    else
    {
        // By property name, so a prefab override points at one property of one slot.
        for(const auto& entry : overrides)
        {
            const auto name = get_property_name(type, entry.property);
            if(!name.empty())
            {
                save_value(ar, name.c_str(), entry.value);
            }
        }
    }
}
SAVE_INSTANTIATE(material_instance, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(material_instance, ser20::oarchive_binary_t);

LOAD(material_instance)
{
    std::string type_name;
    if(!try_load(ar, ser20::make_nvp(MATERIAL_TYPE_KEY, type_name)))
    {
        throw std::runtime_error("Not a material instance: an older save kept a whole material per slot.");
    }
    obj.overrides_.clear();
    obj.changed_properties_.clear();
    obj.legacy_copy_.reset();
    obj.material_type_ = {};
    if(type_name.empty())
    {
        return;
    }
    const auto type = entt::resolve(entt::hashed_string{type_name.c_str()}.value());
    if(!type)
    {
        throw std::runtime_error("A material instance names the unknown material type " + type_name + ".");
    }
    obj.material_type_ = type.id();
    if constexpr(is_binary_archive<Archive>())
    {
        uint32_t count = 0;
        try_load(ar, ser20::make_nvp("count", count));
        for(uint32_t i = 0; i < count; ++i)
        {
            std::string name;
            try_load(ar, ser20::make_nvp("property", name));
            const auto property = entt::hashed_string{name.c_str()}.value();
            const auto data = type.data(property);
            if(!data)
            {
                throw std::runtime_error("A material instance overrides the unknown property " + name + ".");
            }
            auto value = load_value(ar, name.c_str(), data.type());
            if(value)
            {
                obj.overrides_.push_back({property, std::move(value)});
                obj.changed_properties_.push_back(property);
            }
        }
    }
    else
    {
        for_each_material_property(type,
                                   [&](entt::id_type property, const entt::meta_data& data)
                                   {
                                       const auto name = entt::get_attribute_as<std::string>(data, "name");
                                       auto value = load_value(ar, name.c_str(), data.type());
                                       if(value)
                                       {
                                           obj.overrides_.push_back({property, std::move(value)});
                                           obj.changed_properties_.push_back(property);
                                       }
                                   });
    }
}
LOAD_INSTANTIATE(material_instance, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_instance, ser20::iarchive_binary_t);
} // namespace unravel
