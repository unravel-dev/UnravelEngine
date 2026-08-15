// Scene + Entity bindings share one TU: Entity component helpers are used by Scene find APIs.
#include "script_glue_common.h"
#include "script_bindings.h"

#include "../script_interop.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/prefab.h>
#include <engine/meta/ecs/components/all_components.h>
#include <seq/seq.h>

namespace unravel
{
namespace
{

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
        scene::destroy_entity(e);
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
    native_comp_lut::register_native_component<skylight_component>("Unravel.Core.SkylightComponent");
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
    native_comp_lut::register_native_component<volume_component>("Unravel.Core.VolumeComponent");

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

} // namespace

void register_scene_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

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

void register_entity_script_bindings()
{
    APPLOG_TRACE("{}", __func__);

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

} // namespace unravel
