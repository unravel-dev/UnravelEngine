#pragma once
#include <engine/engine_export.h>

#include <uuid/uuid.h>

#include "prefab.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <entt/entt.hpp>
#include <entt/signal/sigh.hpp>
#include <functional>

using namespace entt::literals;

namespace unravel
{

template<class T>
struct on_load_bus
{
    using sink_t = typename entt::sink<entt::sigh<void(entt::registry&, entt::entity)>>;
    entt::sigh<void(entt::registry&, entt::entity)> sig;
};

template<class T>
inline auto on_load(entt::registry& reg) -> typename on_load_bus<T>::sink_t
{
    auto& ctx = reg.ctx();
    if(!ctx.contains<on_load_bus<T>>())
    {
        return typename on_load_bus<T>::sink_t(ctx.emplace<on_load_bus<T>>().sig);
    }
    return typename on_load_bus<T>::sink_t(ctx.get<on_load_bus<T>>().sig);
}

template<class T>
inline auto on_construct(entt::registry& reg) -> decltype(auto)
{
    return reg.on_construct<T>();
}

template<class T>
inline auto on_update(entt::registry& reg) -> decltype(auto)
{
    return reg.on_update<T>();
}

template<class T>
inline auto on_destroy(entt::registry& reg) -> decltype(auto)
{
    return reg.on_destroy<T>();
}

template<class T>
inline void emit_on_load(entt::registry& reg, entt::entity e)
{
    if (auto* bus = reg.ctx().find<on_load_bus<T>>())
    {
        bus->sig.publish(reg, e);
    }
}

/**
 * @struct on_pre_destroy_bus
 * @brief Announces every entity of a subtree that is about to be destroyed.
 *
 * Published from scene::destroy_entity strictly BEFORE entt::registry::destroy is
 * entered, and before any component is removed. That is the only point where such work
 * can legally happen: registry::destroy removes components in reverse pool-registration
 * order - an accident of which type's storage was assured first - and EnTT documents
 * that adding or removing elements on an entity being destroyed is undefined behaviour.
 *
 * Inside a slot the whole subtree is still intact: every component present, script
 * objects still pinned. Slots may add or remove components, run script code, and
 * destroy other entities, including the one being announced.
 *
 * Two rules for subscribers:
 *   - Mutate your own state BEFORE notifying anyone, so a re-entrant destroy of the
 *     same entity finds no remaining work.
 *   - Be idempotent. A slot that destroys the entity currently being announced causes
 *     a second pass over the same subtree.
 */
struct on_pre_destroy_bus
{
    using sink_t = typename entt::sink<entt::sigh<void(entt::registry&, entt::entity)>>;
    entt::sigh<void(entt::registry&, entt::entity)> sig;
};

inline auto on_pre_destroy(entt::registry& reg) -> typename on_pre_destroy_bus::sink_t
{
    auto& ctx = reg.ctx();
    if(!ctx.contains<on_pre_destroy_bus>())
    {
        return typename on_pre_destroy_bus::sink_t(ctx.emplace<on_pre_destroy_bus>().sig);
    }
    return typename on_pre_destroy_bus::sink_t(ctx.get<on_pre_destroy_bus>().sig);
}


#define ENTT_TAG(name) entt::tag<name##_hs>
/**
 * @struct scene
 * @brief Represents a scene in the ACE framework, managing entities and their relationships.
 */
struct scene
{
    /**
     * @brief Constructs a new scene.
     */
    scene(const std::string& tag_name);

    /**
     * @brief Destroys the scene and cleans up resources.
     */
    ~scene();

    static auto get_all_scenes() -> const std::vector<scene*>&;
    /**
     * @brief Loads a scene from a prefab asset.
     * @param pfb The asset handle to the scene prefab.
     * @return True if the scene was loaded successfully, false otherwise.
     */
    auto load_from(const asset_handle<scene_prefab>& pfb, bool call_callbacks = true) -> bool;

    /**
     * @brief Reloads the scene from the source prefab asset.
     * @param call_callbacks Whether to call the load callbacks.
     */
    void reload(bool call_callbacks = true);
    /**
     * @brief Unloads the scene, removing all entities.
     */
    void unload();

    /**
     * @brief Instantiates a prefab in the scene.
     * @param pfb The asset handle to the prefab.
     * @return A handle to the instantiated entity.
     */
    auto instantiate_out(const asset_handle<prefab>& pfb, entt::handle&, bool call_callbacks = true) -> bool;
    static auto instantiate_out(entt::registry& reg, const asset_handle<prefab>& pfb, entt::handle&, bool call_callbacks = true) -> bool;
    auto instantiate(const asset_handle<prefab>& pfb, bool call_callbacks = true) -> entt::handle;
    auto instantiate(const asset_handle<prefab>& pfb, entt::handle parent, bool call_callbacks = true) -> entt::handle;

    void clear_entity(entt::handle& handle);

    /**
     * @brief Destroys an entity and its subtree.
     *
     * The single funnel for entity destruction. Announces the whole subtree on
     * on_pre_destroy - children before parents, with everything still intact - and only
     * then destroys. Safe to call re-entrantly from a slot.
     *
     * Prefer this over entt::handle::destroy anywhere gameplay can observe the entity.
     * A raw destroy skips the announcement, so subsystems holding per-entity state get
     * no chance to settle it while the entity is still readable.
     */
    static void destroy_entity(entt::handle entity);

    /**
     * @struct scoped_destroy_suppression
     * @brief Skips the pre-destroy announcement for bulk teardown.
     *
     * Scene unload, play end and editor scratch entities all destroy en masse, where
     * exit callbacks are noise at best and reentrancy hazards at worst. Nested-safe.
     */
    struct scoped_destroy_suppression
    {
        scoped_destroy_suppression();
        ~scoped_destroy_suppression();

        scoped_destroy_suppression(const scoped_destroy_suppression&) = delete;
        scoped_destroy_suppression(scoped_destroy_suppression&&) = delete;
        auto operator=(const scoped_destroy_suppression&) -> scoped_destroy_suppression& = delete;
        auto operator=(scoped_destroy_suppression&&) -> scoped_destroy_suppression& = delete;
    };

    /**
     * @brief Whether a scoped_destroy_suppression is currently active.
     */
    static auto is_destroy_suppressed() -> bool;

    /**
     * @brief Removes an entity's prefab link while keeping its subtree's prefab ids.
     *
     * Removing prefab_component normally *unpacks* the instance: an on_destroy hook strips
     * prefab_id_component down the subtree, because an unlinked entity has no prefab for those
     * ids to mean anything in. That is right for the user's Unlink. It is wrong for the one
     * other reason to drop the link - turning an instantiated prefab into the authoring root
     * of that same prefab - where the ids are the identities the file is keyed by. Regenerating
     * them on the next save makes every instance of the prefab fail to match its own root,
     * rebuild it, and lose whatever was nested under the old one.
     *
     * Also resets, on the instances directly nested under the entity, the memo of what "the
     * containing document" stated - because in an authoring root that document is this one.
     * Everything those instances record is this document's own authoring and has to read as
     * local, or the prefab's editor shows its own overrides as "from prefab" and cannot revert
     * them.
     */
    static void detach_instance_link(entt::handle entity);

    /**
     * @brief Makes everything the instances directly nested under `root` record read as
     *        `root`'s own authoring.
     *
     * Each nested instance carries a memo of what "the containing document" stated about it.
     * When `root` is the content of the prefab being edited, that document is this one - so
     * the memo has to be empty, or the prefab's own overrides show as "from prefab" and cannot
     * be reverted. Directly nested only: deeper instances still have a containing document
     * above them, the instance they sit in, and its attribution stays right.
     *
     * Used by detach_instance_link, and on its own when the root keeps its instance link.
     */
    static void reset_nested_inheritance(entt::handle root);

    /**
     * @brief Creates an entity in the scene.
     * @param e The entity identifier.
     * @return A handle to the created entity.
     */
    auto create_handle(entt::entity e) -> entt::handle;

    /**
     * @brief Creates an entity in the scene (const version).
     * @param e The entity identifier.
     * @return A constant handle to the created entity.
     */
    auto create_handle(entt::entity e) const -> entt::const_handle;

    /**
     * @brief Creates an entity in the scene with an optional tag and parent.
     * @param tag The tag for the entity.
     * @param parent The parent entity handle.
     * @return A handle to the created entity.
     */
    auto create_entity(const std::string& tag = {}, entt::handle parent = {}) -> entt::handle;

    /**
     * @brief Clones an existing entity in the scene.
     * @param e The handle to the entity to clone.
     * @param keep_parent Whether to keep the parent relationship.
     * @return A handle to the cloned entity.
     */
    auto clone_entity(entt::handle clone_from, bool keep_parent = true, bool call_callbacks = true) -> entt::handle;
    void clone_entity(entt::handle& clone_to, entt::handle clone_from, bool keep_parent = true, bool call_callbacks = true);

    /**
     * @brief Creates an entity in the specified registry with an optional name and parent.
     * @param r The registry to create the entity in.
     * @param name The name for the entity.
     * @param parent The parent entity handle.
     * @return A handle to the created entity.
     */
    static auto create_entity(entt::registry& r, const std::string& name = {}, entt::handle parent = {}) -> entt::handle;

    /**
     * @brief Clones the entities from one scene to another.
     * @param src_scene The source scene.
     * @param dst_scene The destination scene.
     */
    static void clone_scene(const scene& src_scene, scene& dst_scene, bool call_callbacks = true);

    /**
     * @brief Finds an entity by UUID in the scene.
     * @param uuid The UUID of the entity to find.
     * @return The entity handle if found, otherwise an empty handle.
     */
    static auto find_entity_by_prefab_uuid(entt::handle root_entity, const hpp::uuid& target_uuid) -> entt::handle;

        /**
     * @brief Finds an entity by UUID in the scene.
     * @param uuid The UUID of the entity to find.
     * @return The entity handle if found, otherwise an empty handle.
     */
     auto find_entity_by_uuid(const hpp::uuid& target_uuid) const -> entt::handle;

     /**
      * @brief Gets the scene from an entity handle.
      * @param entity The entity handle.
      * @return The scene if found, otherwise nullptr.
      */
     static auto get_scene(entt::handle entity) -> scene*;


    /**
     * @brief The source prefab asset handle for the scene.
     */
    asset_handle<scene_prefab> source;

    /**
     * @brief The registry that manages all entities in the scene.
     */
    std::unique_ptr<entt::registry> registry{};


    std::string tag{};
};


#define TAG_COMPONENT(name) entt::tag<name##_hs>

/**
 * @brief Finds an entity by id_component UUID in a registry (no scene wrapper required).
 */
ENGINE_EXPORT auto find_entity_by_uuid(entt::registry& registry, const hpp::uuid& target_uuid) -> entt::handle;


} // namespace unravel

namespace entt
{
struct uhandle
{
    uhandle() = default;
    uhandle(entt::handle handle);
    uhandle(entt::registry& registry, const hpp::uuid& uuid) : registry(&registry), uuid(uuid) {}
    uhandle(const uhandle& other) = default;
    uhandle(uhandle&& other) = default;
    auto operator=(const uhandle& other) -> uhandle& = default;
    auto operator=(uhandle&& other) -> uhandle& = default;
    ~uhandle() = default;
    auto resolve() const -> entt::handle;
    auto operator==(const uhandle& other) const -> bool
    {
        return registry == other.registry && uuid == other.uuid;
    }
    auto operator!=(const uhandle& other) const -> bool
    {
        return !(*this == other);
    }
    entt::registry* registry{};
    hpp::uuid uuid{};
};

inline auto make_uhandle(entt::handle handle) -> uhandle
{
    return uhandle(handle);
}
}