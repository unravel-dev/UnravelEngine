#include "script_interop.h"
#include "script_system.h"
#include <engine/ecs/ecs.h>
#include <engine/events.h>

#include <engine/engine.h>
#include <monopp/mono_exception.h>
#include <monopp/mono_internal_call.h>
#include <monopp/mono_jit.h>
#include <monort/mono_pod_wrapper.h>
#include <monort/monort.h>

#include <engine/assets/asset_manager.h>
#include <engine/audio/ecs/components/audio_source_component.h>
#include <engine/input/input.h>
#include <engine/meta/ecs/components/all_components.h>
#include <engine/physics/ecs/systems/physics_system.h>
#include <engine/rendering/ecs/systems/model_system.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/settings/settings.h>
#include <graphics/debugdraw.h>

#include <filesystem/filesystem.h>
#include <logging/logging.h>
#include <seq/seq.h>
#include <string_utils/utils.h>

namespace unravel
{
namespace
{

auto get_material_properties(const material::sptr& material) -> mono::managed_interface::material_properties
{
    using converter = mono::managed_interface::converter;

    mono::managed_interface::material_properties props;

    if(rttr::type::get(*material) == rttr::type::get<pbr_material>())
    {
        const auto pbr = std::static_pointer_cast<pbr_material>(material);
        props.base_color = converter::convert<math::color, mono::managed_interface::color>(pbr->get_base_color());
        props.emissive_color =
            converter::convert<math::color, mono::managed_interface::color>(pbr->get_emissive_color());
        props.tiling = converter::convert<math::vec2, mono::managed_interface::vector2>(pbr->get_tiling());
        props.roughness = pbr->get_roughness();
        props.metalness = pbr->get_metalness();
        props.bumpiness = pbr->get_bumpiness();
        props.valid = true;
    }

    return props;
}

void set_material_properties(const material::sptr& material, const mono::managed_interface::material_properties& props)
{
    using converter = mono::managed_interface::converter;

    if(rttr::type::get(*material) == rttr::type::get<pbr_material>())
    {
        auto pbr = std::static_pointer_cast<pbr_material>(material);
        auto base_color = converter::convert<mono::managed_interface::color, math::color>(props.base_color);
        pbr->set_base_color(base_color);

        auto emissive_color = converter::convert<mono::managed_interface::color, math::color>(props.emissive_color);
        pbr->set_emissive_color(emissive_color);

        auto tiling = converter::convert<mono::managed_interface::vector2, math::vec2>(props.tiling);
        pbr->set_tiling(tiling);

        pbr->set_metalness(props.metalness);

        pbr->set_bumpiness(props.bumpiness);
    }
}

struct mono_asset
{
    virtual auto get_asset_uuid(const hpp::uuid& uid) const -> hpp::uuid = 0;
    virtual auto get_asset_uuid(const std::string& key) const -> hpp::uuid = 0;
};

template<typename T>
struct mono_asset_impl : mono_asset
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

auto get_mono_asset(size_t type_hash) -> const mono_asset*
{
    // clang-format off
    static std::map<size_t, std::shared_ptr<mono_asset>> reg =
    {
        {mono::mono_type::get_hash("Ace.Core.Texture"),         std::make_shared<mono_asset_impl<gfx::texture>>()},
        {mono::mono_type::get_hash("Ace.Core.Material"),        std::make_shared<mono_asset_impl<material>>()},
        {mono::mono_type::get_hash("Ace.Core.Mesh"),            std::make_shared<mono_asset_impl<mesh>>()},
        {mono::mono_type::get_hash("Ace.Core.AnimationClip"),   std::make_shared<mono_asset_impl<animation_clip>>()},
        {mono::mono_type::get_hash("Ace.Core.Prefab"),          std::make_shared<mono_asset_impl<prefab>>()},
        {mono::mono_type::get_hash("Ace.Core.Scene"),           std::make_shared<mono_asset_impl<scene_prefab>>()},
        {mono::mono_type::get_hash("Ace.Core.PhysicsMaterial"), std::make_shared<mono_asset_impl<physics_material>>()},
        {mono::mono_type::get_hash("Ace.Core.AudioClip"),       std::make_shared<mono_asset_impl<audio_clip>>()},
        {mono::mono_type::get_hash("Ace.Core.Font"),            std::make_shared<mono_asset_impl<font>>()}
    };
    // clang-format on

    auto it = reg.find(type_hash);
    if(it != reg.end())
    {
        return it->second.get();
    }
    static const mono_asset* empty{};
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
    mono::raise_exception("System", "Exception", "Entity is invalid.");
}

template<typename T>
void raise_missing_component_exception()
{
    mono::raise_exception("System",
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
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& am = ctx.get_cached<asset_manager>();

    ec.get_scene().load_from(am.get_asset<scene_prefab>(key));
}

void internal_m2n_create_scene(const mono::mono_object& this_ptr)
{
    mono::ignore(this_ptr);
}

void internal_m2n_destroy_scene(const mono::mono_object& this_ptr)
{
    mono::ignore(this_ptr);
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
    seconds = std::max(0.0001f, seconds);

    delta_t secs(seconds);
    auto dur = std::chrono::duration_cast<seq::duration_t>(secs);

    auto delay = seq::delay(dur);
    delay.on_end.connect(
        [id]()
        {
            internal_m2n_destroy_entity_immediate(id);
        });

    seq::start(delay, "script");

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
        size_t hash = mono::mono_type::get_hash(name);
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
    native_comp_lut::register_native_component<transform_component>("Ace.Core.TransformComponent");
    native_comp_lut::register_native_component<id_component>("Ace.Core.IdComponent");
    native_comp_lut::register_native_component<model_component>("Ace.Core.ModelComponent");
    native_comp_lut::register_native_component<camera_component>("Ace.Core.CameraComponent");
    native_comp_lut::register_native_component<light_component>("Ace.Core.LightComponent");
    native_comp_lut::register_native_component<reflection_probe_component>("Ace.Core.ReflectionProbeComponent");
    native_comp_lut::register_native_component<physics_component>("Ace.Core.PhysicsComponent");
    native_comp_lut::register_native_component<animation_component>("Ace.Core.AnimationComponent");
    native_comp_lut::register_native_component<audio_listener_component>("Ace.Core.AudioListenerComponent");
    native_comp_lut::register_native_component<audio_source_component>("Ace.Core.AudioSourceComponent");
    native_comp_lut::register_native_component<bone_component>("Ace.Core.BoneComponent");
    native_comp_lut::register_native_component<submesh_component>("Ace.Core.SubmeshComponent");
    native_comp_lut::register_native_component<text_component>("Ace.Core.TextComponent");

    return 0;
}();

auto internal_add_native_component(const mono::mono_type& type, entt::handle e, script_component& script_comp)
    -> mono::mono_object
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

        if(!comp.scoped)
        {
            comp = script_comp.add_native_component(type);
        }
        return static_cast<mono::mono_object&>(comp.scoped->object);
    }

    return {};
}

auto internal_get_native_component_impl(const mono::mono_type& type,
                                        entt::handle e,
                                        script_component& script_comp,
                                        bool exists) -> mono::mono_object
{
    auto comp = script_comp.get_native_component(type);
    if(exists)
    {
        if(!comp.scoped)
        {
            comp = script_comp.add_native_component(type);
        }
        return static_cast<mono::mono_object&>(comp.scoped->object);
    }

    if(comp.scoped)
    {
        script_comp.remove_native_component(comp.scoped->object);
    }

    return {};
}

auto internal_get_native_component(const mono::mono_type& type, entt::handle e, script_component& script_comp)
    -> mono::mono_object
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

auto internal_remove_native_component(const mono::mono_object& obj, entt::handle e, script_component& script_comp)
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

auto internal_remove_native_component(const mono::mono_type& type, entt::handle e, script_component& script_comp)
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

auto internal_m2n_add_component(entt::entity id, const mono::mono_type& type) -> mono::mono_object
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
    return static_cast<mono::mono_object&>(component.scoped->object);
}

auto internal_m2n_get_component(entt::entity id, const mono::mono_type& type) -> mono::mono_object
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

    if(component.scoped)
    {
        return static_cast<mono::mono_object&>(component.scoped->object);
    }

    return {};
}

auto internal_m2n_get_components(entt::entity id, const mono::mono_type& type) -> std::vector<mono::mono_object>
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

auto internal_m2n_get_component_in_children(entt::entity id, const mono::mono_type& type) -> mono::mono_object
{
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

auto internal_m2n_get_components_in_children(entt::entity id, const mono::mono_type& type)
    -> hpp::small_vector<mono::mono_object>
{
    hpp::small_vector<mono::mono_object> components;
    if(auto comp = safe_get_component<transform_component>(id))
    {
        const auto& children = comp->get_children();
        for(const auto& child : children)
        {
            auto child_components = internal_m2n_get_components(child, type);
            std::move(child_components.begin(), child_components.end(), std::back_inserter(components));
        }
    }
    return components;
}

auto internal_m2n_get_transform_component(entt::entity id, const mono::mono_type& type) -> mono::mono_object
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

auto internal_m2n_has_component(entt::entity id, const mono::mono_type& type) -> bool
{
    auto comp = internal_m2n_get_component(id, type);

    return comp.valid();
}

auto internal_m2n_remove_component_instance(entt::entity id, const mono::mono_object& comp) -> bool
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

auto internal_m2n_remove_component_instance_delay(entt::entity id, const mono::mono_object& comp, float seconds_delay)
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

auto internal_m2n_remove_component(entt::entity id, const mono::mono_type& type) -> bool
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

auto internal_m2n_remove_component_delay(entt::entity id, const mono::mono_type& type, float seconds_delay) -> bool
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
        bool shouldEnqueue = recursive ? (candidate.matched_index == 0 || advanced) : (candidate.matched_index == 0);

        if(shouldEnqueue)
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
                                             mono::managed_interface::ray* managed_ray) -> bool
{
    if(auto comp = safe_get_component<camera_component>(id))
    {
        math::vec3 ray_origin{};
        math::vec3 ray_dir{};
        bool result = comp->get_camera().viewport_to_ray(origin, ray_origin, ray_dir);
        if(result)
        {
            using converter = mono::managed_interface::converter;
            managed_ray->origin = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_origin);
            managed_ray->direction = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_dir);
        }
        return result;
    }

    return false;
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
    -> mono::managed_interface::material_properties
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
                                              const mono::managed_interface::material_properties& props,
                                              uint32_t index)
{
    using converter = mono::managed_interface::converter;

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

auto internal_m2n_get_asset_by_uuid(const hpp::uuid& uid, const mono::mono_type& type) -> hpp::uuid
{
    if(auto asset = get_mono_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(uid);
    }

    return {};
}

auto internal_m2n_get_asset_by_key(const std::string& key, const mono::mono_type& type) -> hpp::uuid
{
    if(auto asset = get_mono_asset(type.get_hash()))
    {
        return asset->get_asset_uuid(key);
    }

    return {};
}

auto internal_m2n_get_material_properties(const hpp::uuid& uid) -> mono::managed_interface::material_properties
{
    using converter = mono::managed_interface::converter;

    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    mono::managed_interface::material_properties props;
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
        mono::raise_exception("System", "Exception", fmt::format("Layer index {} is out of bounds.", layer));

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

auto internal_m2n_physics_ray_cast(mono::managed_interface::raycast_hit* hit,
                                   const math::vec3& origin,
                                   const math::vec3& direction,
                                   float max_distance,
                                   int layer_mask,
                                   bool query_sensors) -> bool
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hit = physics.ray_cast(origin, direction, max_distance, layer_mask, query_sensors);

    using converter = mono::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_ray_cast_all(const math::vec3& origin,
                                       const math::vec3& direction,
                                       float max_distance,
                                       int layer_mask,
                                       bool query_sensors) -> hpp::small_vector<mono::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);

    hpp::small_vector<mono::managed_interface::raycast_hit> hits;

    using converter = mono::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit.normal);
        hit.distance = ray_hit.distance;
    }

    return hits;
}

auto internal_m2n_physics_sphere_cast(mono::managed_interface::raycast_hit* hit,
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

    using converter = mono::managed_interface::converter;

    if(ray_hit)
    {
        hit->entity = ray_hit->entity;
        hit->point = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit->point);
        hit->normal = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit->normal);
        hit->distance = ray_hit->distance;
    }

    return ray_hit.has_value();
}

auto internal_m2n_physics_sphere_cast_all(const math::vec3& origin,
                                          const math::vec3& direction,
                                          float radius,
                                          float max_distance,
                                          int layer_mask,
                                          bool query_sensors) -> hpp::small_vector<mono::managed_interface::raycast_hit>
{
    auto& ctx = engine::context();
    auto& physics = ctx.get_cached<physics_system>();

    auto ray_hits = physics.sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);

    hpp::small_vector<mono::managed_interface::raycast_hit> hits;

    using converter = mono::managed_interface::converter;
    for(const auto& ray_hit : ray_hits)
    {
        auto& hit = hits.emplace_back();
        hit.entity = ray_hit.entity;
        hit.point = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit.point);
        hit.normal = converter::convert<math::vec3, mono::managed_interface::vector3>(ray_hit.normal);
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
                                            int num_bones_in_chain,
                                            float threshold,
                                            int max_iterations)
{
    auto e = get_entity_from_id(id);

    ik_set_position_ccd(e, target, num_bones_in_chain, threshold, max_iterations);
}

void internal_m2n_utils_set_ik_posiiton_fabrik(entt::entity id,
                                               const math::vec3& target,
                                               int num_bones_in_chain,
                                               float threshold,
                                               int max_iterations)
{
    auto e = get_entity_from_id(id);

    ik_set_position_fabrik(e, target, num_bones_in_chain, threshold, max_iterations);
}

void internal_m2n_utils_set_ik_posiiton_two_bone(entt::entity id,
                                                 const math::vec3& target,
                                                 const math::vec3& forward,
                                                 float weight,
                                                 float soften,
                                                 int max_iterations)
{
    auto e = get_entity_from_id(id);

    ik_set_position_two_bone(e, target, forward, weight, soften, max_iterations);
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
} // namespace

auto script_system::bind_internal_calls(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    {
        auto reg = mono::internal_call_registry("Ace.Core.Log");
        reg.add_internal_call("internal_m2n_log_trace", internal_call(internal_m2n_log_trace));
        reg.add_internal_call("internal_m2n_log_info", internal_call(internal_m2n_log_info));
        reg.add_internal_call("internal_m2n_log_warning", internal_call(internal_m2n_log_warning));
        reg.add_internal_call("internal_m2n_log_error", internal_call(internal_m2n_log_error));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Scene");
        reg.add_internal_call("internal_m2n_load_scene", internal_call(internal_m2n_load_scene));
        reg.add_internal_call("internal_m2n_create_scene", internal_call(internal_m2n_create_scene));
        reg.add_internal_call("internal_m2n_destroy_scene", internal_call(internal_m2n_destroy_scene));
        reg.add_internal_call("internal_m2n_create_entity", internal_call(internal_m2n_create_entity));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_uid",
                              internal_call(internal_m2n_create_entity_from_prefab_uid));
        reg.add_internal_call("internal_m2n_create_entity_from_prefab_key",
                              internal_call(internal_m2n_create_entity_from_prefab_key));
        reg.add_internal_call("internal_m2n_clone_entity", internal_call(internal_m2n_clone_entity));
        reg.add_internal_call("internal_m2n_destroy_entity", internal_call(internal_m2n_destroy_entity));
        reg.add_internal_call("internal_m2n_destroy_entity_immediate",
                              internal_call(internal_m2n_destroy_entity_immediate));

        reg.add_internal_call("internal_m2n_is_entity_valid", internal_call(internal_m2n_is_entity_valid));
        reg.add_internal_call("internal_m2n_find_entity_by_name", internal_call(internal_m2n_find_entity_by_name));
        reg.add_internal_call("internal_m2n_find_entities_by_name", internal_call(internal_m2n_find_entities_by_name));
        reg.add_internal_call("internal_m2n_find_entity_by_tag", internal_call(internal_m2n_find_entity_by_tag));
        reg.add_internal_call("internal_m2n_find_entities_by_tag", internal_call(internal_m2n_find_entities_by_tag));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Entity");
        reg.add_internal_call("internal_m2n_add_component", internal_call(internal_m2n_add_component));
        reg.add_internal_call("internal_m2n_get_component", internal_call(internal_m2n_get_component));
        reg.add_internal_call("internal_m2n_get_component_in_children",
                              internal_call(internal_m2n_get_component_in_children));
        reg.add_internal_call("internal_m2n_has_component", internal_call(internal_m2n_has_component));
        reg.add_internal_call("internal_m2n_get_components", internal_call(internal_m2n_get_components));
        reg.add_internal_call("internal_m2n_get_components_in_children",
                              internal_call(internal_m2n_get_components_in_children));

        reg.add_internal_call("internal_m2n_remove_component_instance",
                              internal_call(internal_m2n_remove_component_instance));
        reg.add_internal_call("internal_m2n_remove_component_instance_delay",
                              internal_call(internal_m2n_remove_component_instance_delay));

        reg.add_internal_call("internal_m2n_remove_component", internal_call(internal_m2n_remove_component));
        reg.add_internal_call("internal_m2n_remove_component_delay",
                              internal_call(internal_m2n_remove_component_delay));

        reg.add_internal_call("internal_m2n_get_transform_component",
                              internal_call(internal_m2n_get_transform_component));
        reg.add_internal_call("internal_m2n_get_name", internal_call(internal_m2n_get_name));
        reg.add_internal_call("internal_m2n_set_name", internal_call(internal_m2n_set_name));
        reg.add_internal_call("internal_m2n_get_tag", internal_call(internal_m2n_get_tag));
        reg.add_internal_call("internal_m2n_set_tag", internal_call(internal_m2n_set_tag));
        reg.add_internal_call("internal_m2n_get_layers", internal_call(internal_m2n_get_layers));
        reg.add_internal_call("internal_m2n_set_layers", internal_call(internal_m2n_set_layers));

        reg.add_internal_call("internal_m2n_get_active_global", internal_call(internal_m2n_get_active_global));
        reg.add_internal_call("internal_m2n_get_active_local", internal_call(internal_m2n_get_active_local));
        reg.add_internal_call("internal_m2n_set_active_local", internal_call(internal_m2n_set_active_local));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.TransformComponent");
        reg.add_internal_call("internal_m2n_get_children", internal_call(internal_m2n_get_children));
        reg.add_internal_call("internal_m2n_get_child", internal_call(internal_m2n_get_child));
        reg.add_internal_call("internal_m2n_get_parent", internal_call(internal_m2n_get_parent));
        reg.add_internal_call("internal_m2n_set_parent", internal_call(internal_m2n_set_parent));

        reg.add_internal_call("internal_m2n_get_position_global", internal_call(internal_m2n_get_position_global));
        reg.add_internal_call("internal_m2n_set_position_global", internal_call(internal_m2n_set_position_global));
        reg.add_internal_call("internal_m2n_move_by_global", internal_call(internal_m2n_move_by_global));

        reg.add_internal_call("internal_m2n_get_position_local", internal_call(internal_m2n_get_position_local));
        reg.add_internal_call("internal_m2n_set_position_local", internal_call(internal_m2n_set_position_local));
        reg.add_internal_call("internal_m2n_move_by_local", internal_call(internal_m2n_move_by_local));

        // Euler
        reg.add_internal_call("internal_m2n_get_rotation_euler_global",
                              internal_call(internal_m2n_get_rotation_euler_global));
        reg.add_internal_call("internal_m2n_set_rotation_euler_global",
                              internal_call(internal_m2n_set_rotation_euler_global));
        reg.add_internal_call("internal_m2n_rotate_by_euler_global",
                              internal_call(internal_m2n_rotate_by_euler_global));

        reg.add_internal_call("internal_m2n_get_rotation_euler_local",
                              internal_call(internal_m2n_get_rotation_euler_local));
        reg.add_internal_call("internal_m2n_set_rotation_euler_local",
                              internal_call(internal_m2n_set_rotation_euler_local));
        reg.add_internal_call("internal_m2n_rotate_by_euler_local", internal_call(internal_m2n_rotate_by_euler_local));

        // Quat
        reg.add_internal_call("internal_m2n_get_rotation_global", internal_call(internal_m2n_get_rotation_global));
        reg.add_internal_call("internal_m2n_set_rotation_global", internal_call(internal_m2n_set_rotation_global));
        reg.add_internal_call("internal_m2n_rotate_by_global", internal_call(internal_m2n_rotate_by_global));

        reg.add_internal_call("internal_m2n_get_rotation_local", internal_call(internal_m2n_get_rotation_local));
        reg.add_internal_call("internal_m2n_set_rotation_local", internal_call(internal_m2n_set_rotation_local));
        reg.add_internal_call("internal_m2n_rotate_by_local", internal_call(internal_m2n_rotate_by_local));

        // Other
        reg.add_internal_call("internal_m2n_rotate_axis_global", internal_call(internal_m2n_rotate_axis_global));
        reg.add_internal_call("internal_m2n_look_at", internal_call(internal_m2n_look_at));
        reg.add_internal_call("internal_m2n_transform_vector_global",
                              internal_call(internal_m2n_transform_vector_global));
        reg.add_internal_call("internal_m2n_inverse_transform_vector_global",
                              internal_call(internal_m2n_inverse_transform_vector_global));

        reg.add_internal_call("internal_m2n_transform_direction_global",
                              internal_call(internal_m2n_transform_direction_global));
        reg.add_internal_call("internal_m2n_inverse_transform_direction_global",
                              internal_call(internal_m2n_inverse_transform_direction_global));

        // Scale
        reg.add_internal_call("internal_m2n_get_scale_global", internal_call(internal_m2n_get_scale_global));
        reg.add_internal_call("internal_m2n_set_scale_global", internal_call(internal_m2n_set_scale_global));
        reg.add_internal_call("internal_m2n_scale_by_global", internal_call(internal_m2n_scale_by_local));

        reg.add_internal_call("internal_m2n_get_scale_local", internal_call(internal_m2n_get_scale_local));
        reg.add_internal_call("internal_m2n_set_scale_local", internal_call(internal_m2n_set_scale_local));
        reg.add_internal_call("internal_m2n_scale_by_local", internal_call(internal_m2n_scale_by_local));

        // Skew
        reg.add_internal_call("internal_m2n_get_skew_global", internal_call(internal_m2n_get_skew_global));
        reg.add_internal_call("internal_m2n_set_skew_globa", internal_call(internal_m2n_setl_skew_globa));
        reg.add_internal_call("internal_m2n_get_skew_local", internal_call(internal_m2n_get_skew_local));
        reg.add_internal_call("internal_m2n_set_skew_local", internal_call(internal_m2n_set_skew_local));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.PhysicsComponent");
        reg.add_internal_call("internal_m2n_physics_apply_explosion_force",
                              internal_call(internal_m2n_physics_apply_explosion_force));
        reg.add_internal_call("internal_m2n_physics_apply_force", internal_call(internal_m2n_physics_apply_force));
        reg.add_internal_call("internal_m2n_physics_apply_torque", internal_call(internal_m2n_physics_apply_torque));
        reg.add_internal_call("internal_m2n_physics_get_velocity", internal_call(internal_m2n_physics_get_velocity));
        reg.add_internal_call("internal_m2n_physics_set_velocity", internal_call(internal_m2n_physics_set_velocity));
        reg.add_internal_call("internal_m2n_physics_get_angular_velocity",
                              internal_call(internal_m2n_physics_get_angular_velocity));
        reg.add_internal_call("internal_m2n_physics_set_angular_velocity",
                              internal_call(internal_m2n_physics_set_angular_velocity));

        reg.add_internal_call("internal_m2n_physics_get_include_layers",
                              internal_call(internal_m2n_physics_get_include_layers));
        reg.add_internal_call("internal_m2n_physics_set_include_layers",
                              internal_call(internal_m2n_physics_set_include_layers));
        reg.add_internal_call("internal_m2n_physics_get_exclude_layers",
                              internal_call(internal_m2n_physics_get_exclude_layers));
        reg.add_internal_call("internal_m2n_physics_set_exclude_layers",
                              internal_call(internal_m2n_physics_set_exclude_layers));
        reg.add_internal_call("internal_m2n_physics_get_collision_layers",
                              internal_call(internal_m2n_physics_get_collision_layers));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.AnimationComponent");
        reg.add_internal_call("internal_m2n_animation_blend", internal_call(internal_m2n_animation_blend));
        reg.add_internal_call("internal_m2n_animation_play", internal_call(internal_m2n_animation_play));
        reg.add_internal_call("internal_m2n_animation_pause", internal_call(internal_m2n_animation_pause));
        reg.add_internal_call("internal_m2n_animation_resume", internal_call(internal_m2n_animation_resume));
        reg.add_internal_call("internal_m2n_animation_stop", internal_call(internal_m2n_animation_stop));
        reg.add_internal_call("internal_m2n_animation_set_speed", internal_call(internal_m2n_animation_set_speed));
        reg.add_internal_call("internal_m2n_animation_get_speed", internal_call(internal_m2n_animation_get_speed));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.CameraComponent");
        reg.add_internal_call("internal_m2n_camera_screen_point_to_ray",
                              internal_call(internal_m2n_camera_screen_point_to_ray));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.ModelComponent");
        reg.add_internal_call("internal_m2n_model_get_enabled", internal_call(internal_m2n_model_get_enabled));
        reg.add_internal_call("internal_m2n_model_set_enabled", internal_call(internal_m2n_model_set_enabled));
        reg.add_internal_call("internal_m2n_model_get_shared_material",
                              internal_call(internal_m2n_model_get_shared_material));
        reg.add_internal_call("internal_m2n_model_get_shared_material_count",
                              internal_call(internal_m2n_model_get_shared_material_count));
        reg.add_internal_call("internal_m2n_model_set_shared_material",
                              internal_call(internal_m2n_model_set_shared_material));
        reg.add_internal_call("internal_m2n_model_set_material_instance",
                              internal_call(internal_m2n_model_set_material_instance));
        reg.add_internal_call("internal_m2n_model_get_material_instance",
                              internal_call(internal_m2n_model_get_material_instance));
        reg.add_internal_call("internal_m2n_model_get_material_instance_count",
                              internal_call(internal_m2n_model_get_material_instance_count));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.TextComponent");
        reg.add_internal_call("internal_m2n_text_get_text", internal_call(internal_m2n_text_get_text));
        reg.add_internal_call("internal_m2n_text_set_text", internal_call(internal_m2n_text_set_text));
        reg.add_internal_call("internal_m2n_text_get_buffer_type", internal_call(internal_m2n_text_get_buffer_type));
        reg.add_internal_call("internal_m2n_text_set_buffer_type", internal_call(internal_m2n_text_set_buffer_type));
        reg.add_internal_call("internal_m2n_text_get_overflow_type",
                              internal_call(internal_m2n_text_get_overflow_type));
        reg.add_internal_call("internal_m2n_text_set_overflow_type",
                              internal_call(internal_m2n_text_set_overflow_type));
        reg.add_internal_call("internal_m2n_text_get_font", internal_call(internal_m2n_text_get_font));
        reg.add_internal_call("internal_m2n_text_set_font", internal_call(internal_m2n_text_set_font));

        reg.add_internal_call("internal_m2n_text_get_font_size", internal_call(internal_m2n_text_get_font_size));
        reg.add_internal_call("internal_m2n_text_set_font_size", internal_call(internal_m2n_text_set_font_size));
        reg.add_internal_call("internal_m2n_text_get_render_font_size",
                              internal_call(internal_m2n_text_get_render_font_size));

        reg.add_internal_call("internal_m2n_text_get_auto_size", internal_call(internal_m2n_text_get_auto_size));
        reg.add_internal_call("internal_m2n_text_set_auto_size", internal_call(internal_m2n_text_set_auto_size));

        reg.add_internal_call("internal_m2n_text_get_auto_size_range",
                              internal_call(internal_m2n_text_get_auto_size_range));
        reg.add_internal_call("internal_m2n_text_set_auto_size_range",
                              internal_call(internal_m2n_text_set_auto_size_range));

        reg.add_internal_call("internal_m2n_text_get_area", internal_call(internal_m2n_text_get_area));
        reg.add_internal_call("internal_m2n_text_set_area", internal_call(internal_m2n_text_set_area));
        reg.add_internal_call("internal_m2n_text_get_render_area", internal_call(internal_m2n_text_get_render_area));

        reg.add_internal_call("internal_m2n_text_get_is_rich_text", internal_call(internal_m2n_text_get_is_rich_text));
        reg.add_internal_call("internal_m2n_text_set_is_rich_text", internal_call(internal_m2n_text_set_is_rich_text));

        reg.add_internal_call("internal_m2n_text_get_alignment", internal_call(internal_m2n_text_get_alignment));
        reg.add_internal_call("internal_m2n_text_set_alignment", internal_call(internal_m2n_text_set_alignment));

        reg.add_internal_call("internal_m2n_text_get_bounds", internal_call(internal_m2n_text_get_bounds));
        reg.add_internal_call("internal_m2n_text_get_render_bounds", internal_call(internal_m2n_text_get_bounds));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.LightComponent");
        reg.add_internal_call("internal_m2n_light_get_color", internal_call(internal_m2n_light_get_color));
        reg.add_internal_call("internal_m2n_light_set_color", internal_call(internal_m2n_light_set_color));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Assets");
        reg.add_internal_call("internal_m2n_get_asset_by_uuid", internal_call(internal_m2n_get_asset_by_uuid));
        reg.add_internal_call("internal_m2n_get_asset_by_key", internal_call(internal_m2n_get_asset_by_key));
        reg.add_internal_call("internal_m2n_get_material_properties",
                              internal_call(internal_m2n_get_material_properties));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.AudioClip");
        reg.add_internal_call("internal_m2n_audio_clip_get_length", internal_call(internal_m2n_audio_clip_get_length));
    }

    {
        auto reg = mono::internal_call_registry("Quaternion");
        reg.add_internal_call("internal_m2n_from_euler_rad", internal_call(internal_m2n_from_euler_rad));
        reg.add_internal_call("internal_m2n_to_euler_rad", internal_call(internal_m2n_to_euler_rad));
        reg.add_internal_call("internal_m2n_from_to_rotation", internal_call(internal_m2n_from_to_rotation));
        reg.add_internal_call("internal_m2n_angle_axis", internal_call(internal_m2n_angle_axis));
        reg.add_internal_call("internal_m2n_look_rotation", internal_call(internal_m2n_look_rotation));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Gizmos");
        reg.add_internal_call("internal_m2n_gizmos_add_sphere", internal_call(internal_m2n_gizmos_add_sphere));
        reg.add_internal_call("internal_m2n_gizmos_add_ray", internal_call(internal_m2n_gizmos_add_ray));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Tests");
        reg.add_internal_call("m2n_test_uuid", internal_call(m2n_test_uuid));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.LayerMask");
        reg.add_internal_call("internal_m2n_layers_layer_to_name", internal_call(internal_m2n_layers_layer_to_name));
        reg.add_internal_call("internal_m2n_layers_name_to_layer", internal_call(internal_m2n_layers_name_to_layer));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Input");
        reg.add_internal_call("internal_m2n_input_get_analog_value",
                              internal_call(internal_m2n_input_get_analog_value));
        reg.add_internal_call("internal_m2n_input_get_digital_value",
                              internal_call(internal_m2n_input_get_analog_value));
        reg.add_internal_call("internal_m2n_input_is_pressed", internal_call(internal_m2n_input_is_pressed));
        reg.add_internal_call("internal_m2n_input_is_released", internal_call(internal_m2n_input_is_released));
        reg.add_internal_call("internal_m2n_input_is_down", internal_call(internal_m2n_input_is_down));
        reg.add_internal_call("internal_m2n_input_is_key_pressed", internal_call(internal_m2n_input_is_key_pressed));
        reg.add_internal_call("internal_m2n_input_is_key_released", internal_call(internal_m2n_input_is_key_released));
        reg.add_internal_call("internal_m2n_input_is_key_down", internal_call(internal_m2n_input_is_key_down));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_pressed",
                              internal_call(internal_m2n_input_is_mouse_button_pressed));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_released",
                              internal_call(internal_m2n_input_is_mouse_button_released));
        reg.add_internal_call("internal_m2n_input_is_mouse_button_down",
                              internal_call(internal_m2n_input_is_mouse_button_down));
        reg.add_internal_call("internal_m2n_input_get_mouse_position",
                              internal_call(internal_m2n_input_get_mouse_position));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.Physics");
        reg.add_internal_call("internal_m2n_physics_ray_cast", internal_call(internal_m2n_physics_ray_cast));
        reg.add_internal_call("internal_m2n_physics_ray_cast_all", internal_call(internal_m2n_physics_ray_cast_all));
        reg.add_internal_call("internal_m2n_physics_sphere_cast", internal_call(internal_m2n_physics_sphere_cast));
        reg.add_internal_call("internal_m2n_physics_sphere_cast_all",
                              internal_call(internal_m2n_physics_sphere_cast_all));
        reg.add_internal_call("internal_m2n_physics_sphere_overlap",
                              internal_call(internal_m2n_physics_sphere_overlap));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.IK");
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_ccd",
                              internal_call(internal_m2n_utils_set_ik_posiiton_ccd));
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_fabrik",
                              internal_call(internal_m2n_utils_set_ik_posiiton_fabrik));
        reg.add_internal_call("internal_m2n_utils_set_ik_posiiton_two_bone",
                              internal_call(internal_m2n_utils_set_ik_posiiton_two_bone));

        reg.add_internal_call("internal_m2n_utils_set_ik_look_at_posiiton",
                              internal_call(internal_m2n_utils_set_ik_look_at_posiiton));
    }

    {
        auto reg = mono::internal_call_registry("Ace.Core.AudioSourceComponent");
        reg.add_internal_call("internal_m2n_audio_source_get_loop", internal_call(internal_m2n_audio_source_get_loop));
        reg.add_internal_call("internal_m2n_audio_source_set_loop", internal_call(internal_m2n_audio_source_set_loop));
        reg.add_internal_call("internal_m2n_audio_source_get_volume",
                              internal_call(internal_m2n_audio_source_get_volume));
        reg.add_internal_call("internal_m2n_audio_source_set_volume",
                              internal_call(internal_m2n_audio_source_set_volume));
        reg.add_internal_call("internal_m2n_audio_source_get_pitch",
                              internal_call(internal_m2n_audio_source_get_pitch));
        reg.add_internal_call("internal_m2n_audio_source_set_pitch",
                              internal_call(internal_m2n_audio_source_set_pitch));
        reg.add_internal_call("internal_m2n_audio_source_get_volume_rolloff",
                              internal_call(internal_m2n_audio_source_get_volume_rolloff));
        reg.add_internal_call("internal_m2n_audio_source_set_volume_rolloff",
                              internal_call(internal_m2n_audio_source_set_volume_rolloff));
        reg.add_internal_call("internal_m2n_audio_source_get_min_distance",
                              internal_call(internal_m2n_audio_source_get_min_distance));
        reg.add_internal_call("internal_m2n_audio_source_set_min_distance",
                              internal_call(internal_m2n_audio_source_set_min_distance));
        reg.add_internal_call("internal_m2n_audio_source_get_max_distance",
                              internal_call(internal_m2n_audio_source_get_max_distance));
        reg.add_internal_call("internal_m2n_audio_source_set_max_distance",
                              internal_call(internal_m2n_audio_source_set_max_distance));
        reg.add_internal_call("internal_m2n_audio_source_get_mute", internal_call(internal_m2n_audio_source_get_mute));

        reg.add_internal_call("internal_m2n_audio_source_set_mute", internal_call(internal_m2n_audio_source_set_mute));

        reg.add_internal_call("internal_m2n_audio_source_is_playing",
                              internal_call(internal_m2n_audio_source_is_playing));
        reg.add_internal_call("internal_m2n_audio_source_is_paused",
                              internal_call(internal_m2n_audio_source_is_paused));
        reg.add_internal_call("internal_m2n_audio_source_play", internal_call(internal_m2n_audio_source_play));
        reg.add_internal_call("internal_m2n_audio_source_stop", internal_call(internal_m2n_audio_source_stop));

        reg.add_internal_call("internal_m2n_audio_source_pause", internal_call(internal_m2n_audio_source_pause));
        reg.add_internal_call("internal_m2n_audio_source_resume", internal_call(internal_m2n_audio_source_resume));
        reg.add_internal_call("internal_m2n_audio_source_get_audio_clip",
                              internal_call(internal_m2n_audio_source_get_audio_clip));
        reg.add_internal_call("internal_m2n_audio_source_set_audio_clip",
                              internal_call(internal_m2n_audio_source_set_audio_clip));
    }
    // mono::managed_interface::init(assembly);

    return true;
}

} // namespace unravel
