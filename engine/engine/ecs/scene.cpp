#include "scene.h"
#include "uuid/uuid.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/prefab_component.h>

#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/systems/physics_system.h>

#include <engine/rendering/ecs/components/model_component.h>

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>

#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>

#include <engine/events.h>
#include <engine/meta/ecs/entity.hpp>

#include <logging/logging.h>

namespace unravel
{

namespace
{

auto clone_entity_impl(entt::registry& r, entt::handle entity) -> entt::handle
{
    entt::handle object(r, r.create());

    for(auto [id, storage] : r.storage())
    {
        auto name = storage.type().name();

        if(name.find("edyn::") != std::string_view::npos)
        {
            continue;
        }

        if(name.find("bullet::") != std::string_view::npos)
        {
            continue;
        }

        if(storage.contains(entity) && !storage.contains(object))
        {
            storage.push(object, storage.value(entity));
        }
    }

    return object;
}

template<typename Registry>
void remove_all_components(entt::basic_handle<Registry> handle)
{
    auto& registry = *handle.registry();
    auto entity = handle.entity();

    for(auto [id, storage] : registry.storage())
    {
        storage.remove(entity);
    }
}

auto get_scene_registry_impl() -> std::vector<scene*>&
{
    static std::vector<scene*> scenes;
    return scenes;
}

void register_scene(scene* scn)
{
    auto& scenes = get_scene_registry_impl();
    scenes.emplace_back(scn);
}

void unregister_scene(scene* scn)
{
    auto& scenes = get_scene_registry_impl();
    scenes.erase(std::remove(scenes.begin(), scenes.end(), scn), scenes.end());
}


template<typename ...Ts>
void destroy_dependent_components(entt::registry& r, entt::entity e)
{
    r.remove<Ts...>(e);
}

template<typename ...Ts>
void destroy_dependent_components_recursive(entt::registry& r, entt::entity e)
{
    if(!r.valid(e))
    {
        return;
    }
    destroy_dependent_components<Ts...>(r, e);


    auto transform = r.try_get<transform_component>(e);
    if(transform)
    {
        for(auto child : transform->get_children())
        {
            destroy_dependent_components_recursive<Ts...>(r, child);
        }
    }
}

} // namespace

auto scene::get_all_scenes() -> const std::vector<scene*>&
{
    return get_scene_registry_impl();
}

scene::scene(const std::string& tag_name)
    : tag(tag_name)
{
    register_scene(this);

    registry = std::make_unique<entt::registry>();
    unload();

    registry->on_construct<root_component>().connect<&root_component::on_create_component>();
    registry->on_update<root_component>().connect<&root_component::on_update_component>();
    registry->on_destroy<root_component>().connect<&root_component::on_destroy_component>();

    registry->on_construct<transform_component>().connect<&transform_component::on_create_component>();
    registry->on_destroy<transform_component>().connect<&transform_component::on_destroy_component>();

    registry->on_construct<model_component>().connect<&model_component::on_create_component>();
    registry->on_destroy<model_component>().connect<&model_component::on_destroy_component>();

    registry->on_construct<animation_component>().connect<&animation_system::on_create_component>();
    registry->on_destroy<animation_component>().connect<&animation_system::on_destroy_component>();

    registry->on_construct<physics_component>().connect<&physics_system::on_create_component>();
    registry->on_destroy<physics_component>().connect<&physics_system::on_destroy_component>();


    registry->on_construct<prefab_component>().connect<&owned_component::on_create_component<prefab_component>>();
    registry->on_destroy<prefab_component>().connect<&owned_component::on_destroy_component<prefab_component>>();

    registry->on_destroy<prefab_component>().connect<&destroy_dependent_components_recursive<prefab_id_component>>();


    registry->on_construct<script_component>().connect<&script_component::on_create_component>();
    registry->on_destroy<script_component>().connect<&script_component::on_destroy_component>();
}

scene::~scene()
{
    unload();
    unregister_scene(this);
}

void scene::unload()
{
    registry->clear();
    auto reserved_entity = registry->create();
    source = {};
}

auto scene::load_from(const asset_handle<scene_prefab>& pfb) -> bool
{
    if(load_from_prefab(pfb, *this))
    {
        source = pfb;
        return true;
    }

    return false;
}
auto scene::instantiate_out(const asset_handle<prefab>& pfb, entt::handle& e) -> bool
{
    bool result = load_from_prefab_out(pfb, *registry, e);
    return result;
}

auto scene::instantiate(const asset_handle<prefab>& pfb) -> entt::handle
{
    auto e = load_from_prefab(pfb, *registry);
    return e;
}

auto scene::instantiate(const asset_handle<prefab>& pfb, entt::handle parent) -> entt::handle
{
    auto e = load_from_prefab(pfb, *registry);;
    if(parent)
    {
        auto trans_comp = e.get<transform_component>();
        trans_comp.set_parent(parent, true);
    }

    return e;
}

auto scene::create_entity(entt::registry& r, const std::string& name, entt::handle parent) -> entt::handle
{
    entt::handle ent(r, r.create());
    ent.emplace<tag_component>().name = !name.empty() ? name : "Entity";
    ent.emplace<layer_component>();

    auto& transform = ent.emplace<transform_component>();
    if(parent)
    {
        transform.set_parent(parent, false);
    }

    return ent;
}

auto scene::create_entity(const std::string& tag, entt::handle parent) -> entt::handle
{
    return create_entity(*registry, tag, parent);
}

void scene::clone_entity(entt::handle& clone_to, entt::handle clone_from, bool keep_parent)
{
    // APPLOG_TRACE_PERF(std::chrono::microseconds);

    auto* reg = clone_from.registry();
    clone_entity_from_stream(clone_from, clone_to);
    if(keep_parent)
    {
        // get cloned from transform
        auto& clone_from_component = clone_from.get<transform_component>();

        // // get cloned to transform
        auto& clone_to_component = clone_to.get<transform_component>();

        // set parent from original
        auto parent = clone_from_component.get_parent();
        if(parent)
        {
            clone_to_component.set_parent(parent, false);
        }
    }

    // auto clone_to = clone_entity_impl(*registry, clone_from);

    // // get cloned to transform
    // auto& clone_to_component = clone_to.get<transform_component>();

    // // clear parent and children which were copied.
    // clone_to_component._clear_relationships();

    // // get cloned from transform
    // auto& clone_from_component = clone_from.get<transform_component>();

    // // clone children as well
    // const auto& children = clone_from_component.get_children();
    // for(const auto& child : children)
    // {
    //     auto cloned_child = clone_entity(child, false);
    //     auto& comp = cloned_child.get<transform_component>();
    //     comp.set_parent(clone_to);
    // }

    // if(keep_parent)
    // {
    //     // set parent from original
    //     auto parent = clone_from_component.get_parent();
    //     if(parent)
    //     {
    //         clone_to_component.set_parent(parent);
    //     }
    // }
}

auto scene::clone_entity(entt::handle clone_from, bool keep_parent) -> entt::handle
{
    // APPLOG_TRACE_PERF(std::chrono::microseconds);

    auto* reg = clone_from.registry();
    entt::handle clone_to(*reg, reg->create());
    clone_entity(clone_to, clone_from, keep_parent);
    return clone_to;
}

void scene::clone_scene(const scene& src_scene, scene& dst_scene)
{
    clone_scene_from_stream(src_scene, dst_scene);
}

void scene::clear_entity(entt::handle& handle)
{
    remove_all_components(handle);
}


auto scene::create_handle(entt::entity e) -> entt::handle
{
    entt::handle handle(*registry, e);
    return handle;
}

auto scene::create_handle(entt::entity e) const -> entt::const_handle
{
    entt::const_handle handle(*registry, e);
    return handle;
}

auto scene::find_entity_by_prefab_uuid(entt::handle entity, const hpp::uuid& target_uuid) -> entt::handle
{
    if (!entity)
    {
        return {};
    }
    
    auto* id_comp = entity.try_get<prefab_id_component>();
    if (id_comp && id_comp->id == target_uuid)
    {
        return entity;
    }
    
    // Search children
    auto* transform = entity.try_get<transform_component>();
    if (transform)
    {
        for (auto child : transform->get_children())
        {
            auto found = find_entity_by_prefab_uuid(child, target_uuid);
            if (found)
            {
                return found;
            }
        }
    }
    
    return {};
}
} // namespace unravel
