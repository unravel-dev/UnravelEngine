#pragma once
#include <engine/animation/animation_pose.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/model.h>
#include <unordered_map>
namespace unravel
{
class material;

/**
 * @class model_component
 * @brief Class that contains core data for meshes.
 */
class model_component : public component_crtp<model_component, owned_component>
{
public:

    /**
     * @brief Called when the component is created.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_create_component(entt::registry& r, entt::entity e);

    /**
     * @brief Called when the component is destroyed.
     * @param r The registry containing the component.
     * @param e The entity associated with the component.
     */
    static void on_destroy_component(entt::registry& r, entt::entity e);

    /**
     * @brief Sets whether the model is enabled.
     * @param enabled True if the model is enabled, false otherwise.
     */
    void set_enabled(bool enabled);
    /**
     * @brief Sets whether the model casts shadows.
     * @param cast_shadow True if the model casts shadows, false otherwise.
     */
    void set_casts_shadow(bool cast_shadow);

    /**
     * @brief Sets whether the model casts reflections.
     * @param casts_reflection True if the model casts reflections, false otherwise.
     */
    void set_casts_reflection(bool casts_reflection);

    /**
     * @brief Sets whether the model is static.
     * @param is_static True if the model is static, false otherwise.
     */
    void set_static(bool is_static);

    /**
     * @brief Checks if the model is enabled.
     * @return True if the model is enabled, false otherwise.
     */
    auto is_enabled() const -> bool;

    /**
     * @brief Checks if the model casts shadows.
     * @return True if the model casts shadows, false otherwise.
     */
    auto casts_shadow() const -> bool;

    /**
     * @brief Checks if the model casts reflections.
     * @return True if the model casts reflections, false otherwise.
     */
    auto casts_reflection() const -> bool;

    /**
     * @brief Checks if the model is static.
     * @return True if the model is static, false otherwise.
     */
    auto is_static() const -> bool;

    /**
     * @brief Gets the model.
     * @return A constant reference to the model.
     */
    auto get_model() const -> const model&;

    /**
     * @brief Sets the model.
     * @param model The model to set.
     */
    void set_model(const model& model);

    /**
     * @brief Gets the bone transforms.
     * @return A constant reference to the vector of bone transforms.
     */
    auto get_bone_transforms() const -> const pose_mat4&;

    /**
     * @brief Gets the submesh transforms.
     * @return A constant reference to the submesh pose structure.
     */
    auto get_submesh_transforms() const -> const submesh_pose_mat4&;

    /**
     * @brief Gets the armature entities.
     * @return A constant reference to the vector of armature entity handles.
     */
    auto get_armature_entities() const -> const std::vector<entt::handle>&;
    auto get_armature_by_index(size_t index) const -> entt::handle;
    
    /**
     * @brief Gets armature index by name using cached lookup (O(1)).
     * @param node_name The name of the node to find.
     * @return The index of the armature node, or -1 if not found.
     */
    auto get_armature_index_by_name_cached(const std::string& node_name) const -> int;
    
    auto get_skinning_transforms() const -> const std::vector<pose_mat4>&;

    /**
     * @brief Updates the armature of the model.
     */
    auto init_armature(bool force) -> bool;
    auto update_armature() -> bool;

    /**
     * @brief Sets the armature entities.
     * @param submesh_entities A vector of handles to the armature entities.
     */
    void set_armature_entities(const std::vector<entt::handle>& submesh_entities);

    /**
     * @brief Gets the local bounding box for this mesh.
     *
     * @return const math::bbox& The bounding box.
     */
    auto get_world_bounds() const -> const math::bbox&;
    auto get_world_bounds_transform() const -> const math::transform&;

    void update_world_bounds(const math::transform& bounds);
    auto get_local_bounds(uint32_t lod_index) const -> const math::bbox&;

    void set_last_render_frame(uint64_t frame);
    auto is_newly_created() const noexcept -> bool;
    auto get_last_render_frame() const noexcept -> uint64_t;
    auto was_used_last_frame() const noexcept -> bool;

    auto is_skinned() const -> bool;
    auto get_bind_pose() const -> const animation_pose&;

    /**
     * @brief Gets the per-view LOD data for a specific camera/view.
     * Creates a new entry if this is the first access from this view.
     * @param view_id Unique identifier for the view (typically camera pointer as uintptr_t).
     * @param current_frame Current render frame number for tracking access.
     * @return Reference to the LOD data for this view.
     */
    auto get_lod_data_for_camera(const camera* cam, uint64_t current_frame) -> lod_data&;
    
    /**
     * @brief Cleans up stale per-view LOD data entries that haven't been accessed recently.
     * Call this periodically (e.g., once per frame) to prevent unbounded growth.
     * @param current_frame Current render frame number.
     * @param max_frames_inactive Maximum frames since last access before cleanup (default: 120 = ~2 seconds at 60fps).
     */
    void cleanup_stale_lod_data(uint64_t current_frame, uint64_t max_frames_inactive = 120);

private:
    auto create_armature(bool force) -> bool;
    
    /**
     * @brief Rebuilds the armature name-to-index cache.
     * Called automatically when armature entities are set.
     */
    void rebuild_armature_cache();

    /**
     * @brief Indicates if the model is enabled.
     */
    bool enabled_ = true;

    /**
     * @brief Indicates if the model is static.
     */
    bool static_ = true;

    /**
     * @brief Indicates if the model casts shadows.
     */
    bool casts_shadow_ = true;

    /**
     * @brief Indicates if the model casts reflections.
     */
    bool casts_reflection_ = true;

    /**
     * @brief The model object.
     */
    model model_;

    /**
     * @brief Vector of handles to the armature entities.
     */
    std::vector<entt::handle> armature_entities_;
    
    /**
     * @brief Cached name-to-index mapping for fast armature lookup.
     * Rebuilt automatically when armature_entities_ changes.
     */
    std::unordered_map<std::string, size_t> armature_name_to_index_;

    /**
     * @brief Bind pose or reference pose.
     */
    animation_pose bind_pose_;
    /**
     * @brief Vector of bone transforms.
     */
    pose_mat4 bone_pose_;

    /**
     * @brief Vector of submesh transforms.
     */
    submesh_pose_mat4 submesh_pose_;

    /**
     * @brief Skinning pose per palette
     */
    std::vector<pose_mat4> skinning_pose_;

    /**
     * @brief World bounds
     */
    math::bbox world_bounds_;

    /**
     * @brief World bounds transform which was used.
     */
    math::transform world_bounds_transform_;

    /**
     * @brief Last frame this model was rendered.
     */
    uint64_t last_render_frame_{};

    /**
     * @brief Per-camera LOD state map.
     * Key: view identifier (camera pointer as uintptr_t)
     * Value: LOD state and last access frame for that camera
     * Automatically cleaned up when cameras become stale.
     */
    std::unordered_map<uintptr_t, per_camera_lod_state> per_camera_lod_data_;
};

struct bone_component : public component_crtp<bone_component>
{
    uint32_t bone_index{};
};

struct submesh_component : public component_crtp<submesh_component>
{
    std::vector<uint32_t> submeshes{};
};

} // namespace unravel
