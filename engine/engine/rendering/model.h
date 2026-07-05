#pragma once
#include <engine/engine_export.h>

#include <base/basetypes.hpp>

#include <engine/assets/asset_handle.h>

#include "gpu_program.h"
#include "material.h"
#include "mesh.h"
#include "render_proxy.h"
#include "batch_collector.h"

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
    struct transform_index
    {
        uint32_t index{};
        bool active{};
        bool casts_shadow{true};
    };
    hpp::small_vector<hpp::small_vector<transform_index>> submesh_to_transform_indices;
    
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
     * @param submesh_indices Submesh indices that should use this transform.
     * @param transform The transform to add.
     * @return The index of the transform in the transforms array.
     */
    template<typename SubmeshIndices>
    auto add_transform(const SubmeshIndices& submesh_indices, const math::mat4& transform, bool active) -> uint32_t
    {
        // Add the transform to the pool
        uint32_t trans_index = add_transform(transform);

        // Map all submesh indices to this transform
        for(uint32_t submesh_index : submesh_indices)
        {
            map_submesh(submesh_index, trans_index, active, true);
        }
        
        return trans_index;
    }

    /**
     * @brief Adds a transform to the shared pool without mapping any submesh to it.
     * @param transform The transform to add.
     * @return The index of the transform in the transforms array.
     */
    auto add_transform(const math::mat4& transform) -> uint32_t
    {
        uint32_t trans_index = static_cast<uint32_t>(transforms.size());
        transforms.emplace_back(transform);
        return trans_index;
    }

    /**
     * @brief Maps a single submesh index to a pooled transform with per-instance flags.
     * @param submesh_index The submesh index to map.
     * @param trans_index Index of the transform in the pool (see add_transform).
     * @param active Whether this instance is rendered at all.
     * @param casts_shadow Whether this instance is rendered into shadow passes.
     */
    void map_submesh(uint32_t submesh_index, uint32_t trans_index, bool active, bool casts_shadow)
    {
        if(submesh_index >= submesh_to_transform_indices.size())
        {
            submesh_to_transform_indices.resize(submesh_index + 1);
        }
        submesh_to_transform_indices[submesh_index].emplace_back(transform_index{trans_index, active, casts_shadow});
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
                auto transform_index = indices[instance_index];
                if(transform_index.active && transform_index.index < transforms.size())
                {
                    return &transforms[transform_index.index];
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

    /**
     * @brief Checks if a transform is active.
     * @param submesh_index The index of the submesh.
     * @param instance_index The instance index (0 to get_transform_count()-1).
     * @return True if the transform is active, false otherwise.
     */
    auto get_transform_active(uint32_t submesh_index, size_t instance_index) const -> bool
    {
        if(submesh_index < submesh_to_transform_indices.size())
        {
            const auto& indices = submesh_to_transform_indices[submesh_index];
            if(instance_index < indices.size())
            {
                return indices[instance_index].active;
            }
        }
        return false;
    }

    /**
     * @brief Checks if an instance casts shadows.
     * @param submesh_index The index of the submesh.
     * @param instance_index The instance index (0 to get_transform_count()-1).
     * @return True if the instance casts shadows, false otherwise.
     */
    auto get_transform_casts_shadow(uint32_t submesh_index, size_t instance_index) const -> bool
    {
        if(submesh_index < submesh_to_transform_indices.size())
        {
            const auto& indices = submesh_to_transform_indices[submesh_index];
            if(instance_index < indices.size())
            {
                return indices[instance_index].casts_shadow;
            }
        }
        return true;
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
 * @struct model_submit_extras
 * @brief Optional retained render data consumed by the model submit paths.
 *
 * All members are optional; default-constructed extras reproduce the legacy behavior.
 */
struct model_submit_extras
{
    /// Cached world-space per-submesh bounds (owned by model_component and refreshed with the
    /// pose data). When available, per-submesh frustum culling and per-submesh LOD selection
    /// use cheap AABB tests against these bounds - including skinned submeshes, whose bounds
    /// track the animated pose. Submeshes without cached bounds fall back to the legacy
    /// OBB classification (large meshes) or are conservatively drawn.
    const submesh_render_proxies* proxies{nullptr};

    /// Per-submesh material overrides indexed by submesh index. Null entries use the model
    /// material for the submesh's data group.
    const std::vector<material::sptr>* material_overrides{nullptr};

    /// True when submitting into a shadow pass. Instances flagged as not casting shadows
    /// (see submesh_pose_mat4::transform_index::casts_shadow) are skipped.
    bool shadow_pass{false};
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
     *
     * Screen size is measured from the pose-aware world bounds (the same box culling uses -
     * see model_component::get_world_bounds), so animated/root-motion models select LOD from
     * where their geometry actually is, not from the bind-pose box at the entity transform.
     *
     * @param data The LOD data to calculate and update.
     * @param world_bounds Pose-aware world-space bounds of the model.
     * @param cam The camera.
     * @param dt Delta time for updating transition progress.
     * @return True if the LOD data was calculated successfully, false when the model is not
     *         loaded, has unpopulated bounds, or is below the minimum screen size (culled).
     */
    auto calculate_lod_data(lod_data& data, const math::bbox& world_bounds, const camera& cam, float dt) const -> bool;

    /**
     * @brief Selects a LOD for a specific submesh based on its own screen size.
     *
     * Used at submit time when per-submesh culling is active (multi-submesh meshes)
     * and a view camera is available. Each submesh independently picks its LOD from its world-
     * space bounding sphere so tiny/distant submeshes on a large model can drop to a cheaper
     * LOD than the model-wide selection would.
     *
     * The returned LOD is CLAMPED to be at least @p base_lod: per-submesh selection is only
     * allowed to drop quality relative to the model-wide LOD, never raise it. This preserves
     * the guarantee made by @ref calculate_lod_data (with its hysteresis and dithered
     * transitions) that the model-wide LOD is a quality floor.
     *
     * Returns @p base_lod (i.e. "no change") when per-submesh LOD is not viable:
     *   - The model uses manual multi-mesh LODs (submesh identity is not comparable across
     *     different mesh assets).
     *   - No screen-size table is populated, or the model has only one LOD.
     *   - LOD override is enabled.
     *   - The submesh has no populated per-submesh bbox (indistinguishable from the whole model).
     *
     * Must run on the graphics API thread.
     *
     * @param m The mesh asset (must be the same one returned by @ref get_lod for @p base_lod).
     * @param submesh_index Index into @p m's submesh array at @p base_lod.
     * @param base_lod Model-wide LOD (floor for the returned value).
     * @param world_matrix World transform for this submesh instance.
     * @param cam Camera whose position/projection drives the screen-size computation.
     */
    auto calculate_submesh_lod(const mesh& m,
                               uint32_t submesh_index,
                               uint32_t base_lod,
                               const math::mat4& world_matrix,
                               const camera& cam) const -> uint32_t;

    /**
     * @brief Selects a LOD for a submesh from an already-known world-space AABB.
     *
     * Same semantics and guards as @ref calculate_submesh_lod but skips the per-call
     * world matrix decomposition by using cached world bounds (see @ref
     * submesh_render_proxies). Also usable for skinned submeshes whose animated
     * bounds are tracked per frame.
     */
    auto calculate_submesh_lod_from_world_bounds(const mesh& m,
                                                 uint32_t submesh_index,
                                                 uint32_t base_lod,
                                                 const math::bbox& world_bounds,
                                                 const camera& cam) const -> uint32_t;

    /**
     * @brief Computes a LOD index for this model without hysteresis, transitions or
     * visibility culling.
     *
     * Used by passes that need a distance-appropriate LOD but do not track per-camera
     * LOD state (e.g. shadow rendering, which previously always used LOD 0).
     *
     * @param world_bounds Pose-aware world-space bounds of the model (see
     *                     model_component::get_world_bounds).
     * @param cam Camera whose position drives the screen-size computation.
     * @param extra_bias Additional LOD bias on top of the model's own selection bias
     *                   (positive = coarser).
     * @return The selected LOD index (0 when no LOD table is available).
     */
    auto compute_lod_index(const math::bbox& world_bounds, const camera& cam, float extra_bias = 0.0f) const
        -> uint32_t;


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
     * @param skinning_transforms The skinning matrices per submesh.
     * @param lod The level of detail to render.
     * @param callbacks The submit callbacks.
     * @param frustum Optional view frustum for per-submesh culling on large meshes.
     * @param view Optional camera enabling per-submesh LOD selection. When supplied together
     *             with @p frustum and the mesh has enough submeshes to warrant per-submesh
     *             work, distant submeshes may pick a cheaper LOD than @p lod via
     *             @ref calculate_submesh_lod. Only affects non-skinned submeshes.
     */
    void submit(const math::mat4& world_transform,
                const submesh_pose_mat4& submesh_transforms,
                const pose_mat4& bone_transforms,
                const std::vector<pose_mat4>& skinning_transforms,
                unsigned int lod,
                const submit_callbacks& callbacks,
                const math::frustum* frustum = nullptr,
                const camera* view = nullptr,
                const model_submit_extras& extras = {}) const;

    /**
     * @struct submit_vertex_pulling_callbacks
     * @brief Callbacks for submitting the model using vertex-pulling rendering.
     *
     * Vertex-pulling rendering procedurally generates vertices in the vertex shader
     * from @c gl_VertexID and reads per-vertex data (positions, bone indices, bone
     * weights, ...) directly from the vertex buffer bound as a read-only compute
     * buffer. Before each per-submesh callback the model has already:
     *   - Set @c u_world via @c gfx::set_world_transform() with either the per-
     *     submesh non-skinned matrix or the per-submesh bone matrices.
     *   - Bound the mesh's hardware vertex buffer on compute stage @c 0 and its
     *     hardware index buffer on compute stage @c 1 (both read-only).
     * The callback is expected to set the shader program, any additional uniforms,
     * render state, call @c gfx::set_vertex_count(...) with the effect-specific
     * vertex multiplier, and finally @c gfx::submit(...) on the desired view.
     */
    struct submit_vertex_pulling_callbacks
    {
        /**
         * @struct params
         * @brief Per-invocation information for a vertex-pulling submesh submit.
         *
         * Attribute offsets and vertex stride are expressed in @c sizeof(float)
         * elements because the vertex buffer is exposed to shaders as a
         * @c Buffer<float>. Bone indices/weights offsets are only meaningful when
         * @c skinned is @c true; the caller should ignore them otherwise.
         */
        struct params
        {
            bool skinned{};                    ///< True during the skinned pass, false during non-skinned.
            bool preserve_state{};             ///< Hint: mirror @c submit_callbacks::params::preserve_state.
            uint32_t submesh_index{};          ///< Submesh index within the LOD mesh.
            uint32_t index_start{};            ///< Starting index of the submesh in the index buffer (in indices).
            uint32_t index_count{};            ///< Number of indices making up the submesh.
            uint32_t vertex_stride_floats{};   ///< Vertex stride expressed in float-sized elements.
            uint32_t position_offset_floats{}; ///< Byte offset of the position attribute converted to floats.
            uint32_t weight_offset_floats{};   ///< Byte offset of the bone weight attribute converted to floats.
            uint32_t indices_offset_floats{};  ///< Byte offset of the bone indices attribute converted to floats.
        };

        /// Called once per pass (once for non-skinned, once for skinned). Typically used to bind the program.
        std::function<void(const params& info)> setup_begin;
        /// Called once per pass after @c setup_begin. Typically used to set instance-level uniforms.
        std::function<void(const params& info)> setup_params_per_instance;
        /// Called once per submesh instance after u_world and the raw VB/IB have been bound.
        std::function<void(const params& info)> setup_params_per_submesh;
        /// Called once per pass at the end. Typically used to end the program.
        std::function<void(const params& info)> setup_end;
    };

    /**
     * @brief Submits the model using vertex-pulling rendering.
     *
     * Mirrors @ref submit but skips per-submesh material handling and bind_render_buffers
     * calls, and instead exposes the raw vertex/index buffers as read-only compute
     * buffers so the shader can procedurally generate vertices from @c gl_VertexID.
     *
     * @param world_transform The world transform of the model.
     * @param submesh_transforms The submesh transforms (many-to-many mapping).
     * @param skinning_transforms The per-submesh skinning matrices.
     * @param lod The level of detail to render.
     * @param callbacks The vertex-pulling submit callbacks.
     * @param frustum Optional view frustum for per-submesh culling on large meshes.
     * @param view Optional camera enabling per-submesh LOD selection. Same semantics as
     *             @ref submit.
     */
    void submit_for_vertex_pulling(const math::mat4& world_transform,
                                   const submesh_pose_mat4& submesh_transforms,
                                   const std::vector<pose_mat4>& skinning_transforms,
                                   unsigned int lod,
                                   const submit_vertex_pulling_callbacks& callbacks,
                                   const math::frustum* frustum = nullptr,
                                   const camera* view = nullptr,
                                   const model_submit_extras& extras = {}) const;

    /**
     * @brief Collects this model into a batch collector for instanced rendering.
     * @param collector The batch collector to add this model to.
     * @param world_transform The world transform of the model.
     * @param submesh_transforms The submesh transforms (many-to-many mapping).
     * @param lod_index The level of detail to use.
     * @param lod_param The LOD transition parameter (for smooth LOD transitions).
     * @param frustum Optional view frustum for per-submesh culling on large meshes.
     * @param view Optional camera enabling per-submesh LOD selection. When supplied and the
     *             mesh has many submeshes, each submesh may be collected under a batch key
     *             with a per-submesh LOD >= @p lod_index. The batching layer already keys on
     *             LOD, so mixed-LOD submeshes for the same model land in the correct batches
     *             automatically.
     */
    void submit_for_batching(batch_collector& collector,
                            const math::mat4& world_transform,
                            const submesh_pose_mat4& submesh_transforms,
                            uint32_t lod_index,
                            float lod_param = 0.0f,
                            const math::frustum* frustum = nullptr,
                            const camera* view = nullptr,
                            const model_submit_extras& extras = {}) const;


    /**
     * @brief Collects shadow-map geometry into per-cascade shadow batch collectors.
     * Batches by mesh/lod/submesh/cull and alpha-cutout state instead of material pointer.
     */
    auto submit_for_shadow_batching_cascaded(std::vector<shadow_batch_collector>& collectors,
                                             uint8_t cascade_count,
                                             const math::mat4& world_transform,
                                             const submesh_pose_mat4& submesh_transforms,
                                             uint32_t lod_index,
                                             float lod_param,
                                             const math::frustum* frustums,
                                             bool nested_cascades,
                                             const model_submit_extras& extras = {}) const -> bool;

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

    /**
     * @brief Shared core of the per-submesh LOD selection working on a world-space sphere.
     * Applies all viability guards (single-mesh internal LODs, screen size table, override)
     * and picks the coarsest LOD >= base_lod that still fits the projected size.
     */
    auto select_submesh_lod_for_sphere(const mesh& m,
                                       uint32_t submesh_index,
                                       uint32_t base_lod,
                                       const math::bsphere& world_sphere,
                                       const camera& cam) const -> uint32_t;

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
