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
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_property_invoker.h>
#include <monopp/mono_list.h>
#include <monopp/mono_array.h>

#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/core/math/quaternion.hpp>

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/font.hpp>
#include <engine/meta/animation/animation.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/layers/layer_mask.hpp>
#include <graphics/texture.h>
#include <hpp/finally.hpp>

namespace unravel
{

namespace
{
    struct script_component_loader_context
    {
        std::vector<script_component::script_object> script_objects;

        auto get_script_object(size_t hash) -> script_component::script_object*
        {
            for(auto& obj : script_objects)
            {
                auto mono_obj = obj.pinned->get_object();

                if(!mono_obj.valid())
                {
                    APPLOG_ERROR("Script object is invalid for domain version: {}", obj.pinned->get_domain_version());
                    continue;
                }
                const auto& type = mono_obj.get_type();
                if(!type.valid())
                {
                    APPLOG_ERROR("Script object type is invalid for domain version: {}", obj.pinned->get_domain_version());
                    continue;
                }
                if(type.get_hash() == hash)
                {
                    return &obj;
                }
            }
            return nullptr;
        }
    };

    thread_local script_component_loader_context* script_component_loader_ctx{};


}

REFLECT(script_component)
{
    entt::meta_factory<script_component>{}
        .type("script_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "script_component"},
            entt::attribute{"category", "SCRIPTING"},
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
                                      const mono::mono_object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto val = invoker.get_value(obj);
        return try_save(ar, ser20::make_nvp(invoker.get_name(), val));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const mono::mono_object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        // Extract value directly from mono_object using mono_converter
        T value = mono::mono_converter<T>::from_mono(obj.get_internal_ptr());
        return try_save(ar, ser20::make_nvp("value", value));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const mono::mono_object& obj,
                                    const mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<T>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const mono::mono_object& obj,
                                       const mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<T>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive>
struct mono_saver<Archive, entt::entity>
{
    template<typename Invoker>
    static auto try_save_mono_invoker(ser20::detail::OutputArchiveBase& arbase,
                                      const mono::mono_object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto val = invoker.get_value(obj);

        auto& ctx = engine::context();
        auto& ec = ctx.get_cached<ecs>();
        auto& scene = ec.get_scene();
        ser20::const_entity_handle_link e;
        e.handle = scene.create_handle(val);

        return try_save(ar, ser20::make_nvp(invoker.get_name(), e));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const mono::mono_object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        auto entity = mono::mono_converter<entt::entity>::from_mono(obj.get_internal_ptr());
        
        auto& ctx = engine::context();
        auto& ec = ctx.get_cached<ecs>();
        auto& scene = ec.get_scene();
        ser20::const_entity_handle_link e;
        e.handle = scene.create_handle(entity);
        
        return try_save(ar, ser20::make_nvp("value", e));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const mono::mono_object& obj,
                                    const mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<entt::entity>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const mono::mono_object& obj,
                                       const mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<entt::entity>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_saver<Archive, asset_handle<T>>
{
    template<typename Invoker>
    static auto try_save_mono_invoker(ser20::detail::OutputArchiveBase& arbase,
                                      const mono::mono_object& obj,
                                      const Invoker& invoker) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);

        auto val = invoker.get_value(obj);

        asset_handle<T> asset{};
        if(val)
        {
            const auto& invoker_type = invoker.get_type();
            auto guid_property = invoker_type.get_property("uid");
            auto mutable_uid_property = mono::make_property_invoker<hpp::uuid>(guid_property);
            auto uid = mutable_uid_property.get_value(val);

            auto& ctx = engine::context();
            auto& am = ctx.get_cached<asset_manager>();
            asset = am.get_asset<T>(uid);
        }

        return try_save(ar, ser20::make_nvp(invoker.get_name(), asset));
    }

    static auto try_save_mono_object(ser20::detail::OutputArchiveBase& arbase,
                                     const mono::mono_object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        
        asset_handle<T> asset{};
        if(obj.valid())
        {
            auto obj_type = obj.get_type();
            auto prop = obj_type.get_property("uid");
            if(prop.get_internal_ptr())
            {
                auto uid_prop = mono::make_property_invoker<hpp::uuid>(prop);
                auto uid = uid_prop.get_value(obj);
                
                auto& ctx = engine::context();
                auto& am = ctx.get_cached<asset_manager>();
                asset = am.get_asset<T>(uid);
            }
        }
        
        return try_save(ar, ser20::make_nvp("value", asset));
    }

    static auto try_save_mono_field(ser20::detail::OutputArchiveBase& arbase,
                                    const mono::mono_object& obj,
                                    const mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<mono::mono_object>(field);
        return try_save_mono_invoker(arbase, obj, invoker);
    }

    static auto try_save_mono_property(ser20::detail::OutputArchiveBase& arbase,
                                       const mono::mono_object& obj,
                                       const mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
        return try_save_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_loader
{
    template<typename U>
    static auto is_supported_type(const mono::mono_type& type) -> bool
    {
        return mono::is_compatible_type<U>(type);
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      mono::mono_object& obj,
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
                                     mono::mono_object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        T value{};
        if(try_load(ar, ser20::make_nvp("value", value)))
        {
            // Box the value back into the mono_object
            auto obj_type = obj.get_type();
            if(obj_type.is_valuetype())
            {
                auto mono_value = mono::mono_converter<T>::to_mono(value);
                obj.box_value(mono_value, obj_type);
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    mono::mono_object& obj,
                                    mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<T>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       mono::mono_object& obj,
                                       mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<T>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};


template<typename Archive>
struct mono_loader<Archive, std::string>
{
    template<typename U>
    static auto is_supported_type(const mono::mono_type& type) -> bool
    {
        return type.is_string();
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      mono::mono_object& obj,
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
                                     mono::mono_object& obj) -> bool
    {
        auto& ar = static_cast<Archive&>(arbase);
        auto obj_type = obj.get_type();
        if(is_supported_type<std::string>(obj_type))
        {
            std::string value{};
            if(try_load(ar, ser20::make_nvp("value", value)))
            {
                obj = mono::mono_object(mono::mono_converter<std::string>::to_mono(value));
                return true;      
            }
        }

        
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    mono::mono_object& obj,
                                    mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<std::string>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       mono::mono_object& obj,
                                       mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<std::string>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive>
struct mono_loader<Archive, entt::entity>
{
    template<typename U>
    static auto is_supported_type(const mono::mono_type& type) -> bool
    {
        const auto& expected_name = type.get_name();
        bool is_supported = std::is_same_v<entt::entity, U> && expected_name == "Entity";
        return is_supported;
    }

    template<typename Invoker>
    static auto try_load_mono_invoker(ser20::detail::InputArchiveBase& arbase,
                                      mono::mono_object& obj,
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
                                     mono::mono_object& obj) -> bool
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
                auto mono_entity = mono::mono_converter<entt::entity>::to_mono(entity);
                obj.box_value(mono_entity, obj_type);
                return true;
            }
        }
        return false;
    }

    static auto try_load_mono_field(ser20::detail::InputArchiveBase& arbase,
                                    mono::mono_object& obj,
                                    mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<entt::entity>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       mono::mono_object& obj,
                                       mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<entt::entity>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};

template<typename Archive, typename T>
struct mono_loader<Archive, asset_handle<T>>
{
    template<typename U>
    static auto is_supported_type(const mono::mono_type& type) -> bool
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
                                      mono::mono_object& obj,
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
                auto mutable_uid_property = mono::make_property_invoker<hpp::uuid>(guid_property);

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
                                     mono::mono_object& obj) -> bool
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
                    if(prop.get_internal_ptr())
                    {
                        auto uid_prop = mono::make_property_invoker<hpp::uuid>(prop);
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
                                    mono::mono_object& obj,
                                    mono::mono_field& field) -> bool
    {
        auto invoker = mono::make_field_invoker<mono::mono_object>(field);
        return try_load_mono_invoker(arbase, obj, invoker);
    }

    static auto try_load_mono_property(ser20::detail::InputArchiveBase& arbase,
                                       mono::mono_object& obj,
                                       mono::mono_property& prop) -> bool
    {
        auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
        return try_load_mono_invoker(arbase, obj, invoker);
    }
};
} // namespace unravel


auto get_type_by_fullname(const std::string& fullname) -> mono::mono_type
{
    auto& ctx = unravel::engine::context();
    auto& script_sys = ctx.get_cached<unravel::script_system>();
    return script_sys.get_type_by_fullname(fullname);
}

namespace ser20
{
template<typename Archive>
inline auto try_save_mono_type(Archive& ar, const char* name, const mono::mono_type& t) -> bool
{
    return try_save(ar, ser20::make_nvp(name, t.get_fullname()));
}

template<typename Archive>
inline auto try_load_mono_type(Archive& ar, const char* name, mono::mono_type& t) -> bool
{
    std::string fullname;
    if(try_load(ar, ser20::make_nvp(name, fullname)))
    {
        t = get_type_by_fullname(fullname);
        return true;
    }
    return false;
}

template<typename Archive, typename T>
inline void SAVE_FUNCTION_NAME(Archive& ar, const mono::vector_like_wrapper<T>& obj)
{
    try_save_mono_type(ar, "type", obj.type);

    try_save(ar, ser20::make_nvp("size", obj.container.size()));
    try_save(ar, ser20::make_nvp("container", obj.container));
}

template<typename Archive, typename T>
inline void LOAD_FUNCTION_NAME(Archive& ar, mono::vector_like_wrapper<T>& obj)
{
    try_load_mono_type(ar, "type", obj.type);

    if(!obj.type.valid())
    {
        return;
    }
    
    ser20::size_type size{};
    try_load(ar, ser20::make_nvp("size", size));
    obj.container.resize(static_cast<std::size_t>(size));
    for(auto& v : obj.container)
    {
        v = obj.type.new_instance();
    }

    {

        serialization::path_skip_segment_guard guard(true);
        try_load(ar, ser20::make_nvp("container", obj.container));
    }
}

SAVE(mono::mono_object)
{
    using namespace unravel;
    // First, try object-level serializer (like inspector's get_object_inspector)
    using mono_object_serializer = std::function<bool(ser20::detail::OutputArchiveBase&, const mono::mono_object&)>;
    
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
        std::function<bool(ser20::detail::OutputArchiveBase&, const mono::mono_object&, const mono::mono_field&)>;

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
        std::function<bool(ser20::detail::OutputArchiveBase&, const mono::mono_object&, const mono::mono_property&)>;

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
        if(field.get_visibility() == mono::visibility::vis_public)
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
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                auto collection_obj = invoker.get_value(obj);
                if(collection_obj.valid())
                {
                    serialization::path_segment_guard guard(field.get_name());
                    if(field_type.is_array())
                    {
                        mono::mono_array<mono::mono_object> array(collection_obj);
                        auto vec = array.to_vector_wrapper<std::vector<mono::mono_object>>();
                        try_save(ar, ser20::make_nvp(field.get_name(), vec));
                    }
                    else if(field_type.is_list())
                    {
                        mono::mono_list<mono::mono_object> list(collection_obj);
                        auto vec = list.to_vector_wrapper<std::vector<mono::mono_object>>();    
                        try_save(ar, ser20::make_nvp(field.get_name(), vec));
                    }
                }
            }
            else if(field_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    serialization::path_segment_guard guard(field.get_name());
                    try_save(ar, ser20::make_nvp(field.get_name(), nested_obj));
                }
            }
        }
    }

    for(auto& prop : properties)
    {
        if(prop.get_visibility() == mono::visibility::vis_public)
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
                auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                auto collection_obj = invoker.get_value(obj);
                if(collection_obj.valid())
                {
                    serialization::path_segment_guard guard(prop.get_name());
                    if(prop_type.is_array())
                    {
                        mono::mono_array<mono::mono_object> array(collection_obj);
                        auto vec = array.to_vector_wrapper<std::vector<mono::mono_object>>();
                        try_save(ar, ser20::make_nvp(prop.get_name(), vec));
                    }
                    else if(prop_type.is_list())
                    {
                        mono::mono_list<mono::mono_object> list(collection_obj);
                        auto vec = list.to_vector_wrapper<std::vector<mono::mono_object>>();
                        try_save(ar, ser20::make_nvp(prop.get_name(), vec));
                    }
                }
            }
            else if(prop_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    serialization::path_segment_guard guard(prop.get_name());
                    try_save(ar, ser20::make_nvp(prop.get_name(), nested_obj));
                }
            }
        }
    }
    
}

LOAD(mono::mono_object)
{
    using namespace unravel;
    
    // First, try object-level serializer (like inspector's get_object_inspector)
    using mono_object_serializer = std::function<bool(ser20::detail::InputArchiveBase&, mono::mono_object&)>;
    
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
        std::function<bool(ser20::detail::InputArchiveBase&, mono::mono_object&, mono::mono_field&)>;

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
        std::function<bool(ser20::detail::InputArchiveBase&, mono::mono_object&, mono::mono_property&)>;

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
        if(field.get_visibility() == mono::visibility::vis_public)
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
                // Handle arrays and lists - load vector and convert back
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                mono::vector_like_wrapper<std::vector<mono::mono_object>> vec;
                if(try_load(ar, ser20::make_nvp(field.get_name(), vec)))
                {
                    if(field_type.is_array())
                    {
                        mono::mono_array<mono::mono_object> array(vec.container, vec.type);
                        invoker.set_value(obj, array);
                    }
                    else if(field_type.is_list())
                    {
                        mono::mono_list<mono::mono_object> list(vec.container, vec.type);
                        invoker.set_value(obj, list);
                    }
                }
            }
            else if(field_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = mono::make_field_invoker<mono::mono_object>(field);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    try_load(ar, ser20::make_nvp(field.get_name(), nested_obj));
                    invoker.set_value(obj, nested_obj);
                }
            }
        }
    }
    
    for(auto& prop : properties)
    {
        if(prop.get_visibility() == mono::visibility::vis_public)
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
                // Handle arrays and lists - load vector and convert back
                auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                mono::vector_like_wrapper<std::vector<mono::mono_object>> vec;
                if(try_load(ar, ser20::make_nvp(prop.get_name(), vec)))
                {
                    if(prop_type.is_array())
                    {
                        mono::mono_array<mono::mono_object> array(vec.container, vec.type);
                        invoker.set_value(obj, array);
                    }
                    else if(prop_type.is_list())
                    {
                        mono::mono_list<mono::mono_object> list(vec.container, vec.type);
                        invoker.set_value(obj, list);
                    }
                }
            }
            else if(prop_type.is_serializable())
            {
                // Recursively handle serializable nested objects
                auto invoker = mono::make_property_invoker<mono::mono_object>(prop);
                auto nested_obj = invoker.get_value(obj);
                if(nested_obj.valid())
                {
                    try_load(ar, ser20::make_nvp(prop.get_name(), nested_obj));
                    invoker.set_value(obj, nested_obj);
                }
            }
        }
    }
    
}
LOAD_INSTANTIATE(mono::mono_object, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mono::mono_object, ser20::iarchive_binary_t);
}





namespace unravel
{
SAVE(script_component::script_object)
{
    auto object = obj.pinned->get_object();
    const auto& type = object.get_type();

    try_save_mono_type(ar, "type", type);

    SAVE_FUNCTION_NAME(ar, object);
}
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_binary_t);

LOAD(script_component::script_object)
{
    mono::mono_type script_type;
    try_load_mono_type(ar, "type", script_type);


    if(!script_type.valid())
    {
        return;
    }


    serialization::path_segment_guard guard(script_type.get_fullname());

    auto* existing_obj = script_component_loader_ctx
                             ? script_component_loader_ctx->get_script_object(script_type.get_hash())
                             : nullptr;
    if(existing_obj)
    {
        obj = *existing_obj;
    }
    else
    {
        auto object = script_type.new_instance();
        obj = script_component::script_object(object);
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

LOAD(script_component)
{


    auto& load_ctx = get_load_context();
    if(load_ctx.is_updating_prefab())
    {

        script_component_loader_context ctx;
        ctx.script_objects = obj.get_script_components();
        
        script_component_loader_ctx = &ctx;

        auto _ = hpp::finally([&]()
        {
            script_component_loader_ctx = nullptr;
        });


        script_component::script_components_t comps;
        if(try_load(ar, ser20::make_nvp("script_components", comps)))
        {
            obj.add_missing_script_components(comps);
        }

    }
    else
    {
        script_component::script_components_t comps;
        if(try_load(ar, ser20::make_nvp("script_components", comps)))
        {
            obj.add_script_components(comps);
        }
    }
    
}
LOAD_INSTANTIATE(script_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(script_component, ser20::iarchive_binary_t);




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

