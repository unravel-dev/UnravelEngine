#pragma once
#include <engine/engine_export.h>

#include "engine/rendering/camera.h"
#include "engine/rendering/reflection_probe.h"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/ecs.h>
#include <engine/rendering/light.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/model.h>
#include <engine/rendering/reflection_probe.h>

namespace unravel
{

/**
 * @struct defaults
 * @brief Provides default initialization and creation functions for various entities and assets.
 */
struct defaults
{
    /**
     * @brief Initializes default settings and assets.
     * @param ctx The context for initialization.
     * @return True if initialization was successful, false otherwise.
     */
    static auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes default settings and assets.
     * @param ctx The context for deinitialization.
     * @return True if deinitialization was successful, false otherwise.
     */
    static auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Initializes default assets.
     * @param ctx The context for initialization.
     * @return True if initialization was successful, false otherwise.
     */
    static auto init_assets(rtti::context& ctx) -> bool;

    /**
     * @brief Creates an embedded mesh entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param name The name of the entity.
     * @return A handle to the created entity.
     */
    static auto create_embedded_mesh_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle;

    /**
     * @brief Creates a prefab entity at a specified position.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param key The key of the prefab.
     * @param cam The camera to use for positioning.
     * @param pos The 2D position to place the prefab.
     * @return A handle to the created entity.
     */
    static auto create_prefab_at(rtti::context& ctx,
                                 scene& scn,
                                 const std::string& key,
                                 const camera& cam,
                                 math::vec2 pos) -> entt::handle;

    /**
     * @brief Creates a prefab entity at a specified position.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param key The key of the prefab.
     * @param pos The 3D position to place the prefab.
     * @return A handle to the created entity.
     */
    static auto create_prefab_at(rtti::context& ctx,
                                 scene& scn,
                                 const std::string& key,
                                 math::vec3 pos) -> entt::handle;

    /**
     * @brief Creates a prefab entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param key The key of the prefab.
     * @return A handle to the created entity.
     */
    static auto create_prefab_at(rtti::context& ctx,
                                 scene& scn,
                                 const std::string& key) -> entt::handle;
    /**
     * @brief Creates a mesh entity at a specified position.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param key The key of the mesh.
     * @param cam The camera to use for positioning.
     * @param pos The 2D position to place the mesh.
     * @return A handle to the created entity.
     */
    static auto create_mesh_entity_at(rtti::context& ctx,
                                      scene& scn,
                                      const std::string& key,
                                      const camera& cam,
                                      math::vec2 pos) -> entt::handle;

    /**
     * @brief Creates a mesh entity at a specified position.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param key The key of the mesh.
     * @param pos The 3D position to place the mesh.
     * @return A handle to the created entity.
     */
    static auto create_mesh_entity_at(rtti::context& ctx,
                                      scene& scn,
                                      const std::string& key,
                                      math::vec3 pos = {0.0f, 0.0f, 0.0f}) -> entt::handle;

    /**
     * @brief Creates a light entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param type The type of light to create.
     * @param name The name of the light entity.
     * @return A handle to the created entity.
     */
    static auto create_light_entity(rtti::context& ctx, scene& scn, light_type type, const std::string& name)
        -> entt::handle;

    /**
     * @brief Creates a reflection probe entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param type The type of reflection probe to create.
     * @param name The name of the reflection probe entity.
     * @return A handle to the created entity.
     */
    static auto create_reflection_probe_entity(rtti::context& ctx, scene& scn, probe_type type, const std::string& name)
        -> entt::handle;

    /**
     * @brief Creates a camera entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param name The name of the camera entity.
     * @return A handle to the created entity.
     */
    static auto create_camera_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle;


    /**
     * @brief Creates a text entity.
     * @param ctx The context for creation.
     * @param scn The scene to create the entity in.
     * @param name The name of the text entity.
     * @return A handle to the created entity.
     */
    static auto create_text_entity(rtti::context& ctx, scene& scn, const std::string& name) -> entt::handle;

    /**
     * @brief Creates a default 3D scene.
     * @param ctx The context for creation.
     * @param scn The scene to create.
     */
    static void create_default_3d_scene(rtti::context& ctx, scene& scn);

    /**
     * @brief Focuses a camera on a specified entity.
     * @param camera The camera to focus.
     * @param entity The entity to focus on.
     */
    static void focus_camera_on_entities(entt::handle camera, const std::vector<entt::handle>& entities);

    /**
     * @brief Focuses a camera on a specified entity with a timed transition.
     * @param camera The camera to focus.
     * @param entities The entities to focus on.
     * @param duration The duration of the transition in seconds.
     */
    static void focus_camera_on_entities(entt::handle camera, 
                                        const std::vector<entt::handle>& entities,
                                        float duration);

    /**
     * @brief Focuses a camera on bounds with a timed transition.
     * @param camera The camera to focus.
     * @param bounds The bounding sphere to focus on.
     * @param duration The duration of the transition in seconds.
     * @param ease_func The easing function to apply (default is smooth_start_stop).
     */
    static void focus_camera_on_bounds(entt::handle camera, 
                                      const math::bsphere& bounds,
                                      float duration);

    /**
     * @brief Focuses a camera on bounds with a timed transition.
     * @param camera The camera to focus.
     * @param bounds The bounding box to focus on.
     * @param duration The duration of the transition in seconds.
     * @param ease_func The easing function to apply (default is smooth_start_stop).
     */
    static void focus_camera_on_bounds(entt::handle camera, 
                                      const math::bbox& bounds,
                                      float duration);

    /**
     * @brief Creates a default 3D scene for asset preview.
     * @tparam T The type of the asset.
     * @param ctx The context for creation.
     * @param scn The scene to create.
     * @param asset The asset to preview.
     * @param size The size of the preview.
     */
    template<typename T>
    static void create_default_3d_scene_for_asset_preview(rtti::context& ctx,
                                                          scene& scn,
                                                          const asset_handle<T>& asset,
                                                          const usize32_t& size);

    /**
     * @brief Creates a default 3D scene for editing.
     * @param ctx The context for creation.
     * @param scn The scene to create.
     */
    static void create_default_3d_scene_for_editing(rtti::context& ctx, scene& scn);

    /**
     * @brief Calculates the bounding box of an entity.
     * @param entity The entity to calculate the bounds for.
     * @return The bounding box of the entity.
     */
    static auto calc_bounds_global(entt::handle entity, int depth = -1) -> math::bbox;

    /**
     * @brief Calculates the bounding sphere of an entity.
     * @param entity The entity to calculate the bounds for.
     * @return The bounding sphere of the entity.
     */
    static auto calc_bounds_sphere_global(entt::handle entity, bool use_bbox_diagonal = true) -> math::bsphere;

private:
    /**
     * @brief Creates a default 3D scene for preview.
     * @param ctx The context for creation.
     * @param scn The scene to create.
     * @param size The size of the preview.
     * @return A handle to the created entity.
     */
    static auto create_default_3d_scene_for_preview(rtti::context& ctx, scene& scn, const usize32_t& size)
        -> entt::handle;

            // Internal helper for timed focus transitions (shared between bbox and bsphere variants)
    static void run_camera_focus_transition(entt::handle camera,
                                            const math::vec3& target_center,
                                            float radius,
                                            bool keep_rotation,
                                            float duration);
};

} // namespace unravel
