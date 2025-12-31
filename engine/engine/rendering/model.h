#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>

#include <engine/assets/asset_handle.h>

#include "gpu_program.h"
#include "material.h"
#include "mesh.h"

#include <hpp/small_vector.hpp>

#include <graphics/graphics.h>
#include <math/math.h>
#include <reflection/registration.h>
#include <serialization/serialization.h>

#include <chrono>
#include <vector>

namespace unravel
{

/**
 * @struct lod_data
 * @brief Contains level of detail (LOD) data for an entity per view.
 * Uses distance-based hysteresis for stable LOD selection combined with time-based dithered transitions.
 */

struct lod_data
{
    std::uint32_t current_lod_index = 0; ///< Current LOD index being rendered.
    std::uint32_t target_lod_index = 0;  ///< Target LOD index to transition to.
    float current_time = 0.0f;           ///< Current time in the LOD transition.
    float transition_time = 0.0f;        ///< Total time for the LOD transition.
    float percent = 0.0f;                ///< Percentage of the model visible (0.0 to 100.0).
    irect32_t rect;                      ///< Screen rectangle of the model.
    math::vec3 center;                   ///< Center of the model in world space.
    
    void calculate_screen_rect(const camera& cam);
};

/**
 * @struct per_camera_lod_state
 * @brief Tracks LOD state and last access frame for a specific view (camera).
 */
struct per_camera_lod_state
{
    lod_data data;                       ///< LOD state for this camera.
    std::uint64_t last_access_frame = 0; ///< Last frame this view accessed this state.
};

struct submesh_pose_mat4
{
    /**
     * @brief Shared pool of unique transforms.
     * Multiple submeshes can reference the same transform by index.
     */
    std::vector<math::mat4> transforms;
    
    /**
     * @brief Maps submesh index to a list of transform indices.
     * Key: submesh_index, Value: list of indices into the transforms array
     */
    hpp::small_vector<hpp::small_vector<uint32_t>> submesh_to_transform_indices;
    
    /**
     * @brief Clears all data.
     */
    void clear()
    {
        transforms.clear();
        submesh_to_transform_indices.clear();
    }
    
    /**
     * @brief Reserves space for submeshes.
     * @param count Number of submeshes to reserve space for.
     */
    void reserve(size_t count)
    {
        submesh_to_transform_indices.resize(count);
    }
    
    /**
     * @brief Adds a transform and maps multiple submesh indices to it.
     * All submesh indices in the vector will reference the same transform.
     * @param submesh_indices Vector of submesh indices that should use this transform.
     * @param transform The transform to add.
     * @return The index of the transform in the transforms array.
     */
    auto add_transform(const std::vector<uint32_t>& submesh_indices, const math::mat4& transform) -> uint32_t
    {
        // Add the transform to the pool
        uint32_t transform_index = static_cast<uint32_t>(transforms.size());
        transforms.emplace_back(transform);
        
        // Find the maximum submesh index to ensure we have enough space
        uint32_t max_submesh_index = 0;
        for(uint32_t submesh_index : submesh_indices)
        {
            max_submesh_index = std::max(max_submesh_index, submesh_index);
        }
        
        // Resize if needed
        if(max_submesh_index >= submesh_to_transform_indices.size())
        {
            submesh_to_transform_indices.resize(max_submesh_index + 1);
        }
        
        // Map all submesh indices to this transform
        for(uint32_t submesh_index : submesh_indices)
        {
            submesh_to_transform_indices[submesh_index].emplace_back(transform_index);
        }
        
        return transform_index;
    }
    
    /**
     * @brief Gets the number of transform instances for a specific submesh.
     * @param submesh_index The index of the submesh.
     * @return The number of transforms for this submesh.
     */
    auto get_transform_count(uint32_t submesh_index) const -> size_t
    {
        if(submesh_index < submesh_to_transform_indices.size())
        {
            return submesh_to_transform_indices[submesh_index].size();
        }
        return 0;
    }
    
    /**
     * @brief Gets a specific transform for a submesh by its instance index.
     * @param submesh_index The index of the submesh.
     * @param instance_index The instance index (0 to get_transform_count()-1).
     * @return Pointer to the transform, or nullptr if invalid.
     */
    auto get_transform(uint32_t submesh_index, size_t instance_index) const -> const math::mat4*
    {
        if(submesh_index < submesh_to_transform_indices.size())
        {
            const auto& indices = submesh_to_transform_indices[submesh_index];
            if(instance_index < indices.size())
            {
                uint32_t transform_index = indices[instance_index];
                if(transform_index < transforms.size())
                {
                    return &transforms[transform_index];
                }
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Checks if a submesh has any transforms.
     * @param submesh_index The index of the submesh.
     * @return True if the submesh has transforms, false otherwise.
     */
    auto has_transforms(uint32_t submesh_index) const -> bool
    {
        return submesh_index < submesh_to_transform_indices.size() && 
               !submesh_to_transform_indices[submesh_index].empty();
    }
};

struct pose_mat4
{
    /**
     * @brief Vector of bone transforms.
     */
    std::vector<math::mat4> transforms;
};

struct pose_transform
{
    /**
     * @brief Vector of bone transforms.
     */
    std::vector<math::transform> transforms;
};
/**
 * @class model
 * @brief Structure describing a LOD group (set of meshes), LOD transitions, and their materials.
 */
class model : public crtp_meta_type<model>
{
public:
    SERIALIZABLE(model)

    using seconds_t = std::chrono::duration<float>;

    /**
     * @brief Checks if the model is valid.
     * @return True if the model is valid, false otherwise.
     */
    auto is_valid() const -> bool;

    /**
     * @brief Gets the LOD (Level of Detail) mesh for the specified level.
     * @param lod The level of detail.
     * @return The asset handle for the mesh at the specified LOD.
     */
    auto get_lod(uint32_t lod) const -> asset_handle<mesh>;

    /**
     * @brief Sets the LOD (Level of Detail) mesh for the specified level.
     * @param mesh The mesh to set.
     * @param lod The level of detail.
     */
    void set_lod(asset_handle<mesh> mesh, uint32_t lod);

    /**
     * @brief Sets the material for the specified index.
     * @param material The material to set.
     * @param index The index to set the material at.
     */
    void set_material(asset_handle<material> material, uint32_t index);
    void set_material_instance(material::sptr material, uint32_t index);

    /**
     * @brief Gets all the LOD meshes.
     * @return A constant reference to the vector of LOD meshes.
     */
    auto get_lods() const -> const std::vector<asset_handle<mesh>>&;

    /**
     * @brief Gets the number of LOD levels available.
     * If there is only one explicit mesh, returns the internal LOD count of that mesh.
     * Otherwise, returns the number of mesh LODs.
     * @return Number of LOD levels available.
     */
    auto get_lods_count() const -> uint32_t;

    /**
     * @brief Sets the LOD meshes.
     * @param lods The vector of LOD meshes to set.
     */
    void set_lods(const std::vector<asset_handle<mesh>>& lods);

    /**
     * @brief Gets all the materials.
     * @return A constant reference to the vector of materials.
     */
    auto get_materials() const -> const std::vector<asset_handle<material>>&;
    auto get_material_instances() const -> const std::vector<material::sptr>&;

    /**
     * @brief Sets the materials.
     * @param materials The vector of materials to set.
     */
    void set_materials(const std::vector<asset_handle<material>>& materials);
    void set_material_instances(const std::vector<material::sptr>& materials);

    /**
     * @brief Gets the material for the specified index.
     * @param index The index.
     * @return The asset handle for the material of the specified index.
     */
    auto get_material(uint32_t index) const -> asset_handle<material>;
    auto get_material_instance(uint32_t index) const -> material::sptr;
    auto get_or_emplace_material_instance(uint32_t index) -> material::sptr;

    /**
     * @brief Gets whether LOD override is enabled.
     * @return True if LOD override is enabled, false otherwise.
     */
    auto get_lod_override_enabled() const -> bool;

    /**
     * @brief Sets whether LOD override is enabled.
     * @param enabled True to enable LOD override, false to disable.
     */
    void set_lod_override_enabled(bool enabled);

    /**
     * @brief Gets the LOD override level.
     * @return The LOD level to use when override is enabled.
     */
    auto get_lod_override_level() const -> uint32_t;

    /**
     * @brief Sets the LOD override level.
     * @param level The LOD level to use when override is enabled.
     */
    void set_lod_override_level(uint32_t level);

    /**
     * @brief Gets the LOD selection bias.
     * @return The bias value added to the calculated LOD index.
     */
    auto get_lod_selection_bias() const -> float;

    /**
     * @brief Sets the LOD selection bias.
     * @param bias The bias value to add to the calculated LOD index.
     *              Positive values select less detailed LODs, negative values select more detailed LODs.
     */
    void set_lod_selection_bias(float bias);

    /**
     * @brief Gets the LOD hysteresis factor used to prevent rapid LOD switching.
     * @return The hysteresis factor (percentage units for percent-based, dimensionless for screen-radius).
     */
    auto get_lod_hysteresis() const -> float;

    /**
     * @brief Sets the LOD hysteresis factor.
     * @param hysteresis The hysteresis factor to prevent ping-ponging between LOD levels.
     */
    void set_lod_hysteresis(float hysteresis);

    /**
     * @brief Gets the LOD transition time in seconds.
     * @return The transition duration (0 = instant switch, >0 = smooth dithered crossfade).
     */
    auto get_lod_transition_time() const -> seconds_t;

    /**
     * @brief Sets the LOD transition time in seconds.
     * @param time The transition duration (0 = instant switch, >0 = smooth dithered crossfade).
     */
    void set_lod_transition_time(seconds_t time);

    /**
     * @brief Calculates the LOD data for the model using distance-based hysteresis with time-based transitions.
     * Hysteresis prevents rapid LOD switching; transitions smooth the actual switch when it occurs.
     * Uses data.current_lod_index for hysteresis and updates target_lod_index when a switch is triggered.
     * @param data The LOD data to calculate and update.
     * @param world_transform The world transform of the model.
     * @param cam The camera.
     * @param dt Delta time for updating transition progress.
     * @return True if the LOD data was calculated successfully, false otherwise.
     */
    auto calculate_lod_data(lod_data& data, const math::transform& world_transform, const camera& cam, float dt) const -> bool;

 
    /**
     * @brief Gets the minimum screen size used by the screen-radius-squared LOD and culling method.
     * @return Minimum screen size.
     */
    auto get_lod_screen_size_min() const -> float;

    /**
     * @brief Sets the minimum screen size used by the screen-radius-squared LOD and culling method.
     * @param value Minimum screen size.
     */
    void set_lod_screen_size_min(float value);

    /**
     * @brief Gets the auto LOD screen size power base (used for generating a screen-size table).
     * @return Power base.
     */
    auto get_lod_auto_screen_size_power_base() const -> float;

    /**
     * @brief Sets the auto LOD screen size power base (used for generating a screen-size table).
     * @param value Power base.
     */
    void set_lod_auto_screen_size_power_base(float value);

    /**
     * @brief Gets the per-LOD screen size table used by the screen-radius-squared method.
     * @return Screen size table.
     */
    auto get_lod_screen_sizes() const -> const std::vector<float>&;

    /**
     * @brief Sets the per-LOD screen size table used by the screen-radius-squared method.
     * @param sizes Screen size table.
     */
    void set_lod_screen_sizes(const std::vector<float>& sizes);

    /**
     * @brief Recalculates the screen-size LOD thresholds for the provided LOD count.
     * This is a separate mechanism from the percent-based LOD limits and is used by calculate_lod_data_screen_size.
     * @param lod_count Number of LOD levels to calculate thresholds for.
     */
    void recalulate_lod_screen_size_limits(uint32_t lod_count);

    /**
     * @struct submit_callbacks
     * @brief Callbacks for submitting the model for rendering.
     */
    struct submit_callbacks
    {
        /**
         * @struct params
         * @brief Parameters for the submit callbacks.
         */
        struct params
        {
            /// Indicates if the model is skinned.
            bool skinned{};
            bool preserve_state{};
        };

        /// Callback for setup begin.
        std::function<void(const params& info)> setup_begin;
        /// Callback for setting up per instance.
        std::function<void(const params& info)> setup_params_per_instance;
        /// Callback for setting up per submesh.
        std::function<void(const params& info, const material&)> setup_params_per_submesh;
        /// Callback for setup end.
        std::function<void(const params& info)> setup_end;
    };

    /**
     * @brief Submits the model for rendering.
     * @param world_transform The world transform of the model.
     * @param submesh_transforms The submesh transforms (many-to-many mapping).
     * @param bone_transforms The bone transforms for skinned models.
     * @param skinning_matrices The skinning matrices per palette.
     * @param lod The level of detail to render.
     * @param callbacks The submit callbacks.
     */
    void submit(const math::mat4& world_transform,
                const submesh_pose_mat4& submesh_transforms,
                const pose_mat4& bone_transforms,
                const std::vector<pose_mat4>& skinning_matrices,
                unsigned int lod,
                const submit_callbacks& callbacks) const;

    /**
     * @brief Gets the default material.
     * @return A reference to the default material asset handle.
     */
    static auto default_material() -> asset_handle<material>&;

    /**
     * @brief Gets the fallback material.
     * @return A reference to the fallback material asset handle.
     */
    static auto fallback_material() -> asset_handle<material>&;

private:

    /**
     * @brief Resizes the materials based on the mesh.
     * @param mesh The mesh to use for resizing the materials.
     */
    void resize_materials(const asset_handle<mesh>& mesh);

    /// Collection of all materials for this model.
    std::vector<asset_handle<material>> materials_;

    std::vector<material::sptr> material_instances_;

    /// Collection of all LODs for this model.
    std::vector<asset_handle<mesh>> mesh_lods_;

    /// Whether LOD override is enabled.
    bool lod_override_enabled_{false};
    /// LOD level to use when override is enabled.
    uint32_t lod_override_level_{0};
    /// Bias value added to calculated LOD index (positive = less detailed, negative = more detailed).
    float lod_selection_bias_{0.0f};
    /// Hysteresis factor to prevent rapid LOD switching (percentage units for percent-based, dimensionless for screen-radius).
    float lod_hysteresis_{0.02f};
    /// LOD transition duration in seconds (0 = instant switching, >0 = smooth dithered crossfade).
    seconds_t lod_transition_time_{0.0f};

    std::vector<float> lod_screen_sizes_;
};

} // namespace unravel
