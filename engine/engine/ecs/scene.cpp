#include <engine/animation/ecs/components/ik_components.h>
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

#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/audio/ecs/components/audio_listener_component.h>

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
#include <hpp/string_view.hpp>

#include <utility>
#include <vector>

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

//         if(name.find("edyn::") != hpp::string_view::npos)
//         {
//             continue;
//         }

//         if(name.find("bullet::") != hpp::string_view::npos)
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

/// An additive load is an instance of its scene, as a prefab instance is: its entities get
/// fresh ids before anything starts, so a second copy of the same scene shares none.
void regenerate_ids(hpp::span<const entt::handle> entities)
{
    for(const auto& entity : entities)
    {
        if(auto* id = entity.try_get<id_component>())
        {
            id->regenerate_id();
        }
    }
}

/**
 * @brief Makes every root created while it lives join a group.
 */
class scoped_new_root_group
{
public:
    scoped_new_root_group(entt::registry& registry, scene_group_id group)
        : groups_(registry.ctx().emplace<scene_group_context>())
        , previous_(std::exchange(groups_.new_root_group, group))
    {
    }

    ~scoped_new_root_group()
    {
        groups_.new_root_group = previous_;
    }

    scoped_new_root_group(const scoped_new_root_group&) = delete;
    auto operator=(const scoped_new_root_group&) -> scoped_new_root_group& = delete;

private:
    /// Registry context entries are heap allocated, so the reference outlives entries added during the load.
    scene_group_context& groups_;
    scene_group_id previous_{};
};

/// Bounds a partial teardown: each pass takes what scripts created while the previous one was destroyed.
constexpr int MAX_TEARDOWN_PASSES = 16;

/// The roots of a group, collected first so destroying them can not disturb the walk.
auto collect_group_roots(entt::registry& registry, scene_group_id group) -> std::vector<entt::handle>
{
    std::vector<entt::handle> roots;
    auto view = registry.view<root_component>();
    for(auto e : view)
    {
        if(view.get<root_component>(e).group == group)
        {
            roots.emplace_back(registry, e);
        }
    }
    return roots;
}

/**
 * @struct switch_teardown
 * @brief What a scene switch removes when persistent roots stay.
 */
struct switch_teardown
{
    /// Every root outside the persistent group, and every entity outside any hierarchy (no transform, so no
    /// root and no group) that holds a component. The bare entity unload() reserves holds none and stays.
    std::vector<entt::handle> doomed;
    bool has_persistent_roots{};
};

auto collect_switch_teardown(entt::registry& registry) -> switch_teardown
{
    switch_teardown teardown;
    auto roots = registry.view<root_component>();
    for(auto e : roots)
    {
        if(roots.get<root_component>(e).group == PERSISTENT_SCENE_GROUP)
        {
            teardown.has_persistent_roots = true;
            continue;
        }
        teardown.doomed.emplace_back(registry, e);
    }
    for(auto [e] : registry.storage<entt::entity>().each())
    {
        if(!registry.all_of<transform_component>(e) && !registry.orphan(e))
        {
            teardown.doomed.emplace_back(registry, e);
        }
    }
    return teardown;
}

/// Destroys entities one by one through the announced path, so entities that stay get the exits of their
/// contacts with what goes, which a bulk unload mutes. Scripts torn down here may create entities (OnDestroy
/// spawning loot); `collect_more` finds those, and they go in the next pass.
template<typename Collect>
void destroy_announced(std::vector<entt::handle> doomed, Collect collect_more)
{
    for(int pass = 0; !doomed.empty(); ++pass)
    {
        if(pass == MAX_TEARDOWN_PASSES)
        {
            APPLOG_WARNING("Scripts kept creating entities while a scene was unloaded. The last ones stay.");
            return;
        }
        for(const auto& entity : doomed)
        {
            scene::destroy_entity(entity);
        }
        doomed = collect_more();
    }
}

/// Warns when `group` brings an active Component while another loaded scene has one active too.
template<typename Component>
void warn_if_active_in_other_groups(entt::registry& registry,
                                    scene_group_id group,
                                    hpp::string_view scene_name,
                                    hpp::string_view what)
{
    // Read through the const registry: a storage nobody made stays unmade. With fewer than two there is no clash.
    auto view = std::as_const(registry).view<Component, active_component>();
    if(!view || view.size_hint() < 2)
    {
        return;
    }
    bool is_in_group = false;
    bool is_elsewhere = false;
    for(auto e : view)
    {
        const bool is_member = find_scene_group(entt::handle(registry, e)) == group;
        is_in_group = is_in_group || is_member;
        is_elsewhere = is_elsewhere || !is_member;
    }
    if(is_in_group && is_elsewhere)
    {
        APPLOG_WARNING("Scene {} brings an active {} while another loaded scene has one active too. Only one of "
                       "them should stay active.",
                       scene_name,
                       what);
    }
}

/// Scenes loaded side by side each tend to carry their own camera and listener. Every active
/// camera renders the whole scene, so a second one doubles the cost. Skylights need no warning:
/// the main scene's wins (find_skylight_in_effect).
void warn_about_duplicate_singletons(entt::registry& registry, scene_group_id group, hpp::string_view scene_name)
{
    warn_if_active_in_other_groups<camera_component>(registry, group, scene_name, "camera");
    warn_if_active_in_other_groups<audio_listener_component>(registry, group, scene_name, "audio listener");
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

    registry->ctx().emplace<scene_group_context>();
    on_construct<root_component>(*registry).connect<&root_component::on_create_component>();
    on_update<root_component>(*registry).connect<&root_component::on_update_component>();
    on_destroy<root_component>(*registry).connect<&root_component::on_destroy_component>();

    on_construct<transform_component>(*registry).connect<&transform_component::on_create_component>();
    on_destroy<transform_component>(*registry).connect<&transform_component::on_destroy_component>();

    on_construct<model_component>(*registry).connect<&model_component::on_create_component>();
    on_destroy<model_component>(*registry).connect<&model_component::on_destroy_component>();

    // Shadow caster cache invalidation. Membership changes the per-frame model walk cannot
    // observe, because they take the entity OUT of the view that walk iterates (or put it
    // back in): deactivation removes active_component AFTER the transform flags were marked,
    // so the walk never gets to consume those bits. Model removal is handled in
    // model_component::on_destroy_component, which is already wired above.
    registry->ctx().emplace<shadow_caster_revision>();
    on_construct<active_component>(*registry).connect<&bump_shadow_caster_revision>();
    on_destroy<active_component>(*registry).connect<&bump_shadow_caster_revision>();

    // The component finds its entity through the owner; replace / patch reassign it.
    on_construct<animation_component>(*registry).connect<&owned_component::on_create_component<animation_component>>();
    on_update<animation_component>(*registry).connect<&owned_component::on_create_component<animation_component>>();

    // Rig modifiers find their bone through the owner; replace / patch reassign it, like prefab_component.
    on_construct<two_bone_ik_component>(*registry).connect<&owned_component::on_create_component<two_bone_ik_component>>();
    on_update<two_bone_ik_component>(*registry).connect<&owned_component::on_create_component<two_bone_ik_component>>();
    on_destroy<two_bone_ik_component>(*registry).connect<&owned_component::on_destroy_component<two_bone_ik_component>>();
    on_construct<chain_ik_component>(*registry).connect<&owned_component::on_create_component<chain_ik_component>>();
    on_update<chain_ik_component>(*registry).connect<&owned_component::on_create_component<chain_ik_component>>();
    on_destroy<chain_ik_component>(*registry).connect<&owned_component::on_destroy_component<chain_ik_component>>();
    on_construct<look_at_ik_component>(*registry).connect<&owned_component::on_create_component<look_at_ik_component>>();
    on_update<look_at_ik_component>(*registry).connect<&owned_component::on_create_component<look_at_ik_component>>();
    on_destroy<look_at_ik_component>(*registry).connect<&owned_component::on_destroy_component<look_at_ik_component>>();
    on_construct<foot_ik_component>(*registry).connect<&owned_component::on_create_component<foot_ik_component>>();
    on_update<foot_ik_component>(*registry).connect<&owned_component::on_create_component<foot_ik_component>>();
    on_destroy<foot_ik_component>(*registry).connect<&owned_component::on_destroy_component<foot_ik_component>>();
    on_construct<spring_bone_component>(*registry).connect<&owned_component::on_create_component<spring_bone_component>>();
    on_update<spring_bone_component>(*registry).connect<&owned_component::on_create_component<spring_bone_component>>();
    on_destroy<spring_bone_component>(*registry).connect<&owned_component::on_destroy_component<spring_bone_component>>();

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
    // load_from tears the scene down itself, keeping the persistent roots.
    load_from(src, call_callbacks);
}

auto scene::load_from(const asset_handle<scene_prefab>& pfb, bool call_callbacks) -> bool
{
    APP_SCOPE_PERF("Scene/Load");
    if(call_callbacks)
    {   
        push_on_load_callbacks({on_load_callback});
    }

    bool result = load_from_prefab(pfb, *this);

    if(result)
    {
        source = pfb;
        warn_about_duplicate_singletons(*registry, MAIN_SCENE_GROUP, pfb.id());
    }
    if(call_callbacks)
    {
        pop_on_load_callbacks();
    }
    return result;
}

void scene::unload_except_persistent()
{
    auto teardown = collect_switch_teardown(*registry);
    if(!teardown.has_persistent_roots)
    {
        unload();
        return;
    }
    destroy_announced(std::move(teardown.doomed),
                      [this]()
                      {
                          return collect_switch_teardown(*registry).doomed;
                      });
    source = {};
}

auto scene::reserve_scene_group() -> scene_group_id
{
    return registry->ctx().emplace<scene_group_context>().next_additive_group++;
}

auto scene::load_additive(const asset_handle<scene_prefab>& pfb, scene_group_id group, bool call_callbacks) -> bool
{
    APP_SCOPE_PERF("Scene/Load Additive");
    push_on_load_callbacks({[call_callbacks](hpp::span<const entt::handle> entities)
                            {
                                regenerate_ids(entities);
                                if(call_callbacks)
                                {
                                    on_load_callback(entities);
                                }
                            }});
    bool result{};
    {
        scoped_new_root_group joins_group(*registry, group);
        result = load_from_prefab(pfb, *registry);
    }
    pop_on_load_callbacks();
    if(result)
    {
        warn_about_duplicate_singletons(*registry, group, pfb.id());
    }
    return result;
}

void scene::unload_group(scene_group_id group)
{
    APP_SCOPE_PERF("Scene/Unload Group");
    // Roots the departing scripts create meanwhile join the group, so they go with it.
    scoped_new_root_group joins_group(*registry, group);
    destroy_announced(collect_group_roots(*registry, group),
                      [this, group]()
                      {
                          return collect_group_roots(*registry, group);
                      });
}

auto scene::make_persistent(entt::handle entity) -> bool
{
    auto* root = entity ? entity.try_get<root_component>() : nullptr;
    if(root == nullptr)
    {
        APPLOG_WARNING("Only a root entity can survive a scene load: a child goes with its root.");
        return false;
    }
    root->group = PERSISTENT_SCENE_GROUP;
    return true;
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
