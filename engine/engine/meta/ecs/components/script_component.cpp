#include "script_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <monopp/mono_field_invoker.h>
#include <monopp/mono_property_invoker.h>

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
                if(obj.scoped->object.get_type().get_hash() == hash)
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
    rttr::registration::class_<script_component>("script_component")(rttr::metadata("category", "SCRIPTING"),
                                                                     rttr::metadata("pretty_name", "Script"))
        .constructor<>()(rttr::policy::ctor::as_std_shared_ptr)
        .method("component_exists", &component_exists<script_component>);

    // Register script_component class with entt
    entt::meta_factory<script_component>{}
        .type("script_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"category", "SCRIPTING"},
            entt::attribute{"pretty_name", "Script"},
        })
        .func<&component_exists<script_component>>("component_exists"_hs);
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
            return true;
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
            }
            return true;
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

SAVE(script_component::script_object)
{
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

    const auto& object = obj.scoped->object;
    const auto& type = object.get_type();

    try_save(ar, ser20::make_nvp("type", type.get_fullname()));

    auto fields = type.get_fields();
    auto properties = type.get_properties();
    for(auto& field : fields)
    {
        if(field.get_visibility() == mono::visibility::vis_public)
        {
            const auto& field_type = field.get_type();

            auto field_serilizer = get_field_serilizer(field_type.get_name());
            if(field_serilizer)
            {
                field_serilizer(ar, object, field);
            }
            else if(field_type.is_enum())
            {
                auto enum_type = field_type.get_enum_base_type();

                auto enum_serilizer = get_field_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, object, field);
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
                prop_serilizer(ar, object, prop);
            }
            else if(prop_type.is_enum())
            {
                auto enum_type = prop_type.get_enum_base_type();

                auto enum_serilizer = get_property_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, object, prop);
                }
            }
        }
    }
}
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(script_component::script_object, ser20::oarchive_binary_t);

LOAD(script_component::script_object)
{
    auto& ctx = engine::context();
    auto& sys = ctx.get_cached<script_system>();
    const auto& all_scriptable_components = sys.get_all_scriptable_components();

    std::string type;
    try_load(ar, ser20::make_nvp("type", type));

    auto it = std::find_if(std::begin(all_scriptable_components),
                           std::end(all_scriptable_components),
                           [&](const mono::mono_type& script_type)
                           {
                               return type == script_type.get_fullname();
                           });

    if(it == std::end(all_scriptable_components))
    {
        return;
    }

    serialization::path_segment_guard guard(type);

    const auto& script_type = *it;

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

    auto fields = script_type.get_fields();
    auto properties = script_type.get_properties();

    for(auto& field : fields)
    {
        if(field.get_visibility() == mono::visibility::vis_public)
        {
            const auto& field_type = field.get_type();

            auto field_serilizer = get_field_serilizer(field_type.get_name());
            if(field_serilizer)
            {
                field_serilizer(ar, obj.scoped->object, field);
            }
            else if(field_type.is_enum())
            {
                auto enum_type = field_type.get_enum_base_type();

                auto enum_serilizer = get_field_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj.scoped->object, field);
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
                prop_serilizer(ar, obj.scoped->object, prop);
            }
            else if(prop_type.is_enum())
            {
                auto enum_type = prop_type.get_enum_base_type();

                auto enum_serilizer = get_property_serilizer(enum_type.get_name());
                if(enum_serilizer)
                {
                    enum_serilizer(ar, obj.scoped->object, prop);
                }
            }
        }
    }
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


        script_component::script_components_t comps;
        if(try_load(ar, ser20::make_nvp("script_components", comps)))
        {
            obj.add_missing_script_components(comps);
        }

        script_component_loader_ctx = nullptr;
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
} // namespace unravel
