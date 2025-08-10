#pragma once
#include <engine/engine_export.h>

#include "prefab.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <entt/entt.hpp>

using namespace entt::literals;

namespace unravel
{

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
    auto load_from(const asset_handle<scene_prefab>& pfb) -> bool;

    /**
     * @brief Unloads the scene, removing all entities.
     */
    void unload();

    /**
     * @brief Instantiates a prefab in the scene.
     * @param pfb The asset handle to the prefab.
     * @return A handle to the instantiated entity.
     */
    auto instantiate_out(const asset_handle<prefab>& pfb, entt::handle& e) -> bool;
    auto instantiate(const asset_handle<prefab>& pfb) -> entt::handle;
    auto instantiate(const asset_handle<prefab>& pfb, entt::handle parent) -> entt::handle;

    void clear_entity(entt::handle& handle);
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
    auto clone_entity(entt::handle clone_from, bool keep_parent = true) -> entt::handle;
    void clone_entity(entt::handle& clone_to, entt::handle clone_from, bool keep_parent = true);

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
    static void clone_scene(const scene& src_scene, scene& dst_scene);

    /**
     * @brief Finds an entity by UUID in the scene.
     * @param uuid The UUID of the entity to find.
     * @return The entity handle if found, otherwise an empty handle.
     */
    static auto find_entity_by_prefab_uuid(entt::handle root_entity, const hpp::uuid& target_uuid) -> entt::handle;

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

} // namespace unravel
