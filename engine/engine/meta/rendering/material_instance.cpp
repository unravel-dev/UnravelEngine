#include "material_instance.hpp"
#include "material.hpp"
#include "standard_material.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>

#include <hpp/optional.hpp>

#include <stdexcept>

namespace unravel
{
namespace
{

/// The key every saved instance has: an older save wrote a whole material per slot instead (see the model's LOAD).
constexpr const char* MATERIAL_TYPE_KEY = "material_type";
/// A graph material's overrides by name: objects whose keys are the parameters' names and the texture nodes' ids.
constexpr const char* GRAPH_PARAMETERS_KEY = "graph_parameters";
constexpr const char* GRAPH_TEXTURES_KEY = "graph_textures";

/// The value of a named override (graph_parameter_value::value, graph_texture_value::texture).
inline auto get_named_value(graph_parameter_value& entry) -> math::vec4&
{
    return entry.value;
}

inline auto get_named_value(graph_texture_value& entry) -> asset_handle<gfx::texture>&
{
    return entry.texture;
}

inline auto get_named_value(const graph_parameter_value& entry) -> const math::vec4&
{
    return entry.value;
}

inline auto get_named_value(const graph_texture_value& entry) -> const asset_handle<gfx::texture>&
{
    return entry.texture;
}

/// Saves named overrides: a count and the entries in binary, an object keyed by name otherwise. Nothing for none.
template<typename Archive, typename Value>
void save_named_values(Archive& ar, const char* key, const std::vector<Value>& values)
{
    if constexpr(is_binary_archive<Archive>())
    {
        auto count = static_cast<uint32_t>(values.size());
        try_save(ar, ser20::make_nvp("count", count));
        for(const auto& entry : values)
        {
            try_save(ar, ser20::make_nvp("name", entry.name));
            try_save(ar, ser20::make_nvp("value", get_named_value(entry)));
        }
    }
    else
    {
        if(values.empty())
        {
            return;
        }
        ar.setNextName(key);
        ar.startNode();
        for(const auto& entry : values)
        {
            try_save(ar, ser20::make_nvp(entry.name.c_str(), get_named_value(entry)));
        }
        ar.finishNode();
    }
}

/// Loads what save_named_values saved; none when the key is not there.
template<typename Archive, typename Value>
auto load_named_values(Archive& ar, const char* key) -> std::vector<Value>
{
    std::vector<Value> values;
    if constexpr(is_binary_archive<Archive>())
    {
        uint32_t count = 0;
        try_load(ar, ser20::make_nvp("count", count));
        values.resize(count);
        for(auto& entry : values)
        {
            try_load(ar, ser20::make_nvp("name", entry.name));
            try_load(ar, ser20::make_nvp("value", get_named_value(entry)));
        }
    }
    else
    {
        if(!ar.hasNextName(key))
        {
            return values;
        }
        ar.setNextName(key);
        ar.startNode();
        // Every member is read, so each step moves on: a value that does not load ends the list.
        while(const char* name = ar.getNodeName())
        {
            Value entry{};
            entry.name = name;
            if(!try_load(ar, ser20::make_nvp(entry.name.c_str(), get_named_value(entry))))
            {
                break;
            }
            values.push_back(std::move(entry));
        }
        ar.finishNode();
    }
    return values;
}

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

/// The material type a save names for the overrides of a PBR material, which every material was before graphs.
constexpr const char* LEGACY_PBR_TYPE_NAME = "pbr_material";
/// The PBR material's properties no Standard parameter keeps: the cull type and the alpha mode are the graph's, so a
/// per-renderer one is dropped.
constexpr const char* LEGACY_CULL_TYPE_KEY = "cull_type";
constexpr const char* LEGACY_ALPHA_MODE_KEY = "alpha_mode";
/// A PBR material saved its dither threshold as two numbers: the alpha threshold, and the distance the Standard graph's
/// Dither Threshold parameter keeps.
constexpr const char* LEGACY_DITHER_THRESHOLD_KEY = "dither_threshold";

/// The type a PBR material saved its property @p key as; invalid for a key it did not have.
auto get_legacy_pbr_value_type(const std::string& key) -> entt::meta_type
{
    if(key == LEGACY_CULL_TYPE_KEY)
    {
        return entt::resolve<cull_type>();
    }
    if(key == LEGACY_ALPHA_MODE_KEY)
    {
        return entt::resolve<alpha_mode>();
    }
    if(key == LEGACY_DITHER_THRESHOLD_KEY)
    {
        return entt::resolve<math::vec2>();
    }
    const auto* property = find_legacy_pbr_property(key);
    if(property == nullptr)
    {
        return {};
    }
    switch(property->type)
    {
        case standard_property::kind::color:
            return entt::resolve<math::color>();
        case standard_property::kind::scalar:
            return entt::resolve<float>();
        case standard_property::kind::vector2:
            return entt::resolve<math::vec2>();
        case standard_property::kind::texture:
            return entt::resolve<asset_handle<gfx::texture>>();
    }
    return {};
}

/// A PBR material's saved value as the vec4 make_standard_parameter_value reads; empty for a texture or a mode.
auto get_legacy_pbr_vector(const std::string& key, const entt::meta_any& value) -> hpp::optional<math::vec4>
{
    if(const auto* color = value.try_cast<math::color>())
    {
        return color->value;
    }
    if(const auto* number = value.try_cast<float>())
    {
        return math::vec4(*number);
    }
    if(const auto* pair = value.try_cast<math::vec2>())
    {
        return key == LEGACY_DITHER_THRESHOLD_KEY ? math::vec4(pair->y) : math::vec4(*pair, 0.0f, 0.0f);
    }
    return std::nullopt;
}

/// The overrides a PBR material's instance saved, as the Standard graph takes them.
struct legacy_pbr_overrides
{
    std::vector<graph_parameter_value> parameters;
    std::vector<graph_texture_value> textures;
};

/// Takes one override a PBR material's instance saved, as the Standard graph overrides it: a parameter or a texture by
/// name. The modes are the graph's.
void take_legacy_pbr_override(legacy_pbr_overrides& overrides, const std::string& key, const entt::meta_any& value)
{
    if(!value || key == LEGACY_ALPHA_MODE_KEY || key == LEGACY_CULL_TYPE_KEY)
    {
        return;
    }
    const auto* property = find_legacy_pbr_property(key);
    if(property == nullptr)
    {
        return;
    }
    if(const auto* texture = value.try_cast<asset_handle<gfx::texture>>())
    {
        overrides.textures.push_back({property->name, *texture});
        return;
    }
    if(const auto vector = get_legacy_pbr_vector(key, value))
    {
        overrides.parameters.push_back({property->name, make_standard_parameter_value(*property, *vector)});
    }
}

/// Loads the overrides a PBR material's instance saved: by name in text, as (name, value) pairs in binary.
template<typename Archive>
auto load_legacy_pbr_overrides(Archive& ar) -> legacy_pbr_overrides
{
    legacy_pbr_overrides overrides;
    if constexpr(is_binary_archive<Archive>())
    {
        uint32_t count = 0;
        try_load(ar, ser20::make_nvp("count", count));
        for(uint32_t i = 0; i < count; ++i)
        {
            std::string name;
            try_load(ar, ser20::make_nvp("property", name));
            const auto type = get_legacy_pbr_value_type(name);
            if(!type)
            {
                throw std::runtime_error("A material instance overrides the unknown property " + name + ".");
            }
            take_legacy_pbr_override(overrides, name, load_value(ar, name.c_str(), type));
        }
    }
    else
    {
        const auto take = [&](const std::string& name)
        {
            take_legacy_pbr_override(overrides, name, load_value(ar, name.c_str(), get_legacy_pbr_value_type(name)));
        };
        take(LEGACY_CULL_TYPE_KEY);
        for(const auto& property : get_standard_properties())
        {
            take(get_legacy_pbr_key(property));
        }
        take(LEGACY_ALPHA_MODE_KEY);
    }
    return overrides;
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
    std::vector<graph_parameter_value> graph_parameters;
    std::vector<graph_texture_value> graph_textures;
    obj.collect_graph_overrides(graph_parameters, graph_textures);
    save_named_values(ar, GRAPH_PARAMETERS_KEY, graph_parameters);
    save_named_values(ar, GRAPH_TEXTURES_KEY, graph_textures);
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
    obj.are_graph_overrides_changed_ = obj.are_graph_overrides_changed_ || obj.has_graph_overrides();
    obj.graph_parameters_.clear();
    obj.graph_textures_.clear();
    if(type_name.empty())
    {
        return;
    }
    if(type_name == LEGACY_PBR_TYPE_NAME)
    {
        // A PBR material's overrides, saved by a version before every material was a graph: its file loads as the
        // Standard graph. Its save ends with them.
        obj.material_type_ = entt::resolve<graph_material>().id();
        auto legacy = load_legacy_pbr_overrides(ar);
        obj.graph_parameters_ = std::move(legacy.parameters);
        obj.graph_textures_ = std::move(legacy.textures);
        obj.are_graph_overrides_changed_ = obj.are_graph_overrides_changed_ || obj.has_graph_overrides();
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
            // A read-only property (the cull type) is the graph's: its saved override is read past.
            if(value && !data.is_const())
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
    obj.graph_parameters_ = load_named_values<Archive, graph_parameter_value>(ar, GRAPH_PARAMETERS_KEY);
    obj.graph_textures_ = load_named_values<Archive, graph_texture_value>(ar, GRAPH_TEXTURES_KEY);
    obj.are_graph_overrides_changed_ = obj.are_graph_overrides_changed_ || obj.has_graph_overrides();
}
LOAD_INSTANTIATE(material_instance, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_instance, ser20::iarchive_binary_t);
} // namespace unravel
