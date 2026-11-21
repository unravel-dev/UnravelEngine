#include "material_instance.hpp"
#include "material.hpp"
#include "standard_material.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/string.hpp>

#include <hpp/optional.hpp>

#include <algorithm>
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

/// Saves named overrides as an object keyed by name. Nothing for none.
template<typename Archive, typename Value>
void save_named_values(Archive& ar, const char* key, const std::vector<Value>& values)
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

/// Loads what save_named_values saved; none when the key is not there.
template<typename Archive, typename Value>
auto load_named_values(Archive& ar, const char* key) -> std::vector<Value>
{
    std::vector<Value> values;
    serialization::load_named_members(ar,
                                      key,
                                      [&](const std::string& name)
                                      {
                                          Value entry{};
                                          entry.name = name;
                                          if(!try_load(ar, ser20::make_nvp(entry.name.c_str(), get_named_value(entry))))
                                          {
                                              return false;
                                          }
                                          values.push_back(std::move(entry));
                                          return true;
                                      });
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

/// Loads the overrides a PBR material's instance saved, by name.
template<typename Archive>
auto load_legacy_pbr_overrides(Archive& ar) -> legacy_pbr_overrides
{
    legacy_pbr_overrides overrides;
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
    // By property name, so a prefab override points at one property of one slot.
    for(const auto& entry : overrides)
    {
        const auto name = get_property_name(type, entry.property);
        if(!name.empty())
        {
            save_value(ar, name.c_str(), entry.value);
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

namespace
{

/// Whether the path filter in force reads the member @p name of the value being loaded; always without a filter.
auto is_member_listed(const std::string& name) -> bool
{
    const auto* path_ctx = serialization::get_path_context();
    if(path_ctx == nullptr || !path_ctx->is_recording())
    {
        return true;
    }
    const serialization::path_segment_guard member(name);
    return path_ctx->should_serialize_property(path_ctx->get_current_path());
}

/// A member name no saved instance has: the filter in force reads it only when it lists the instance as a whole.
constexpr const char* WHOLE_INSTANCE_PROBE = "*";

/// Merges the listed named overrides under @p key into @p values: one the document has is set, one it lacks is
/// reverted, and the rest stay. True when any listed one changed.
template<typename Archive, typename Value>
auto merge_listed_named_values(Archive& ar, const char* key, std::vector<Value>& values) -> bool
{
    std::vector<Value> saved;
    {
        const serialization::path_context_pause unfiltered;
        saved = load_named_values<Archive, Value>(ar, key);
    }
    const serialization::path_segment_guard list(key);
    const auto is_saved = [&saved](const std::string& name)
    {
        return std::any_of(saved.begin(),
                           saved.end(),
                           [&name](const Value& entry)
                           {
                               return entry.name == name;
                           });
    };
    const auto reverted = std::remove_if(values.begin(),
                                         values.end(),
                                         [&](const Value& entry)
                                         {
                                             return !is_saved(entry.name) && is_member_listed(entry.name);
                                         });
    bool is_changed = reverted != values.end();
    values.erase(reverted, values.end());
    for(auto& entry : saved)
    {
        if(!is_member_listed(entry.name))
        {
            continue;
        }
        auto found = std::find_if(values.begin(),
                                  values.end(),
                                  [&entry](const Value& existing)
                                  {
                                      return existing.name == entry.name;
                                  });
        if(found != values.end())
        {
            *found = std::move(entry);
        }
        else
        {
            values.push_back(std::move(entry));
        }
        is_changed = true;
    }
    return is_changed;
}

/// Reverts, by @p revert, the named overrides of @p values that the filter in force lists under @p key.
template<typename Value, typename Revert>
void revert_listed_named(const char* key, const std::vector<Value>& values, Revert&& revert)
{
    const serialization::path_segment_guard list(key);
    std::vector<std::string> listed;
    for(const auto& entry : values)
    {
        if(is_member_listed(entry.name))
        {
            listed.push_back(entry.name);
        }
    }
    for(const auto& name : listed)
    {
        revert(name);
    }
}

} // namespace

auto is_instance_listed_in_part() -> bool
{
    const auto* path_ctx = serialization::get_path_context();
    if(path_ctx == nullptr || !path_ctx->is_recording())
    {
        return false;
    }
    const std::string path = path_ctx->get_current_path();
    return path_ctx->should_serialize_property(path) &&
           !path_ctx->should_serialize_property(path + "/" + WHOLE_INSTANCE_PROBE);
}

void revert_listed_overrides(material_instance& instance)
{
    if(const auto type = instance.get_material_type())
    {
        std::vector<entt::id_type> listed;
        for(const auto& entry : instance.get_overrides())
        {
            const auto name = get_property_name(type, entry.property);
            if(!name.empty() && is_member_listed(name))
            {
                listed.push_back(entry.property);
            }
        }
        for(const auto property : listed)
        {
            instance.revert(property);
        }
    }
    revert_listed_named(GRAPH_PARAMETERS_KEY,
                        instance.get_graph_parameter_overrides(),
                        [&instance](const std::string& name)
                        {
                            instance.revert_graph_parameter(name);
                        });
    revert_listed_named(GRAPH_TEXTURES_KEY,
                        instance.get_graph_texture_overrides(),
                        [&instance](const std::string& name)
                        {
                            instance.revert_graph_texture(name);
                        });
}

LOAD(material_instance)
{
    if(is_instance_listed_in_part())
    {
        // The listed overrides merge into the instance as it is: one the document has is set, one it lacks is
        // reverted, every other one stays.
        std::string partial_type_name;
        {
            const serialization::path_context_pause unfiltered;
            try_load(ar, ser20::make_nvp(MATERIAL_TYPE_KEY, partial_type_name));
        }
        const auto partial_type = partial_type_name.empty() || partial_type_name == LEGACY_PBR_TYPE_NAME
                                      ? entt::meta_type{}
                                      : entt::resolve(entt::hashed_string{partial_type_name.c_str()}.value());
        // Overrides of another material type do not apply to this instance's material.
        if(partial_type && obj.material_type_ != entt::id_type{} && obj.material_type_ != partial_type.id())
        {
            return;
        }
        if(partial_type)
        {
            if(obj.material_type_ == entt::id_type{})
            {
                obj.material_type_ = partial_type.id();
            }
            for_each_material_property(
                partial_type,
                [&](entt::id_type property, const entt::meta_data& data)
                {
                    const auto name = entt::get_attribute_as<std::string>(data, "name");
                    if(!is_member_listed(name))
                    {
                        return;
                    }
                    auto value = load_value(ar, name.c_str(), data.type());
                    auto found = std::find_if(obj.overrides_.begin(),
                                              obj.overrides_.end(),
                                              [property](const material_instance::property_override& entry)
                                              {
                                                  return entry.property == property;
                                              });
                    if(value && found != obj.overrides_.end())
                    {
                        found->value = std::move(value);
                    }
                    else if(value)
                    {
                        obj.overrides_.push_back({property, std::move(value)});
                    }
                    else if(found != obj.overrides_.end())
                    {
                        obj.overrides_.erase(found);
                    }
                    else
                    {
                        return;
                    }
                    obj.changed_properties_.push_back(property);
                });
        }
        const bool are_parameters_changed =
            merge_listed_named_values<Archive, graph_parameter_value>(ar, GRAPH_PARAMETERS_KEY, obj.graph_parameters_);
        const bool are_textures_changed =
            merge_listed_named_values<Archive, graph_texture_value>(ar, GRAPH_TEXTURES_KEY, obj.graph_textures_);
        obj.are_graph_overrides_changed_ = obj.are_graph_overrides_changed_ || are_parameters_changed || are_textures_changed;
        return;
    }
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
    obj.graph_parameters_ = load_named_values<Archive, graph_parameter_value>(ar, GRAPH_PARAMETERS_KEY);
    obj.graph_textures_ = load_named_values<Archive, graph_texture_value>(ar, GRAPH_TEXTURES_KEY);
    obj.are_graph_overrides_changed_ = obj.are_graph_overrides_changed_ || obj.has_graph_overrides();
}
LOAD_INSTANTIATE(material_instance, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(material_instance, ser20::iarchive_binary_t);
} // namespace unravel
