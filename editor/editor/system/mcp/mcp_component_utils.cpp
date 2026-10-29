#include "mcp_component_utils.h"

#include "mcp_meta_properties.h"
#include "mcp_tools_common.h"

#include <engine/meta/ecs/components/all_components.h>

#include <engine/animation/animation.h>
#include <engine/animation/animation_controller.h>
#include <engine/assets/asset_manager.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/font.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/scripting/script_asset_reference.h>
#include <graphics/texture.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <hpp/optional.hpp>
#include <unordered_set>

namespace unravel::mcp
{
namespace
{

auto resolve_script_type(rtti::context& ctx, const std::string& script_type, std::string& error) -> dotnet::type
{
    if(script_type.empty())
    {
        error = "type_name is required for Script component";
        return {};
    }
    if(!ctx.has<script_system>())
    {
        error = "Script system unavailable";
        return {};
    }
    auto& ss = ctx.get_cached<script_system>();
    auto type = ss.get_type_by_fullname(script_type);
    if(type.valid())
    {
        return type;
    }
    // Allow short names (FlyOrbit) by scanning scriptable types.
    for(const auto& candidate : ss.get_all_scriptable_components())
    {
        if(candidate.get_name() == script_type || candidate.get_fullname() == script_type)
        {
            return candidate;
        }
    }
    error = "Unknown script type: " + script_type;
    return {};
}

/// How one C# field type is read and written as JSON: as a field, and as an element of an array or List.
struct script_field_codec
{
    std::function<hpp::optional<std::string>(rtti::context&, const dotnet::object&, const dotnet::field&)> to_json;
    std::function<bool(rtti::context&, dotnet::object&, const dotnet::field&, const simdjson::dom::element&, std::string&)>
        from_json;
    /// One element of an array or List of the given element type, as the collection holds it: boxed for a value
    /// type, a reference or null otherwise. Empty for a type whose collections MCP does not express.
    std::function<hpp::optional<std::string>(rtti::context&, const dotnet::type&, const dotnet::object&)>
        element_to_json;
    /// A new element of the given element type from the JSON, for a collection about to be written.
    std::function<
        bool(rtti::context&, const dotnet::type&, const simdjson::dom::element&, dotnet::object&, std::string&)>
        element_from_json;
};

/// The value a collection element of a value type holds (boxed), or the text of a string element.
template<typename T>
auto read_script_element(const dotnet::object& element) -> T
{
    if constexpr(std::is_same_v<T, std::string>)
    {
        return dotnet_converter<std::string>::from_managed(dotnet::get_managed_ptr(element));
    }
    else
    {
        return dotnet::unbox_value<T>(element);
    }
}

/// A collection element holding @p value: a managed string, or @p value boxed as @p element_type.
template<typename T>
auto make_script_element(const T& value, const dotnet::type& element_type) -> dotnet::object
{
    if constexpr(std::is_same_v<T, std::string>)
    {
        return dotnet::object(dotnet_converter<std::string>::to_managed(value));
    }
    else
    {
        return dotnet::box_value(dotnet_converter<T>::to_managed(value), element_type);
    }
}

/// A field accessed as exactly the C++ type @p T the inspector edits it as (the managed bridge copies the field's
/// own width, so a narrower or wider type misreads it), and spelled in JSON as the reflection bridge spells T.
template<typename T>
auto make_script_field_codec() -> script_field_codec
{
    return {.to_json = [](rtti::context& ctx, const dotnet::object& obj, const dotnet::field& field)
                -> hpp::optional<std::string>
            {
                entt::meta_any value{dotnet::make_field_invoker<T>(field).get_value(obj)};
                return meta_value_to_json(ctx, value);
            },
            .from_json = [](rtti::context& ctx,
                            dotnet::object& obj,
                            const dotnet::field& field,
                            const simdjson::dom::element& element,
                            std::string& error) -> bool
            {
                auto invoker = dotnet::make_field_invoker<T>(field);
                entt::meta_any value{invoker.get_value(obj)};
                if(!meta_value_from_json(ctx, element, value, error))
                {
                    return false;
                }
                invoker.set_value(obj, value.cast<T>());
                return true;
            },
            .element_to_json = [](rtti::context& ctx, const dotnet::type&, const dotnet::object& element)
                -> hpp::optional<std::string>
            {
                entt::meta_any value{read_script_element<T>(element)};
                return meta_value_to_json(ctx, value);
            },
            .element_from_json = [](rtti::context& ctx,
                                    const dotnet::type& element_type,
                                    const simdjson::dom::element& json,
                                    dotnet::object& element,
                                    std::string& error) -> bool
            {
                entt::meta_any value{T{}};
                if(!meta_value_from_json(ctx, json, value, error))
                {
                    return false;
                }
                element = make_script_element(value.cast<T>(), element_type);
                return true;
            }};
}

/// @p value spelled by the name enum @p type gives it, or as its number when no name declares it (a combination of
/// flags).
template<typename T>
auto script_enum_to_json(const dotnet::type& type, T value) -> std::string
{
    for(const auto& [candidate, name] : type.get_enum_values<T>())
    {
        if(candidate == value)
        {
            return make_json_string(name);
        }
    }
    return fmt::format("{}", value);
}

/// The value of enum @p type that @p element names; false with @p error listing the names when none matches.
template<typename T>
auto script_enum_from_json(const dotnet::type& type, const simdjson::dom::element& element, T& value, std::string& error)
    -> bool
{
    const auto values = type.get_enum_values<T>();
    hpp::string_view name;
    if(element.get_string().get(name) == simdjson::SUCCESS)
    {
        for(const auto& [candidate, candidate_name] : values)
        {
            if(candidate_name == name)
            {
                value = candidate;
                return true;
            }
        }
    }
    std::vector<std::string> names;
    std::transform(values.begin(),
                   values.end(),
                   std::back_inserter(names),
                   [](const auto& entry)
                   {
                       return entry.second;
                   });
    error = "expected one of " + fmt::format("{}", fmt::join(names, ", "));
    return false;
}

/// A C# enum field, accessed as its underlying type @p T and spelled by its value names.
template<typename T>
auto make_script_enum_codec() -> script_field_codec
{
    return {.to_json = [](rtti::context&, const dotnet::object& obj, const dotnet::field& field) -> hpp::optional<std::string>
            {
                return script_enum_to_json<T>(field.get_type(), dotnet::make_field_invoker<T>(field).get_value(obj));
            },
            .from_json = [](rtti::context&,
                            dotnet::object& obj,
                            const dotnet::field& field,
                            const simdjson::dom::element& element,
                            std::string& error) -> bool
            {
                T value{};
                if(!script_enum_from_json<T>(field.get_type(), element, value, error))
                {
                    return false;
                }
                dotnet::make_field_invoker<T>(field).set_value(obj, value);
                return true;
            },
            .element_to_json = [](rtti::context&, const dotnet::type& element_type, const dotnet::object& element)
                -> hpp::optional<std::string>
            {
                return script_enum_to_json<T>(element_type, dotnet::unbox_value<T>(element));
            },
            .element_from_json = [](rtti::context&,
                                    const dotnet::type& element_type,
                                    const simdjson::dom::element& json,
                                    dotnet::object& element,
                                    std::string& error) -> bool
            {
                T value{};
                if(!script_enum_from_json<T>(element_type, json, value, error))
                {
                    return false;
                }
                element = dotnet::box_value(value, element_type);
                return true;
            }};
}

/// An Entity field: the managed struct holds the entity of the scene being edited, accessed as entt::entity as the
/// inspector does, and spelled as the reflection bridge spells entity references (its id, or null).
auto make_script_entity_codec() -> script_field_codec
{
    const auto handle_of = [](rtti::context& ctx, entt::entity entity) -> entt::handle
    {
        auto* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
        return scn != nullptr && scn->registry ? entt::handle(*scn->registry, entity) : entt::handle{};
    };
    return {.to_json = [handle_of](rtti::context& ctx, const dotnet::object& obj, const dotnet::field& field)
                -> hpp::optional<std::string>
            {
                entt::meta_any value{handle_of(ctx, dotnet::make_field_invoker<entt::entity>(field).get_value(obj))};
                return meta_value_to_json(ctx, value);
            },
            .from_json = [handle_of](rtti::context& ctx,
                                     dotnet::object& obj,
                                     const dotnet::field& field,
                                     const simdjson::dom::element& element,
                                     std::string& error) -> bool
            {
                auto invoker = dotnet::make_field_invoker<entt::entity>(field);
                entt::meta_any value{handle_of(ctx, invoker.get_value(obj))};
                if(!meta_value_from_json(ctx, element, value, error))
                {
                    return false;
                }
                const auto handle = value.cast<entt::handle>();
                invoker.set_value(obj, handle ? handle.entity() : entt::entity{entt::null});
                return true;
            },
            .element_to_json = [handle_of](rtti::context& ctx, const dotnet::type&, const dotnet::object& element)
                -> hpp::optional<std::string>
            {
                entt::meta_any value{handle_of(ctx, read_script_element<entt::entity>(element))};
                return meta_value_to_json(ctx, value);
            },
            .element_from_json = [](rtti::context& ctx,
                                    const dotnet::type& element_type,
                                    const simdjson::dom::element& json,
                                    dotnet::object& element,
                                    std::string& error) -> bool
            {
                entt::meta_any value{entt::handle{}};
                if(!meta_value_from_json(ctx, json, value, error))
                {
                    return false;
                }
                const auto handle = value.cast<entt::handle>();
                element = make_script_element(handle ? handle.entity() : entt::entity{entt::null}, element_type);
                return true;
            }};
}

/// A new reference of @p reference_type to @p asset, as an asset element of an array or List holds it; null for an
/// empty handle.
template<typename T>
auto make_script_asset_reference(const dotnet::type& reference_type, const asset_handle<T>& asset) -> dotnet::object
{
    if(!asset)
    {
        return {};
    }
    auto reference = reference_type.new_instance();
    dotnet::make_property_invoker<hpp::uuid>(reference_type.get_property("uid")).set_value(reference, asset.uid());
    return reference;
}

/// An asset field (Prefab, Texture, ...): the managed reference read and written as the inspector does, spelled as
/// the reflection bridge spells asset handles (the asset's key, or null).
template<typename T>
auto make_script_asset_codec() -> script_field_codec
{
    return {.to_json = [](rtti::context& ctx, const dotnet::object& obj, const dotnet::field& field)
                -> hpp::optional<std::string>
            {
                const auto reference = dotnet::make_field_invoker<dotnet::object>(field).get_value(obj);
                entt::meta_any value{get_script_asset<T>(ctx.get_cached<asset_manager>(), reference)};
                return meta_value_to_json(ctx, value);
            },
            .from_json = [](rtti::context& ctx,
                            dotnet::object& obj,
                            const dotnet::field& field,
                            const simdjson::dom::element& element,
                            std::string& error) -> bool
            {
                auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
                entt::meta_any value{get_script_asset<T>(ctx.get_cached<asset_manager>(), invoker.get_value(obj))};
                if(!meta_value_from_json(ctx, element, value, error))
                {
                    return false;
                }
                set_script_asset(obj, invoker, value.cast<asset_handle<T>>());
                return true;
            },
            .element_to_json = [](rtti::context& ctx, const dotnet::type&, const dotnet::object& element)
                -> hpp::optional<std::string>
            {
                entt::meta_any value{get_script_asset<T>(ctx.get_cached<asset_manager>(), element)};
                return meta_value_to_json(ctx, value);
            },
            .element_from_json = [](rtti::context& ctx,
                                    const dotnet::type& element_type,
                                    const simdjson::dom::element& json,
                                    dotnet::object& element,
                                    std::string& error) -> bool
            {
                entt::meta_any value{asset_handle<T>{}};
                if(!meta_value_from_json(ctx, json, value, error))
                {
                    return false;
                }
                element = make_script_asset_reference(element_type, value.cast<asset_handle<T>>());
                return true;
            }};
}

/// C# field types by name, each with the C++ type the inspector's field table edits it as.
auto script_field_codecs() -> const std::map<std::string, script_field_codec>&
{
    static const std::map<std::string, script_field_codec> table{
        {"SByte", make_script_field_codec<int8_t>()},
        {"Byte", make_script_field_codec<uint8_t>()},
        {"Int16", make_script_field_codec<int16_t>()},
        {"UInt16", make_script_field_codec<uint16_t>()},
        {"Int32", make_script_field_codec<int32_t>()},
        {"UInt32", make_script_field_codec<uint32_t>()},
        {"Int64", make_script_field_codec<int64_t>()},
        {"UInt64", make_script_field_codec<uint64_t>()},
        {"Boolean", make_script_field_codec<bool>()},
        {"Single", make_script_field_codec<float>()},
        {"Double", make_script_field_codec<double>()},
        {"String", make_script_field_codec<std::string>()},
        {"Vector2", make_script_field_codec<math::vec2>()},
        {"Vector3", make_script_field_codec<math::vec3>()},
        {"Vector4", make_script_field_codec<math::vec4>()},
        {"Color", make_script_field_codec<math::color>()},
        {"LayerMask", make_script_field_codec<layer_mask>()},
        {"Entity", make_script_entity_codec()},
        {"Texture", make_script_asset_codec<gfx::texture>()},
        {"Material", make_script_asset_codec<material>()},
        {"Mesh", make_script_asset_codec<mesh>()},
        {"AnimationClip", make_script_asset_codec<animation_clip>()},
        {"AnimationController", make_script_asset_codec<animation_controller>()},
        {"Prefab", make_script_asset_codec<prefab>()},
        {"Scene", make_script_asset_codec<scene_prefab>()},
        {"PhysicsMaterial", make_script_asset_codec<physics_material>()},
        {"AudioClip", make_script_asset_codec<audio_clip>()},
        {"Font", make_script_asset_codec<font>()},
    };
    return table;
}

/// C# enum fields by their underlying type's name.
auto script_enum_codecs() -> const std::map<std::string, script_field_codec>&
{
    static const std::map<std::string, script_field_codec> table{
        {"SByte", make_script_enum_codec<int8_t>()},
        {"Byte", make_script_enum_codec<uint8_t>()},
        {"Int16", make_script_enum_codec<int16_t>()},
        {"UInt16", make_script_enum_codec<uint16_t>()},
        {"Int32", make_script_enum_codec<int32_t>()},
        {"UInt32", make_script_enum_codec<uint32_t>()},
        {"Int64", make_script_enum_codec<int64_t>()},
        {"UInt64", make_script_enum_codec<uint64_t>()},
    };
    return table;
}

auto find_script_field_codec(const dotnet::type& type) -> const script_field_codec*;

/// Whether @p type is a collection a Script field holds and MCP writes whole: a one-dimensional array or a List.
auto is_script_collection(const dotnet::type& type) -> bool
{
    return (type.is_array() && type.get_rank() == 1) || type.is_list();
}

/// The codec of the elements of collection type @p type, or none when MCP cannot express them (collections of
/// collections, of objects, of dictionaries).
auto find_script_element_codec(const dotnet::type& type) -> const script_field_codec*
{
    const auto* codec = is_script_collection(type) ? find_script_field_codec(type.get_element_type()) : nullptr;
    return codec != nullptr && codec->element_to_json ? codec : nullptr;
}

/// The elements an array or a List of @p collection_type holds.
auto read_script_collection(const dotnet::type& collection_type, const dotnet::object& collection)
    -> std::vector<dotnet::object>
{
    if(collection_type.is_array())
    {
        return dotnet::array<dotnet::object>(collection).to_vector();
    }
    return dotnet::list<dotnet::object>(collection).to_vector();
}

/// Points collection field @p field of @p obj at @p elements: a new array of them, or the List it holds cleared and
/// refilled (a new List when it holds none).
void write_script_collection(dotnet::object& obj, const dotnet::field& field, const std::vector<dotnet::object>& elements)
{
    const auto& collection_type = field.get_type();
    const auto element_type = collection_type.get_element_type();
    auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
    if(collection_type.is_array())
    {
        invoker.set_value(obj, dotnet::array<dotnet::object>(elements, element_type));
        return;
    }
    const auto current = invoker.get_value(obj);
    if(!current.valid())
    {
        invoker.set_value(obj, dotnet::list<dotnet::object>(elements, element_type));
        return;
    }
    dotnet::list<dotnet::object> list(current);
    list.set(elements, element_type);
}

/// An array or a List field whose element type has a codec: read as a JSON array of the elements' JSON, written whole
/// from one, every element checked before the field changes. null stands for a null field both ways.
auto make_script_collection_codec() -> script_field_codec
{
    return {.to_json = [](rtti::context& ctx, const dotnet::object& obj, const dotnet::field& field)
                -> hpp::optional<std::string>
            {
                const auto& collection_type = field.get_type();
                const auto* element_codec = find_script_element_codec(collection_type);
                if(element_codec == nullptr)
                {
                    return std::nullopt;
                }
                const auto collection = dotnet::make_field_invoker<dotnet::object>(field).get_value(obj);
                if(!collection.valid())
                {
                    return std::string("null");
                }
                const auto element_type = collection_type.get_element_type();
                const auto elements = read_script_collection(collection_type, collection);
                const auto pins = dotnet::pin_vector_elements(elements);
                std::string json = "[";
                for(const auto& element : elements)
                {
                    const auto element_json = element_codec->element_to_json(ctx, element_type, element);
                    if(!element_json)
                    {
                        return std::nullopt;
                    }
                    json += (json.size() > 1 ? "," : "") + *element_json;
                }
                return json + "]";
            },
            .from_json = [](rtti::context& ctx,
                            dotnet::object& obj,
                            const dotnet::field& field,
                            const simdjson::dom::element& value,
                            std::string& error) -> bool
            {
                const auto& collection_type = field.get_type();
                const auto* element_codec = find_script_element_codec(collection_type);
                if(element_codec == nullptr)
                {
                    error = "field type " + collection_type.get_name() + " cannot be set through MCP";
                    return false;
                }
                if(value.is_null())
                {
                    dotnet::make_field_invoker<dotnet::object>(field).set_value(obj, dotnet::object{});
                    return true;
                }
                const auto element_type = collection_type.get_element_type();
                simdjson::dom::array items;
                if(value.get_array().get(items) != simdjson::SUCCESS)
                {
                    error = "expected an array of " + element_type.get_name() + ", or null";
                    return false;
                }
                std::vector<dotnet::object> elements;
                std::vector<dotnet::object_pinned_ptr> pins;
                elements.reserve(items.size());
                pins.reserve(items.size());
                for(auto item : items)
                {
                    dotnet::object element;
                    std::string element_error;
                    if(!element_codec->element_from_json(ctx, element_type, item, element, element_error))
                    {
                        error = fmt::format("element {}: {}", elements.size(), element_error);
                        return false;
                    }
                    pins.push_back(dotnet::make_object_pinned(element));
                    elements.push_back(element);
                }
                write_script_collection(obj, field, elements);
                return true;
            }};
}

/// The one codec of every array and List field MCP expresses; each call finds its element codec by the field's type.
auto script_collection_codec() -> const script_field_codec&
{
    static const script_field_codec codec = make_script_collection_codec();
    return codec;
}

/// The codec for a field of @p type, or none when MCP cannot express it (nested objects, dictionaries, collections of
/// those).
auto find_script_field_codec(const dotnet::type& type) -> const script_field_codec*
{
    if(is_script_collection(type))
    {
        return find_script_element_codec(type) != nullptr ? &script_collection_codec() : nullptr;
    }
    const auto& table = type.is_enum() ? script_enum_codecs() : script_field_codecs();
    const auto found = table.find(type.is_enum() ? type.get_enum_base_type().get_name() : type.get_name());
    return found != table.end() ? &found->second : nullptr;
}

/// The fields a Script exposes: public, per instance, not hidden from the inspector.
auto is_exposed_field(const dotnet::field& field) -> bool
{
    return field.get_visibility() == dotnet::visibility::vis_public && !field.is_static() &&
           !field.has_attribute("HideAttribute");
}

/// The index-th of the entity's scripts of exactly `type` - counted as scripts_summary_json and the
/// remove action count them - or an empty one with `error` saying how many there are.
auto find_selected_script(const script_component& comp,
                          const dotnet::type& type,
                          const script_selector& script,
                          std::string& error) -> script_component::script_object
{
    size_t seen = 0;
    for(const auto& candidate : comp.get_script_components())
    {
        if(!candidate.pinned || candidate.is_marked_for_destroy() || !candidate.pinned->get_object().get_type().equals(type))
        {
            continue;
        }
        if(seen++ == script.index)
        {
            return candidate;
        }
    }
    error = seen == 0 ? "Script type not present on entity: " + script.type
                      : fmt::format("script_index {} is past the {} {} script(s) on the entity", script.index, seen, script.type);
    return {};
}

/// The managed object of the script @p script selects, or an invalid one with @p error set.
auto find_script_object(rtti::context& ctx,
                        script_component& comp,
                        const script_selector& script,
                        dotnet::type& type,
                        std::string& error) -> dotnet::object
{
    type = resolve_script_type(ctx, script.type, error);
    if(!type.valid())
    {
        return {};
    }
    auto script_obj = find_selected_script(comp, type, script, error);
    if(!script_obj.pinned || !script_obj.pinned->get_object().valid())
    {
        return {};
    }
    return script_obj.pinned->get_object();
}

auto script_to_json(rtti::context& ctx,
                    script_component& comp,
                    const script_selector& script,
                    const std::unordered_set<std::string>* filter,
                    std::string& error) -> std::string
{
    dotnet::type type;
    auto obj = find_script_object(ctx, comp, script, type, error);
    if(!obj.valid())
    {
        return {};
    }
    std::string json = R"({"type_name":)" + make_json_string(type.get_fullname());
    for(auto& field : type.get_fields(true))
    {
        const auto name = field.get_name();
        const auto* codec = is_exposed_field(field) ? find_script_field_codec(field.get_type()) : nullptr;
        if(codec == nullptr || (filter != nullptr && filter->count(name) == 0))
        {
            continue;
        }
        // A field the managed side refuses to read is left out rather than failing the whole script.
        try
        {
            if(const auto value_json = codec->to_json(ctx, obj, field))
            {
                json += "," + make_json_string(name) + ":" + *value_json;
            }
        }
        catch(const std::exception& ex)
        {
            APPLOG_WARNING("MCP could not read script field {}.{}: {}", type.get_fullname(), name, ex.what());
        }
    }
    return json + "}";
}

/// Writes one script field; false with @p error set when it cannot.
auto apply_script_field(rtti::context& ctx,
                        dotnet::object& obj,
                        const dotnet::field& field,
                        const simdjson::dom::element& value,
                        std::string& error) -> bool
{
    if(!is_exposed_field(field))
    {
        error = "field is not a public instance field";
        return false;
    }
    const auto* codec = find_script_field_codec(field.get_type());
    if(codec == nullptr)
    {
        error = "field type " + field.get_type().get_name() + " cannot be set through MCP";
        return false;
    }
    try
    {
        return codec->from_json(ctx, obj, field, value, error);
    }
    catch(const std::exception& ex)
    {
        error = ex.what();
        return false;
    }
}

auto apply_script_properties(rtti::context& ctx,
                             script_component& comp,
                             const script_selector& script,
                             const simdjson::dom::object& properties,
                             component_apply_result& result) -> void
{
    std::string error;
    dotnet::type type;
    auto obj = find_script_object(ctx, comp, script, type, error);
    if(!obj.valid())
    {
        result.ok = false;
        result.errors.push_back(error);
        return;
    }
    const auto fields = type.get_fields(true);
    for(auto entry : properties)
    {
        const std::string key(entry.key);
        // A read lists the type; writing it back is not an edit.
        if(key == "type_name")
        {
            continue;
        }
        const auto field = std::find_if(fields.begin(),
                                        fields.end(),
                                        [&key](const dotnet::field& candidate)
                                        {
                                            return candidate.get_name() == key;
                                        });
        if(field == fields.end())
        {
            result.unknown.push_back(key);
            continue;
        }
        std::string field_error;
        if(!apply_script_field(ctx, obj, *field, entry.value, field_error))
        {
            result.ok = false;
            result.errors.push_back(key + ": " + field_error);
            continue;
        }
        result.applied.push_back(key);
    }
    if(!result.unknown.empty())
    {
        result.ok = false;
        result.errors.push_back("unknown fields: " + fmt::format("{}", fmt::join(result.unknown, ", ")) +
                                " (see scene_get_component_properties_batch for the script's fields)");
    }
}

auto to_filter_set(const std::vector<std::string>* filter) -> std::unique_ptr<std::unordered_set<std::string>>
{
    if(!filter || filter->empty())
    {
        return nullptr;
    }
    return std::make_unique<std::unordered_set<std::string>>(filter->begin(), filter->end());
}

/// The addable component named `pretty_name` on `entity`, as a meta_any referring to it (empty
/// when the entity lacks it). `known` reports whether the name is an addable component at all.
/// all_addable_components is the list the Add Component menu offers, so the two always agree.
auto find_addable_component(entt::handle entity, const std::string& pretty_name, bool& known) -> entt::meta_any
{
    entt::meta_any found;
    known = false;
    hpp::for_each_tuple_type<all_addable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
            if(known || entt::get_pretty_name(entt::resolve<ctype>()) != pretty_name)
            {
                return;
            }
            known = true;
            if(auto* comp = entity.try_get<ctype>())
            {
                found = entt::forward_as_meta(*comp);
            }
        });
    return found;
}

/// Pretty names of every addable component, in the Add Component menu's order.
auto addable_component_pretty_names() -> const std::vector<std::string>&
{
    static const auto names = []
    {
        std::vector<std::string> result;
        hpp::for_each_tuple_type<all_addable_components>(
            [&](auto index)
            {
                using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
                result.push_back(entt::get_pretty_name(entt::resolve<ctype>()));
            });
        return result;
    }();
    return names;
}

auto addable_component_type(const std::string& pretty_name) -> entt::meta_type
{
    entt::meta_type found;
    hpp::for_each_tuple_type<all_addable_components>(
        [&](auto index)
        {
            using ctype = std::tuple_element_t<decltype(index)::value, all_addable_components>;
            auto type = entt::resolve<ctype>();
            if(!found && entt::get_pretty_name(type) == pretty_name)
            {
                found = type;
            }
        });
    return found;
}

/// The property bag of @p component on @p entity for entity_supported_component_properties_json, or empty.
auto component_bag_json(rtti::context& ctx,
                        entt::handle entity,
                        const std::string& component,
                        const script_selector& script = {}) -> std::string
{
    std::string error;
    return component_properties_to_json(ctx, entity, component, script, nullptr, error);
}

} // namespace

auto list_component_property_schema_json(const std::string& component_filter) -> std::string
{
    std::string json = "[";
    bool first = true;
    for(const auto& pretty : addable_component_pretty_names())
    {
        if(component_filter.empty() || component_filter == pretty)
        {
            append_meta_schema(addable_component_type(pretty), pretty, json, first);
        }
    }
    if(component_filter.empty() || component_filter == "Script")
    {
        json += first ? "" : ",";
        json += R"({"component":"Script","name":"type_name","type":"string","description":"Required C# type full name or short name"},)";
        json += R"({"component":"Script","name":"script_index","type":"integer","description":"Which script of that type, counting from 0; default 0"},)";
        json += R"({"component":"Script","name":"*","type":"any","description":"Public instance fields: numbers at their C# width, bool, string, Vector2/3/4, Color, LayerMask, enums by value name, Entity by id, assets by key or null; a T[] or List<T> of those as a JSON array of the element form, set whole, or null for a null field"})";
    }
    return json + "]";
}

auto list_components_json() -> std::string
{
    std::string json = "[";
    for(const auto& pretty : addable_component_pretty_names())
    {
        json += fmt::format(R"({}{{"name":{},"properties":{}}})",
                            json.size() > 1 ? "," : "",
                            make_json_string(pretty),
                            count_meta_properties(addable_component_type(pretty)));
    }
    return json + "]";
}

auto component_missing_error(const std::string& component) -> std::string
{
    if(component == "Script")
    {
        return "No Script on the entity (attach one with scene_add_components_batch, component Script)";
    }
    return "Component not present on entity: " + component + " (add it with scene_add_components_batch)";
}

auto is_supported_component_pretty_name(const std::string& component_pretty_name) -> bool
{
    // Script is not an addable component but carries one field bag per attached type.
    if(component_pretty_name == "Script")
    {
        return true;
    }
    // A component with nothing to set (Audio Listener) reads as an empty bag.
    return static_cast<bool>(addable_component_type(component_pretty_name));
}

auto component_meta_type(const std::string& component_pretty_name) -> entt::meta_type
{
    if(component_pretty_name == "Script")
    {
        return entt::resolve<script_component>();
    }
    return addable_component_type(component_pretty_name);
}

auto component_properties_to_json(rtti::context& ctx,
                                  entt::handle entity,
                                  const std::string& component_pretty_name,
                                  const script_selector& script,
                                  const std::vector<std::string>* property_filter,
                                  std::string& error) -> std::string
{
    error.clear();
    if(!entity)
    {
        error = "Invalid entity";
        return {};
    }
    const auto filter = to_filter_set(property_filter);
    if(component_pretty_name == "Script")
    {
        auto* comp = entity.try_get<script_component>();
        if(comp == nullptr)
        {
            error = component_missing_error("Script");
            return {};
        }
        return script_to_json(ctx, *comp, script, filter.get(), error);
    }
    bool known = false;
    auto object = find_addable_component(entity, component_pretty_name, known);
    if(!known)
    {
        error = "Unknown component: " + component_pretty_name + " (see scene_list_components)";
        return {};
    }
    if(!object)
    {
        error = component_missing_error(component_pretty_name);
        return {};
    }
    // Names asked for by name are read even while the current mode hides them.
    return meta_properties_to_json(ctx, object, filter.get(), filter != nullptr);
}

auto apply_component_properties(rtti::context& ctx,
                                entt::handle entity,
                                const std::string& component_pretty_name,
                                const script_selector& script,
                                const simdjson::dom::object& properties) -> component_apply_result
{
    component_apply_result result;
    const auto fail = [&result](const std::string& message)
    {
        result.ok = false;
        result.errors.push_back(message);
        return result;
    };
    if(!entity)
    {
        return fail("Invalid entity");
    }
    if(component_pretty_name == "Script")
    {
        auto* comp = entity.try_get<script_component>();
        if(comp == nullptr)
        {
            return fail(component_missing_error("Script"));
        }
        apply_script_properties(ctx, *comp, script, properties, result);
        return result;
    }
    bool known = false;
    auto object = find_addable_component(entity, component_pretty_name, known);
    if(!known)
    {
        return fail("Unknown component: " + component_pretty_name + " (see scene_list_components)");
    }
    if(!object)
    {
        return fail(component_missing_error(component_pretty_name));
    }
    // A playing emitter restarts so the edit shows at once; a stopped one stays stopped.
    auto* emitter = entity.try_get<particle_emitter_component>();
    const bool restarts_emitter =
        component_pretty_name == entt::get_pretty_name(entt::resolve<particle_emitter_component>()) && emitter != nullptr &&
        emitter->is_playing();
    apply_meta_properties(ctx, object, properties, result);
    if(restarts_emitter && result.ok)
    {
        emitter->stop_and_reset();
        emitter->play();
    }
    return result;
}

auto entity_supported_component_properties_json(rtti::context& ctx,
                                                 entt::handle entity,
                                                 const std::vector<std::string>* component_filter) -> std::string
{
    const auto is_wanted = [component_filter](const std::string& name)
    {
        return component_filter == nullptr || component_filter->empty() ||
               std::find(component_filter->begin(), component_filter->end(), name) != component_filter->end();
    };
    std::string json = "{";
    const auto add_bag = [&json](const std::string& key, const std::string& bag)
    {
        if(!bag.empty())
        {
            json += (json.size() > 1 ? "," : "") + make_json_string(key) + ":" + bag;
        }
    };
    for(const auto& pretty : addable_component_pretty_names())
    {
        if(is_wanted(pretty))
        {
            add_bag(pretty, component_bag_json(ctx, entity, pretty));
        }
    }
    // For Script, one bag per attached script, keyed by its type (and its index past the first).
    const auto* scripts = entity.try_get<script_component>();
    if(scripts != nullptr && is_wanted("Script"))
    {
        std::map<std::string, size_t> type_counts;
        for(const auto& script : scripts->get_script_components())
        {
            if(!script.pinned || !script.pinned->get_object().valid())
            {
                continue;
            }
            const auto type_name = script.pinned->get_object().get_type().get_fullname();
            const auto index = type_counts[type_name]++;
            const auto key = index == 0 ? type_name : fmt::format("{}[{}]", type_name, index);
            add_bag("Script:" + key, component_bag_json(ctx, entity, "Script", {type_name, index}));
        }
    }
    return json + "}";
}

auto component_apply_result_to_json(const component_apply_result& result) -> std::string
{
    return apply_result_to_json(result);
}

auto read_script_selector(const simdjson::dom::object& item, script_selector& out, std::string& error) -> bool
{
    hpp::string_view type;
    if(!item["type_name"].get(type))
    {
        out.type = std::string(type);
    }
    simdjson::dom::element index;
    if(item["script_index"].get(index))
    {
        return true;
    }
    uint64_t value{};
    if(index.get(value))
    {
        error = "script_index must be a whole number of 0 or more";
        return false;
    }
    out.index = static_cast<size_t>(value);
    return true;
}

auto find_script_position(entt::handle entity, const script_selector& script) -> hpp::optional<script_position>
{
    const auto* scripts = entity.try_get<script_component>();
    if(scripts == nullptr)
    {
        return std::nullopt;
    }
    const auto& list = scripts->get_script_components();
    size_t seen = 0;
    for(size_t i = 0; i < list.size(); ++i)
    {
        if(!list[i].pinned || !list[i].pinned->get_object().valid())
        {
            continue;
        }
        const auto type = list[i].pinned->get_object().get_type();
        if(type.get_fullname() != script.type && type.get_name() != script.type)
        {
            continue;
        }
        if(seen++ == script.index)
        {
            return script_position{i, type.get_fullname()};
        }
    }
    return std::nullopt;
}

auto resolve_script_type_name(rtti::context& ctx, const std::string& name, std::string& full_name, std::string& error)
    -> bool
{
    const auto type = resolve_script_type(ctx, name, error);
    if(!type.valid())
    {
        return false;
    }
    full_name = type.get_fullname();
    return true;
}

auto scripts_summary_json(entt::handle entity) -> std::string
{
    const auto* scripts = entity.try_get<script_component>();
    if(scripts == nullptr)
    {
        return "[]";
    }
    std::string json = "[";
    std::map<std::string, size_t> type_counts;
    for(const auto& script : scripts->get_script_components())
    {
        if(!script.pinned || !script.pinned->get_object().valid())
        {
            continue;
        }
        const auto type_name = script.pinned->get_object().get_type().get_fullname();
        if(json.size() > 1)
        {
            json += ",";
        }
        json += fmt::format(R"({{"type":{},"script_index":{}}})", make_json_string(type_name), type_counts[type_name]++);
    }
    return json + "]";
}

} // namespace unravel::mcp
