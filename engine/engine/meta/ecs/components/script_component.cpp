#include "script_component.hpp"
#include "logging/logging.h"
#include "serialization/serialization.h"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <dotnetpp/dotnetpp.h>

#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/core/math/quaternion.hpp>

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/font.hpp>
#include <engine/meta/animation/animation.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/layers/layer_mask.hpp>
#include <graphics/texture.h>
#include <hpp/finally.hpp>
#include <uuid/uuid.h>

#include <map>
#include <set>
#include <utility>

namespace unravel
{

namespace
{
    /**
     * @brief The state of one script list while it loads.
     *
     * A prefab update matches each script the document lists to the live script with the same
     * slot and loads it in place. The document's scripts that match none are added afterwards,
     * and the live ones the document states as removed go.
     */
    struct script_list_load_context
    {
        /// The live scripts, for a prefab update; empty otherwise.
        script_component::script_components_t live_scripts;

        /// Scripts of each type read so far, for the slots of scripts saved without one.
        std::map<std::string, size_t> type_counts;

        /// Every slot the document lists, whether the replay let it through or not.
        std::set<hpp::uuid> document_slots;

        /// Live scripts the document states as removed.
        std::vector<hpp::uuid> removed_slots;

        auto find_live(const hpp::uuid& slot) -> script_component::script_object*
        {
            auto it = std::find_if(std::begin(live_scripts),
                                   std::end(live_scripts),
                                   [&](const auto& script)
                                   {
                                       return script.slot == slot;
                                   });
            return it != std::end(live_scripts) ? &(*it) : nullptr;
        }
    };

    thread_local script_list_load_context* script_list_ctx{};

    /// What precedes a script's slot in a property path below the script component.
    constexpr std::string_view SCRIPT_LIST_SEGMENT = "script_components[";

    /// A slot for a script saved without one. The k-th script of a type gets the same slot in a
    /// prefab and in every instance of it, so files written before slots keep matching.
    auto derive_script_slot(const std::string& type_name, size_t occurrence) -> hpp::uuid
    {
        return generate_uuid(type_name + "#" + std::to_string(occurrence));
    }

    /// Whether the replay filter lets the property at the current path load.
    auto is_current_path_replayed() -> bool
    {
        const auto* path_ctx = serialization::get_path_context();
        return path_ctx == nullptr || path_ctx->should_serialize_property(path_ctx->get_current_path());
    }
}

REFLECT(script_component)
{
    entt::meta_factory<script_component>{}
        .type("script_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "script_component"},
            entt::attribute{"category", "Scripting"},
            entt::attribute{"pretty_name", "Script"},
        })
        .func<&component_meta<script_component>::exists>("component_exists"_hs)
        .func<&component_meta<script_component>::add>("component_add"_hs)
        .func<&component_meta<script_component>::save>("component_save"_hs)
        .func<&component_meta<script_component>::load>("component_load"_hs)
        .func<&component_meta<script_component>::remove>("component_remove"_hs);
}

template<typename Archive, typename T>
struct mono_saver
{
    template<typename Invoker>
    static auto try_save_mono_invoker(ser20::detail::OutputArchiveBase& arbase,
                                      const dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto val = invoker.get_value(obj);
        return try_save(ar, ser20::make_nvp(invoker.get_name(), val));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        // Extract value directly from mono_object using mono_converter
        T value = dotnet_converter<T>::from_managed(dotnet::get_managed_ptr(obj));
        return try_save(ar, ser20::make_nvp("value", value));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const dotnet::object& obj,
                                    const dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<T>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const dotnet::object& obj,
                                       const dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<T>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive>
struct mono_saver<Archive, entt::entity>
{
    template<typename Invoker>
    static auto try_save_mono_invoker(ser20::detail::OutputArchiveBase& arbase,
                                      const dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto val = invoker.get_value(obj);


        auto& save_ctx = get_save_context();
        auto& registry = *save_ctx.save_source.registry();

        ser20::const_entity_handle_link e;
        e.handle = entt::const_handle(registry, val);

        return try_save(ar, ser20::make_nvp(invoker.get_name(), e));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        auto entity = dotnet_converter<entt::entity>::from_managed(dotnet::get_managed_ptr(obj));
        
        auto& save_ctx = get_save_context();
        auto& registry = *save_ctx.save_source.registry();

        ser20::const_entity_handle_link e;
        e.handle = entt::const_handle(registry, entity);
        
        return try_save(ar, ser20::make_nvp("value", e));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const dotnet::object& obj,
                                    const dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<entt::entity>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const dotnet::object& obj,
                                       const dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<entt::entity>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_saver<Archive, asset_handle<T>>
{
    template<typename Invoker>
    static auto try_save_mono_invoker(ser20::detail::OutputArchiveBase& arbase,
                                      const dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        auto val = invoker.get_value(obj);

        asset_handle<T> asset{};
        if(val)
        {
            const auto& invoker_type = invoker.get_type();
            auto guid_property = invoker_type.get_property("uid");
            auto mutable_uid_property = dotnet::make_property_invoker<hpp::uuid>(guid_property);
            auto uid = mutable_uid_property.get_value(val);

            auto& ctx = engine::context();
            auto& am = ctx.get_cached<asset_manager>();
            asset = am.get_asset<T>(uid);
        }

        return try_save(ar, ser20::make_nvp(invoker.get_name(), asset));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        
        asset_handle<T> asset{};
        if(obj.valid())
        {
            auto obj_type = obj.get_type();
            auto prop = obj_type.get_property("uid");
            if(prop.is_valid())
            {
                auto uid_prop = dotnet::make_property_invoker<hpp::uuid>(prop);
                auto uid = uid_prop.get_value(obj);
                
                auto& ctx = engine::context();
                auto& am = ctx.get_cached<asset_manager>();
                asset = am.get_asset<T>(uid);
            }
        }
        
        return try_save(ar, ser20::make_nvp("value", asset));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const dotnet::object& obj,
                                    const dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const dotnet::object& obj,
                                       const dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<dotnet::object>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_loader
{
    template<typename U>
    static auto is_supported_type(const dotnet::type& type) -> bool
    {
        return dotnet::is_compatible_type<U>(type);
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        if(is_supported_type<T>(invoker.get_type()))
        {
            T val{};
            if(try_load(ar, ser20::make_nvp(invoker.get_name(), val)))
            {
                invoker.set_value(obj, val);
            }
            return true;
        }
        return false;
    }

    static auto try_load_mono_object(ser20::detail::InputArchiveBase& arbase,
                                     dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        T value{};
        if(try_load(ar, ser20::make_nvp("value", value)))
        {
            // Box the value back into the mono_object
            auto obj_type = obj.get_type();
            if(obj_type.is_valuetype())
            {
                auto mono_value = dotnet_converter<T>::to_managed(value);
                obj.box_value(mono_value, obj_type);
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    dotnet::object& obj,
                                    dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<T>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       dotnet::object& obj,
                                       dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<T>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};


template<typename Archive>
struct mono_loader<Archive, std::string>
{
    template<typename U>
    static auto is_supported_type(const dotnet::type& type) -> bool
    {
        return type.is_string();
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        if(is_supported_type<std::string>(invoker.get_type()))
        {
            std::string val{};
            if(try_load(ar, ser20::make_nvp(invoker.get_name(), val)))
            {
                invoker.set_value(obj, val);
            }
            return true;
        }
        return false;
    }

    static auto try_load_mono_object(ser20::detail::InputArchiveBase& arbase,
                                     dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto obj_type = obj.get_type();
        if(is_supported_type<std::string>(obj_type))
        {
            std::string value{};
            if(try_load(ar, ser20::make_nvp("value", value)))
            {
                obj = dotnet::object(dotnet_converter<std::string>::to_managed(value));
                return true;      
            }
        }

        
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    dotnet::object& obj,
                                    dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<std::string>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       dotnet::object& obj,
                                       dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<std::string>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive>
struct mono_loader<Archive, entt::entity>
{
    template<typename U>
    static auto is_supported_type(const dotnet::type& type) -> bool
    {
        const auto& expected_name = type.get_name();
        bool is_supported = std::is_same_v<entt::entity, U> && expected_name == "Entity";
        return is_supported;
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        if(is_supported_type<entt::entity>(invoker.get_type()))
        {
            ser20::entity_handle_link val{};
            if(try_load(ar, ser20::make_nvp(invoker.get_name(), val)))
            {
                invoker.set_value(obj, val.handle.entity());
            }
            else
            {
                return true;
            }
            return true;
        }
        return false;
    }

    static auto try_load_mono_object(ser20::detail::InputArchiveBase& arbase,
                                     dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        ser20::entity_handle_link val{};
        if(try_load(ar, ser20::make_nvp("value", val)))
        {
            entt::entity entity = val.handle ? val.handle.entity() : entt::null;
            // Box the entity value back into the mono_object
            auto obj_type = obj.get_type();
            if(obj_type.is_valuetype())
            {
                auto mono_entity = dotnet_converter<entt::entity>::to_managed(entity);
                obj.box_value(mono_entity, obj_type);
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    dotnet::object& obj,
                                    dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<entt::entity>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       dotnet::object& obj,
                                       dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<entt::entity>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_loader<Archive, asset_handle<T>>
{
    template<typename U>
    static auto is_supported_type(const dotnet::type& type) -> bool
    {
        const auto& expected_name = type.get_name();
        bool is_supported = false;

        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<gfx::texture>, U> && expected_name == "Texture";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<material>, U> && expected_name == "Material";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<mesh>, U> && expected_name == "Mesh";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<animation_clip>, U> && expected_name == "AnimationClip";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<prefab>, U> && expected_name == "Prefab";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<scene_prefab>, U> && expected_name == "Scene";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<physics_material>, U> && expected_name == "PhysicsMaterial";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<audio_clip>, U> && expected_name == "AudioClip";
        }
        if(!is_supported)
        {
            is_supported |= std::is_same_v<asset_handle<font>, U> && expected_name == "Font";
        }
        return is_supported;
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      dotnet::object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        if(is_supported_type<asset_handle<T>>(invoker.get_type()))
        {
            asset_handle<T> val{};
            if(try_load(ar, ser20::make_nvp(invoker.get_name(), val)))
            {
                const auto& field_type = invoker.get_type();
                auto guid_property = field_type.get_property("uid");
                auto mutable_uid_property = dotnet::make_property_invoker<hpp::uuid>(guid_property);

                auto var = invoker.get_value(obj);
                if(!var && val)
                {
                    var = field_type.new_instance();
                    invoker.set_value(obj, var);
                }

                if(var)
                {
                    mutable_uid_property.set_value(var, val.uid());
                }
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_object(ser20::detail::InputArchiveBase& arbase,
                                     dotnet::object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        const auto& obj_type = obj.get_type();

        if(is_supported_type<asset_handle<T>>(obj_type))
        {
            asset_handle<T> asset{};
            if(try_load(ar, ser20::make_nvp("value", asset)))
            {
                if(obj.valid())
                {
                    auto prop = obj_type.get_property("uid");
                    if(prop.is_valid())
                    {
                        auto uid_prop = dotnet::make_property_invoker<hpp::uuid>(prop);
                        uid_prop.set_value(obj, asset ? asset.uid() : hpp::uuid{});
                        return true;
                    }
                    
                }
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    dotnet::object& obj,
                                    dotnet::field& field) -> bool
    {
        auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       dotnet::object& obj,
                                       dotnet::property& prop) -> bool
    {
        auto invoker = dotnet::make_property_invoker<dotnet::object>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};
} // namespace unravel


auto get_type_by_fullname(const std::string& fullname) -> dotnet::type
{
    auto& ctx = unravel::engine::context();
    auto& script_sys = ctx.get_cached<unravel::script_system>();
    return script_sys.get_type_by_fullname(fullname);
}

namespace ser20
{
template<typename Archive>
inline auto try_save_mono_type(Archive& ar, const char* name, const dotnet::type& t) -> bool
{
    return try_save(ar, ser20::make_nvp(name, t.get_fullname()));
}

template<typename Archive>
inline auto try_load_mono_type(Archive& ar, const char* name, dotnet::type& t) -> bool
{
    std::string fullname;
    if(try_load(ar, ser20::make_nvp(name, fullname)))
    {
        t = get_type_by_fullname(fullname);
        return true;
    }
    return false;
}

/// Saves a list as the name of its element type, its size and its elements.
template<typename Archive, typename VectorLike>
void save_mono_elements(Archive& ar, const dotnet::type& type, const VectorLike& container)
{
    try_save_mono_type(ar, "type", type);

    try_save(ar, ser20::make_nvp("size", container.size()));
    try_save(ar, ser20::make_nvp("container", container));
}

/// Loads the size and the elements of a saved list into new objects of the given type.
template<typename Archive, typename VectorLike>
void load_mono_elements(Archive& ar, const dotnet::type& type, VectorLike& container)
{
    ser20::size_type size{};
    try_load(ar, ser20::make_nvp("size", size));
    container.resize(static_cast<std::size_t>(size));
    std::vector<dotnet::object_pinned_ptr> element_pins;
    element_pins.reserve(container.size());
    for(auto& v : container)
    {
        v = type.new_instance();
        element_pins.push_back(dotnet::make_object_pinned(v));
    }
    {
        serialization::path_skip_segment_guard guard(true);
        try_load(ar, ser20::make_nvp("container", container));
    }
}

template<typename Archive, typename T>
inline void SAVE_FUNCTION_NAME(Archive& ar, const dotnet::vector_like_wrapper<T>& obj)
{
    save_mono_elements(ar, obj.type, obj.container);
}

/// The elements of an array, a List or one side of a Dictionary. They are saved with the name of
/// their type but loaded as the type the member declares: a saved name can lead to another type of
/// that name (Vector3, which has no namespace, found System.Numerics.Vector3), and a collection of
/// the wrong type throws on assignment, which drops every script of the entity.
struct mono_element_list
{
    dotnet::type type;
    std::vector<dotnet::object> container;
};

template<typename Archive>
inline void SAVE_FUNCTION_NAME(Archive& ar, const mono_element_list& obj)
{
    save_mono_elements(ar, obj.type, obj.container);
}

template<typename Archive>
inline void LOAD_FUNCTION_NAME(Archive& ar, mono_element_list& obj)
{
    load_mono_elements(ar, obj.type, obj.container);
}

//-----------------------------------------------------------------------------
/// <summary>
/// Loads an array or a List member as a new collection of the element type the member declares.
/// </summary>
//-----------------------------------------------------------------------------
template<typename Archive, typename Invoker>
void try_load_mono_collection(Archive& ar, dotnet::object& obj, const Invoker& invoker)
{
    const auto collection_type = invoker.get_type();
    mono_element_list elements{collection_type.get_element_type(), {}};
    if(!elements.type.valid() || !try_load(ar, ser20::make_nvp(invoker.get_name(), elements)))
    {
        return;
    }
    auto element_pins = dotnet::pin_vector_elements(elements.container);
    if(collection_type.is_array())
    {
        dotnet::array<dotnet::object> array(elements.container, elements.type);
        invoker.set_value(obj, array);
    }
    else
    {
        dotnet::list<dotnet::object> list(elements.container, elements.type);
        invoker.set_value(obj, list);
    }
}

/// The entries of a C# Dictionary as two lists of the same length, keys and values, so a dictionary
/// serializes in the form lists have here already.
struct mono_dictionary_entries
{
    mono_element_list keys;
    mono_element_list values;
};

template<typename Archive>
inline void SAVE_FUNCTION_NAME(Archive& ar, const mono_dictionary_entries& obj)
{
    try_save(ar, ser20::make_nvp("keys", obj.keys));
    try_save(ar, ser20::make_nvp("values", obj.values));
}

template<typename Archive>
inline void LOAD_FUNCTION_NAME(Archive& ar, mono_dictionary_entries& obj)
{
    try_load(ar, ser20::make_nvp("keys", obj.keys));
    try_load(ar, ser20::make_nvp("values", obj.values));
}

template<typename Archive, typename Invoker>
void try_save_mono_dictionary(Archive& ar, const dotnet::object& obj, const Invoker& invoker)
{
    auto dictionary_obj = invoker.get_value(obj);
    if(!dictionary_obj.valid())
    {
        return;
    }
    dotnet::dictionary dictionary(dictionary_obj);
    auto pinned_dictionary = dotnet::make_object_pinned(dictionary);
    mono_dictionary_entries entries{{dictionary.get_key_type(), {}}, {dictionary.get_value_type(), {}}};
    for(const auto& entry : dictionary.to_vector())
    {
        entries.keys.container.push_back(entry.first);
        entries.values.container.push_back(entry.second);
    }
    auto key_pins = dotnet::pin_vector_elements(entries.keys.container);
    auto value_pins = dotnet::pin_vector_elements(entries.values.container);
    serialization::path_segment_guard guard(invoker.get_name());
    try_save(ar, ser20::make_nvp(invoker.get_name(), entries));
}

//-----------------------------------------------------------------------------
/// <summary>
/// Loads the entries into the dictionary the member holds, so a get-only member works too, or into
/// a new one when it holds none.
/// </summary>
//-----------------------------------------------------------------------------
template<typename Archive, typename Invoker>
void try_load_mono_dictionary(Archive& ar, dotnet::object& obj, const Invoker& invoker)
{
    // TKey and TValue.
    const auto arguments = invoker.get_type().get_generic_arguments();
    if(arguments.size() != 2)
    {
        return;
    }
    mono_dictionary_entries entries{{arguments[0], {}}, {arguments[1], {}}};
    if(!try_load(ar, ser20::make_nvp(invoker.get_name(), entries)))
    {
        return;
    }
    auto key_pins = dotnet::pin_vector_elements(entries.keys.container);
    auto value_pins = dotnet::pin_vector_elements(entries.values.container);
    const auto count = std::min(entries.keys.container.size(), entries.values.container.size());
    std::vector<dotnet::dictionary::entry> items;
    items.reserve(count);
    for(std::size_t i = 0; i < count; ++i)
    {
        items.emplace_back(entries.keys.container[i], entries.values.container[i]);
    }

    auto dictionary_obj = invoker.get_value(obj);
    const bool is_new = !dictionary_obj.valid();
    auto dictionary = is_new ? dotnet::dictionary::create(invoker.get_type()) : dotnet::dictionary(dictionary_obj);
    auto pinned_dictionary = dotnet::make_object_pinned(dictionary);
    dictionary.assign(items);
    if(is_new)
    {
        invoker.set_value(obj, dictionary);
    }
}

//-----------------------------------------------------------------------------
/// <summary>
/// Runs the load of one public member of a script object. try_load already skips a member that
/// is missing or does not read; this catches what fails around it, such as assigning the loaded
/// value to the member, so that member keeps its current value and is reported while the rest
/// of the object and the other scripts of the entity still load.
/// </summary>
//-----------------------------------------------------------------------------
template<typename Load>
void load_mono_member(const dotnet::type& owner, const std::string& member, Load&& load)
{
    try
    {
        load();
    }
    catch(const std::exception& e)
    {
        APPLOG_WARNING("Script member {}.{} did not load and keeps its current value: {}",
                       owner.get_fullname(),
                       member,
                       e.what());
    }
}

SAVE(dotnet::object)
{
    using namespace unravel;
    auto pinned_obj = dotnet::make_object_pinned(obj);
    // First, try object-level serializer (like inspector's get_object_inspector)
    using mono_object_serializer = std::function<bool(ser20::detail::OutputArchiveBase&, const dotnet::object&)>;
    
    auto get_object_serializer = [](const std::string& type_name) -> const mono_object_serializer&
    {
        // clang-format off
        static std::map<std::string, mono_object_serializer> reg = {
            {"SByte",   &mono_saver<Archive, int8_t>::try_save_mono_object},
            {"Byte",    &mono_saver<Archive, uint8_t>::try_save_mono_object},
            {"Int16",   &mono_saver<Archive, int16_t>::try_save_mono_object},
            {"UInt16",  &mono_saver<Archive, uint16_t>::try_save_mono_object},
            {"Int32",   &mono_saver<Archive, int32_t>::try_save_mono_object},
            {"UInt32",  &mono_saver<Archive, uint32_t>::try_save_mono_object},
            {"Int64",   &mono_saver<Archive, int64_t>::try_save_mono_object},
            {"UInt64",  &mono_saver<Archive, uint64_t>::try_save_mono_object},
            {"Boolean", &mono_saver<Archive, bool>::try_save_mono_object},
            {"Single",  &mono_saver<Archive, float>::try_save_mono_object},
            {"Double",  &mono_saver<Archive, double>::try_save_mono_object},
            {"Char",    &mono_saver<Archive, char16_t>::try_save_mono_object},
            {"String",  &mono_saver<Archive, std::string>::try_save_mono_object},
            {"Entity",  &mono_saver<Archive, entt::entity>::try_save_mono_object},
            {"Vector2", &mono_saver<Archive, math::vec2>::try_save_mono_object},
            {"Vector3", &mono_saver<Archive, math::vec3>::try_save_mono_object},
            {"Vector4", &mono_saver<Archive, math::vec4>::try_save_mono_object},
            {"Quaternion", &mono_saver<Archive, math::quat>::try_save_mono_object},
            {"Color", &mono_saver<Archive, math::color>::try_save_mono_object},
            {"LayerMask", &mono_saver<Archive, layer_mask>::try_save_mono_object},
            {"Texture",         &mono_saver<Archive, asset_handle<gfx::texture>>::try_save_mono_object},
            {"Material",        &mono_saver<Archive, asset_handle<material>>::try_save_mono_object},
            {"Mesh",            &mono_saver<Archive, asset_handle<mesh>>::try_save_mono_object},
            {"AnimationClip",   &mono_saver<Archive, asset_handle<animation_clip>>::try_save_mono_object},
            {"Prefab",          &mono_saver<Archive, asset_handle<prefab>>::try_save_mono_object},
            {"Scene",           &mono_saver<Archive, asset_handle<scene_prefab>>::try_save_mono_object},
            {"PhysicsMaterial", &mono_saver<Archive, asset_handle<physics_material>>::try_save_mono_object},
            {"AudioClip",       &mono_saver<Archive, asset_handle<audio_clip>>::try_save_mono_object},
            {"Font",            &mono_saver<Archive, asset_handle<font>>::try_save_mono_object},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_object_serializer empty;
        return empty;
    };

    const auto& type = obj.get_type();
    if(!type.valid())
    {
        return;
    }
    auto object_serializer = get_object_serializer(type.get_name());
    if(object_serializer)
    {
        object_serializer(ar, obj);
        return;
    }

    auto& ctx = engine::context();
    auto& script_sys = ctx.get_cached<script_system>();
    bool should_serialize = (type.is_serializable() || type.is_derived_from(script_sys.get_scriptable_component_base_type()));

    if(!should_serialize)
    {
        return;
    }

    
    if(!obj.valid())
    {
        return;
    }

    // If no object serializer found, iterate through fields/properties (like inspector)
    using mono_field_serializer =
        std::function<bool(ser20::detail::OutputArchiveBase&, const dotnet::object&, const dotnet::field&)>;

    auto get_field_serilizer = [](const std::string& type_name) -> const mono_field_serializer&
    {
        // clang-format off
        static std::map<std::string, mono_field_serializer> reg = {
            {"SByte",   &mono_saver<Archive, int8_t>::try_save_mono_field},
            {"Byte",    &mono_saver<Archive, uint8_t>::try_save_mono_field},
            {"Int16",   &mono_saver<Archive, int16_t>::try_save_mono_field},
            {"UInt16",  &mono_saver<Archive, uint16_t>::try_save_mono_field},
            {"Int32",   &mono_saver<Archive, int32_t>::try_save_mono_field},
            {"UInt32",  &mono_saver<Archive, uint32_t>::try_save_mono_field},
            {"Int64",   &mono_saver<Archive, int64_t>::try_save_mono_field},
            {"UInt64",  &mono_saver<Archive, uint64_t>::try_save_mono_field},
            {"Boolean", &mono_saver<Archive, bool>::try_save_mono_field},
            {"Single",  &mono_saver<Archive, float>::try_save_mono_field},
            {"Double",  &mono_saver<Archive, double>::try_save_mono_field},
            {"Char",    &mono_saver<Archive, char16_t>::try_save_mono_field},
            {"String",  &mono_saver<Archive, std::string>::try_save_mono_field},
            {"Entity",  &mono_saver<Archive, entt::entity>::try_save_mono_field},
            {"Vector2", &mono_saver<Archive, math::vec2>::try_save_mono_field},
            {"Vector3", &mono_saver<Archive, math::vec3>::try_save_mono_field},
            {"Vector4", &mono_saver<Archive, math::vec4>::try_save_mono_field},
            {"Quaternion", &mono_saver<Archive, math::quat>::try_save_mono_field},
            {"Color", &mono_saver<Archive, math::color>::try_save_mono_field},
            {"LayerMask", &mono_saver<Archive, layer_mask>::try_save_mono_field},
            {"Texture",         &mono_saver<Archive, asset_handle<gfx::texture>>::try_save_mono_field},
            {"Material",        &mono_saver<Archive, asset_handle<material>>::try_save_mono_field},
            {"Mesh",            &mono_saver<Archive, asset_handle<mesh>>::try_save_mono_field},
            {"AnimationClip",   &mono_saver<Archive, asset_handle<animation_clip>>::try_save_mono_field},
            {"Prefab",          &mono_saver<Archive, asset_handle<prefab>>::try_save_mono_field},
            {"Scene",           &mono_saver<Archive, asset_handle<scene_prefab>>::try_save_mono_field},
            {"PhysicsMaterial", &mono_saver<Archive, asset_handle<physics_material>>::try_save_mono_field},
            {"AudioClip",       &mono_saver<Archive, asset_handle<audio_clip>>::try_save_mono_field},
            {"Font",            &mono_saver<Archive, asset_handle<font>>::try_save_mono_field},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_field_serializer empty;
        return empty;
    };

    using mono_property_serializer =
        std::function<bool(ser20::detail::OutputArchiveBase&, const dotnet::object&, const dotnet::property&)>;

    auto get_property_serilizer = [](const std::string& type_name) -> const mono_property_serializer&
    {
        // clang-format off
        static std::map<std::string, mono_property_serializer> reg = {
            {"SByte",   &mono_saver<Archive, int8_t>::try_save_mono_property},
            {"Byte",    &mono_saver<Archive, uint8_t>::try_save_mono_property},
            {"Int16",   &mono_saver<Archive, int16_t>::try_save_mono_property},
            {"UInt16",  &mono_saver<Archive, uint16_t>::try_save_mono_property},
            {"Int32",   &mono_saver<Archive, int32_t>::try_save_mono_property},
            {"UInt32",  &mono_saver<Archive, uint32_t>::try_save_mono_property},
            {"Int64",   &mono_saver<Archive, int64_t>::try_save_mono_property},
            {"UInt64",  &mono_saver<Archive, uint64_t>::try_save_mono_property},
            {"Boolean", &mono_saver<Archive, bool>::try_save_mono_property},
            {"Single",  &mono_saver<Archive, float>::try_save_mono_property},
            {"Double",  &mono_saver<Archive, double>::try_save_mono_property},
            {"Char",    &mono_saver<Archive, char16_t>::try_save_mono_property},
            {"String",  &mono_saver<Archive, std::string>::try_save_mono_property},
            {"Entity",  &mono_saver<Archive, entt::entity>::try_save_mono_property},
            {"Vector2", &mono_saver<Archive, math::vec2>::try_save_mono_property},
            {"Vector3", &mono_saver<Archive, math::vec3>::try_save_mono_property},
            {"Vector4", &mono_saver<Archive, math::vec4>::try_save_mono_property},
            {"Quaternion", &mono_saver<Archive, math::quat>::try_save_mono_property},
            {"Color", &mono_saver<Archive, math::color>::try_save_mono_property},
            {"LayerMask", &mono_saver<Archive, layer_mask>::try_save_mono_property},
            {"Texture",         &mono_saver<Archive, asset_handle<gfx::texture>>::try_save_mono_property},
            {"Material",        &mono_saver<Archive, asset_handle<material>>::try_save_mono_property},
            {"Mesh",            &mono_saver<Archive, asset_handle<mesh>>::try_save_mono_property},
            {"AnimationClip",   &mono_saver<Archive, asset_handle<animation_clip>>::try_save_mono_property},
            {"Prefab",          &mono_saver<Archive, asset_handle<prefab>>::try_save_mono_property},
            {"Scene",           &mono_saver<Archive, asset_handle<scene_prefab>>::try_save_mono_property},
            {"PhysicsMaterial", &mono_saver<Archive, asset_handle<physics_material>>::try_save_mono_property},
            {"AudioClip",       &mono_saver<Archive, asset_handle<audio_clip>>::try_save_mono_property},
            {"Font",            &mono_saver<Archive, asset_handle<font>>::try_save_mono_property},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_property_serializer empty;
        return empty;
    };

    bool include_base = true;
    auto fields = type.get_fields(include_base);
    auto properties = type.get_properties(include_base);
    
    for(auto& field : fields)
    {
        if(field.get_visibility() == dotnet::visibility::vis_public)
        {
            if(field.is_static() || field.has_attribute("HideAttribute"))
            {
                continue;
            }

            const auto& field_type = field.get_type();
            auto field_serilizer = get_field_serilizer(field_type.get_name());
            if(field_serilizer)
            {
                field_serilizer(ar, obj, field);
            }
            else if(field_type.is_enum())
            {
                auto enum_type = field_type.get_enum_base_type();
                auto enum_serilizer = get_field_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj, field);
                }
            }
            else if(field_type.is_array() || field_type.is_list())
            {
                // Handle arrays and lists - convert to vector and serialize
                auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
                auto collection_obj = invoker.get_value(obj);
                if(collection_obj.valid())
                {
                    serialization::path_segment_guard guard(field.get_name());
                    if(field_type.is_array())
                    {
                        dotnet::array<dotnet::object> array(collection_obj);
                        auto pinned_array = dotnet::make_array_pinned(array);
                        auto vec = array.to_vector_wrapper<std::vector<dotnet::object>>();
                        auto element_pins = dotnet::pin_vector_elements(vec.container);
                        try_save(ar, ser20::make_nvp(field.get_name(), vec));
                    }
                    else if(field_type.is_list())
                    {
                        dotnet::list<dotnet::object> list(collection_obj);
                        auto pinned_list = dotnet::make_list_pinned(list);
                        auto vec = list.to_vector_wrapper<std::vector<dotnet::object>>();
                        auto element_pins = dotnet::pin_vector_elements(vec.container);
                        try_save(ar, ser20::make_nvp(field.get_name(), vec));
                    }
                }
            }
            else if(field_type.is_dictionary())
            {
                try_save_mono_dictionary(ar, obj, dotnet::make_field_invoker<dotnet::object>(field));
            }
            else if(field_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    auto pinned_nested = dotnet::make_object_pinned(nested_obj);
                    serialization::path_segment_guard guard(field.get_name());
                    try_save(ar, ser20::make_nvp(field.get_name(), nested_obj));
                }
            }
        }
    }

    for(auto& prop : properties)
    {
        if(prop.get_visibility() == dotnet::visibility::vis_public)
        {
            if(prop.is_static() || prop.has_attribute("HideAttribute"))
            {
                continue;
            }
            const auto& prop_type = prop.get_type();

            auto prop_serilizer = get_property_serilizer(prop_type.get_name());
            if(prop_serilizer)
            {
                prop_serilizer(ar, obj, prop);
            }
            else if(prop_type.is_enum())
            {
                auto enum_type = prop_type.get_enum_base_type();
                auto enum_serilizer = get_property_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj, prop);
                }
            }
            else if(prop_type.is_array() || prop_type.is_list())
            {
                // Handle arrays and lists - convert to vector and serialize
                auto invoker = dotnet::make_property_invoker<dotnet::object>(prop);
                auto collection_obj = invoker.get_value(obj);
                if(collection_obj.valid())
                {
                    serialization::path_segment_guard guard(prop.get_name());
                    if(prop_type.is_array())
                    {
                        dotnet::array<dotnet::object> array(collection_obj);
                        auto pinned_array = dotnet::make_array_pinned(array);
                        auto vec = array.to_vector_wrapper<std::vector<dotnet::object>>();
                        auto element_pins = dotnet::pin_vector_elements(vec.container);
                        try_save(ar, ser20::make_nvp(prop.get_name(), vec));
                    }
                    else if(prop_type.is_list())
                    {
                        dotnet::list<dotnet::object> list(collection_obj);
                        auto pinned_list = dotnet::make_list_pinned(list);
                        auto vec = list.to_vector_wrapper<std::vector<dotnet::object>>();
                        auto element_pins = dotnet::pin_vector_elements(vec.container);
                        try_save(ar, ser20::make_nvp(prop.get_name(), vec));
                    }
                }
            }
            else if(prop_type.is_dictionary())
            {
                try_save_mono_dictionary(ar, obj, dotnet::make_property_invoker<dotnet::object>(prop));
            }
            else if(prop_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = dotnet::make_property_invoker<dotnet::object>(prop);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    auto pinned_nested = dotnet::make_object_pinned(nested_obj);
                    serialization::path_segment_guard guard(prop.get_name());
                    try_save(ar, ser20::make_nvp(prop.get_name(), nested_obj));
                }
            }
        }
    }
    
}

LOAD(dotnet::object)
{
    using namespace unravel;
    
    auto pinned_obj = dotnet::make_object_pinned(obj);
    // First, try object-level serializer (like inspector's get_object_inspector)
    using mono_object_serializer = std::function<bool(ser20::detail::InputArchiveBase&, dotnet::object&)>;
    
    auto get_object_serializer = [](const std::string& type_name) -> const mono_object_serializer&
    {
        // clang-format off
        static const std::map<std::string, mono_object_serializer> reg = {
            {"SByte",   &mono_loader<Archive, int8_t>::try_load_mono_object},
            {"Byte",    &mono_loader<Archive, uint8_t>::try_load_mono_object},
            {"Int16",   &mono_loader<Archive, int16_t>::try_load_mono_object},
            {"UInt16",  &mono_loader<Archive, uint16_t>::try_load_mono_object},
            {"Int32",   &mono_loader<Archive, int32_t>::try_load_mono_object},
            {"UInt32",  &mono_loader<Archive, uint32_t>::try_load_mono_object},
            {"Int64",   &mono_loader<Archive, int64_t>::try_load_mono_object},
            {"UInt64",  &mono_loader<Archive, uint64_t>::try_load_mono_object},
            {"Boolean", &mono_loader<Archive, bool>::try_load_mono_object},
            {"Single",  &mono_loader<Archive, float>::try_load_mono_object},
            {"Double",  &mono_loader<Archive, double>::try_load_mono_object},
            {"Char",    &mono_loader<Archive, char16_t>::try_load_mono_object},
            {"String",  &mono_loader<Archive, std::string>::try_load_mono_object},
            {"Entity",  &mono_loader<Archive, entt::entity>::try_load_mono_object},
            {"Vector2", &mono_loader<Archive, math::vec2>::try_load_mono_object},
            {"Vector3", &mono_loader<Archive, math::vec3>::try_load_mono_object},
            {"Vector4", &mono_loader<Archive, math::vec4>::try_load_mono_object},
            {"Quaternion", &mono_loader<Archive, math::quat>::try_load_mono_object},
            {"Color", &mono_loader<Archive, math::color>::try_load_mono_object},
            {"LayerMask", &mono_loader<Archive, layer_mask>::try_load_mono_object},
            {"Texture",         &mono_loader<Archive, asset_handle<gfx::texture>>::try_load_mono_object},
            {"Material",        &mono_loader<Archive, asset_handle<material>>::try_load_mono_object},
            {"Mesh",            &mono_loader<Archive, asset_handle<mesh>>::try_load_mono_object},
            {"AnimationClip",   &mono_loader<Archive, asset_handle<animation_clip>>::try_load_mono_object},
            {"Prefab",          &mono_loader<Archive, asset_handle<prefab>>::try_load_mono_object},
            {"Scene",           &mono_loader<Archive, asset_handle<scene_prefab>>::try_load_mono_object},
            {"PhysicsMaterial", &mono_loader<Archive, asset_handle<physics_material>>::try_load_mono_object},
            {"AudioClip",       &mono_loader<Archive, asset_handle<audio_clip>>::try_load_mono_object},
            {"Font",            &mono_loader<Archive, asset_handle<font>>::try_load_mono_object},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_object_serializer empty;
        return empty;
    };

    const auto& type = obj.get_type();


    if(!type.valid())
    {
        return;
    }
    auto object_serializer = get_object_serializer(type.get_name());
    if(object_serializer)
    {
        object_serializer(ar, obj);
        return;
    }

    
    auto& ctx = engine::context();
    auto& script_sys = ctx.get_cached<script_system>();
    bool should_serialize = (type.is_serializable() || type.is_derived_from(script_sys.get_scriptable_component_base_type()));

    if(!should_serialize)
    {
        return;
    }
    
    // If no object serializer found, iterate through fields/properties (like inspector)
    using mono_field_serializer =
        std::function<bool(ser20::detail::InputArchiveBase&, dotnet::object&, dotnet::field&)>;

    auto get_field_serilizer = [](const std::string& type_name) -> const mono_field_serializer&
    {
        // clang-format off
        static const std::map<std::string, mono_field_serializer> reg = {
            {"SByte",   &mono_loader<Archive, int8_t>::try_load_mono_field},
            {"Byte",    &mono_loader<Archive, uint8_t>::try_load_mono_field},
            {"Int16",   &mono_loader<Archive, int16_t>::try_load_mono_field},
            {"UInt16",  &mono_loader<Archive, uint16_t>::try_load_mono_field},
            {"Int32",   &mono_loader<Archive, int32_t>::try_load_mono_field},
            {"UInt32",  &mono_loader<Archive, uint32_t>::try_load_mono_field},
            {"Int64",   &mono_loader<Archive, int64_t>::try_load_mono_field},
            {"UInt64",  &mono_loader<Archive, uint64_t>::try_load_mono_field},
            {"Boolean", &mono_loader<Archive, bool>::try_load_mono_field},
            {"Single",  &mono_loader<Archive, float>::try_load_mono_field},
            {"Double",  &mono_loader<Archive, double>::try_load_mono_field},
            {"Char",    &mono_loader<Archive, char16_t>::try_load_mono_field},
            {"String",  &mono_loader<Archive, std::string>::try_load_mono_field},
            {"Entity",  &mono_loader<Archive, entt::entity>::try_load_mono_field},
            {"Vector2", &mono_loader<Archive, math::vec2>::try_load_mono_field},
            {"Vector3", &mono_loader<Archive, math::vec3>::try_load_mono_field},
            {"Vector4", &mono_loader<Archive, math::vec4>::try_load_mono_field},
            {"Quaternion", &mono_loader<Archive, math::quat>::try_load_mono_field},
            {"Color", &mono_loader<Archive, math::color>::try_load_mono_field},
            {"LayerMask", &mono_loader<Archive, layer_mask>::try_load_mono_field},
            {"Texture",         &mono_loader<Archive, asset_handle<gfx::texture>>::try_load_mono_field},
            {"Material",        &mono_loader<Archive, asset_handle<material>>::try_load_mono_field},
            {"Mesh",            &mono_loader<Archive, asset_handle<mesh>>::try_load_mono_field},
            {"AnimationClip",   &mono_loader<Archive, asset_handle<animation_clip>>::try_load_mono_field},
            {"Prefab",          &mono_loader<Archive, asset_handle<prefab>>::try_load_mono_field},
            {"Scene",           &mono_loader<Archive, asset_handle<scene_prefab>>::try_load_mono_field},
            {"PhysicsMaterial", &mono_loader<Archive, asset_handle<physics_material>>::try_load_mono_field},
            {"AudioClip",       &mono_loader<Archive, asset_handle<audio_clip>>::try_load_mono_field},
            {"Font",            &mono_loader<Archive, asset_handle<font>>::try_load_mono_field},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_field_serializer empty;
        return empty;
    };

    using mono_property_serializer =
        std::function<bool(ser20::detail::InputArchiveBase&, dotnet::object&, dotnet::property&)>;

    auto get_property_serilizer = [](const std::string& type_name) -> const mono_property_serializer&
    {
        // clang-format off
        static const std::map<std::string, mono_property_serializer> reg = {
            {"SByte",   &mono_loader<Archive, int8_t>::try_load_mono_property},
            {"Byte",    &mono_loader<Archive, uint8_t>::try_load_mono_property},
            {"Int16",   &mono_loader<Archive, int16_t>::try_load_mono_property},
            {"UInt16",  &mono_loader<Archive, uint16_t>::try_load_mono_property},
            {"Int32",   &mono_loader<Archive, int32_t>::try_load_mono_property},
            {"UInt32",  &mono_loader<Archive, uint32_t>::try_load_mono_property},
            {"Int64",   &mono_loader<Archive, int64_t>::try_load_mono_property},
            {"UInt64",  &mono_loader<Archive, uint64_t>::try_load_mono_property},
            {"Boolean", &mono_loader<Archive, bool>::try_load_mono_property},
            {"Single",  &mono_loader<Archive, float>::try_load_mono_property},
            {"Double",  &mono_loader<Archive, double>::try_load_mono_property},
            {"Char",    &mono_loader<Archive, char16_t>::try_load_mono_property},
            {"String",  &mono_loader<Archive, std::string>::try_load_mono_property},
            {"Entity",  &mono_loader<Archive, entt::entity>::try_load_mono_property},
            {"Vector2", &mono_loader<Archive, math::vec2>::try_load_mono_property},
            {"Vector3", &mono_loader<Archive, math::vec3>::try_load_mono_property},
            {"Vector4", &mono_loader<Archive, math::vec4>::try_load_mono_property},
            {"Quaternion", &mono_loader<Archive, math::quat>::try_load_mono_property},
            {"Color", &mono_loader<Archive, math::color>::try_load_mono_property},
            {"LayerMask", &mono_loader<Archive, layer_mask>::try_load_mono_property},
            {"Texture",         &mono_loader<Archive, asset_handle<gfx::texture>>::try_load_mono_property},
            {"Material",        &mono_loader<Archive, asset_handle<material>>::try_load_mono_property},
            {"Mesh",            &mono_loader<Archive, asset_handle<mesh>>::try_load_mono_property},
            {"AnimationClip",   &mono_loader<Archive, asset_handle<animation_clip>>::try_load_mono_property},
            {"Prefab",          &mono_loader<Archive, asset_handle<prefab>>::try_load_mono_property},
            {"Scene",           &mono_loader<Archive, asset_handle<scene_prefab>>::try_load_mono_property},
            {"PhysicsMaterial", &mono_loader<Archive, asset_handle<physics_material>>::try_load_mono_property},
            {"AudioClip",       &mono_loader<Archive, asset_handle<audio_clip>>::try_load_mono_property},
            {"Font",            &mono_loader<Archive, asset_handle<font>>::try_load_mono_property},
        };
        // clang-format on

        auto it = reg.find(type_name);
        if(it != reg.end())
        {
            return it->second;
        }
        static const mono_property_serializer empty;
        return empty;
    };

    bool include_base = true;
    auto fields = type.get_fields(include_base);
    auto properties = type.get_properties(include_base);
    
    for(auto& field : fields)
    {
        if(field.get_visibility() != dotnet::visibility::vis_public)
        {
            continue;
        }
        load_mono_member(type, field.get_name(), [&]()
        {
            const auto& field_type = field.get_type();
            auto field_serilizer = get_field_serilizer(field_type.get_name());
            if(field_serilizer)
            {
                field_serilizer(ar, obj, field);
            }
            else if(field_type.is_enum())
            {
                auto enum_type = field_type.get_enum_base_type();
                auto enum_serilizer = get_field_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj, field);
                }
            }
            else if(field_type.is_array() || field_type.is_list())
            {
                try_load_mono_collection(ar, obj, dotnet::make_field_invoker<dotnet::object>(field));
            }
            else if(field_type.is_dictionary())
            {
                try_load_mono_dictionary(ar, obj, dotnet::make_field_invoker<dotnet::object>(field));
            }
            else if(field_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = dotnet::make_field_invoker<dotnet::object>(field);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    auto pinned_nested = dotnet::make_object_pinned(nested_obj);
                    try_load(ar, ser20::make_nvp(field.get_name(), nested_obj));
                    invoker.set_value(obj, nested_obj);
                }
            }
        });
    }
    
    for(auto& prop : properties)
    {
        if(prop.get_visibility() != dotnet::visibility::vis_public)
        {
            continue;
        }
        load_mono_member(type, prop.get_name(), [&]()
        {
            const auto& prop_type = prop.get_type();
            auto prop_serilizer = get_property_serilizer(prop_type.get_name());
            if(prop_serilizer)
            {
                prop_serilizer(ar, obj, prop);
            }
            else if(prop_type.is_enum())
            {
                auto enum_type = prop_type.get_enum_base_type();
                auto enum_serilizer = get_property_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj, prop);
                }
            }
            else if(prop_type.is_array() || prop_type.is_list())
            {
                try_load_mono_collection(ar, obj, dotnet::make_property_invoker<dotnet::object>(prop));
            }
            else if(prop_type.is_dictionary())
            {
                try_load_mono_dictionary(ar, obj, dotnet::make_property_invoker<dotnet::object>(prop));
            }
            else if(prop_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = dotnet::make_property_invoker<dotnet::object>(prop);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    auto pinned_nested = dotnet::make_object_pinned(nested_obj);
                    try_load(ar, ser20::make_nvp(prop.get_name(), nested_obj));
                    invoker.set_value(obj, nested_obj);
                }
            }
        });
    }
    
}
LOAD_INSTANTIATE(dotnet::object, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(dotnet::object, ser20::iarchive_binary_t);
}





namespace unravel
{
SAVE(script_component::script_object)
{
    auto object = obj.pinned->get_object();
    const auto& type = object.get_type();

    try_save_mono_type(ar, "type", type);
    try_save(ar, ser20::make_nvp(SCRIPT_SLOT_KEY, obj.slot));

    SAVE_FUNCTION_NAME(ar, object);
}
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_binary_t);

LOAD(script_component::script_object)
{
    // Which script this is decides what the replay filter is asked, so it is read past it.
    std::string type_name;
    try_serialize_direct(ar, ser20::make_nvp("type", type_name));
    hpp::uuid slot;
    try_serialize_direct(ar, ser20::make_nvp(SCRIPT_SLOT_KEY, slot));

    const auto script_type = get_type_by_fullname(type_name);
    if(!script_type.valid())
    {
        return;
    }

    auto* list_ctx = script_list_ctx;
    if(slot.is_nil())
    {
        slot = derive_script_slot(type_name, list_ctx != nullptr ? list_ctx->type_counts[type_name] : 0);
    }
    if(list_ctx != nullptr)
    {
        ++list_ctx->type_counts[type_name];
        list_ctx->document_slots.insert(slot);
    }
    obj.slot = slot;

    // Held back as a whole: removed here, or not the replayed document's to state.
    serialization::path_segment_guard slot_guard(script_slot_segment(slot));
    if(!is_current_path_replayed())
    {
        return;
    }

    serialization::path_segment_guard type_guard(script_type.get_fullname());

    auto* existing_obj = list_ctx != nullptr ? list_ctx->find_live(slot) : nullptr;
    if(existing_obj)
    {
        obj = *existing_obj;
    }
    else
    {
        obj = script_component::script_object(script_type.new_instance());
        obj.slot = slot;
    }

    auto pinned_object = obj.pinned->get_object();

    LOAD_FUNCTION_NAME(ar, pinned_object);

}
LOAD_INSTANTIATE(script_component::script_object, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(script_component::script_object, ser20::iarchive_binary_t);

SAVE(script_component)
{
    const auto& comps = obj.get_script_components();
    try_save(ar, ser20::make_nvp("script_components", comps));
}
SAVE_INSTANTIATE(script_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(script_component, ser20::oarchive_binary_t);

namespace
{
/// A script list as the loader reads it: saved like any vector, read script by script - no
/// position in the path and no position-based filtering; each script names itself by its slot.
struct script_list
{
    script_component::script_components_t scripts;
};

template<typename Archive>
void LOAD_FUNCTION_NAME(Archive& ar, script_list& list)
{
    ser20::size_type size{};
    ar(ser20::make_size_tag(size));
    list.scripts.resize(static_cast<std::size_t>(size));
    for(auto& script : list.scripts)
    {
        ar(script);
    }

    // A document that states a live script as a whole and does not list it removed it.
    auto* list_ctx = script_list_ctx;
    const auto* path_ctx = serialization::get_path_context();
    if(list_ctx == nullptr || path_ctx == nullptr)
    {
        return;
    }
    for(const auto& live : list_ctx->live_scripts)
    {
        if(list_ctx->document_slots.contains(live.slot))
        {
            continue;
        }
        serialization::path_segment_guard slot_guard(script_slot_segment(live.slot));
        if(path_ctx->is_property_stated_whole(path_ctx->get_current_path()))
        {
            list_ctx->removed_slots.push_back(live.slot);
        }
    }
}
} // namespace

LOAD(script_component)
{
    const bool is_update = get_load_context().is_updating_prefab();

    script_list_load_context list_ctx;
    if(is_update)
    {
        list_ctx.live_scripts = obj.get_script_components();
    }
    auto* previous_ctx = std::exchange(script_list_ctx, &list_ctx);
    auto restore_ctx = hpp::finally(
        [&]()
        {
            script_list_ctx = previous_ctx;
        });

    script_list list;
    if(!try_load(ar, ser20::make_nvp("script_components", list)))
    {
        return;
    }

    if(!is_update)
    {
        obj.add_script_components(list.scripts);
        return;
    }

    // Live scripts were loaded in place; the document's scripts that matched none are new to
    // this instance.
    for(const auto& script : list.scripts)
    {
        if(script.pinned && list_ctx.find_live(script.slot) == nullptr)
        {
            obj.add_script_component(script);
        }
    }

    for(const auto& slot : list_ctx.removed_slots)
    {
        if(auto removed = obj.get_script_component_by_slot(slot); removed.pinned)
        {
            obj.remove_script_component(removed.pinned->get_object());
        }
    }
    if(!list_ctx.removed_slots.empty())
    {
        obj.process_pending_deletions();
    }
}
LOAD_INSTANTIATE(script_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(script_component, ser20::iarchive_binary_t);




auto script_slot_segment(const hpp::uuid& slot) -> std::string
{
    return "[" + hpp::to_string(slot) + "]";
}

auto parse_script_slot(std::string_view property_path) -> std::optional<hpp::uuid>
{
    if(!property_path.starts_with(SCRIPT_LIST_SEGMENT))
    {
        return std::nullopt;
    }
    const auto close = property_path.find(']', SCRIPT_LIST_SEGMENT.size());
    if(close == std::string_view::npos)
    {
        return std::nullopt;
    }
    return hpp::uuid::from_string(property_path.substr(SCRIPT_LIST_SEGMENT.size(), close - SCRIPT_LIST_SEGMENT.size()));
}

auto save_to_stream(std::ostream& stream, entt::const_handle e, const script_component::script_object& obj) -> bool
{
    bool pushed = push_save_context();
    auto& save_ctx = get_save_context();
    save_ctx.save_source = e;
    save_ctx.to_prefab = false;

    bool was_successful = false;
    try
    {
        ser20::oarchive_associative_t ar(stream);
        ar(ser20::make_nvp("script_object", obj));
        was_successful = true;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to save script component to stream: {}", e.what());
    }

    save_ctx.to_prefab = false;
    save_ctx.save_source = {};
    pop_save_context(pushed);
    return was_successful;
}
auto load_from_stream(std::istream& stream, entt::handle e, script_component::script_object& obj) -> bool
{
    bool pushed = push_load_context(*e.registry());

    bool was_successful = false;
    try
    {
        ser20::iarchive_associative_t ar(stream);
        ar(ser20::make_nvp("script_object", obj));
        was_successful = true;
    }
    catch(const std::exception& e)
    {
        APPLOG_ERROR("Failed to load script component from stream: {}", e.what());
    }
    pop_load_context(pushed);
    return was_successful;
}

} // namespace unravel

