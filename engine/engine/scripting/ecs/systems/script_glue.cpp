#include "script_interop.h"
#include "script_system.h"
#include <engine/ecs/ecs.h>
#include <engine/events.h>
#include <engine/play_mode.h>

#include <engine/engine.h>
#include <dotnetpp/dotnetpp.h>

#include <engine/assets/asset_manager.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/input/input.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/rendering/ecs/systems/ik_solvers.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/settings/settings.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/ui/rmlui/RmlUi_SystemInterface.h>
#include <engine/profiler/profiler.h>
#include <graphics/debugdraw.h>

// RmlUi includes
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Variant.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Event.h>

// Mono includes for method invocation

#include <simulation/simulation.h>
#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <seq/seq.h>
#include <string_utils/utils.h>

namespace unravel
{
namespace
{

auto get_material_properties(const material::sptr& material) -> dotnetpp_backend::managed_interface::material_properties
{
    using converter = dotnet::managed_interface::converter;

    dotnetpp_backend::managed_interface::material_properties props;

    if(material->is<pbr_material>())
    {
        const auto pbr = std::static_pointer_cast<pbr_material>(material);
        props.base_color = converter::convert<math::color, dotnetpp_backend::managed_interface::color>(pbr->get_base_color());
        props.emissive_color =
            converter::convert<math::color, dotnetpp_backend::managed_interface::color>(pbr->get_emissive_color());
        props.tiling = converter::convert<math::vec2, dotnetpp_backend::managed_interface::vector2>(pbr->get_tiling());
        props.roughness = pbr->get_roughness();
        props.metalness = pbr->get_metalness();
        props.bumpiness = pbr->get_bumpiness();
        props.valid = true;
    }

    return props;
}

void set_material_properties(const material::sptr& material, const dotnetpp_backend::managed_interface::material_properties& props)
{
    using converter = dotnet::managed_interface::converter;

    if(material->is<pbr_material>())
    {
        auto pbr = std::static_pointer_cast<pbr_material>(material);
        auto base_color = converter::convert<dotnetpp_backend::managed_interface::color, math::color>(props.base_color);
        pbr->set_base_color(base_color);

        auto emissive_color = converter::convert<dotnetpp_backend::managed_interface::color, math::color>(props.emissive_color);
        pbr->set_emissive_color(emissive_color);

        auto tiling = converter::convert<dotnetpp_backend::managed_interface::vector2, math::vec2>(props.tiling);
        pbr->set_tiling(tiling);

        pbr->set_metalness(props.metalness);

        pbr->set_bumpiness(props.bumpiness);
    }
}

struct dotnet_asset
{
    virtual auto get_asset_uuid(const hpp::uuid& uid) const -> hpp::uuid = 0;
    virtual auto get_asset_uuid(const std::string& key) const -> hpp::uuid = 0;
};

template<typename T>
struct dotnet_asset_impl : dotnet_asset
{
    auto get_asset_uuid(const hpp::uuid& uid) const -> hpp::uuid override
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<T>(uid);
        return asset.uid();
    }

    auto get_asset_uuid(const std::string& key) const -> hpp::uuid override
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<T>(key);
        return asset.uid();
    }
};

auto get_dotnet_asset(size_t type_hash) -> const dotnet_asset*
{
    // clang-format off
    static std::map<size_t, std::shared_ptr<dotnet_asset>> reg =
    {
        {dotnet::type::get_hash("Unravel.Core.Texture"),         std::make_shared<dotnet_asset_impl<gfx::texture>>()},
        {dotnet::type::get_hash("Unravel.Core.Material"),        std::make_shared<dotnet_asset_impl<material>>()},
        {dotnet::type::get_hash("Unravel.Core.Mesh"),            std::make_shared<dotnet_asset_impl<mesh>>()},
        {dotnet::type::get_hash("Unravel.Core.AnimationClip"),   std::make_shared<dotnet_asset_impl<animation_clip>>()},
        {dotnet::type::get_hash("Unravel.Core.Prefab"),          std::make_shared<dotnet_asset_impl<prefab>>()},
        {dotnet::type::get_hash("Unravel.Core.Scene"),           std::make_shared<dotnet_asset_impl<scene_prefab>>()},
        {dotnet::type::get_hash("Unravel.Core.PhysicsMaterial"), std::make_shared<dotnet_asset_impl<physics_material>>()},
        {dotnet::type::get_hash("Unravel.Core.AudioClip"),       std::make_shared<dotnet_asset_impl<audio_clip>>()},
        {dotnet::type::get_hash("Unravel.Core.Font"),            std::make_shared<dotnet_asset_impl<font>>()}
    };
    // clang-format on

    auto it = reg.find(type_hash);
    if(it != reg.end())
    {
        return it->second.get();
    }
    static const dotnet_asset* empty{};
    return empty;
};

auto get_entity_from_id(entt::entity id) -> entt::handle
{
    if(id == entt::entity(0))
    {
        return {};
    }

    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();

    return ec.get_scene().create_handle(id);
}

void raise_invalid_entity_exception()
{
    dotnet::raise_exception("System", "Exception", "Entity is invalid.");
}

template<typename T>
void raise_missing_component_exception()
{
    dotnet::raise_exception("System",
                          "Exception",
                          fmt::format("Entity does not have component of type {}.", hpp::type_name_str<T>()));
}

template<typename T>
auto safe_get_component(entt::entity id) -> T*
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return nullptr;
    }
    auto comp = e.try_get<T>();

    if(!comp)
    {
        raise_missing_component_exception<T>();
        return nullptr;
    }

    return comp;
}

void internal_m2n_load_scene(const std::string& key)
{
    auto delay = seq::delay(0ms);
    delay.on_end.connect(
        [key]()
        {
            auto& ctx = engine::context();
            auto& ec = ctx.get_cached<ecs>();
            auto& am = ctx.get_cached<asset_manager>();
        
            ec.get_scene().load_from(am.get_asset<scene_prefab>(key));
        });

    seq::queue(delay, "script");
}

void internal_m2n_load_scene_uid(const hpp::uuid& uid)
{
    auto delay = seq::delay(0ms);
    delay.on_end.connect(
        [uid]()
        {
            auto& ctx = engine::context();
            auto& ec = ctx.get_cached<ecs>();
            auto& am = ctx.get_cached<asset_manager>();
        
            ec.get_scene().load_from(am.get_asset<scene_prefab>(uid));
        });

    seq::queue(delay, "script");
}

void internal_m2n_reload_scene()
{
    auto delay = seq::delay(0ms);
    delay.on_end.connect(
        []()
        {
            auto& ctx = engine::context();
            auto& ec = ctx.get_cached<ecs>();
        
            ec.get_scene().reload();
        });

    seq::queue(delay, "script");
}

void internal_m2n_create_scene(const dotnet::object& this_ptr)
{
    dotnet::ignore(this_ptr);
}

void internal_m2n_destroy_scene(const dotnet::object& this_ptr)
{
    dotnet::ignore(this_ptr);
}

auto internal_m2n_create_entity(const std::string& tag) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();

    auto e = ec.get_scene().create_entity(tag);

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_uid(const hpp::uuid& uid) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(uid);
    auto e = ec.get_scene().instantiate(pfb);

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_key(const std::string& key) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(key);
    auto e = ec.get_scene().instantiate(pfb);

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_uid_with_parent(const hpp::uuid& uid, entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(uid);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_key_with_parent(const std::string& key, entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(key);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_uid_with_position(const hpp::uuid& uid, const math::vec3& position) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(uid);
    auto e = ec.get_scene().instantiate(pfb);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
    }

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_key_with_position(const std::string& key, const math::vec3& position) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(key);
    auto e = ec.get_scene().instantiate(pfb);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
    }

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_uid_with_position_parent(const hpp::uuid& uid,
                                                                      const math::vec3& position,
                                                                      entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(uid);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
    }

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_key_with_position_parent(const std::string& key,
                                                                       const math::vec3& position,
                                                                       entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(key);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
    }

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent(const hpp::uuid& uid,
                                                                               const math::vec3& position,
                                                                               const math::quat& rotation,
                                                                               entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(uid);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
        comp->set_rotation_global(rotation);
    }

    return e.entity();
}

auto internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent(const std::string& key,
                                                                                const math::vec3& position,
                                                                                const math::quat& rotation,
                                                                                entt::entity parent) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    auto pfb = am.get_asset<prefab>(key);
    auto parent_handle = get_entity_from_id(parent);
    auto e = ec.get_scene().instantiate(pfb, parent_handle);

    if(auto comp = e.try_get<transform_component>())
    {
        comp->set_position_global(position);
        comp->set_rotation_global(rotation);
    }

    return e.entity();
}

auto internal_m2n_clone_entity(entt::entity id) -> entt::entity
{
    auto e = get_entity_from_id(id);
    if(e)
    {
        auto& ctx = engine::context();
        auto& ec = ctx.get_cached<ecs>();

        auto cloned = ec.get_scene().clone_entity(e);
        return cloned.entity();
    }

    entt::handle invalid;
    return invalid.entity();
}

auto internal_m2n_destroy_entity_immediate(entt::entity id) -> bool
{
    auto e = get_entity_from_id(id);
    if(e)
    {
        e.destroy();
        return true;
    }
    return false;
}

auto internal_m2n_destroy_entity(entt::entity id, float seconds) -> bool
{
    seconds = std::max(0.0f, seconds);

    delta_t secs(seconds);
    auto dur = std::chrono::duration_cast<seq::duration_t>(secs);

    auto delay = seq::delay(dur);
    delay.on_end.connect(
        [id]()
        {
            internal_m2n_destroy_entity_immediate(id);
        });

    seq::queue(delay, "script");

    return true;
}

auto internal_m2n_is_entity_valid(entt::entity id) -> bool
{
    auto e = get_entity_from_id(id);
    bool valid = e.valid();
    return valid;
}

auto internal_m2n_find_entity_by_name(const std::string& name) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto view = registry.view<tag_component>();

    for(const auto& e : view)
    {
        if(registry.get<tag_component>(e).name == name)
        {
            return e;
        }
    }

    const entt::handle invalid;
    return invalid.entity();
}

auto internal_m2n_find_entities_by_name(const std::string& name) -> hpp::small_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto view = registry.view<tag_component>();

    hpp::small_vector<entt::entity> result;
    for(const auto& e : view)
    {
        if(registry.get<tag_component>(e).name == name)
        {
            result.emplace_back(e);
        }
    }

    return result;
}

auto internal_m2n_find_entity_by_tag(const std::string& tag) -> entt::entity
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto view = registry.view<tag_component>();

    for(const auto& e : view)
    {
        if(registry.get<tag_component>(e).tag == tag)
        {
            return e;
        }
    }

    const entt::handle invalid;
    return invalid.entity();
}

auto internal_m2n_find_entities_by_tag(const std::string& tag) -> hpp::small_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto view = registry.view<tag_component>();

    hpp::small_vector<entt::entity> result;
    for(const auto& e : view)
    {
        if(registry.get<tag_component>(e).tag == tag)
        {
            result.emplace_back(e);
        }
    }

    return result;
}

struct native_comp_lut
{
    auto is_valid() const -> bool
    {
        return add_native != nullptr;
    }
    std::function<bool(size_t type_hash, entt::handle e)> add_native;
    std::function<bool(size_t type_hash, entt::handle e)> has_native;
    std::function<bool(size_t type_hash, entt::handle e)> remove_native;

    static auto get_registry() -> std::unordered_map<size_t, native_comp_lut>&
    {
        static std::unordered_map<size_t, native_comp_lut> lut;
        return lut;
    }

    static auto get_action_table(size_t type_hash) -> const native_comp_lut&
    {
        const auto& registry = get_registry();
        auto it = registry.find(type_hash);
        if(it != registry.end())
        {
            return it->second;
        }

        static const native_comp_lut empty;
        return empty;
    }

    template<typename T>
    static auto register_native_component(const std::string& name)
    {
        size_t hash = dotnet::type::get_hash(name);
        native_comp_lut lut;
        lut.add_native = [hash](size_t type_hash, entt::handle e)
        {
            if(type_hash == hash)
            {
                auto& native = e.get_or_emplace<T>();
                return true;
            }

            return false;
        };

        lut.has_native = [hash](size_t type_hash, entt::handle e)
        {
            if(type_hash == hash)
            {
                return e.all_of<T>();
            }

            return false;
        };

        lut.remove_native = [hash](size_t type_hash, entt::handle e)
        {
            if(type_hash == hash)
            {
                return e.remove<T>() > 0;
            }

            return false;
        };

        get_registry()[hash] = lut;
    }
};

int register_componetns = []()
{
    native_comp_lut::register_native_component<transform_component>("Unravel.Core.TransformComponent");
    native_comp_lut::register_native_component<id_component>("Unravel.Core.IdComponent");
    native_comp_lut::register_native_component<model_component>("Unravel.Core.ModelComponent");
    native_comp_lut::register_native_component<camera_component>("Unravel.Core.CameraComponent");
    native_comp_lut::register_native_component<light_component>("Unravel.Core.LightComponent");
    native_comp_lut::register_native_component<reflection_probe_component>("Unravel.Core.ReflectionProbeComponent");
    native_comp_lut::register_native_component<physics_component>("Unravel.Core.PhysicsComponent");
    native_comp_lut::register_native_component<animation_component>("Unravel.Core.AnimationComponent");
    native_comp_lut::register_native_component<audio_listener_component>("Unravel.Core.AudioListenerComponent");
    native_comp_lut::register_native_component<audio_source_component>("Unravel.Core.AudioSourceComponent");
    native_comp_lut::register_native_component<bone_component>("Unravel.Core.BoneComponent");
    native_comp_lut::register_native_component<submesh_component>("Unravel.Core.SubmeshComponent");
    native_comp_lut::register_native_component<text_component>("Unravel.Core.TextComponent");
    native_comp_lut::register_native_component<particle_emitter_component>("Unravel.Core.ParticleEmitterComponent");
    native_comp_lut::register_native_component<ui_document_component>("Unravel.Core.UIDocumentComponent");
    native_comp_lut::register_native_component<character_controller_component>("Unravel.Core.CharacterControllerComponent");

    return 0;
}();

auto internal_add_native_component(const dotnet::type& type, entt::handle e, script_component& script_comp)
    -> dotnet::object
{
    // TODO OPTIMIZE

    const auto& type_hash = type.get_hash();
    bool add = false;

    const auto& lut = native_comp_lut::get_action_table(type_hash);
    if(lut.is_valid())
    {
        add = lut.add_native(type_hash, e);
    }

    if(add)
    {
        auto comp = script_comp.get_native_component(type);

        if(!comp.pinned)
        {
            comp = script_comp.add_native_component(type);
        }
        return comp.pinned->get_object();
    }

    return {};
}

auto internal_get_native_component_impl(const dotnet::type& type,
                                        entt::handle e,
                                        script_component& script_comp,
                                        bool exists) -> dotnet::object
{
    auto comp = script_comp.get_native_component(type);
    if(exists)
    {
        if(!comp.pinned)
        {
            comp = script_comp.add_native_component(type);
        }
        return comp.pinned->get_object();
    }

    if(comp.pinned)
    {
        script_comp.remove_native_component(comp.pinned->get_object());
    }

    return {};
}

auto internal_get_native_component(const dotnet::type& type, entt::handle e, script_component& script_comp)
    -> dotnet::object
{
    const auto& type_hash = type.get_hash();

    // TODO OPTIMIZE
    bool native = false;
    bool has = false;

    const auto& lut = native_comp_lut::get_action_table(type_hash);
    if(lut.is_valid())
    {
        has = lut.has_native(type_hash, e);
        native = true;
    }

    if(native)
    {
        return internal_get_native_component_impl(type, e, script_comp, has);
    }

    return {};
}

auto internal_remove_native_component(const dotnet::object& obj, entt::handle e, script_component& script_comp)
    -> bool
{
    const auto& type = obj.get_type();
    const auto& type_hash = type.get_hash();

    // TODO OPTIMIZE

    bool removed = false;
    const auto& lut = native_comp_lut::get_action_table(type_hash);
    if(lut.is_valid())
    {
        removed = lut.remove_native(type_hash, e);
    }

    if(removed)
    {
        return script_comp.remove_native_component(obj);
    }

    return false;
}

auto internal_remove_native_component(const dotnet::type& type, entt::handle e, script_component& script_comp)
    -> bool
{
    const auto& type_hash = type.get_hash();

    // TODO OPTIMIZE

    bool removed = false;
    const auto& lut = native_comp_lut::get_action_table(type_hash);
    if(lut.is_valid())
    {
        removed = lut.remove_native(type_hash, e);
    }

    if(removed)
    {
        return script_comp.remove_native_component(type);
    }

    return false;
}

auto internal_m2n_add_component(entt::entity id, const dotnet::type& type) -> dotnet::object
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return {};
    }
    auto& script_comp = e.get_or_emplace<script_component>();

    if(auto native_comp = internal_add_native_component(type, e, script_comp))
    {
        return native_comp;
    }

    auto component = script_comp.add_script_component(type);
    return component.pinned->get_object();
}

auto internal_m2n_get_component(entt::entity id, const dotnet::type& type) -> dotnet::object
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return {};
    }

    auto& script_comp = e.get_or_emplace<script_component>();

    if(auto native_comp = internal_get_native_component(type, e, script_comp))
    {
        return native_comp;
    }

    auto component = script_comp.get_script_component(type);

    if(component.pinned)
    {
        // Validate the object is still valid before returning
        // Even with pinned handles, objects can be finalized or corrupted
        if(!component.is_marked_for_destroy())
        {
            return component.pinned->get_object();
        }
    }

    return {};
}


auto internal_m2n_get_components_impl(entt::entity id, const dotnet::type& type) -> std::vector<dotnet::object>
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return {};
    }

    auto& script_comp = e.get_or_emplace<script_component>();

    if(auto native_comp = internal_get_native_component(type, e, script_comp))
    {
        return {native_comp};
    }

    return script_comp.get_script_components(type);
}

auto internal_m2n_get_components(entt::entity id, const dotnet::type& type) -> dotnet::array<dotnet::object>
{
    auto components = internal_m2n_get_components_impl(id, type);
    return dotnet::array<dotnet::object>(components, type);
}

auto internal_m2n_get_component_in_children(entt::entity id, const dotnet::type& type) -> dotnet::object
{
    auto comp = internal_m2n_get_component(id, type);
    if(comp.valid())
    {
        return comp;
    }
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& children = comp->get_children();
        for(const auto& child : children)
        {
            if(auto result = internal_m2n_get_component(child, type))
            {
                return result;
            }
        }
    }

    return {};
}

auto internal_m2n_get_components_in_children(entt::entity id, const dotnet::type& type)
    -> dotnet::array<dotnet::object>
{
    auto components = internal_m2n_get_components_impl(id, type);
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& children = comp->get_children();
        for(const auto& child : children)
        {
            auto child_components = internal_m2n_get_components_impl(child, type);
            std::move(child_components.begin(), child_components.end(), std::back_inserter(components));
        }
    }
    return dotnet::array<dotnet::object>(components, type);
}

auto internal_m2n_get_transform_component(entt::entity id, const dotnet::type& type) -> dotnet::object
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return {};
    }

    auto& script_comp = e.get_or_emplace<script_component>();
    return internal_get_native_component_impl(type, e, script_comp, true);
}

auto internal_m2n_get_name(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<tag_component>(id))
    {
        return comp->name;
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_set_name(entt::entity id, const std::string& name)
{
    if(auto comp = safe_get_component<tag_component>(id))
    {
        comp->name = name;
    }
}

auto internal_m2n_get_tag(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<tag_component>(id))
    {
        return comp->tag;
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_set_tag(entt::entity id, const std::string& tag)
{
    if(auto comp = safe_get_component<tag_component>(id))
    {
        comp->tag = tag;
    }
}

auto internal_m2n_get_layers(entt::entity id) -> int
{
    if(auto comp = safe_get_component<layer_component>(id))
    {
        return comp->layers.mask;
    }

    return layer_reserved::nothing_layer;
}

void internal_m2n_set_layers(entt::entity id, int mask)
{
    if(auto comp = safe_get_component<layer_component>(id))
    {
        comp->layers.mask = mask;
    }
}

auto internal_m2n_get_active_global(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->is_active_global();
    }

    return false;
}

auto internal_m2n_get_active_local(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->is_active();
    }

    return false;
}

void internal_m2n_set_active_local(entt::entity id, bool active)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_active(active);
    }
}

auto internal_m2n_has_component(entt::entity id, const dotnet::type& type) -> bool
{
    auto comp = internal_m2n_get_component(id, type);

    return comp.valid();
}

auto internal_m2n_find_entities_with_component(const dotnet::type& component_type) -> hpp::small_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    hpp::small_vector<entt::entity> result;
    
    // Iterate through all entities using the storage directly
    for(auto [entity] : registry.storage<entt::entity>().each())
    {
        if(registry.valid(entity))
        {
            // Check if entity has the component using the same logic as internal_m2n_has_component
            auto comp = internal_m2n_get_component(entity, component_type);
            if(comp.valid())
            {
                result.emplace_back(entity);
            }
        }
    }

    return result;
}

auto internal_m2n_find_entities_with_components(const std::vector<dotnet::type>& component_types) -> hpp::small_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    hpp::small_vector<entt::entity> result;
    
    if(component_types.empty())
    {
        return result;
    }
    
    // Iterate through all entities using the storage directly
    for(auto [entity] : registry.storage<entt::entity>().each())
    {
        if(registry.valid(entity))
        {
            bool has_all_components = true;
            
            // Check if entity has all required components
            for(const auto& component_type : component_types)
            {
                auto comp = internal_m2n_get_component(entity, component_type);
                if(!comp.valid())
                {
                    has_all_components = false;
                    break;
                }
            }
            
            if(has_all_components)
            {
                result.emplace_back(entity);
            }
        }
    }

    return result;
}

auto internal_m2n_remove_component_instance(entt::entity id, const dotnet::object& comp) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    auto& script_comp = e.get_or_emplace<script_component>();

    if(internal_remove_native_component(comp, e, script_comp))
    {
        return true;
    }

    return script_comp.remove_script_component(comp);
}

auto internal_m2n_remove_component_instance_delay(entt::entity id, const dotnet::object& comp, float seconds_delay)
    -> bool
{
    delta_t secs(seconds_delay);
    auto dur = std::chrono::duration_cast<seq::duration_t>(secs);

    auto delay = seq::delay(dur);
    delay.on_end.connect(
        [id, comp]()
        {
            internal_m2n_remove_component_instance(id, comp);
        });

    seq::start(delay, "script");

    return true;
}

auto internal_m2n_remove_component(entt::entity id, const dotnet::type& type) -> bool
{
    auto e = get_entity_from_id(id);
    if(!e)
    {
        raise_invalid_entity_exception();
        return false;
    }
    auto& script_comp = e.get_or_emplace<script_component>();

    if(internal_remove_native_component(type, e, script_comp))
    {
        return true;
    }

    return script_comp.remove_script_component(type);
}

auto internal_m2n_remove_component_delay(entt::entity id, const dotnet::type& type, float seconds_delay) -> bool
{
    delta_t secs(seconds_delay);
    auto dur = std::chrono::duration_cast<seq::duration_t>(secs);

    auto delay = seq::delay(dur);
    delay.on_end.connect(
        [id, type]()
        {
            internal_m2n_remove_component(id, type);
        });

    seq::start(delay, "script");

    return true;
}

//-------------------------------------------------------------------------
/*

  _      ____   _____
 | |    / __ \ / ____|
 | |   | |  | | |  __
 | |   | |  | | | |_ |
 | |___| |__| | |__| |
 |______\____/ \_____|


*/
//-------------------------------------------------------------------------

void internal_m2n_log_trace(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_TRACE_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_info(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_INFO_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_warning(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_WARNING_LOC(file.c_str(), line, func.c_str(), message);
}

void internal_m2n_log_error(const std::string& message, const std::string& func, const std::string& file, int line)
{
    APPLOG_ERROR_LOC(file.c_str(), line, func.c_str(), message);
}

//-------------------------------------------------------------------------

void internal_m2n_application_quit()
{
    auto delay = seq::delay(0ms);
    delay.on_end.connect(
        []()
        {
            auto& ctx = engine::context();
            auto& ev = ctx.get_cached<events>();
            ctx.get_cached<play_mode>().set_active(ctx, false);
        });

    seq::queue(delay, "script");
}

void internal_m2n_set_time_scale(float scale)
{
    auto& ctx = engine::context();
    auto& sim = ctx.get_cached<simulation>();
    sim.set_time_scale(scale);
}

void internal_m2n_profiler_add_record(const std::string& name, float time_ms)
{
    auto profiler = get_app_profiler();
    if(profiler)
    {
        profiler->add_record(name, time_ms);
    }
}

//-------------------------------------------------------------------------
/*

  _______ _____            _   _  _____ ______ ____  _____  __  __
 |__   __|  __ \     /\   | \ | |/ ____|  ____/ __ \|  __ \|  \/  |
    | |  | |__) |   /  \  |  \| | (___ | |__ | |  | | |__) | \  / |
    | |  |  _  /   / /\ \ | . ` |\___ \|  __|| |  | |  _  /| |\/| |
    | |  | | \ \  / ____ \| |\  |____) | |   | |__| | | \ \| |  | |
    |_|  |_|  \_\/_/    \_\_| \_|_____/|_|    \____/|_|  \_\_|  |_|


*/
//-------------------------------------------------------------------------
auto internal_m2n_get_children(entt::entity id) -> hpp::small_vector<entt::entity>
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& children = comp->get_children();
        hpp::small_vector<entt::entity> children_id;
        children_id.reserve(children.size());
        for(const auto& child : children)
        {
            children_id.emplace_back(child.entity());
        }
        return children_id;
    }

    return {};
}

// Helper structure carrying an entity and the count of path segments matched so far.
struct node_candidate
{
    entt::entity entity;
    size_t matched_index{}; // number of path segments matched so far
};

auto internal_m2n_get_child(entt::entity id, const std::string& path, bool recursive) -> entt::entity
{
    auto root = get_entity_from_id(id);
    if(!root || path.empty())
        return entt::null;

    // Tokenize the path once.
    const auto parts = string_utils::tokenize(path, "/");
    if(parts.empty())
        return entt::null;

    // Use a vector as a queue to reduce dynamic allocations.
    hpp::small_vector<node_candidate> queue;
    queue.reserve(4); // Reserve a reasonable number based on expected hierarchy size.
    queue.push_back({root, 0});

    // Process the vector as a queue.
    for(size_t idx = 0; idx < queue.size(); ++idx)
    {
        auto candidate = queue[idx];
        bool advanced = false;

        // Try matching current candidate.
        if(candidate.matched_index < parts.size())
        {
            if(auto tag_comp = safe_get_component<tag_component>(candidate.entity))
            {
                if(tag_comp->name == parts[candidate.matched_index])
                {
                    candidate.matched_index++;
                    advanced = true;
                    if(candidate.matched_index == parts.size())
                    {
                        return candidate.entity;
                    }
                }
            }
        }

        // Determine if we should enqueue children.
        // For recursive mode: allow children if no match yet or just advanced.
        // For non-recursive mode: allow children only if no match has started.
        bool should_enqueue = recursive ? (candidate.matched_index == 0 || advanced) : (candidate.matched_index == 0);

        if(should_enqueue)
        {
            if(auto trans_comp = safe_get_component<transform_component>(candidate.entity))
            {
                for(const auto& child : trans_comp->get_children())
                {
                    queue.push_back({child.entity(), candidate.matched_index});
                }
            }
        }
    }
    // No matching entity found.
    return entt::null;
}

auto internal_m2n_get_parent(entt::entity id) -> entt::entity
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_parent().entity();
    }

    return {};
}

void internal_m2n_set_parent(entt::entity id, entt::entity new_parent, bool global_stays)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        auto parent = get_entity_from_id(new_parent);
        comp->set_parent(parent, global_stays);
    }
}

auto internal_m2n_get_position_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_position_global();
    }

    return {};
}

void internal_m2n_set_position_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_position_global(value);
    }
}

void internal_m2n_move_by_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->move_by_global(value);
    }
}

auto internal_m2n_get_position_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_position_local();
    }

    return {};
}

void internal_m2n_set_position_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_position_local(value);
    }
}

void internal_m2n_move_by_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->move_by_local(value);
    }
}

//--------------------------------------------------
auto internal_m2n_get_rotation_euler_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_euler_global();
    }

    return {};
}

void internal_m2n_rotate_by_euler_global(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_euler_global(amount);
    }
}

void internal_m2n_rotate_axis_global(entt::entity id, float degrees, const math::vec3& axis)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_axis_global(degrees, axis);
    }
}

auto internal_m2n_transform_vector_global(entt::entity id, const math::vec3& coord) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.transform_coord(coord);
    }

    return {};
}

auto internal_m2n_inverse_transform_vector_global(entt::entity id, const math::vec3& coord) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.inverse_transform_coord(coord);
    }

    return {};
}

auto internal_m2n_transform_direction_global(entt::entity id, const math::vec3& direction) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.transform_normal(direction);
    }

    return {};
}

auto internal_m2n_inverse_transform_direction_global(entt::entity id, const math::vec3& direction) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& global = comp->get_transform_global();
        return global.inverse_transform_normal(direction);
    }

    return {};
}

void internal_m2n_look_at(entt::entity id, const math::vec3& point, const math::vec3& up)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->look_at(point, up);
    }
}

void internal_m2n_set_rotation_euler_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_euler_global(value);
    }
}

auto internal_m2n_get_rotation_euler_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_euler_local();
    }

    return {};
}

void internal_m2n_set_rotation_euler_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_euler_local(value);
    }
}

void internal_m2n_rotate_by_euler_local(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_euler_local(amount);
    }
}

auto internal_m2n_get_rotation_global(entt::entity id) -> math::quat
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_global();
    }

    return {};
}

void internal_m2n_set_rotation_global(entt::entity id, const math::quat& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_global(value);
    }
}

void internal_m2n_rotate_by_global(entt::entity id, const math::quat& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_global(amount);
    }
}

auto internal_m2n_get_rotation_local(entt::entity id) -> math::quat
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_rotation_local();
    }

    return {};
}

void internal_m2n_set_rotation_local(entt::entity id, const math::quat& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_rotation_local(value);
    }
}

void internal_m2n_rotate_by_local(entt::entity id, const math::quat& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->rotate_by_local(amount);
    }
}

//--------------------------------------------------
auto internal_m2n_get_scale_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_scale_global();
    }

    return {};
}

void internal_m2n_set_scale_global(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_scale_global(value);
    }
}

void internal_m2n_scale_by_global(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->scale_by_global(amount);
    }
}

auto internal_m2n_get_scale_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_scale_local();
    }

    return {};
}

void internal_m2n_set_scale_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_scale_local(value);
    }
}

void internal_m2n_scale_by_local(entt::entity id, const math::vec3& amount)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->scale_by_local(amount);
    }
}

//--------------------------------------------------
auto internal_m2n_get_skew_global(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_skew_global();
    }

    return {};
}

void internal_m2n_setl_skew_globa(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_skew_global(value);
    }
}

auto internal_m2n_get_skew_local(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        return comp->get_skew_local();
    }

    return {};
}

void internal_m2n_set_skew_local(entt::entity id, const math::vec3& value)
{
    if(auto comp = safe_get_component<transform_component>(id))
    {
        comp->set_skew_local(value);
    }
}

//------------------------------

void internal_m2n_physics_apply_explosion_force(entt::entity id,
                                                float explosion_force,
                                                const math::vec3& explosion_position,
                                                float explosion_radius,
                                                float upwards_modifier,
                                                force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_explosion_force(explosion_force, explosion_position, explosion_radius, upwards_modifier, mode);
    }
}
void internal_m2n_physics_apply_force(entt::entity id, const math::vec3& value, force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_force(value, mode);
    }
}

void internal_m2n_physics_apply_torque(entt::entity id, const math::vec3& value, force_mode mode)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->apply_torque(value, mode);
    }
}

auto internal_m2n_physics_get_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_velocity();
    }

    return {};
}

void internal_m2n_physics_set_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_velocity(velocity);
    }
}

auto internal_m2n_physics_get_angular_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_angular_velocity();
    }

    return {};
}

void internal_m2n_physics_set_angular_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_angular_velocity(velocity);
    }
}

auto internal_m2n_physics_get_include_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_include_mask();
    }

    return {};
}

void internal_m2n_physics_set_include_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_collision_include_mask(mask);
    }
}

auto internal_m2n_physics_get_exclude_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_exclude_mask();
    }

    return {};
}

void internal_m2n_physics_set_exclude_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_collision_exclude_mask(mask);
    }
}

auto internal_m2n_physics_get_collision_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_collision_mask();
    }

    return {};
}

auto internal_m2n_physics_get_is_sensor(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_sensor();
    }

    return false;
}

void internal_m2n_physics_set_is_sensor(entt::entity id, bool sensor)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_sensor(sensor);
    }
}

auto internal_m2n_physics_get_mass(entt::entity id) -> float
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->get_mass();
    }

    return 1.0f;
}

void internal_m2n_physics_set_mass(entt::entity id, float mass)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_mass(mass);
    }
}

auto internal_m2n_physics_get_is_kinematic(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_kinematic();
    }

    return false;
}

void internal_m2n_physics_set_is_kinematic(entt::entity id, bool kinematic)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_kinematic(kinematic);
    }
}

auto internal_m2n_physics_get_use_gravity(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        return comp->is_using_gravity();
    }

    return true;
}

void internal_m2n_physics_set_use_gravity(entt::entity id, bool use_gravity)
{
    if(auto comp = safe_get_component<physics_component>(id))
    {
        comp->set_is_using_gravity(use_gravity);
    }
}
//------------------------------

//------------------------------
// Character Controller Component
//------------------------------
void internal_m2n_cc_move(entt::entity id, const math::vec3& displacement)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->move(displacement);
    }
}

void internal_m2n_cc_jump(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->jump(velocity);
    }
}

void internal_m2n_cc_apply_impulse(entt::entity id, const math::vec3& impulse)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->apply_impulse(impulse);
    }
}

void internal_m2n_cc_warp(entt::entity id, const math::vec3& position)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->warp(position);
    }
}

auto internal_m2n_cc_get_is_grounded(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->is_grounded();
    }
    return false;
}

auto internal_m2n_cc_get_can_jump(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->can_jump();
    }
    return false;
}

auto internal_m2n_cc_get_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_velocity();
    }
    return {};
}

void internal_m2n_cc_set_linear_velocity(entt::entity id, const math::vec3& velocity)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_linear_velocity(velocity);
    }
}

auto internal_m2n_cc_get_linear_velocity(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_linear_velocity();
    }
    return {};
}

auto internal_m2n_cc_get_radius(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_radius();
    }
    return 0.5f;
}

void internal_m2n_cc_set_radius(entt::entity id, float radius)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_radius(radius);
    }
}

auto internal_m2n_cc_get_height(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_height();
    }
    return 1.0f;
}

void internal_m2n_cc_set_height(entt::entity id, float height)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_height(height);
    }
}

auto internal_m2n_cc_get_center(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_center();
    }
    return {};
}

void internal_m2n_cc_set_center(entt::entity id, const math::vec3& center)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_center(center);
    }
}

auto internal_m2n_cc_get_step_height(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_step_height();
    }
    return 0.3f;
}

void internal_m2n_cc_set_step_height(entt::entity id, float step_height)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_step_height(step_height);
    }
}

auto internal_m2n_cc_get_slope_limit(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_slope_limit();
    }
    return 45.0f;
}

void internal_m2n_cc_set_slope_limit(entt::entity id, float slope_limit)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_slope_limit(slope_limit);
    }
}

auto internal_m2n_cc_get_skin_width(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_skin_width();
    }
    return 0.08f;
}

void internal_m2n_cc_set_skin_width(entt::entity id, float skin_width)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_skin_width(skin_width);
    }
}

auto internal_m2n_cc_get_gravity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_gravity_scale();
    }
    return 1.0f;
}

void internal_m2n_cc_set_gravity_scale(entt::entity id, float scale)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_gravity_scale(scale);
    }
}

auto internal_m2n_cc_get_terminal_velocity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_terminal_velocity();
    }
    return 55.0f;
}

void internal_m2n_cc_set_terminal_velocity(entt::entity id, float speed)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_terminal_velocity(speed);
    }
}

auto internal_m2n_cc_get_linear_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_linear_damping();
    }
    return 0.0f;
}

void internal_m2n_cc_set_linear_damping(entt::entity id, float damping)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_linear_damping(damping);
    }
}

auto internal_m2n_cc_get_include_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_include_mask();
    }
    return {};
}

void internal_m2n_cc_set_include_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_collision_include_mask(mask);
    }
}

auto internal_m2n_cc_get_exclude_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_exclude_mask();
    }
    return {};
}

void internal_m2n_cc_set_exclude_layers(entt::entity id, layer_mask mask)
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        comp->set_collision_exclude_mask(mask);
    }
}

auto internal_m2n_cc_get_collision_layers(entt::entity id) -> layer_mask
{
    if(auto comp = safe_get_component<character_controller_component>(id))
    {
        return comp->get_collision_mask();
    }
    return {};
}
//------------------------------

void internal_m2n_animation_blend(entt::entity id, int layer, hpp::uuid guid, float seconds, bool loop, bool phase_sync)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<animation_clip>(guid);
        comp->get_player().blend_to(layer, asset, animation_player::seconds_t(seconds), loop, phase_sync);
    }
}

void internal_m2n_animation_play(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().play();
    }
}

void internal_m2n_animation_pause(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().pause();
    }
}

void internal_m2n_animation_resume(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().resume();
    }
}

void internal_m2n_animation_stop(entt::entity id)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->get_player().stop();
    }
}

void internal_m2n_animation_set_speed(entt::entity id, float speed)
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        comp->set_speed(speed);
    }
}

auto internal_m2n_animation_get_speed(entt::entity id) -> float
{
    if(auto comp = safe_get_component<animation_component>(id))
    {
        return comp->get_speed();
    }
    return 1.0f;
}

//------------------------------
auto internal_m2n_camera_screen_point_to_ray(entt::entity id,
                                             const math::vec2& origin,
                                             dotnetpp_backend::managed_interface::ray* managed_ray) -> bool
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec3 ray_origin{};
        math::vec3 ray_dir{};
        bool result = comp->get_camera().viewport_to_ray(origin, ray_origin, ray_dir);
        if(result)
        {
            using converter = dotnet::managed_interface::converter;
            managed_ray->origin = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_origin);
            managed_ray->direction = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_dir);
        }
        return result;
    }

    return false;
}

auto internal_m2n_camera_screen_point_to_world_2d(entt::entity id, const math::vec2& origin) -> math::vec3
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec3 world_pos{};
        
        const auto& frustum = comp->get_camera().get_frustum();

        bool result = comp->get_camera().viewport_to_world(origin, frustum.planes[math::volume_plane::near_plane], world_pos, false);
        if(!result)
        {
            return {};
        }
        return world_pos;
    }
    return {};
}

auto internal_m2n_camera_screen_point_to_world(entt::entity id, const math::vec3& origin) -> math::vec3
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec2 screen_point(origin.x, origin.y);
        float distance_from_camera = origin.z;
        
        math::vec3 ray_origin{};
        math::vec3 ray_dir{};
        
        if(!comp->get_camera().viewport_to_ray(screen_point, ray_origin, ray_dir))
        {
            return {};
        }
        
        math::vec3 world_pos = ray_origin + (ray_dir * distance_from_camera);
        return world_pos;
    }
    return {};
}
//------------------------------
auto internal_m2n_model_get_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->is_enabled();
    }

    return false;
}

void internal_m2n_model_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        comp->set_enabled(enabled);
    }
}

auto internal_m2n_model_get_shared_material(entt::entity id, uint32_t index) -> hpp::uuid
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_material(index).uid();
    }

    return {};
}

auto internal_m2n_model_get_shared_material_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_materials().size();
    }

    return {};
}

auto internal_m2n_model_get_material_instance(entt::entity id, uint32_t index)
    -> dotnetpp_backend::managed_interface::material_properties
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto instance = comp->get_model().get_material_instance(index);
        return get_material_properties(instance);
    }

    return {};
}

void internal_m2n_model_set_shared_material(entt::entity id, const hpp::uuid& uid, uint32_t index)
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto asset = am.get_asset<material>(uid);

        auto model = comp->get_model();
        model.set_material(asset, index);
        comp->set_model(model);
    }
}

void internal_m2n_model_set_material_instance(entt::entity id,
                                              const dotnetpp_backend::managed_interface::material_properties& props,
                                              uint32_t index)
{
    using converter = dotnet::managed_interface::converter;

    if(auto comp = safe_get_component<model_component>(id))
    {
        auto model = comp->get_model();

        if(props.valid)
        {
            auto material = model.get_or_emplace_material_instance(index);
            set_material_properties(material, props);
            model.set_material_instance(material, index);
        }
        else
        {
            model.set_material_instance(nullptr, index);
        }
        comp->set_model(model);
    }
}

auto internal_m2n_model_get_material_instance_count(entt::entity id) -> int
{
    if(auto comp = safe_get_component<model_component>(id))
    {
        return comp->get_model().get_material_instances().size();
    }

    return {};
}

//------------------------------
// Particle Emitter Component Functions
//------------------------------

auto internal_m2n_particle_emitter_get_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_enabled();
    }
    return false;
}

void internal_m2n_particle_emitter_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_enabled(enabled);
    }
}

auto internal_m2n_particle_emitter_get_max_particles(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_max_particles();
    }
    return 0;
}

void internal_m2n_particle_emitter_set_max_particles(entt::entity id, uint32_t max_particles)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_max_particles(max_particles);
    }
}

auto internal_m2n_particle_emitter_get_shape(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_shape());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_shape(entt::entity id, int shape)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_shape(static_cast<EmitterShape::Enum>(shape));
    }
}

auto internal_m2n_particle_emitter_get_direction(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_direction());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_direction(entt::entity id, int direction)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_direction(static_cast<EmitterDirection::Enum>(direction));
    }
}

auto internal_m2n_particle_emitter_get_gravity_scale(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_gravity_scale();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_gravity_scale(entt::entity id, float scale)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_gravity_scale(scale);
    }
}

auto internal_m2n_particle_emitter_get_emission_rate(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_rate();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_emission_rate(entt::entity id, float rate)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_rate(rate);
    }
}

auto internal_m2n_particle_emitter_get_temporal_motion(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_temporal_motion();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_temporal_motion(entt::entity id, float motion)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_temporal_motion(motion);
    }
}

auto internal_m2n_particle_emitter_get_velocity_damping(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_velocity_damping();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_velocity_damping(entt::entity id, float damping)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_velocity_damping(damping);
    }
}

auto internal_m2n_particle_emitter_get_opacity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_opacity();
    }
    return 1.0f;
}

void internal_m2n_particle_emitter_set_opacity(entt::entity id, float opacity)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_opacity(opacity);
    }
}

auto internal_m2n_particle_emitter_get_force_over_lifetime(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_force_over_lifetime();
    }
    return math::vec3{0.0f, 0.0f, 0.0f};
}

void internal_m2n_particle_emitter_set_force_over_lifetime(entt::entity id, const math::vec3& force)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_force_over_lifetime(force);
    }
}

auto internal_m2n_particle_emitter_get_emission_shape_scale(entt::entity id) -> math::vec3
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_shape_scale();
    }
    return math::vec3{1.0f, 1.0f, 1.0f};
}

void internal_m2n_particle_emitter_set_emission_shape_scale(entt::entity id, const math::vec3& scale)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_shape_scale(scale);
    }
}

auto internal_m2n_particle_emitter_get_emission_lifetime(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_emission_lifetime().count();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_emission_lifetime(entt::entity id, float lifetime)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_emission_lifetime(std::chrono::duration<float>(lifetime));
    }
}

auto internal_m2n_particle_emitter_get_lifetime(entt::entity id) -> float
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_lifetime().count();
    }
    return 0.0f;
}

void internal_m2n_particle_emitter_set_lifetime(entt::entity id, float lifetime)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_lifetime(std::chrono::duration<float>(lifetime));
    }
}

auto internal_m2n_particle_emitter_get_position_easing(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_position_easing());
    }
    return 0;
}

void internal_m2n_particle_emitter_set_position_easing(entt::entity id, int easing)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_position_easing(static_cast<bx::Easing::Enum>(easing));
    }
}

auto internal_m2n_particle_emitter_get_num_particles(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_num_particles();
    }
    return 0;
}

auto internal_m2n_particle_emitter_is_playing(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_playing();
    }
    return false;
}

auto internal_m2n_particle_emitter_is_paused(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_paused();
    }
    return false;
}

auto internal_m2n_particle_emitter_get_texture(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->get_texture().uid();
    }
    return hpp::uuid{};
}

void internal_m2n_particle_emitter_set_texture(entt::entity id, const hpp::uuid& texture)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();
        auto handle = am.get_asset<gfx::texture>(texture);
        comp->set_texture(handle);
    }
}

void internal_m2n_particle_emitter_play(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->play();
    }
}

void internal_m2n_particle_emitter_stop(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop();
    }
}

void internal_m2n_particle_emitter_stop_and_reset(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->stop_and_reset();
    }
}

void internal_m2n_particle_emitter_pause(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->pause();
    }
}

void internal_m2n_particle_emitter_resume(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->resume();
    }
}

void internal_m2n_particle_emitter_reset_emitter(entt::entity id)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->reset_emitter();
    }
}

auto internal_m2n_particle_emitter_get_loop(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return comp->is_loop();
    }
    return true; // Default to true
}

void internal_m2n_particle_emitter_set_loop(entt::entity id, bool loop)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_loop(loop);
    }
}

auto internal_m2n_particle_emitter_get_blend_mode(entt::entity id) -> int
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        return static_cast<int>(comp->get_blend_mode());
    }
    return static_cast<int>(BlendMode::Normal);
}

void internal_m2n_particle_emitter_set_blend_mode(entt::entity id, int mode)
{
    if(auto comp = safe_get_component<particle_emitter_component>(id))
    {
        comp->set_blend_mode(static_cast<BlendMode::Enum>(mode));
    }
}

//------------------------------
auto internal_m2n_text_get_text(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_text();
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_text_set_text(entt::entity id, const std::string& text)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_text(text);
    }
}

auto internal_m2n_text_get_buffer_type(entt::entity id) -> text_component::buffer_type
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_buffer_type();
    }

    return text_component::buffer_type::static_buffer;
}

void internal_m2n_text_set_buffer_type(entt::entity id, text_component::buffer_type type)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_buffer_type(type);
    }
}

auto internal_m2n_text_get_overflow_type(entt::entity id) -> text_component::overflow_type
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_overflow_type();
    }

    return text_component::overflow_type::word;
}

void internal_m2n_text_set_overflow_type(entt::entity id, text_component::overflow_type type)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_overflow_type(type);
    }
}

auto internal_m2n_text_get_font(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_font().uid();
    }

    return hpp::uuid{};
}

void internal_m2n_text_set_font(entt::entity id, hpp::uuid uid)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<font>(uid);
        comp->set_font(asset);
    }
}

auto internal_m2n_text_get_font_size(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_font_size();
    }

    return 0;
}

void internal_m2n_text_set_font_size(entt::entity id, uint32_t font_size)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_font_size(font_size);
    }
}

auto internal_m2n_text_get_render_font_size(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_render_font_size();
    }

    return 0;
}

auto internal_m2n_text_get_auto_size(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_auto_size();
    }

    return false;
}

void internal_m2n_text_set_auto_size(entt::entity id, bool auto_size)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_auto_size(auto_size);
    }
}
auto internal_m2n_text_get_auto_size_range(entt::entity id) -> urange32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_auto_size_range();
    }

    return {};
}

void internal_m2n_text_set_auto_size_range(entt::entity id, urange32_t range)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_auto_size_range(range);
    }
}

auto internal_m2n_text_get_area(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto area = comp->get_area();
        return {area.width, area.height};
    }

    return {};
}

void internal_m2n_text_set_area(entt::entity id, math::vec2 area)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_area({area.x, area.y});
    }
}

auto internal_m2n_text_get_render_area(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto area = comp->get_render_area();
        return {area.width, area.height};
    }

    return {};
}

auto internal_m2n_text_get_is_rich_text(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_is_rich_text();
    }

    return false;
}

void internal_m2n_text_set_is_rich_text(entt::entity id, bool rich)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_is_rich_text(rich);
    }
}

auto internal_m2n_text_get_alignment(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_alignment().flags;
    }

    return alignment{}.flags;
}

void internal_m2n_text_set_alignment(entt::entity id, uint32_t alignment_flags)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        comp->set_alignment({alignment_flags});
    }
}

auto internal_m2n_text_get_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_bounds();
    }

    return math::bbox::empty;
}

auto internal_m2n_text_get_render_bounds(entt::entity id) -> math::bbox
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_render_bounds();
    }

    return math::bbox::empty;
}

// ==== Text Style Functions ====

void internal_m2n_text_set_opacity(entt::entity id, float opacity)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_opacity(opacity);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_opacity(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_opacity();
    }
    return 1.0f;
}

void internal_m2n_text_set_text_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_text_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_text_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_text_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_background_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_background_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_background_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_background_color();
    }
    return math::color::transparent();
}

void internal_m2n_text_set_foreground_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_foreground_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_foreground_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_foreground_color();
    }
    return math::color::transparent();
}

void internal_m2n_text_set_overline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_overline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_overline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_overline_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_underline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_underline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_underline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_underline_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_strike_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_strike_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_strike_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_strike_color();
    }
    return math::color::white();
}

void internal_m2n_text_set_outline_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_outline_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_outline_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_outline_color();
    }
    return math::color::black();
}

void internal_m2n_text_set_outline_width(entt::entity id, float width)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.outline_width = width;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_outline_width(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().outline_width;
    }
    return 0.0f;
}

void internal_m2n_text_set_shadow_offsets(entt::entity id, math::vec2 offsets)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.shadow_offsets = offsets;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_offsets(entt::entity id) -> math::vec2
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().shadow_offsets;
    }
    return {0.0f, 0.0f};
}

void internal_m2n_text_set_shadow_color(entt::entity id, math::color color)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_shadow_color(color);
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_shadow_color();
    }
    return math::color::black();
}

void internal_m2n_text_set_shadow_softener(entt::entity id, float softener)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.shadow_softener = softener;
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_shadow_softener(entt::entity id) -> float
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().shadow_softener;
    }
    return 1.0f;
}

void internal_m2n_text_set_style_flags(entt::entity id, uint32_t flags)
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        auto style = comp->get_style();
        style.set_style_flags({flags});
        comp->set_style(style);
    }
}

auto internal_m2n_text_get_style_flags(entt::entity id) -> uint32_t
{
    if(auto comp = safe_get_component<text_component>(id))
    {
        return comp->get_style().get_style_flags().flags;
    }
    return gfx::style_normal;
}

//------------------------------

void internal_m2n_light_set_color(entt::entity id, const math::color& color)
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        auto l = comp->get_light();
        l.color = color;
        comp->set_light(l);
    }
}

auto internal_m2n_light_get_color(entt::entity id) -> math::color
{
    if(auto comp = safe_get_component<light_component>(id))
    {
        return comp->get_light().color;
    }

    return math::color::white();
}
//------------------------------

auto internal_m2n_from_euler_rad(const math::vec3& euler) -> math::quat
{
    return {euler};
}

auto internal_m2n_to_euler_rad(const math::quat& euler) -> math::vec3
{
    return math::eulerAngles(euler);
}

auto internal_m2n_angle_axis(float angle, const math::vec3& axis) -> math::quat
{
    return math::angleAxis(angle, axis);
}

auto internal_m2n_look_rotation(const math::vec3& forward, const math::vec3& up) -> math::quat
{
    return math::look_rotation(forward, up);
}

auto internal_m2n_from_to_rotation(const math::vec3& from, const math::vec3& to) -> math::quat
{
    return math::from_to_rotation(from, to);
}

auto internal_m2n_get_asset_by_uuid(const hpp::uuid& uid, const dotnet::type& type) -> hpp::uuid
{
    if(auto asset = get_dotnet_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(uid);
    }

    return {};
}

auto internal_m2n_get_asset_by_key(const std::string& key, const dotnet::type& type) -> hpp::uuid
{
    if(auto asset = get_dotnet_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(key);
    }

    return {};
}

auto internal_m2n_get_material_properties(const hpp::uuid& uid) -> dotnetpp_backend::managed_interface::material_properties
{
    using converter = dotnet::managed_interface::converter;

    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    dotnetpp_backend::managed_interface::material_properties props;
    auto asset = am.get_asset<material>(uid);
    if(!asset)
    {
        return props;
    }
    auto material = asset.get();

    return get_material_properties(material);
}

auto internal_m2n_audio_clip_get_length(const hpp::uuid& uid) -> float
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<audio_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            float secs = clip->get_info().duration.count();
            return secs;
        }
    }

    return 0.0f;
}

auto internal_m2n_animation_clip_get_length(const hpp::uuid& uid) -> float
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<animation_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            return clip->duration.count();
        }
    }

    return 0.0f;
}

auto internal_m2n_animation_clip_get_name(const hpp::uuid& uid) -> std::string
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto asset = am.get_asset<animation_clip>(uid);

    if(asset.is_valid())
    {
        if(auto clip = asset.get())
        {
            return clip->name;
        }
    }

    return {};
}

auto m2n_test_uuid(const hpp::uuid& uid) -> hpp::uuid
{
    APPLOG_INFO("{}:: From C# {}", __func__, hpp::to_string(uid));

    auto newuid = generate_uuid();
    APPLOG_INFO("{}:: New C++ {}", __func__, hpp::to_string(newuid));

    return newuid;
}

void internal_m2n_gizmos_add_sphere(const math::color& color, const math::vec3& position, float radius)
{
    auto& ctx = engine::context();
    auto& path = ctx.get_cached<rendering_system>();
    path.add_debugdraw_call(
        [color, position, radius](gfx::dd_raii& dd)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(color);
            dd.encoder.setWireframe(true);

            bx::Sphere sphere;
            sphere.center.x = position.x;
            sphere.center.y = position.y;
            sphere.center.z = position.z;
            sphere.radius = radius;
            dd.encoder.draw(sphere);
        });
}

void internal_m2n_gizmos_add_ray(const math::color& color,
                                 const math::vec3& position,
                                 const math::vec3& direction,
                                 float max_distance)
{
    auto& ctx = engine::context();
    auto& path = ctx.get_cached<rendering_system>();
    path.add_debugdraw_call(
        [color, position, direction, max_distance](gfx::dd_raii& dd)
        {
            DebugDrawEncoderScopePush scope(dd.encoder);
            dd.encoder.setColor(color);
            dd.encoder.setWireframe(true);

            bx::Ray ray;
            ray.pos.x = position.x;
            ray.pos.y = position.y;
            ray.pos.z = position.z;

            ray.dir.x = direction.x;
            ray.dir.y = direction.y;
            ray.dir.z = direction.z;

            dd.encoder.push();
            dd.encoder.moveTo(ray.pos);
            dd.encoder.lineTo(bx::mul(ray.dir, max_distance));
            dd.encoder.pop();
        });
}

auto internal_m2n_layers_layer_to_name(int layer) -> const std::string&
{
    auto& ctx = engine::context();
    auto& csettings = ctx.get<settings>();

    if(layer >= csettings.layer.layers.size())
    {
        dotnet::raise_exception("System", "Exception", fmt::format("Layer index {} is out of bounds.", layer));

        static const std::string empty;
        return empty;
    }
    return csettings.layer.layers[layer];
}

auto internal_m2n_layers_name_to_layer(const std::string& name) -> int
{
    auto& ctx = engine::context();
    auto& csettings = ctx.get<settings>();

    auto it = std::find(csettings.layer.layers.begin(), csettings.layer.layers.end(), name);
    if(it != csettings.layer.layers.end())
    {
        return static_cast<int>(std::distance(csettings.layer.layers.begin(), it));
    }

    return -1;
}

auto internal_m2n_input_get_analog_value(const std::string& name) -> float
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.get_analog_value(name);
}

auto internal_m2n_input_get_digital_value(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.get_digital_value(name);
}

auto internal_m2n_input_is_pressed(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_pressed(name);
}

auto internal_m2n_input_is_released(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_released(name);
}

auto internal_m2n_input_is_down(const std::string& name) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.is_down(name);
}

auto internal_m2n_input_is_key_pressed(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_pressed(code);
}

auto internal_m2n_input_is_key_released(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_released(code);
}

auto internal_m2n_input_is_key_down(input::key_code code) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_keyboard().is_down(code);
}

auto internal_m2n_input_is_mouse_button_pressed(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_pressed(button);
}

auto internal_m2n_input_is_mouse_button_released(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_released(button);
}

auto internal_m2n_input_is_mouse_button_down(int32_t button) -> bool
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    return input.manager.get_mouse().is_down(button);
}

auto internal_m2n_input_get_mouse_position() -> math::vec2
{
    auto& ctx = engine::context();
    auto& input = ctx.get_cached<input_system>();
    auto coord = input.manager.get_mouse().get_position();
    return {coord.x, coord.y};
}

//-------------------------------------------------

auto internal_m2n_physics_ray_cast(dotnetpp_backend::managed_interface::raycast_hit* hit,
                                   const math::vec3& origin,
                                   const math::vec3& direction,
                                   float max_distance,
                                   int layer_mask,
                                   bool query_sensors) -> bool
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hit = physics.ray_cast(origin, direction, max_distance, layer_mask, query_sensors);

    using converter = dotnet::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_ray_cast_all(const math::vec3& origin,
                                       const math::vec3& direction,
                                       float max_distance,
                                       int layer_mask,
                                       bool query_sensors) -> hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);

    hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit> hits;

    using converter = dotnet::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.normal);
        hit.distance = ray_hit.distance;
    }

    return hits;
}

auto internal_m2n_physics_sphere_cast(dotnetpp_backend::managed_interface::raycast_hit* hit,
                                      const math::vec3& origin,
                                      const math::vec3& direction,
                                      float radius,
                                      float max_distance,
                                      int layer_mask,
                                      bool query_sensors) -> bool
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hit = physics.sphere_cast(origin, direction, radius, max_distance, layer_mask, query_sensors);

    using converter = dotnet::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_sphere_cast_all(const math::vec3& origin,
                                          const math::vec3& direction,
                                          float radius,
                                          float max_distance,
                                          int layer_mask,
                                          bool query_sensors) -> hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);

    hpp::small_vector<dotnetpp_backend::managed_interface::raycast_hit> hits;

    using converter = dotnet::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, dotnetpp_backend::managed_interface::vector3>(ray_hit.normal);
        hit.distance = ray_hit.distance;
    }

    return hits;
}

auto internal_m2n_physics_sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
    -> physics_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto hits = physics.sphere_overlap(origin, radius, layer_mask, query_sensors);

    return hits;
}

//-------------------------------------------------

auto internal_m2n_audio_source_get_loop(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_looping();
    }

    return {};
}

//-------------------------------------------------

void internal_m2n_utils_set_ik_posiiton_ccd(entt::entity id,
                                            const math::vec3& target,
                                            const math::vec3& pole,
                                            int num_bones_in_chain,
                                            int max_iterations,
                                            float threshold)
{
    auto e = get_entity_from_id(id);

    ik_set_position_ccd(e, target, pole, num_bones_in_chain, max_iterations, threshold);
}

void internal_m2n_utils_set_ik_posiiton_fabrik(entt::entity id,
                                               const math::vec3& target,
                                               const math::vec3& pole,
                                               int num_bones_in_chain,
                                               int max_iterations,
                                               float threshold)
{
    auto e = get_entity_from_id(id);

    ik_set_position_fabrik(e, target, pole, num_bones_in_chain, max_iterations, threshold);
}

void internal_m2n_utils_set_ik_posiiton_two_bone(entt::entity id,
                                                 const math::vec3& target,
                                                 const math::vec3& pole,
                                                 float weight,
                                                 float soften)
{
    auto e = get_entity_from_id(id);

    ik_set_position_two_bone(e, target, pole, weight, soften);
}

void internal_m2n_utils_set_ik_look_at_posiiton(entt::entity id, const math::vec3& target, float weight)
{
    auto e = get_entity_from_id(id);

    ik_look_at_position(e, target, weight);
}

void internal_m2n_audio_source_set_loop(entt::entity id, bool loop)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_loop(loop);
    }
}

auto internal_m2n_audio_source_get_volume(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_volume();
    }

    return {};
}

void internal_m2n_audio_source_set_volume(entt::entity id, float volume)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_volume(volume);
    }
}

auto internal_m2n_audio_source_get_pitch(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_pitch();
    }

    return {};
}

void internal_m2n_audio_source_set_pitch(entt::entity id, float pitch)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_pitch(pitch);
    }
}

auto internal_m2n_audio_source_get_volume_rolloff(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_volume_rolloff();
    }

    return {};
}

void internal_m2n_audio_source_set_volume_rolloff(entt::entity id, float rolloff)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_volume_rolloff(rolloff);
    }
}

auto internal_m2n_audio_source_get_min_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_range().min;
    }

    return {};
}

void internal_m2n_audio_source_set_min_distance(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto range = comp->get_range();
        range.min = distance;
        comp->set_range(range);
    }
}

auto internal_m2n_audio_source_get_max_distance(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_range().max;
    }

    return {};
}

void internal_m2n_audio_source_set_max_distance(entt::entity id, float distance)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto range = comp->get_range();
        range.max = distance;
        comp->set_range(range);
    }
}

auto internal_m2n_audio_source_get_mute(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_muted();
    }

    return {};
}

void internal_m2n_audio_source_set_mute(entt::entity id, bool mute)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_mute(mute);
    }
}

auto internal_m2n_audio_source_get_time(entt::entity id) -> float
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return float(comp->get_playback_position().count());
    }

    return {};
}

void internal_m2n_audio_source_set_time(entt::entity id, float seconds)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->set_playback_position(audio::duration_t(seconds));
    }
}

auto internal_m2n_audio_source_is_playing(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_playing();
    }

    return {};
}

auto internal_m2n_audio_source_is_paused(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->is_paused();
    }

    return {};
}

void internal_m2n_audio_source_play(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->play();
    }
}

void internal_m2n_audio_source_stop(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->stop();
    }
}

void internal_m2n_audio_source_pause(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->pause();
    }
}

void internal_m2n_audio_source_resume(entt::entity id)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        comp->resume();
    }
}

auto internal_m2n_audio_source_get_audio_clip(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        return comp->get_clip().uid();
    }

    return {};
}

void internal_m2n_audio_source_set_audio_clip(entt::entity id, hpp::uuid uid)
{
    if(auto comp = safe_get_component<audio_source_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<audio_clip>(uid);
        comp->set_clip(asset);
    }
}

//--------------------------------------------------

//-------------------------------------------------------------------------
/*

  _    _ _____   _____           _____  _    _ __  __ ______ _   _ _______
 | |  | |_   _| |  __ \   /\    / ____|  |  | |  \/  |  ____| \ | |__   __|
 | |  | | | |   | |  | | /  \  | |    | |  | | \  / | |__  |  \| |  | |
 | |  | | | |   | |  | |/ /\ \ | |    | |  | | |\/| |  __| | . ` |  | |
 | |__| |_| |_  | |__| / ____ \| |____| |__| | |  | | |____| |\  |  | |
  \____/|_____| |_____/_/    \_\\_____|\____/|_|  |_|______|_| \_|  |_|


*/
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_get_asset(entt::entity id) -> hpp::uuid
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->asset.uid();
    }

    return {};
}

void internal_m2n_ui_document_set_asset(entt::entity id, const hpp::uuid& uid)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        auto& ctx = engine::context();
        auto& am = ctx.get_cached<asset_manager>();

        auto asset = am.get_asset<ui_tree>(uid);
        comp->asset = asset;
    }
}

auto internal_m2n_ui_document_is_loaded(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->is_loaded();
    }

    return false;
}

auto internal_m2n_ui_document_is_enabled(entt::entity id) -> bool
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        return comp->is_enabled();
    }

    return false;
}

void internal_m2n_ui_document_set_enabled(entt::entity id, bool enabled)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        comp->set_enabled(enabled);
    }
}
void internal_m2n_ui_document_close(entt::entity id)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        if(comp->document)
        {
            comp->document->Close();
            comp->document = nullptr;
        }
    }
}

auto internal_m2n_ui_document_get_title(entt::entity id) -> const std::string&
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        if(comp->document)
        {
            return comp->document->GetTitle();
        }
    }

    static const std::string empty;
    return empty;
}

void internal_m2n_ui_document_set_title(entt::entity id, const std::string& title)
{
    if(auto comp = safe_get_component<ui_document_component>(id))
    {
        if(comp->document)
        {
            comp->document->SetTitle(title);
        }
    }
}

//-------------------------------------------------------------------------
/*

  ______ _      ______ __  __ ______ _   _ _______
 |  ____| |    |  ____|  \/  |  ____| \ | |__   __|
 | |__  | |    | |__  | \  / | |__  |  \| |  | |
 |  __| | |    |  __| | |\/| |  __| | . ` |  | |
 | |____| |____| |____| |  | | |____| |\  |  | |
 |______|______|______|_|  |_|______|_| \_|  |_|


*/
//-------------------------------------------------------------------------

// Helper function to get UI element safely
auto get_ui_element_safe(entt::entity entity_id, const std::string& element_id) -> Rml::Element*
{
    if(auto comp = safe_get_component<ui_document_component>(entity_id))
    {
        if(comp->document)
        {
            return comp->document->GetElementById(element_id);
        }
    }
    return nullptr;
}


//-------------------------------------------------------------------------
/*

  ______ _    _ ______ _   _ _______    _____          _      _      ____          _____ _  __ _____ 
 |  ____| |  | |  ____| \ | |__   __|  / ____|   /\   | |    | |    |  _ \   /\   / ____| |/ // ____|
 | |__  | |  | | |__  |  \| |  | |    | |       /  \  | |    | |    | |_) | /  \ | |    | ' /| (___  
 |  __| | |  | |  __| | . ` |  | |    | |      / /\ \ | |    | |    |  _ < / /\ \| |    |  <  \___ \ 
 | |____| |__| | |____| |\  |  | |    | |____ / ____ \| |____| |____| |_) / ____ \ |____| . \ ____) |
 |______|\____/|______|_| \_|  |_|     \_____/_/    \_\______|______|____/_/    \_\_____|_|\_\_____/ 


*/
//-------------------------------------------------------------------------

template<typename T>
void dispatch_ui_event_to_manager(const T& event_data)
{
    try
    {
        const auto& ctx = engine::context();
        const auto& script_cache = ctx.get_cached<script_system>().get_cache();

        if(!script_cache.ui_dispatch_event_method.valid())
        {
            APPLOG_ERROR("UIEventManager.InternalDispatchEvent method not found");
            return;
        }

        auto method_invoker =
            dotnet::make_method_invoker<void(const T&)>(script_cache.ui_dispatch_event_method, false);
        method_invoker(event_data);
    }
    catch (const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception dispatching UI event: {}", e.what());
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error dispatching UI event: {}", e.what());
    }
}



// UI Event Type Classification
enum class ui_event_type
{
    unknown,
    key,
    textinput,
    pointer,
    change,
    value
};

// Determine UI event type for efficient dispatch
auto get_ui_event_type(const Rml::Event& event) -> ui_event_type
{
    const auto event_id = event.GetId();
    
    // Check key events first (most common check)
    if (event_id == Rml::EventId::Keydown || event_id == Rml::EventId::Keyup)
    {
        return ui_event_type::key;
    }
    
    // Check text input events
    if (event_id == Rml::EventId::Textinput)
    {
        return ui_event_type::textinput;
    }
    
    // Check pointer events
    if (event_id == Rml::EventId::Click || event_id == Rml::EventId::Mousedown || event_id == Rml::EventId::Mouseup ||
        event_id == Rml::EventId::Mousemove || event_id == Rml::EventId::Mouseover || event_id == Rml::EventId::Mouseout ||
        event_id == Rml::EventId::Mousescroll || event_id == Rml::EventId::Dblclick || event_id == Rml::EventId::Drag ||
        event_id == Rml::EventId::Dragstart || event_id == Rml::EventId::Dragover || event_id == Rml::EventId::Dragdrop)
    {
        return ui_event_type::pointer;
    }
    
    // Check change events (need to examine the event more closely)
    if (event_id == Rml::EventId::Change)
    {
        auto value_str = event.GetParameter<std::string>("value", "");
        if (!value_str.empty())
        {
            // If the element doesn't have min/max attributes, it's likely a text input or similar (change event)
            if (auto* element = event.GetCurrentElement())
            {
                if (!element->HasAttribute("min") && !element->HasAttribute("max"))
                {
                    return ui_event_type::change;
                }
                else
                {
                    // Has min/max attributes, likely a slider (value event)
                    return ui_event_type::value;
                }
            }
        }
    }
    
    return ui_event_type::unknown;
}


// Fill base event data common to all event types
void fill_base_event_data(dotnetpp_backend::managed_interface::ui_event_base& event_data, 
                         const Rml::Event& event, 
                         Rml::Element* target_element, 
                         Rml::Element* current_element)
{
    event_data.native_ptr = reinterpret_cast<std::intptr_t>(&event);
    event_data.target_element_id = target_element->GetId();
    event_data.target_element_ptr = reinterpret_cast<std::intptr_t>(target_element);
    event_data.current_element_id = current_element->GetId();
    event_data.current_element_ptr = reinterpret_cast<std::intptr_t>(current_element);
    event_data.event_type = event.GetType();
    event_data.phase = static_cast<int>(event.GetPhase());
}

// Dispatch key event to C# UIEventManager
void dispatch_key_event_to_manager(const Rml::Event& event, 
                                  Rml::Element* target_element, 
                                  Rml::Element* current_element)
{
     
    // Create key event data
    dotnetpp_backend::managed_interface::ui_key_event key_event_data;
    fill_base_event_data(key_event_data, event, target_element, current_element);
    
    // Fill key-specific data based on actual RmlUi parameters
    auto key_identifier = event.GetParameter<int>("key_identifier", 0);
    key_event_data.key_code = RmlEngine::convert_rml_key_to_input(static_cast<Rml::Input::KeyIdentifier>(key_identifier));
    key_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    key_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    key_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    key_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    
    dispatch_ui_event_to_manager(key_event_data);
}

// Dispatch pointer event to C# UIEventManager
void dispatch_pointer_event_to_manager(const Rml::Event& event, 
                                      Rml::Element* target_element, 
                                      Rml::Element* current_element)
{

    // Create pointer event data
    dotnetpp_backend::managed_interface::ui_pointer_event pointer_event_data;
    fill_base_event_data(pointer_event_data, event, target_element, current_element);
    
    // Fill pointer-specific data based on actual RmlUi parameters
    pointer_event_data.x = event.GetParameter<float>("mouse_x", 0.0f);
    pointer_event_data.y = event.GetParameter<float>("mouse_y", 0.0f);
    pointer_event_data.button = event.GetParameter<int>("button", -1);
    pointer_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    pointer_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    pointer_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    pointer_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    pointer_event_data.delta_x = event.GetParameter<float>("wheel_delta_x", 0.0f);
    pointer_event_data.delta_y = event.GetParameter<float>("wheel_delta_y", 0.0f);
    
    dispatch_ui_event_to_manager(pointer_event_data);

}

// Dispatch text input event to C# UIEventManager
void dispatch_textinput_event_to_manager(const Rml::Event& event, 
                                         Rml::Element* target_element, 
                                         Rml::Element* current_element)
{
    // Create text input event data
    dotnetpp_backend::managed_interface::ui_textinput_event textinput_event_data;
    fill_base_event_data(textinput_event_data, event, target_element, current_element);

          
    // Fill text input-specific data
    textinput_event_data.text = event.GetParameter<std::string>("text", "");
    textinput_event_data.ctrl_key = event.GetParameter<int>("ctrl_key", 0) > 0;
    textinput_event_data.shift_key = event.GetParameter<int>("shift_key", 0) > 0;
    textinput_event_data.alt_key = event.GetParameter<int>("alt_key", 0) > 0;
    textinput_event_data.meta_key = event.GetParameter<int>("meta_key", 0) > 0;
    
    
    dispatch_ui_event_to_manager(textinput_event_data);
}

// Dispatch value event to C# UIEventManager
void dispatch_value_event_to_manager(const Rml::Event& event, 
                                     Rml::Element* target_element, 
                                     Rml::Element* current_element)
{
   
    // Create value event data
    dotnetpp_backend::managed_interface::ui_slider_event value_event_data;
    fill_base_event_data(value_event_data, event, target_element, current_element);
    
    // Fill value-specific data
    value_event_data.value = event.GetParameter<float>("value", 0);

    if(auto* slider_element = event.GetCurrentElement())
    {
        value_event_data.min_value = slider_element->GetAttribute<float>("min", 0);
        value_event_data.max_value = slider_element->GetAttribute<float>("max", 0);
        value_event_data.step = slider_element->GetAttribute<float>("step", 0);
    }

    dispatch_ui_event_to_manager(value_event_data);
}

// Dispatch change event to C# UIEventManager
void dispatch_change_event_to_manager(const Rml::Event& event, 
                                      Rml::Element* target_element, 
                                      Rml::Element* current_element)
{
   
    // Create change event data
    dotnetpp_backend::managed_interface::ui_change_event change_event_data;
    fill_base_event_data(change_event_data, event, target_element, current_element);
    
    // Fill change-specific data
    change_event_data.value = event.GetParameter<std::string>("value", "");

    dispatch_ui_event_to_manager(change_event_data);

}

// Dispatch base event to C# UIEventManager (fallback)
void dispatch_base_event_to_manager(const Rml::Event& event, 
                                   Rml::Element* target_element, 
                                   Rml::Element* current_element)
{
    dotnetpp_backend::managed_interface::ui_event_base event_data;
    fill_base_event_data(event_data, event, target_element, current_element);
    dispatch_ui_event_to_manager(event_data);
}

// Global event listener that dispatches all UI events to C# UIEventManager
class ui_global_event_listener : public Rml::EventListener
{
    Rml::Event* current_event_ = nullptr;
public:
    void ProcessEvent(Rml::Event& event) override
    {
        current_event_ = &event;
        try
        {
            // Get event information
            auto* target_element = event.GetTargetElement();
            if (!target_element)
            {
                return;
            }

            auto* current_element = event.GetCurrentElement();
            if (!current_element)
            {
                return;
            }

            // Determine event type and dispatch accordingly using efficient switch
            const auto event_type = get_ui_event_type(event);
            
            switch (event_type)
            {
                case ui_event_type::key:
                    dispatch_key_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::textinput:
                    dispatch_textinput_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::pointer:
                    dispatch_pointer_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::change:
                    dispatch_change_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::value:
                    dispatch_value_event_to_manager(event, target_element, current_element);
                    break;
                    
                case ui_event_type::unknown:
                default:
                    // Fallback to base event for unknown types
                    dispatch_base_event_to_manager(event, target_element, current_element);
                    break;
            }
        }
        catch (const std::exception& e)
        {
            APPLOG_ERROR("Error processing UI event: {}", e.what());
        }
        current_event_ = nullptr;
    }

    // Allow access to current event for propagation control
    auto get_current_event() const -> Rml::Event*
    {
        return current_event_;
    }
};
    
// Global event listener instance
ui_global_event_listener g_ui_global_listener;

// Ensure a native event listener is attached to the element for the given event type
void internal_m2n_ui_ensure_native_event_listener(std::intptr_t element_ptr, const std::string& event_type)
{
    if (element_ptr == 0)
    {
        return;
    }

    try
    {        
        auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
        
        // Add event listener to the element
        // Note: RmlUi handles duplicate listeners internally, so it's safe to call this multiple times
        element->AddEventListener(event_type, &g_ui_global_listener);
        
        APPLOG_TRACE("Ensured native UI event listener: element='{}', event='{}'", element->GetId(), event_type);
    }
    catch (const dotnet::exception& e)
    {
        APPLOG_ERROR("C# exception ensuring native UI event listener: {}", e.what());
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error ensuring native UI event listener: {}", e.what());
    }
}

// Stop event propagation - called from C# UIEventBase.StopPropagation()
void internal_m2n_ui_stop_propagation(std::intptr_t native_ptr)
{
    try
    {

        auto* current_event = g_ui_global_listener.get_current_event();
        if (current_event && current_event == reinterpret_cast<Rml::Event*>(native_ptr))
        {
            current_event->StopPropagation();
        }
        else
        {
            APPLOG_WARNING("No current UI event to stop propagation on");
        }

    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error stopping UI event propagation: {}", e.what());
    }
}

// Stop immediate event propagation - called from C# UIEventBase.StopImmediatePropagation()
void internal_m2n_ui_stop_immediate_propagation(std::intptr_t native_ptr)
{
    try
    {
        auto* current_event = g_ui_global_listener.get_current_event();
        if (current_event)
        {
            current_event->StopImmediatePropagation();
        }
        else
        {
            APPLOG_WARNING("No current UI event to stop immediate propagation on");
        }
    }
    catch (const std::exception& e)
    {
        APPLOG_ERROR("Error stopping UI event immediate propagation: {}", e.what());
    }
}

//-------------------------------------------------------------------------
/*

  _    _ _____  __          _______             _____  _____  ______ _____    _____ 
 | |  | |_   _| \ \        / /  __ \     /\    |  __ \|  __ \|  ____|  __ \  / ____|
 | |  | | | |    \ \  /\  / /| |__) |   /  \   | |__) | |__) | |__  | |__) || (___  
 | |  | | | |     \ \/  \/ / |  _  /   / /\ \  |  ___/|  ___/|  __| |  _  /  \___ \ 
 | |__| |_| |_     \  /\  /  | | \ \  / ____ \ | |    | |    | |____| | \ \  ____) |
  \____/|_____|     \/  \/   |_|  \_\/_/    \_\|_|    |_|    |______|_|  \_\|_____/ 


*/
//-------------------------------------------------------------------------

// Helper function to validate element pointer by checking if it exists in the owner entity's UI document
auto validate_ui_element_wrapper(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    if (element_ptr == 0)
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    
    // Check if this element exists in the owner entity's UI document
    if (auto comp = safe_get_component<ui_document_component>(owner_entity))
    {
        if (comp->document)
        {
            // Check if this element belongs to this document
            return comp->document->Contains(element);
        }
    }
    
    return false;
}

// Helper function to validate document pointer by checking if it matches the owner entity's UI component
auto validate_ui_document_wrapper(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    if (document_ptr == 0)
    {
        return false;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    
    // Check if this document exists in the owner entity's UI component
    if (auto comp = safe_get_component<ui_document_component>(owner_entity))
    {
        if (comp->document && comp->document == document)
        {
            return true;
        }
    }
    
    return false;
}

//-------------------------------------------------------------------------
// UI Document Wrapper Functions
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_get_wrapper(entt::entity entity_id) -> std::intptr_t
{
    if (auto comp = safe_get_component<ui_document_component>(entity_id))
    {
        if (comp->document)
        {
            return reinterpret_cast<std::intptr_t>(comp->document);
        }
    }
    return 0;
}

auto internal_m2n_ui_document_get_element_wrapper_by_id(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& element_id) -> std::intptr_t
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return 0;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    auto* element = document->GetElementById(element_id);
    return reinterpret_cast<std::intptr_t>(element);
}

auto internal_m2n_ui_document_query_selector_wrapper(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& selector) -> std::intptr_t
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return 0;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    auto element = document->QuerySelector(selector);
    if (element)
    {
        return reinterpret_cast<std::intptr_t>(element);
    }
    
    return 0;
}

// Get element ID from element pointer
auto internal_m2n_ui_element_wrapper_get_id(std::intptr_t element_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetId();
}

//-------------------------------------------------------------------------
// UI Document Wrapper Methods
//-------------------------------------------------------------------------

auto internal_m2n_ui_document_wrapper_is_valid(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    return validate_ui_document_wrapper(document_ptr, owner_entity);
}

auto internal_m2n_ui_document_wrapper_get_title(std::intptr_t document_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return "";
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    return document->GetTitle();
}

void internal_m2n_ui_document_wrapper_set_title(std::intptr_t document_ptr, entt::entity owner_entity, const std::string& title)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->SetTitle(title);
}

auto internal_m2n_ui_document_wrapper_is_visible(std::intptr_t document_ptr, entt::entity owner_entity) -> bool
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return false;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    return document->IsVisible();
}

void internal_m2n_ui_document_wrapper_show(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Show();
}

void internal_m2n_ui_document_wrapper_hide(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Hide();
}

void internal_m2n_ui_document_wrapper_close(std::intptr_t document_ptr, entt::entity owner_entity)
{
    if (!validate_ui_document_wrapper(document_ptr, owner_entity))
    {
        return;
    }
    
    auto* document = reinterpret_cast<Rml::ElementDocument*>(document_ptr);
    document->Close();
}

//-------------------------------------------------------------------------
// UI Element Wrapper Methods  
//-------------------------------------------------------------------------

auto internal_m2n_ui_element_wrapper_is_valid(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    return validate_ui_element_wrapper(element_ptr, owner_entity);
}

auto internal_m2n_ui_element_wrapper_get_inner_rml(std::intptr_t element_ptr, entt::entity owner_entity) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetInnerRML();
}

void internal_m2n_ui_element_wrapper_set_inner_rml(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& rml)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    auto* element_text = rmlui_dynamic_cast<Rml::ElementText*>(element);

    if(!element_text)
    {
        if(auto* first_child = element->GetFirstChild())
        {
            element_text = rmlui_dynamic_cast<Rml::ElementText*>(first_child);
        }
    }

    if(element_text)
    {
        element_text->SetText(rml);
    }
    else
    {
        auto current_rml = element->GetInnerRML();
        if(current_rml != rml)
        {
            element->SetInnerRML(rml);
        }
    }
}

auto internal_m2n_ui_element_wrapper_is_visible(std::intptr_t element_ptr, entt::entity owner_entity) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->IsVisible();
}

void internal_m2n_ui_element_wrapper_set_visible(std::intptr_t element_ptr, entt::entity owner_entity, bool visible)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    if (visible)
    {
        element->SetProperty("display", "block");
    }
    else
    {
        element->SetProperty("display", "none");
    }
}

auto internal_m2n_ui_element_wrapper_get_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name) -> std::string
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return "";
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->GetAttribute<Rml::String>(attribute_name, "");
}

void internal_m2n_ui_element_wrapper_set_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name, const std::string& value)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->SetAttribute(attribute_name, value);
}

void internal_m2n_ui_element_wrapper_remove_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->RemoveAttribute(attribute_name);
}

auto internal_m2n_ui_element_wrapper_has_attribute(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& attribute_name) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->HasAttribute(attribute_name);
}

void internal_m2n_ui_element_wrapper_set_class(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& class_name, bool activate)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->SetClass(class_name, activate);
}

auto internal_m2n_ui_element_wrapper_is_class_set(std::intptr_t element_ptr, entt::entity owner_entity, const std::string& class_name) -> bool
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return false;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    return element->IsClassSet(class_name);
}

void internal_m2n_ui_element_wrapper_sync_transform_to_entity(std::intptr_t element_ptr, entt::entity owner_entity, entt::entity transform_entity)
{
    auto transform = get_entity_from_id(transform_entity);
    if (!transform)
    {
        return;
    }
    
    auto* transform_comp = transform.try_get<transform_component>();
    if (!transform_comp)
    {
        return;
    }

    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);

    const auto& matrix = transform_comp->get_transform_global().get_matrix();
    std::stringstream s;

    float perspective = transform_comp->get_perspective_global().w;
    if (perspective > 0)
    {
        s << "perspective(" << perspective << "dp) ";
    }
    s << "matrix3d(" << matrix[0][0] << ", " << matrix[0][1] << ", " << matrix[0][2] << ", " << matrix[0][3] << ", " << matrix[1][0] << ", " << matrix[1][1] << ", " << matrix[1][2] << ", " << matrix[1][3] << ", " << matrix[2][0] << ", " << matrix[2][1] << ", " << matrix[2][2] << ", " << matrix[2][3] << ", " << matrix[3][0] << ", " << matrix[3][1] << ", " << matrix[3][2] << ", " << matrix[3][3] << ")";
    element->SetProperty("transform", s.str());
}

void internal_m2n_ui_element_wrapper_focus(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Focus();
}

void internal_m2n_ui_element_wrapper_blur(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Blur();
}

void internal_m2n_ui_element_wrapper_click(std::intptr_t element_ptr, entt::entity owner_entity)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->Click();
}

void internal_m2n_ui_element_wrapper_scroll_into_view(std::intptr_t element_ptr, entt::entity owner_entity, bool align_with_top)
{
    if (!validate_ui_element_wrapper(element_ptr, owner_entity))
    {
        return;
    }
    
    auto* element = reinterpret_cast<Rml::Element*>(element_ptr);
    element->ScrollIntoView(align_with_top);
}


//--------------------------------------------------
} // namespace

auto script_system::bind_internal_calls(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Log");
        reg.add_internal_call("internal_m2n_log_trace", dotnet_internal_call(internal_m2n_log_trace));
        reg.add_internal_call("internal_m2n_log_info", dotnet_internal_call(internal_m2n_log_info));
        reg.add_internal_call("internal_m2n_log_warning", dotnet_internal_call(internal_m2n_log_warning));
        reg.add_internal_call("internal_m2n_log_error", dotnet_internal_call(internal_m2n_log_error));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Scene");
        reg.add_internal_call("internal_m2n_load_scene", dotnet_internal_call(internal_m2n_load_scene));
        reg.add_internal_call("internal_m2n_load_scene_uid", dotnet_internal_call(internal_m2n_load_scene_uid));
        reg.add_internal_call("internal_m2n_reload_scene", dotnet_internal_call(internal_m2n_reload_scene));
        reg.add_internal_call("internal_m2n_create_scene", dotnet_internal_call(internal_m2n_create_scene));
        reg.add_internal_call("internal_m2n_destroy_scene", dotnet_internal_call(internal_m2n_destroy_scene));
        reg.add_internal_call("internal_m2n_create_entity", dotnet_internal_call(internal_m2n_create_entity));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_uid));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_key));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid_with_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_uid_with_parent));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key_with_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_key_with_parent));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid_with_position",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_uid_with_position));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key_with_position",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_key_with_position));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid_with_position_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_uid_with_position_parent));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key_with_position_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_key_with_position_parent));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent",
                              dotnet_internal_call(internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent));
        reg.add_internal_call("internal_m2n_clone_entity", dotnet_internal_call(internal_m2n_clone_entity));
        reg.add_internal_call("internal_m2n_destroy_entity", dotnet_internal_call(internal_m2n_destroy_entity));
        reg.add_internal_call("internal_m2n_destroy_entity_immediate",
                              dotnet_internal_call(internal_m2n_destroy_entity_immediate));

        reg.add_internal_call("internal_m2n_is_entity_valid", dotnet_internal_call(internal_m2n_is_entity_valid));
        reg.add_internal_call("internal_m2n_find_entity_by_name", dotnet_internal_call(internal_m2n_find_entity_by_name));
        reg.add_internal_call("internal_m2n_find_entities_by_name", dotnet_internal_call(internal_m2n_find_entities_by_name));
        reg.add_internal_call("internal_m2n_find_entity_by_tag", dotnet_internal_call(internal_m2n_find_entity_by_tag));
        reg.add_internal_call("internal_m2n_find_entities_by_tag", dotnet_internal_call(internal_m2n_find_entities_by_tag));
        reg.add_internal_call("internal_m2n_find_entities_with_component", dotnet_internal_call(internal_m2n_find_entities_with_component));
        reg.add_internal_call("internal_m2n_find_entities_with_components", dotnet_internal_call(internal_m2n_find_entities_with_components));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Entity");
        reg.add_internal_call("internal_m2n_add_component", dotnet_internal_call(internal_m2n_add_component));
        reg.add_internal_call("internal_m2n_get_component", dotnet_internal_call(internal_m2n_get_component));
        reg.add_internal_call("internal_m2n_get_component_in_children",
                              dotnet_internal_call(internal_m2n_get_component_in_children));
        reg.add_internal_call("internal_m2n_has_component", dotnet_internal_call(internal_m2n_has_component));
        reg.add_internal_call("internal_m2n_get_components", dotnet_internal_call(internal_m2n_get_components));
        reg.add_internal_call("internal_m2n_get_components_in_children",
                              dotnet_internal_call(internal_m2n_get_components_in_children));

        reg.add_internal_call("internal_m2n_remove_component_instance",
                              dotnet_internal_call(internal_m2n_remove_component_instance));
        reg.add_internal_call("internal_m2n_remove_component_instance_delay",
                              dotnet_internal_call(internal_m2n_remove_component_instance_delay));

        reg.add_internal_call("internal_m2n_remove_component", dotnet_internal_call(internal_m2n_remove_component));
        reg.add_internal_call("internal_m2n_remove_component_delay",
                              dotnet_internal_call(internal_m2n_remove_component_delay));

        reg.add_internal_call("internal_m2n_get_transform_component",
                              dotnet_internal_call(internal_m2n_get_transform_component));
        reg.add_internal_call("internal_m2n_get_name", dotnet_internal_call(internal_m2n_get_name));
        reg.add_internal_call("internal_m2n_set_name", dotnet_internal_call(internal_m2n_set_name));
        reg.add_internal_call("internal_m2n_get_tag", dotnet_internal_call(internal_m2n_get_tag));
        reg.add_internal_call("internal_m2n_set_tag", dotnet_internal_call(internal_m2n_set_tag));
        reg.add_internal_call("internal_m2n_get_layers", dotnet_internal_call(internal_m2n_get_layers));
        reg.add_internal_call("internal_m2n_set_layers", dotnet_internal_call(internal_m2n_set_layers));

        reg.add_internal_call("internal_m2n_get_active_global", dotnet_internal_call(internal_m2n_get_active_global));
        reg.add_internal_call("internal_m2n_get_active_local", dotnet_internal_call(internal_m2n_get_active_local));
        reg.add_internal_call("internal_m2n_set_active_local", dotnet_internal_call(internal_m2n_set_active_local));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.TransformComponent");
        reg.add_internal_call("internal_m2n_get_children", dotnet_internal_call(internal_m2n_get_children));
        reg.add_internal_call("internal_m2n_get_child", dotnet_internal_call(internal_m2n_get_child));
        reg.add_internal_call("internal_m2n_get_parent", dotnet_internal_call(internal_m2n_get_parent));
        reg.add_internal_call("internal_m2n_set_parent", dotnet_internal_call(internal_m2n_set_parent));

        reg.add_internal_call("internal_m2n_get_position_global", dotnet_internal_call(internal_m2n_get_position_global));
        reg.add_internal_call("internal_m2n_set_position_global", dotnet_internal_call(internal_m2n_set_position_global));
        reg.add_internal_call("internal_m2n_move_by_global", dotnet_internal_call(internal_m2n_move_by_global));

        reg.add_internal_call("internal_m2n_get_position_local", dotnet_internal_call(internal_m2n_get_position_local));
        reg.add_internal_call("internal_m2n_set_position_local", dotnet_internal_call(internal_m2n_set_position_local));
        reg.add_internal_call("internal_m2n_move_by_local", dotnet_internal_call(internal_m2n_move_by_local));

        // Euler
        reg.add_internal_call("internal_m2n_get_rotation_euler_global",
                              dotnet_internal_call(internal_m2n_get_rotation_euler_global));
        reg.add_internal_call("internal_m2n_set_rotation_euler_global",
                              dotnet_internal_call(internal_m2n_set_rotation_euler_global));
        reg.add_internal_call("internal_m2n_rotate_by_euler_global",
                              dotnet_internal_call(internal_m2n_rotate_by_euler_global));

        reg.add_internal_call("internal_m2n_get_rotation_euler_local",
                              dotnet_internal_call(internal_m2n_get_rotation_euler_local));
        reg.add_internal_call("internal_m2n_set_rotation_euler_local",
                              dotnet_internal_call(internal_m2n_set_rotation_euler_local));
        reg.add_internal_call("internal_m2n_rotate_by_euler_local", dotnet_internal_call(internal_m2n_rotate_by_euler_local));

        // Quat
        reg.add_internal_call("internal_m2n_get_rotation_global", dotnet_internal_call(internal_m2n_get_rotation_global));
        reg.add_internal_call("internal_m2n_set_rotation_global", dotnet_internal_call(internal_m2n_set_rotation_global));
        reg.add_internal_call("internal_m2n_rotate_by_global", dotnet_internal_call(internal_m2n_rotate_by_global));

        reg.add_internal_call("internal_m2n_get_rotation_local", dotnet_internal_call(internal_m2n_get_rotation_local));
        reg.add_internal_call("internal_m2n_set_rotation_local", dotnet_internal_call(internal_m2n_set_rotation_local));
        reg.add_internal_call("internal_m2n_rotate_by_local", dotnet_internal_call(internal_m2n_rotate_by_local));

        // Other
        reg.add_internal_call("internal_m2n_rotate_axis_global", dotnet_internal_call(internal_m2n_rotate_axis_global));
        reg.add_internal_call("internal_m2n_look_at", dotnet_internal_call(internal_m2n_look_at));
        reg.add_internal_call("internal_m2n_transform_vector_global",
                              dotnet_internal_call(internal_m2n_transform_vector_global));
        reg.add_internal_call("internal_m2n_inverse_transform_vector_global",
                              dotnet_internal_call(internal_m2n_inverse_transform_vector_global));

        reg.add_internal_call("internal_m2n_transform_direction_global",
                              dotnet_internal_call(internal_m2n_transform_direction_global));
        reg.add_internal_call("internal_m2n_inverse_transform_direction_global",
                              dotnet_internal_call(internal_m2n_inverse_transform_direction_global));

        // Scale
        reg.add_internal_call("internal_m2n_get_scale_global", dotnet_internal_call(internal_m2n_get_scale_global));
        reg.add_internal_call("internal_m2n_set_scale_global", dotnet_internal_call(internal_m2n_set_scale_global));
        reg.add_internal_call("internal_m2n_scale_by_global", dotnet_internal_call(internal_m2n_scale_by_local));

        reg.add_internal_call("internal_m2n_get_scale_local", dotnet_internal_call(internal_m2n_get_scale_local));
        reg.add_internal_call("internal_m2n_set_scale_local", dotnet_internal_call(internal_m2n_set_scale_local));
        reg.add_internal_call("internal_m2n_scale_by_local", dotnet_internal_call(internal_m2n_scale_by_local));

        // Skew
        reg.add_internal_call("internal_m2n_get_skew_global", dotnet_internal_call(internal_m2n_get_skew_global));
        reg.add_internal_call("internal_m2n_set_skew_globa", dotnet_internal_call(internal_m2n_setl_skew_globa));
        reg.add_internal_call("internal_m2n_get_skew_local", dotnet_internal_call(internal_m2n_get_skew_local));
        reg.add_internal_call("internal_m2n_set_skew_local", dotnet_internal_call(internal_m2n_set_skew_local));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.PhysicsComponent");
        reg.add_internal_call("internal_m2n_physics_apply_explosion_force",
                              dotnet_internal_call(internal_m2n_physics_apply_explosion_force));
        reg.add_internal_call("internal_m2n_physics_apply_force", dotnet_internal_call(internal_m2n_physics_apply_force));
        reg.add_internal_call("internal_m2n_physics_apply_torque", dotnet_internal_call(internal_m2n_physics_apply_torque));
        reg.add_internal_call("internal_m2n_physics_get_velocity", dotnet_internal_call(internal_m2n_physics_get_velocity));
        reg.add_internal_call("internal_m2n_physics_set_velocity", dotnet_internal_call(internal_m2n_physics_set_velocity));
        reg.add_internal_call("internal_m2n_physics_get_angular_velocity",
                              dotnet_internal_call(internal_m2n_physics_get_angular_velocity));
        reg.add_internal_call("internal_m2n_physics_set_angular_velocity",
                              dotnet_internal_call(internal_m2n_physics_set_angular_velocity));

        reg.add_internal_call("internal_m2n_physics_get_include_layers",
                              dotnet_internal_call(internal_m2n_physics_get_include_layers));
        reg.add_internal_call("internal_m2n_physics_set_include_layers",
                              dotnet_internal_call(internal_m2n_physics_set_include_layers));
        reg.add_internal_call("internal_m2n_physics_get_exclude_layers",
                              dotnet_internal_call(internal_m2n_physics_get_exclude_layers));
        reg.add_internal_call("internal_m2n_physics_set_exclude_layers",
                              dotnet_internal_call(internal_m2n_physics_set_exclude_layers));
        reg.add_internal_call("internal_m2n_physics_get_collision_layers",
                              dotnet_internal_call(internal_m2n_physics_get_collision_layers));

        reg.add_internal_call("internal_m2n_physics_get_is_sensor",
                              dotnet_internal_call(internal_m2n_physics_get_is_sensor));
        reg.add_internal_call("internal_m2n_physics_set_is_sensor",
                              dotnet_internal_call(internal_m2n_physics_set_is_sensor));
        reg.add_internal_call("internal_m2n_physics_get_mass",
                              dotnet_internal_call(internal_m2n_physics_get_mass));
        reg.add_internal_call("internal_m2n_physics_set_mass",
                              dotnet_internal_call(internal_m2n_physics_set_mass));
        reg.add_internal_call("internal_m2n_physics_get_is_kinematic",
                              dotnet_internal_call(internal_m2n_physics_get_is_kinematic));
        reg.add_internal_call("internal_m2n_physics_set_is_kinematic",
                              dotnet_internal_call(internal_m2n_physics_set_is_kinematic));
        reg.add_internal_call("internal_m2n_physics_get_use_gravity",
                              dotnet_internal_call(internal_m2n_physics_get_use_gravity));
        reg.add_internal_call("internal_m2n_physics_set_use_gravity",
                              dotnet_internal_call(internal_m2n_physics_set_use_gravity));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.CharacterControllerComponent");
        reg.add_internal_call("internal_m2n_cc_move", dotnet_internal_call(internal_m2n_cc_move));
        reg.add_internal_call("internal_m2n_cc_jump", dotnet_internal_call(internal_m2n_cc_jump));
        reg.add_internal_call("internal_m2n_cc_apply_impulse", dotnet_internal_call(internal_m2n_cc_apply_impulse));
        reg.add_internal_call("internal_m2n_cc_warp", dotnet_internal_call(internal_m2n_cc_warp));

        reg.add_internal_call("internal_m2n_cc_get_is_grounded", dotnet_internal_call(internal_m2n_cc_get_is_grounded));
        reg.add_internal_call("internal_m2n_cc_get_can_jump", dotnet_internal_call(internal_m2n_cc_get_can_jump));
        reg.add_internal_call("internal_m2n_cc_get_velocity", dotnet_internal_call(internal_m2n_cc_get_velocity));

        reg.add_internal_call("internal_m2n_cc_get_linear_velocity", dotnet_internal_call(internal_m2n_cc_get_linear_velocity));
        reg.add_internal_call("internal_m2n_cc_set_linear_velocity", dotnet_internal_call(internal_m2n_cc_set_linear_velocity));

        reg.add_internal_call("internal_m2n_cc_get_radius", dotnet_internal_call(internal_m2n_cc_get_radius));
        reg.add_internal_call("internal_m2n_cc_set_radius", dotnet_internal_call(internal_m2n_cc_set_radius));
        reg.add_internal_call("internal_m2n_cc_get_height", dotnet_internal_call(internal_m2n_cc_get_height));
        reg.add_internal_call("internal_m2n_cc_set_height", dotnet_internal_call(internal_m2n_cc_set_height));
        reg.add_internal_call("internal_m2n_cc_get_center", dotnet_internal_call(internal_m2n_cc_get_center));
        reg.add_internal_call("internal_m2n_cc_set_center", dotnet_internal_call(internal_m2n_cc_set_center));
        reg.add_internal_call("internal_m2n_cc_get_step_height", dotnet_internal_call(internal_m2n_cc_get_step_height));
        reg.add_internal_call("internal_m2n_cc_set_step_height", dotnet_internal_call(internal_m2n_cc_set_step_height));
        reg.add_internal_call("internal_m2n_cc_get_slope_limit", dotnet_internal_call(internal_m2n_cc_get_slope_limit));
        reg.add_internal_call("internal_m2n_cc_set_slope_limit", dotnet_internal_call(internal_m2n_cc_set_slope_limit));
        reg.add_internal_call("internal_m2n_cc_get_skin_width", dotnet_internal_call(internal_m2n_cc_get_skin_width));
        reg.add_internal_call("internal_m2n_cc_set_skin_width", dotnet_internal_call(internal_m2n_cc_set_skin_width));
        reg.add_internal_call("internal_m2n_cc_get_gravity_scale", dotnet_internal_call(internal_m2n_cc_get_gravity_scale));
        reg.add_internal_call("internal_m2n_cc_set_gravity_scale", dotnet_internal_call(internal_m2n_cc_set_gravity_scale));
        reg.add_internal_call("internal_m2n_cc_get_terminal_velocity", dotnet_internal_call(internal_m2n_cc_get_terminal_velocity));
        reg.add_internal_call("internal_m2n_cc_set_terminal_velocity", dotnet_internal_call(internal_m2n_cc_set_terminal_velocity));
        reg.add_internal_call("internal_m2n_cc_get_linear_damping", dotnet_internal_call(internal_m2n_cc_get_linear_damping));
        reg.add_internal_call("internal_m2n_cc_set_linear_damping", dotnet_internal_call(internal_m2n_cc_set_linear_damping));

        reg.add_internal_call("internal_m2n_cc_get_include_layers", dotnet_internal_call(internal_m2n_cc_get_include_layers));
        reg.add_internal_call("internal_m2n_cc_set_include_layers", dotnet_internal_call(internal_m2n_cc_set_include_layers));
        reg.add_internal_call("internal_m2n_cc_get_exclude_layers", dotnet_internal_call(internal_m2n_cc_get_exclude_layers));
        reg.add_internal_call("internal_m2n_cc_set_exclude_layers", dotnet_internal_call(internal_m2n_cc_set_exclude_layers));
        reg.add_internal_call("internal_m2n_cc_get_collision_layers", dotnet_internal_call(internal_m2n_cc_get_collision_layers));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationComponent");
        reg.add_internal_call("internal_m2n_animation_blend", dotnet_internal_call(internal_m2n_animation_blend));
        reg.add_internal_call("internal_m2n_animation_play", dotnet_internal_call(internal_m2n_animation_play));
        reg.add_internal_call("internal_m2n_animation_pause", dotnet_internal_call(internal_m2n_animation_pause));
        reg.add_internal_call("internal_m2n_animation_resume", dotnet_internal_call(internal_m2n_animation_resume));
        reg.add_internal_call("internal_m2n_animation_stop", dotnet_internal_call(internal_m2n_animation_stop));
        reg.add_internal_call("internal_m2n_animation_set_speed", dotnet_internal_call(internal_m2n_animation_set_speed));
        reg.add_internal_call("internal_m2n_animation_get_speed", dotnet_internal_call(internal_m2n_animation_get_speed));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.CameraComponent");
        reg.add_internal_call("internal_m2n_camera_screen_point_to_ray",
                              dotnet_internal_call(internal_m2n_camera_screen_point_to_ray));
        reg.add_internal_call("internal_m2n_camera_screen_point_to_world_2d",
                              dotnet_internal_call(internal_m2n_camera_screen_point_to_world_2d));
        reg.add_internal_call("internal_m2n_camera_screen_point_to_world",
                              dotnet_internal_call(internal_m2n_camera_screen_point_to_world));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.ModelComponent");
        reg.add_internal_call("internal_m2n_model_get_enabled", dotnet_internal_call(internal_m2n_model_get_enabled));
        reg.add_internal_call("internal_m2n_model_set_enabled", dotnet_internal_call(internal_m2n_model_set_enabled));
        reg.add_internal_call("internal_m2n_model_get_shared_material",
                              dotnet_internal_call(internal_m2n_model_get_shared_material));
        reg.add_internal_call("internal_m2n_model_get_shared_material_count",
                              dotnet_internal_call(internal_m2n_model_get_shared_material_count));
        reg.add_internal_call("internal_m2n_model_set_shared_material",
                              dotnet_internal_call(internal_m2n_model_set_shared_material));
        reg.add_internal_call("internal_m2n_model_set_material_instance",
                              dotnet_internal_call(internal_m2n_model_set_material_instance));
        reg.add_internal_call("internal_m2n_model_get_material_instance",
                              dotnet_internal_call(internal_m2n_model_get_material_instance));
        reg.add_internal_call("internal_m2n_model_get_material_instance_count",
                              dotnet_internal_call(internal_m2n_model_get_material_instance_count));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.ParticleEmitterComponent");
        reg.add_internal_call("internal_m2n_particle_emitter_get_enabled", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_enabled));
        reg.add_internal_call("internal_m2n_particle_emitter_set_enabled", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_enabled));
        reg.add_internal_call("internal_m2n_particle_emitter_get_max_particles", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_max_particles));
        reg.add_internal_call("internal_m2n_particle_emitter_set_max_particles", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_max_particles));
        reg.add_internal_call("internal_m2n_particle_emitter_get_shape", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_shape));
        reg.add_internal_call("internal_m2n_particle_emitter_set_shape", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_shape));
        reg.add_internal_call("internal_m2n_particle_emitter_get_direction", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_direction));
        reg.add_internal_call("internal_m2n_particle_emitter_set_direction", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_direction));
        reg.add_internal_call("internal_m2n_particle_emitter_get_gravity_scale", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_gravity_scale));
        reg.add_internal_call("internal_m2n_particle_emitter_set_gravity_scale", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_gravity_scale));
        reg.add_internal_call("internal_m2n_particle_emitter_get_emission_rate", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_emission_rate));
        reg.add_internal_call("internal_m2n_particle_emitter_set_emission_rate", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_emission_rate));
        reg.add_internal_call("internal_m2n_particle_emitter_get_temporal_motion", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_temporal_motion));
        reg.add_internal_call("internal_m2n_particle_emitter_set_temporal_motion", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_temporal_motion));
        reg.add_internal_call("internal_m2n_particle_emitter_get_velocity_damping", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_velocity_damping));
        reg.add_internal_call("internal_m2n_particle_emitter_set_velocity_damping", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_velocity_damping));
        reg.add_internal_call("internal_m2n_particle_emitter_get_opacity", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_opacity));
        reg.add_internal_call("internal_m2n_particle_emitter_set_opacity", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_opacity));
        reg.add_internal_call("internal_m2n_particle_emitter_get_force_over_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_force_over_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_set_force_over_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_force_over_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_get_emission_shape_scale", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_emission_shape_scale));
        reg.add_internal_call("internal_m2n_particle_emitter_set_emission_shape_scale", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_emission_shape_scale));
        reg.add_internal_call("internal_m2n_particle_emitter_get_emission_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_emission_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_set_emission_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_emission_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_get_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_set_lifetime", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_lifetime));
        reg.add_internal_call("internal_m2n_particle_emitter_get_position_easing", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_position_easing));
        reg.add_internal_call("internal_m2n_particle_emitter_set_position_easing", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_position_easing));
        reg.add_internal_call("internal_m2n_particle_emitter_get_num_particles", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_num_particles));
        reg.add_internal_call("internal_m2n_particle_emitter_is_playing", 
                              dotnet_internal_call(internal_m2n_particle_emitter_is_playing));
        reg.add_internal_call("internal_m2n_particle_emitter_is_paused", 
                              dotnet_internal_call(internal_m2n_particle_emitter_is_paused));
        reg.add_internal_call("internal_m2n_particle_emitter_get_texture", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_texture));
        reg.add_internal_call("internal_m2n_particle_emitter_set_texture", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_texture));
        reg.add_internal_call("internal_m2n_particle_emitter_play", 
                              dotnet_internal_call(internal_m2n_particle_emitter_play));
        reg.add_internal_call("internal_m2n_particle_emitter_stop", 
                              dotnet_internal_call(internal_m2n_particle_emitter_stop));
        reg.add_internal_call("internal_m2n_particle_emitter_stop_and_reset", 
                              dotnet_internal_call(internal_m2n_particle_emitter_stop_and_reset));
        reg.add_internal_call("internal_m2n_particle_emitter_pause", 
                              dotnet_internal_call(internal_m2n_particle_emitter_pause));
        reg.add_internal_call("internal_m2n_particle_emitter_resume", 
                              dotnet_internal_call(internal_m2n_particle_emitter_resume));
        reg.add_internal_call("internal_m2n_particle_emitter_reset_emitter", 
                              dotnet_internal_call(internal_m2n_particle_emitter_reset_emitter));
        reg.add_internal_call("internal_m2n_particle_emitter_get_loop", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_loop));
        reg.add_internal_call("internal_m2n_particle_emitter_set_loop", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_loop));
        reg.add_internal_call("internal_m2n_particle_emitter_get_blend_mode", 
                              dotnet_internal_call(internal_m2n_particle_emitter_get_blend_mode));
        reg.add_internal_call("internal_m2n_particle_emitter_set_blend_mode", 
                              dotnet_internal_call(internal_m2n_particle_emitter_set_blend_mode));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.TextComponent");
        reg.add_internal_call("internal_m2n_text_get_text", dotnet_internal_call(internal_m2n_text_get_text));
        reg.add_internal_call("internal_m2n_text_set_text", dotnet_internal_call(internal_m2n_text_set_text));
        reg.add_internal_call("internal_m2n_text_get_buffer_type", dotnet_internal_call(internal_m2n_text_get_buffer_type));
        reg.add_internal_call("internal_m2n_text_set_buffer_type", dotnet_internal_call(internal_m2n_text_set_buffer_type));
        reg.add_internal_call("internal_m2n_text_get_overflow_type",
                              dotnet_internal_call(internal_m2n_text_get_overflow_type));
        reg.add_internal_call("internal_m2n_text_set_overflow_type",
                              dotnet_internal_call(internal_m2n_text_set_overflow_type));
        reg.add_internal_call("internal_m2n_text_get_font", dotnet_internal_call(internal_m2n_text_get_font));
        reg.add_internal_call("internal_m2n_text_set_font", dotnet_internal_call(internal_m2n_text_set_font));

        reg.add_internal_call("internal_m2n_text_get_font_size", dotnet_internal_call(internal_m2n_text_get_font_size));
        reg.add_internal_call("internal_m2n_text_set_font_size", dotnet_internal_call(internal_m2n_text_set_font_size));
        reg.add_internal_call("internal_m2n_text_get_render_font_size",
                              dotnet_internal_call(internal_m2n_text_get_render_font_size));

        reg.add_internal_call("internal_m2n_text_get_auto_size", dotnet_internal_call(internal_m2n_text_get_auto_size));
        reg.add_internal_call("internal_m2n_text_set_auto_size", dotnet_internal_call(internal_m2n_text_set_auto_size));

        reg.add_internal_call("internal_m2n_text_get_auto_size_range",
                              dotnet_internal_call(internal_m2n_text_get_auto_size_range));
        reg.add_internal_call("internal_m2n_text_set_auto_size_range",
                              dotnet_internal_call(internal_m2n_text_set_auto_size_range));

        reg.add_internal_call("internal_m2n_text_get_area", dotnet_internal_call(internal_m2n_text_get_area));
        reg.add_internal_call("internal_m2n_text_set_area", dotnet_internal_call(internal_m2n_text_set_area));
        reg.add_internal_call("internal_m2n_text_get_render_area", dotnet_internal_call(internal_m2n_text_get_render_area));

        reg.add_internal_call("internal_m2n_text_get_is_rich_text", dotnet_internal_call(internal_m2n_text_get_is_rich_text));
        reg.add_internal_call("internal_m2n_text_set_is_rich_text", dotnet_internal_call(internal_m2n_text_set_is_rich_text));

        reg.add_internal_call("internal_m2n_text_get_alignment", dotnet_internal_call(internal_m2n_text_get_alignment));
        reg.add_internal_call("internal_m2n_text_set_alignment", dotnet_internal_call(internal_m2n_text_set_alignment));

        reg.add_internal_call("internal_m2n_text_get_bounds", dotnet_internal_call(internal_m2n_text_get_bounds));
        reg.add_internal_call("internal_m2n_text_get_render_bounds", dotnet_internal_call(internal_m2n_text_get_render_bounds));

        // Text Style Functions
        reg.add_internal_call("internal_m2n_text_set_opacity", dotnet_internal_call(internal_m2n_text_set_opacity));
        reg.add_internal_call("internal_m2n_text_get_opacity", dotnet_internal_call(internal_m2n_text_get_opacity));
        reg.add_internal_call("internal_m2n_text_set_text_color", dotnet_internal_call(internal_m2n_text_set_text_color));
        reg.add_internal_call("internal_m2n_text_get_text_color", dotnet_internal_call(internal_m2n_text_get_text_color));
        reg.add_internal_call("internal_m2n_text_set_background_color", dotnet_internal_call(internal_m2n_text_set_background_color));
        reg.add_internal_call("internal_m2n_text_get_background_color", dotnet_internal_call(internal_m2n_text_get_background_color));
        reg.add_internal_call("internal_m2n_text_set_foreground_color", dotnet_internal_call(internal_m2n_text_set_foreground_color));
        reg.add_internal_call("internal_m2n_text_get_foreground_color", dotnet_internal_call(internal_m2n_text_get_foreground_color));
        reg.add_internal_call("internal_m2n_text_set_overline_color", dotnet_internal_call(internal_m2n_text_set_overline_color));
        reg.add_internal_call("internal_m2n_text_get_overline_color", dotnet_internal_call(internal_m2n_text_get_overline_color));
        reg.add_internal_call("internal_m2n_text_set_underline_color", dotnet_internal_call(internal_m2n_text_set_underline_color));
        reg.add_internal_call("internal_m2n_text_get_underline_color", dotnet_internal_call(internal_m2n_text_get_underline_color));
        reg.add_internal_call("internal_m2n_text_set_strike_color", dotnet_internal_call(internal_m2n_text_set_strike_color));
        reg.add_internal_call("internal_m2n_text_get_strike_color", dotnet_internal_call(internal_m2n_text_get_strike_color));
        reg.add_internal_call("internal_m2n_text_set_outline_color", dotnet_internal_call(internal_m2n_text_set_outline_color));
        reg.add_internal_call("internal_m2n_text_get_outline_color", dotnet_internal_call(internal_m2n_text_get_outline_color));
        reg.add_internal_call("internal_m2n_text_set_outline_width", dotnet_internal_call(internal_m2n_text_set_outline_width));
        reg.add_internal_call("internal_m2n_text_get_outline_width", dotnet_internal_call(internal_m2n_text_get_outline_width));
        reg.add_internal_call("internal_m2n_text_set_shadow_offsets", dotnet_internal_call(internal_m2n_text_set_shadow_offsets));
        reg.add_internal_call("internal_m2n_text_get_shadow_offsets", dotnet_internal_call(internal_m2n_text_get_shadow_offsets));
        reg.add_internal_call("internal_m2n_text_set_shadow_color", dotnet_internal_call(internal_m2n_text_set_shadow_color));
        reg.add_internal_call("internal_m2n_text_get_shadow_color", dotnet_internal_call(internal_m2n_text_get_shadow_color));
        reg.add_internal_call("internal_m2n_text_set_shadow_softener", dotnet_internal_call(internal_m2n_text_set_shadow_softener));
        reg.add_internal_call("internal_m2n_text_get_shadow_softener", dotnet_internal_call(internal_m2n_text_get_shadow_softener));
        reg.add_internal_call("internal_m2n_text_set_style_flags", dotnet_internal_call(internal_m2n_text_set_style_flags));
        reg.add_internal_call("internal_m2n_text_get_style_flags", dotnet_internal_call(internal_m2n_text_get_style_flags));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LightComponent");
        reg.add_internal_call("internal_m2n_light_get_color", dotnet_internal_call(internal_m2n_light_get_color));
        reg.add_internal_call("internal_m2n_light_set_color", dotnet_internal_call(internal_m2n_light_set_color));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Assets");
        reg.add_internal_call("internal_m2n_get_asset_by_uuid", dotnet_internal_call(internal_m2n_get_asset_by_uuid));
        reg.add_internal_call("internal_m2n_get_asset_by_key", dotnet_internal_call(internal_m2n_get_asset_by_key));
        reg.add_internal_call("internal_m2n_get_material_properties",
                              dotnet_internal_call(internal_m2n_get_material_properties));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AudioClip");
        reg.add_internal_call("internal_m2n_audio_clip_get_length", dotnet_internal_call(internal_m2n_audio_clip_get_length));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AnimationClip");
        reg.add_internal_call("internal_m2n_animation_clip_get_length", dotnet_internal_call(internal_m2n_animation_clip_get_length));
        reg.add_internal_call("internal_m2n_animation_clip_get_name", dotnet_internal_call(internal_m2n_animation_clip_get_name));
    }

    {
        auto reg = dotnet::internal_call_registry("Quaternion");
        reg.add_internal_call("internal_m2n_from_euler_rad", dotnet_internal_call(internal_m2n_from_euler_rad));
        reg.add_internal_call("internal_m2n_to_euler_rad", dotnet_internal_call(internal_m2n_to_euler_rad));
        reg.add_internal_call("internal_m2n_from_to_rotation", dotnet_internal_call(internal_m2n_from_to_rotation));
        reg.add_internal_call("internal_m2n_angle_axis", dotnet_internal_call(internal_m2n_angle_axis));
        reg.add_internal_call("internal_m2n_look_rotation", dotnet_internal_call(internal_m2n_look_rotation));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Gizmos");
        reg.add_internal_call("internal_m2n_gizmos_add_sphere", dotnet_internal_call(internal_m2n_gizmos_add_sphere));
        reg.add_internal_call("internal_m2n_gizmos_add_ray", dotnet_internal_call(internal_m2n_gizmos_add_ray));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Tests");
        reg.add_internal_call("m2n_test_uuid", dotnet_internal_call(m2n_test_uuid));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.LayerMask");
        reg.add_internal_call("internal_m2n_layers_layer_to_name", dotnet_internal_call(internal_m2n_layers_layer_to_name));
        reg.add_internal_call("internal_m2n_layers_name_to_layer", dotnet_internal_call(internal_m2n_layers_name_to_layer));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Input");
        reg.add_internal_call("internal_m2n_input_get_analog_value",
                              dotnet_internal_call(internal_m2n_input_get_analog_value));
        reg.add_internal_call("internal_m2n_input_get_digital_value",
                              dotnet_internal_call(internal_m2n_input_get_analog_value));
        reg.add_internal_call("internal_m2n_input_is_pressed", dotnet_internal_call(internal_m2n_input_is_pressed));
        reg.add_internal_call("internal_m2n_input_is_released", dotnet_internal_call(internal_m2n_input_is_released));
        reg.add_internal_call("internal_m2n_input_is_down", dotnet_internal_call(internal_m2n_input_is_down));
        reg.add_internal_call("internal_m2n_input_is_key_pressed", dotnet_internal_call(internal_m2n_input_is_key_pressed));
        reg.add_internal_call("internal_m2n_input_is_key_released", dotnet_internal_call(internal_m2n_input_is_key_released));
        reg.add_internal_call("internal_m2n_input_is_key_down", dotnet_internal_call(internal_m2n_input_is_key_down));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_pressed",
                              dotnet_internal_call(internal_m2n_input_is_mouse_button_pressed));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_released",
                              dotnet_internal_call(internal_m2n_input_is_mouse_button_released));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_down",
                              dotnet_internal_call(internal_m2n_input_is_mouse_button_down));
        reg.add_internal_call("internal_m2n_input_get_mouse_position",
                              dotnet_internal_call(internal_m2n_input_get_mouse_position));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Physics");
        reg.add_internal_call("internal_m2n_physics_ray_cast", dotnet_internal_call(internal_m2n_physics_ray_cast));
        reg.add_internal_call("internal_m2n_physics_ray_cast_all", dotnet_internal_call(internal_m2n_physics_ray_cast_all));
        reg.add_internal_call("internal_m2n_physics_sphere_cast", dotnet_internal_call(internal_m2n_physics_sphere_cast));
        reg.add_internal_call("internal_m2n_physics_sphere_cast_all",
                              dotnet_internal_call(internal_m2n_physics_sphere_cast_all));
        reg.add_internal_call("internal_m2n_physics_sphere_overlap",
                              dotnet_internal_call(internal_m2n_physics_sphere_overlap));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.IK");
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_ccd",
                              dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_ccd));
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_fabrik",
                              dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_fabrik));
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_two_bone",
                              dotnet_internal_call(internal_m2n_utils_set_ik_posiiton_two_bone));

        reg.add_internal_call("internal_m2n_utils_set_ik_look_at_posiiton",
                              dotnet_internal_call(internal_m2n_utils_set_ik_look_at_posiiton));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.AudioSourceComponent");
        reg.add_internal_call("internal_m2n_audio_source_get_loop", dotnet_internal_call(internal_m2n_audio_source_get_loop));
        reg.add_internal_call("internal_m2n_audio_source_set_loop", dotnet_internal_call(internal_m2n_audio_source_set_loop));
        reg.add_internal_call("internal_m2n_audio_source_get_volume",
                              dotnet_internal_call(internal_m2n_audio_source_get_volume));
        reg.add_internal_call("internal_m2n_audio_source_set_volume",
                              dotnet_internal_call(internal_m2n_audio_source_set_volume));
        reg.add_internal_call("internal_m2n_audio_source_get_pitch",
                              dotnet_internal_call(internal_m2n_audio_source_get_pitch));
        reg.add_internal_call("internal_m2n_audio_source_set_pitch",
                              dotnet_internal_call(internal_m2n_audio_source_set_pitch));
        reg.add_internal_call("internal_m2n_audio_source_get_volume_rolloff",
                              dotnet_internal_call(internal_m2n_audio_source_get_volume_rolloff));
        reg.add_internal_call("internal_m2n_audio_source_set_volume_rolloff",
                              dotnet_internal_call(internal_m2n_audio_source_set_volume_rolloff));
        reg.add_internal_call("internal_m2n_audio_source_get_min_distance",
                              dotnet_internal_call(internal_m2n_audio_source_get_min_distance));
        reg.add_internal_call("internal_m2n_audio_source_set_min_distance",
                              dotnet_internal_call(internal_m2n_audio_source_set_min_distance));
        reg.add_internal_call("internal_m2n_audio_source_get_max_distance",
                              dotnet_internal_call(internal_m2n_audio_source_get_max_distance));
        reg.add_internal_call("internal_m2n_audio_source_set_max_distance",
                              dotnet_internal_call(internal_m2n_audio_source_set_max_distance));
        reg.add_internal_call("internal_m2n_audio_source_get_mute", dotnet_internal_call(internal_m2n_audio_source_get_mute));

        reg.add_internal_call("internal_m2n_audio_source_set_mute", dotnet_internal_call(internal_m2n_audio_source_set_mute));

        reg.add_internal_call("internal_m2n_audio_source_is_playing",
                              dotnet_internal_call(internal_m2n_audio_source_is_playing));
        reg.add_internal_call("internal_m2n_audio_source_is_paused",
                              dotnet_internal_call(internal_m2n_audio_source_is_paused));
        reg.add_internal_call("internal_m2n_audio_source_play", dotnet_internal_call(internal_m2n_audio_source_play));
        reg.add_internal_call("internal_m2n_audio_source_stop", dotnet_internal_call(internal_m2n_audio_source_stop));

        reg.add_internal_call("internal_m2n_audio_source_pause", dotnet_internal_call(internal_m2n_audio_source_pause));
        reg.add_internal_call("internal_m2n_audio_source_resume", dotnet_internal_call(internal_m2n_audio_source_resume));
        reg.add_internal_call("internal_m2n_audio_source_get_audio_clip",
                              dotnet_internal_call(internal_m2n_audio_source_get_audio_clip));
        reg.add_internal_call("internal_m2n_audio_source_set_audio_clip",
                              dotnet_internal_call(internal_m2n_audio_source_set_audio_clip));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIDocumentComponent");
        reg.add_internal_call("internal_m2n_ui_document_get_asset", dotnet_internal_call(internal_m2n_ui_document_get_asset));
        reg.add_internal_call("internal_m2n_ui_document_set_asset", dotnet_internal_call(internal_m2n_ui_document_set_asset));
        reg.add_internal_call("internal_m2n_ui_document_is_loaded", dotnet_internal_call(internal_m2n_ui_document_is_loaded));
        reg.add_internal_call("internal_m2n_ui_document_is_enabled", dotnet_internal_call(internal_m2n_ui_document_is_enabled));
        reg.add_internal_call("internal_m2n_ui_document_set_enabled", dotnet_internal_call(internal_m2n_ui_document_set_enabled));
        reg.add_internal_call("internal_m2n_ui_document_close", dotnet_internal_call(internal_m2n_ui_document_close));
        reg.add_internal_call("internal_m2n_ui_document_get_title", dotnet_internal_call(internal_m2n_ui_document_get_title));
        reg.add_internal_call("internal_m2n_ui_document_set_title", dotnet_internal_call(internal_m2n_ui_document_set_title));
        reg.add_internal_call("internal_m2n_ui_document_get_wrapper", dotnet_internal_call(internal_m2n_ui_document_get_wrapper));
    }


    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIDocument");
        reg.add_internal_call("internal_m2n_ui_document_wrapper_is_valid", dotnet_internal_call(internal_m2n_ui_document_wrapper_is_valid));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_get_title", dotnet_internal_call(internal_m2n_ui_document_wrapper_get_title));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_set_title", dotnet_internal_call(internal_m2n_ui_document_wrapper_set_title));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_is_visible", dotnet_internal_call(internal_m2n_ui_document_wrapper_is_visible));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_show", dotnet_internal_call(internal_m2n_ui_document_wrapper_show));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_hide", dotnet_internal_call(internal_m2n_ui_document_wrapper_hide));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_close", dotnet_internal_call(internal_m2n_ui_document_wrapper_close));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_get_element_by_id", dotnet_internal_call(internal_m2n_ui_document_get_element_wrapper_by_id));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_query_selector", dotnet_internal_call(internal_m2n_ui_document_query_selector_wrapper));
        reg.add_internal_call("internal_m2n_ui_document_wrapper_query_selector_all", dotnet_internal_call(internal_m2n_ui_document_query_selector_wrapper));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIElement");
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_valid", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_valid));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_inner_rml", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_inner_rml));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_visible", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_visible));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_remove_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_remove_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_has_attribute", dotnet_internal_call(internal_m2n_ui_element_wrapper_has_attribute));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_set_class", dotnet_internal_call(internal_m2n_ui_element_wrapper_set_class));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_is_class_set", dotnet_internal_call(internal_m2n_ui_element_wrapper_is_class_set));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_sync_transform_to_entity", dotnet_internal_call(internal_m2n_ui_element_wrapper_sync_transform_to_entity));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_focus", dotnet_internal_call(internal_m2n_ui_element_wrapper_focus));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_blur", dotnet_internal_call(internal_m2n_ui_element_wrapper_blur));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_click", dotnet_internal_call(internal_m2n_ui_element_wrapper_click));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_scroll_into_view", dotnet_internal_call(internal_m2n_ui_element_wrapper_scroll_into_view));
        reg.add_internal_call("internal_m2n_ui_element_wrapper_get_id", dotnet_internal_call(internal_m2n_ui_element_wrapper_get_id));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIEventManager");
        reg.add_internal_call("internal_m2n_ui_ensure_native_event_listener", dotnet_internal_call(internal_m2n_ui_ensure_native_event_listener));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.UIEventBase");
        reg.add_internal_call("internal_m2n_ui_stop_propagation", dotnet_internal_call(internal_m2n_ui_stop_propagation));
        reg.add_internal_call("internal_m2n_ui_stop_immediate_propagation", dotnet_internal_call(internal_m2n_ui_stop_immediate_propagation));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Application");
        reg.add_internal_call("internal_m2n_application_quit", dotnet_internal_call(internal_m2n_application_quit));
    }

    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Time");
        reg.add_internal_call("internal_m2n_set_time_scale", dotnet_internal_call(internal_m2n_set_time_scale));
    }
    
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.Profiler");
        reg.add_internal_call("internal_m2n_profiler_add_record", dotnet_internal_call(internal_m2n_profiler_add_record));
    }
    
    {
        auto reg = dotnet::internal_call_registry("Unravel.Core.GCMonitor");
        reg.add_internal_call("internal_m2n_get_dotnet_heap_size", dotnet_internal_call(dotnet::gc_get_heap_size));
        reg.add_internal_call("internal_m2n_get_dotnet_used_size", dotnet_internal_call(dotnet::gc_get_used_size));
    }

    return true;
}

} // namespace unravel
