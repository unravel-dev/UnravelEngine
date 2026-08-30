#include "scene.h"

#include <functional>
#include "uuid/uuid.h"
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/physics/ecs/systems/physics_system.h>

#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/animation/ecs/systems/animation_system.h>

#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>

#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/profiler/profiler.h>

#include <hpp/small_vector.hpp>

#include <logging/logging.h>

namespace unravel
{

namespace
{

// auto clone_entity_impl(entt::registry& r, entt::handle entity) -> entt::handle
// {
//     entt::handle object(r, r.create());

//     for(auto [id, storage] : r.storage())
//     {
//         auto name = storage.type().name();

//         if(name.find("edyn::") != std::string_view::npos)
//         {
//             continue;
//         }

//         if(name.find("bullet::") != std::string_view::npos)
//         {
//             continue;
//         }

//         if(storage.contains(entity) && !storage.contains(object))
//         {
//             storage.push(object, storage.value(entity));
//         }
//     }

//     return object;
// }

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


auto destroy_suppression_depth() -> int&
{
    static int depth = 0;
    return depth;
}

/**
 * @brief Collects a subtree depth-first, children before parents.
 *
 * Children first so a child's slot still observes an intact parent.
 *
 * Iterated by reference on purpose: this walk runs no user code, so nothing can
 * reparent or destroy anything while it is in progress. The snapshot it fills exists
 * because the publish pass afterwards does run user code.
 */
void collect_subtree_post_order(entt::handle entity, hpp::small_vector<entt::handle, 16>& out)
{
    if(!entity)
    {
        return;
    }

    if(const auto* transform = entity.try_get<transform_component>())
    {
        for(const auto& child : transform->get_children())
        {
            collect_subtree_post_order(child, out);
        }
    }

    out.push_back(entity);
}

template<typename ...Ts>
void destroy_dependent_components(entt::registry& r, entt::entity e)
{
    r.remove<Ts...>(e);
}

/**
 * @brief Strips components down a subtree, stopping at nested prefab instances.
 *
 * Wired to on_destroy<prefab_component> to clear prefab ids when an instance is unpacked.
 * The recursion stops at any child that is itself an instance root: that child belongs to
 * a different asset, and stripping its ids would silently sever its own link while the
 * user was only unpacking the outer one.
 */
template<typename ...Ts>
void destroy_dependent_components_in_children(entt::registry& r, entt::entity e)
{
    if(!r.valid(e))
    {
        return;
    }

    // A child that is itself an instance root belongs to a different asset. Unpacking the
    // outer instance must not reach into it: stripping its prefab ids would sever its own
    // link too, silently and with nothing in the UI to say so.
    if(r.try_get<prefab_component>(e) != nullptr)
    {
        return;
    }

    destroy_dependent_components<Ts...>(r, e);

    auto transform = r.try_get<transform_component>(e);
    if(transform)
    {
        for(auto child : transform->get_children())
        {
            destroy_dependent_components_in_children<Ts...>(r, child);
        }
    }
}

/// Raised by scene::detach_instance_link around its removal, so the unpack hook below knows
/// this particular removal is a detach rather than an unlink.
auto keep_prefab_ids_depth() -> int&
{
    static thread_local int depth = 0;
    return depth;
}

/// Entry point for on_destroy<prefab_component>. Kept at exactly (registry&, entity) so
/// entt's connect<> can bind it.
template<typename ...Ts>
void destroy_dependent_components_recursive(entt::registry& r, entt::entity e)
{
    if(!r.valid(e))
    {
        return;
    }

    // A detach, not an unpack: the link goes, the ids stay. See scene::detach_instance_link.
    if(keep_prefab_ids_depth() > 0)
    {
        return;
    }

    // The entity whose prefab_component is going away: strip it, then descend. The
    // instance-root check is deliberately not applied here - this *is* the instance root.
    destroy_dependent_components<Ts...>(r, e);

    auto transform = r.try_get<transform_component>(e);
    if(transform)
    {
        for(auto child : transform->get_children())
        {
            destroy_dependent_components_in_children<Ts...>(r, child);
        }
    }
}

auto on_load_callback(hpp::span<const entt::handle> entities) -> void
{
    if(!entities.empty())
    {   
        APP_SCOPE_PERF("On Load Callback");
        auto& ctx = engine::context();
        auto& play = ctx.get_cached<play_mode>();

        if(play.is_simulation_running())
        {
            auto& rsys = ctx.get_cached<rendering_system>(); 
            auto& ssys = ctx.get_cached<script_system>();

            delta_t dt(0.016667f);
            rsys.on_play_begin(entities, dt);
            ssys.on_play_begin(entities);
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

    on_construct<root_component>(*registry).connect<&root_component::on_create_component>();
    on_update<root_component>(*registry).connect<&root_component::on_update_component>();
    on_destroy<root_component>(*registry).connect<&root_component::on_destroy_component>();

    on_construct<transform_component>(*registry).connect<&transform_component::on_create_component>();
    on_destroy<transform_component>(*registry).connect<&transform_component::on_destroy_component>();

    on_construct<model_component>(*registry).connect<&model_component::on_create_component>();
    on_destroy<model_component>(*registry).connect<&model_component::on_destroy_component>();

    on_construct<animation_component>(*registry).connect<&animation_system::on_create_component>();
    on_destroy<animation_component>(*registry).connect<&animation_system::on_destroy_component>();

    on_construct<physics_component>(*registry).connect<&physics_system::on_create_component>();
    on_destroy<physics_component>(*registry).connect<&physics_system::on_destroy_component>();

    on_construct<character_controller_component>(*registry).connect<&physics_system::on_create_cc_component>();
    on_destroy<character_controller_component>(*registry).connect<&physics_system::on_destroy_cc_component>();

    on_construct<prefab_component>(*registry).connect<&owned_component::on_create_component<prefab_component>>();
    // on_update too: emplace_or_replace / replace / patch on an existing component assign a
    // whole object over the live one, owner handle included, and fire only this signal. Without
    // re-stamping here a replaced instance is left with a null owner - a crash waiting in any
    // code that trusts get_owner().
    on_update<prefab_component>(*registry).connect<&owned_component::on_create_component<prefab_component>>();
    on_destroy<prefab_component>(*registry).connect<&owned_component::on_destroy_component<prefab_component>>();

    on_destroy<prefab_component>(*registry).connect<&destroy_dependent_components_recursive<prefab_id_component>>();


    on_construct<script_component>(*registry).connect<&script_component::on_create_component>();
    on_destroy<script_component>(*registry).connect<&script_component::on_destroy_component>();

    on_construct<ui_document_component>(*registry).connect<&ui_system::on_create_component>();
    on_destroy<ui_document_component>(*registry).connect<&ui_system::on_destroy_component>();
    on_load<ui_document_component>(*registry).connect<&ui_system::on_load_component>();

    on_construct<particle_emitter_component>(*registry).connect<&particle_emitter_component::on_create_component>();
    on_destroy<particle_emitter_component>(*registry).connect<&particle_emitter_component::on_destroy_component>();

    // Activation must refresh a probe whose product cubemaps were released while inactive.
    // Wired here (not play-gated like the script/audio hooks) because the editor's active
    // toggle drives the same transform-flags path in edit mode.
    on_construct<active_component>(*registry).connect<&reflection_probe_system::on_create_active_component>();
    

}

scene::~scene()
{
    unload();
    unregister_scene(this);
}

void scene::unload()
{
    // Bulk teardown: every entity goes at once, so contact exit callbacks would be
    // noise - the guard turns them off for the duration.
    scoped_destroy_suppression no_pre_destroy;

    // Same order the single-entity path uses: physics leaves the world before the
    // scripts that might otherwise be called back into, and both before the untyped
    // clear() tears down everything else in whatever order the pools happen to sit in.
    registry->clear<physics_component>();
    registry->clear<character_controller_component>();
    registry->clear<script_component>();
    registry->clear();
    auto reserved_entity = registry->create();
    source = {};
}

void scene::reload(bool call_callbacks)
{
    auto src = source;  
    if(!src)
    {
        return;
    }
    unload();   
    load_from(src, call_callbacks);
}

auto scene::load_from(const asset_handle<scene_prefab>& pfb, bool call_callbacks) -> bool
{
    if(call_callbacks)
    {   
        push_on_load_callbacks({on_load_callback});
    }

    bool result = load_from_prefab(pfb, *this);

    if(result)
    {
        source = pfb;
    }
    if(call_callbacks)
    {
        pop_on_load_callbacks();
    }
    return result;
}
auto scene::instantiate_out(const asset_handle<prefab>& pfb, entt::handle& e, bool call_callbacks) -> bool
{
    return instantiate_out(*registry, pfb, e, call_callbacks);
}

auto scene::instantiate_out(entt::registry& reg, const asset_handle<prefab>& pfb, entt::handle& e, bool call_callbacks) -> bool
{
    APP_SCOPE_PERF("Instantiate Out Prefab");
    if(call_callbacks)
    {
        push_on_load_callbacks({on_load_callback});
    }
    bool result = load_from_prefab_out(pfb, reg, e);
    if(call_callbacks)
    {
        pop_on_load_callbacks();
    }
    return result;
}

auto scene::instantiate(const asset_handle<prefab>& pfb, bool call_callbacks) -> entt::handle
{
    APP_SCOPE_PERF_OWNED(fmt::format("Instantiate Prefab {}", pfb.id()));
    if(call_callbacks)
    {
        push_on_load_callbacks({on_load_callback});
    }
    auto e = load_from_prefab(pfb, *registry);
    if(call_callbacks)
    {
        pop_on_load_callbacks();
    }
    return e;
}

auto scene::instantiate(const asset_handle<prefab>& pfb, entt::handle parent, bool call_callbacks) -> entt::handle
{
    APP_SCOPE_PERF_OWNED(fmt::format("Instantiate Prefab {}", pfb.id()));
    auto load_callback_override = [&](hpp::span<const entt::handle> entities) -> void
    {
        if(parent && !entities.empty())
        {
            auto e = entities[0];
            if(e)
            {
                auto& trans_comp = e.get<transform_component>();
                trans_comp.set_parent(parent, false);
            }
            else
            {
                APPLOG_ERROR("Entity not found in instantiate callback");
            }
            
        }

        if(call_callbacks)
        {
            on_load_callback(entities);
        }
    };
    push_on_load_callbacks({load_callback_override});
    auto e = load_from_prefab(pfb, *registry);
    pop_on_load_callbacks();

    return e;
}

auto scene::create_entity(entt::registry& r, const std::string& name, entt::handle parent) -> entt::handle
{
    entt::handle ent(r, r.create());
    ent.emplace<tag_component>().name = !name.empty() ? name : "Entity";
    ent.emplace<layer_component>();
    ent.emplace<id_component>().regenerate_id();

    auto& transform = ent.emplace<transform_component>();
    if(parent)
    {
        transform.set_parent(parent, false);
    }

    return ent;
}

auto find_entity_by_uuid(entt::registry& registry, const hpp::uuid& target_uuid) -> entt::handle
{
    if(target_uuid.is_nil())
    {
        return {};
    }
    auto view = registry.view<id_component>();
    for(auto e : view)
    {
        if(view.get<id_component>(e).id == target_uuid)
        {
            return entt::handle(registry, e);
        }
    }
    return {};
}

auto scene::find_entity_by_uuid(const hpp::uuid& target_uuid) const -> entt::handle
{
    return ::unravel::find_entity_by_uuid(*registry, target_uuid);
}

auto scene::get_scene(entt::handle entity) -> scene*
{
    if(!entity)
    {
        return nullptr;
    }

    for(auto scene : get_all_scenes())
    {
        if(scene->registry.get() == entity.registry())
        {
            return scene;
        }
    }
    return nullptr;
}

auto scene::create_entity(const std::string& tag, entt::handle parent) -> entt::handle
{
    return create_entity(*registry, tag, parent);
}

void scene::clone_entity(entt::handle& clone_to, entt::handle clone_from, bool keep_parent, bool call_callbacks)
{
    APP_SCOPE_PERF("Clone Entity");

    auto load_callback_override = [&](hpp::span<const entt::handle> entities) -> void
    {
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

        if(call_callbacks)
        {
            on_load_callback(entities);
        }
    };
    push_on_load_callbacks({load_callback_override});
    clone_entity_from_stream(clone_from, clone_to);
    pop_on_load_callbacks();

}

auto scene::clone_entity(entt::handle clone_from, bool keep_parent, bool call_callbacks) -> entt::handle
{
    APP_SCOPE_PERF("Clone Entity To");

    auto* reg = clone_from.registry();

    // clone_entity_from_stream takes a handle, and the loader behind it reassigns that
    // handle to entities it creates itself rather than filling in the one it was given.
    // So this scratch entity exists only to carry the registry through that API, and is
    // then left over - a componentless entity per clone unless it is cleaned up here.
    const auto stub = reg->create();
    entt::handle clone_to(*reg, stub);
    clone_entity(clone_to, clone_from, keep_parent, call_callbacks);

    if(reg->valid(stub) && (!clone_to || clone_to.entity() != stub))
    {
        // Never visible to gameplay, so its teardown must not announce anything.
        scoped_destroy_suppression no_announce;
        destroy_entity(entt::handle(*reg, stub));
    }

    return clone_to;
}

void scene::clone_scene(const scene& src_scene, scene& dst_scene, bool call_callbacks)
{
    APP_SCOPE_PERF("Clone Scene");
    if(call_callbacks)
    {
        push_on_load_callbacks({on_load_callback});
    }
    clone_scene_from_stream(src_scene, dst_scene);
    if(call_callbacks)
    {
        pop_on_load_callbacks();
    }
}

void scene::clear_entity(entt::handle& handle)
{
    remove_all_components(handle);
}

scene::scoped_destroy_suppression::scoped_destroy_suppression()
{
    ++destroy_suppression_depth();
}

scene::scoped_destroy_suppression::~scoped_destroy_suppression()
{
    --destroy_suppression_depth();
}

auto scene::is_destroy_suppressed() -> bool
{
    return destroy_suppression_depth() > 0;
}

void scene::adopt_document_statements(entt::handle root)
{
    if(!root)
    {
        return;
    }
    auto* prefab_comp = root.try_get<prefab_component>();
    if(prefab_comp == nullptr)
    {
        return;
    }
    prefab_comp->local.merge(prefab_comp->from_document);
    prefab_comp->from_document.clear();
}

void scene::detach_instance_link(entt::handle entity)
{
    if(!entity || !entity.all_of<prefab_component>())
    {
        return;
    }
    // What the document stated about the nested content is this scene's now.
    re_home_document_statements(entity);
    ++keep_prefab_ids_depth();
    entity.remove<prefab_component>();
    --keep_prefab_ids_depth();
}

void scene::destroy_entity(entt::handle entity)
{
    if(!entity)
    {
        return;
    }

    if(!is_destroy_suppressed())
    {
        auto* bus = entity.registry()->ctx().find<on_pre_destroy_bus>();

        // Nobody listening is the common case - edit mode, and play mode with no
        // physics world - and it must cost nothing.
        if(bus != nullptr && !bus->sig.empty())
        {
            // Announce the entire subtree BEFORE anything is torn down, so every slot
            // sees a whole entity with whole ancestors. Snapshot first: a slot may
            // destroy entities, including ones still ahead in this walk.
            hpp::small_vector<entt::handle, 16> subtree;
            collect_subtree_post_order(entity, subtree);

            for(const auto& node : subtree)
            {
                if(node)
                {
                    bus->sig.publish(*node.registry(), node.entity());
                }
            }
        }
    }

    // A slot may already have destroyed the root out from under us.
    if(entity)
    {
        // Suppressed for the teardown itself. Destroy hooks run inside
        // entt::registry::destroy, where reaching script code is exactly what the
        // announcement above exists to avoid; anything that needed to talk to gameplay
        // has already had its turn.
        scoped_destroy_suppression teardown;

        // One of only two raw destroys in the codebase - this one, and the child
        // cascade in transform_component::on_destroy_component. Everywhere else calls
        // scene::destroy_entity, so that nothing can be torn down without the
        // subsystems holding per-entity state getting a chance to settle it first.
        entity.destroy();
    }
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

namespace entt
{
uhandle::uhandle(entt::handle handle)
{
    if(handle)
    {
        registry = handle.registry();
        if(auto id = handle.try_get<unravel::id_component>())
        {
            uuid = id->id;
        }
    }
}
auto uhandle::resolve() const -> entt::handle
{
    return ::unravel::find_entity_by_uuid(*registry, uuid);
}
}
