#pragma once
#include <engine/animation/animation_pose.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/model.h>
#include <engine/rendering/render_proxy.h>
#include <atomic>
#include <bitset>
#include <unordered_map>
namespace unravel
{
class material;

/**
 * @brief Counts changes to the SET of shadow casters that the per-frame model walk cannot
 * observe: a model or an activation appearing or disappearing, or a model that stopped being
 * a caster (the walk cannot tell one that LEFT the set from one that was never in it).
 *
 * Lives in the registry context, so it dies with the scene - no global table, nothing to
 * prune. Consumers cache the value they last built from and rebuild when it differs.
 */
struct shadow_caster_revision
{
    uint64_t value{};
};

/// Bumps the scene's shadow_caster_revision. Shaped for an entt on_construct / on_destroy
/// sink (which is why the entity is unused); also called directly when a model's caster flags
/// change. Deliberately NOT overloaded: an overload set cannot be named in a sink connect<>.
void bump_shadow_caster_revision(entt::registry& registry, entt::entity entity);

/**
 * @class model_component
 * @brief Class that contains core data for meshes.
 */
class model_component : public component_crtp<model_component, owned_component>
{
public:
    /// Indexed dirty flags for this model's render state. The slots are unravel::dirty_ids,
    /// the same ones transform_component uses: the transform's copy says where a model IS,
    /// this one what it renders as.

    /**
     * @brief Marks every consumer of this model's render state dirty, and bumps the scene's
     * shadow_caster_revision because the caster SET may have changed.
     *
     * Hides basic_component::touch(), which is an empty function, so the flag setters that
     * already call it gain the signal. Name hiding is enough here: every caller is a member
     * of this class or holds a model_component.
     */
    void touch();

    /// @see transform_component::is_dirty(uint8_t)
    auto is_dirty(uint8_t id) const noexcept -> bool;
    void set_dirty(uint8_t id, bool dirty) noexcept;

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
     * @brief Assigns the shared material asset for a slot.
     *
     * Materials are resolved live at submit time and cannot move geometry, so this skips
     * the pose, proxy and bounds invalidation @ref set_model performs. Prefer it over the
     * get_model / mutate / set_model round trip, which copies the whole model twice and
     * forces a redundant armature refresh.
     * @param material The material asset to assign.
     * @param index The material slot index.
     */
    void set_shared_material(const asset_handle<material>& material, uint32_t index);

    /**
     * @brief Gets the material instance of a slot: this model's overrides of the slot's shared material.
     */
    auto get_material_instance(uint32_t index) const -> const material_instance&;

    /**
     * @brief Overrides one property of a slot's shared material for this model only.
     * @param index The material slot index.
     * @param property The reflected property id of the material type (for example "base_color"_hs).
     * @param value The value; it must convert to the property's type.
     * @return False when the slot's shared material has no such property or the value does not convert to it.
     */
    auto set_material_instance_value(uint32_t index, entt::id_type property, const entt::meta_any& value) -> bool;

    /**
     * @brief Drops one override of a slot's material instance: the property follows the shared material again.
     */
    void revert_material_instance_value(uint32_t index, entt::id_type property);

    /**
     * @brief Drops every override of a slot's material instance: the slot draws like its shared material.
     */
    void revert_material_instance(uint32_t index);

    /**
     * @brief Brings the resolved materials of the material instances up to date (see
     * model::update_material_instances); the model system calls it once a frame.
     */
    void update_material_instances();

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
    /**
     * @brief Index of the armature node whose name matches `node_name` once both drop their
     * namespace (see get_name_without_namespace), or -1 when no node or several do.
     */
    auto get_armature_index_by_base_name_cached(const std::string& node_name) const -> int;
    /**
     * @brief Changes whenever the armature entity set changes, so caches keyed by armature
     * index (animation clip bindings, layer masks) know to rebuild.
     */
    auto get_armature_version() const noexcept -> uint64_t;
    
    auto get_skinning_transforms() const -> const std::vector<pose_mat4>&;

    /**
     * @brief Updates the armature of the model.
     * @param created_roots When not null, receives the roots of the node entities the build
     * created (the nodes no existing child of the owner provided).
     */
    auto init_armature(bool force, std::vector<entt::handle>* created_roots = nullptr) -> bool;

    /**
     * @brief Gives the model its mesh's default materials once the mesh loads, when it was made
     * while the mesh was still loading (model::set_default_materials_pending).
     */
    void resolve_pending_default_materials();

    /**
     * @brief Whether the mesh changed since the armature was built: the next init_armature
     * rebuilds it and sorts the old mesh's node entities into kept and stale.
     */
    auto is_armature_rebuild_pending() const -> bool;

    /**
     * @brief Runs the rebuild a mesh change left pending now instead of in the next frame update.
     * @return The roots of the node entities the rebuild created.
     */
    auto rebuild_armature() -> std::vector<entt::handle>;

    /**
     * @brief Takes the node entities only a previous mesh used: the roots of the subtrees the
     * current mesh no longer binds. A subtree that also holds anything else, such as a child
     * added under a node, is not among them.
     *
     * The caller removes them: the editor through an undoable delete, play mode directly.
     * @return The valid stale roots; the list is empty afterwards.
     */
    auto take_stale_armature_entities() -> std::vector<entt::handle>;

    /**
     * @brief Refreshes pose-derived render data: submesh/bone poses, cached world-space
     * render-proxy bounds and skinning palettes.
     *
     * Two gates keep this cheap:
     *  - Visibility: models not consumed by any view (camera/shadow) recently skip both the
     *    dirty scan and the refresh entirely (O(1)). Whole-model culling stays valid because
     *    update_world_bounds switches to the conservative grow-only culling bounds anchored
     *    to the root bone, which never depend on fresh proxies. When those bounds enter a
     *    frustum the model is drawn conservatively (stale proxies withheld), which marks it
     *    used and un-gates the full refresh on the next frame - a wrong skip self-heals in
     *    one frame and can never permanently hide a model.
     *  - Change-driven: visible models early-out when no armature entity transform changed
     *    since the last refresh (dirty_ids::model_pose bit-scan).
     *
     * @return True when a refresh actually ran, false when skipped or no mesh is loaded.
     */
    auto update_armature() -> bool;

    /**
     * @brief Forces the next update_armature call to run a full refresh even when no
     * armature transform is dirty (armature rebuilds, submesh entry settings edits).
     */
    void mark_pose_dirty() noexcept;

    /**
     * @brief Sets the armature entities.
     * @param submesh_entities A vector of handles to the armature entities.
     */
    void set_armature_entities(const std::vector<entt::handle>& submesh_entities);

    /**
     * @brief Gets the pose-aware world-space bounding box for this model.
     *
     * Static mesh bounds transformed to world space, unioned with the cached pose bounds
     * (node-attached submesh placements and skinned geometry). Refreshed every frame by
     * model_system::on_frame_before_render, before any render path runs. This is the ONLY
     * box that is valid for culling.
     */
    auto get_world_bounds() const -> const math::bbox&;
    auto get_world_bounds_transform() const -> const math::transform&;

    void update_world_bounds(const math::transform& world_transform);

    /**
     * @brief Gets the bind-pose local bounding box of the mesh asset for a given LOD.
     *
     * NEVER use this for culling: it does not track node or bone animation. It is only
     * meaningful for editor tooling (placement helpers, bounds gizmos).
     */
    auto get_local_bounds(uint32_t lod_index) const -> const math::bbox&;

    /**
     * @brief Retained render proxies: cached world-space per-submesh bounds (including
     * animated bounds for skinned submeshes) refreshed alongside the pose data.
     */
    auto get_render_proxies() const -> const submesh_render_proxies&;

    /**
     * @brief Convenience: builds the submit extras referencing the retained proxy data.
     * @param shadow_pass True when the submit target is a shadow pass.
     */
    auto get_submit_extras(bool shadow_pass = false) const -> model_submit_extras;

    void set_last_render_frame(uint64_t frame);
    auto is_newly_created() const noexcept -> bool;
    auto get_last_render_frame() const noexcept -> uint64_t;
    auto was_used_last_frame() const noexcept -> bool;

    /**
     * @brief Global request window for velocity (motion vector) state recording. The deferred
     * pipeline requests recording on every camera run that produces the velocity buffer;
     * recording decays two frames after the last request, so the feature-off CPU cost is zero.
     */
    static void request_velocity_recording(uint32_t frame);
    static auto is_velocity_recording_active(uint32_t frame) -> bool;

    /**
     * @brief Promotes the previous-frame motion state (world matrix, submesh pose, skinning
     * palettes) exactly once per render frame. Must run BEFORE update_armature refreshes the
     * pose caches (the caches still hold last frame's values at that point). Guarded against
     * the editor's multiple before-render invocations per frame (scene + game panels,
     * thumbnails) by a render-frame stamp.
     * @param transform_moved The consumed transform_component velocity dirty bit.
     */
    void record_velocity_state(uint32_t frame, const math::mat4& current_world, bool transform_moved);

    /**
     * @brief ORs additional motion evidence into this frame's mover flag (e.g. an armature
     * pose refresh that ran without the owner transform changing). Safe to call repeatedly.
     */
    void mark_motion(bool moved);

    /// True when this model moved since the last rendered frame (or is skinned) and should be
    /// drawn into the velocity buffer with per-object motion.
    auto has_motion() const -> bool;
    auto get_prev_world_transform() const -> const math::mat4&;
    auto get_prev_submesh_transforms() const -> const submesh_pose_mat4&;
    auto get_prev_skinning_transforms() const -> const std::vector<pose_mat4>&;

    auto is_skinned() const -> bool;
    auto get_bind_pose() const -> const animation_pose&;

    /**
     * @brief Armature root entity (first node of the imported skeleton hierarchy).
     */
    auto get_armature_root_entity() const -> entt::handle;

    /**
     * @brief Local rotation of the armature root (used for root motion / IK remapping).
     */
    auto get_facing_adjustment_rotation() const -> math::quat;

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
    auto create_armature(bool force, std::vector<entt::handle>* created_roots = nullptr) -> bool;

    /// Ends a pending rebuild: the previous mesh's nodes the current armature does not bind
    /// again become stale.
    void settle_previous_armature();

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
     * @brief The model object.
     */
    model model_;

    /// See the is_dirty / set_dirty pair above, unravel::dirty_ids and ALL_CONSUMERS_DIRTY.
    std::bitset<32> render_dirty_{ALL_CONSUMERS_DIRTY};

    /**
     * @brief Vector of handles to the armature entities.
     */
    std::vector<entt::handle> armature_entities_;

    /// The node entities the armature bound before a mesh change, until the rebuild sorts them.
    std::vector<entt::handle> previous_armature_entities_;
    /// Roots of the node subtrees only a previous mesh used; see take_stale_armature_entities.
    std::vector<entt::handle> stale_armature_entities_;
    /// Set by a mesh change, cleared once the armature was rebuilt for the new mesh.
    bool armature_rebuild_pending_{false};

    /**
     * @brief Cached name-to-index mapping for fast armature lookup.
     * Rebuilt automatically when armature_entities_ changes.
     */
    std::unordered_map<std::string, size_t> armature_name_to_index_;
    /// Armature indices by name without namespace; a name several nodes share maps to no node.
    std::unordered_map<std::string, size_t> armature_base_name_to_index_;

    /// Bumped by set_armature_entities; see get_armature_version.
    uint64_t armature_version_{1};

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
     * @brief Retained render proxies (cached world-space per-submesh bounds).
     * Refreshed together with the pose data so culling always matches the drawn pose.
     */
    submesh_render_proxies render_proxies_;

    /**
     * @brief World bounds
     */
    math::bbox world_bounds_;

    /**
     * @brief World bounds transform which was used.
     */
    math::transform world_bounds_transform_;

    /**
     * @brief Tight pose bounds (world pose union) captured in owner-local space at the last
     * full pose refresh. Visible-but-idle frames (proxies valid, nothing moved) re-anchor
     * this to the owner's current world transform so culling stays tight for free.
     */
    math::bbox pose_local_bounds_;

    /**
     * @brief Conservative culling bounds in bounds-anchor space (see bounds_anchor_).
     * Grow-only union of every tight pose bound observed while the model was visible,
     * seeded on the first refresh. This is what whole-model culling uses when the pose
     * refresh is skipped (off-screen gate): it never depends on fresh proxies, so a model
     * whose animation moves it into view is always re-discovered by the frustum test.
     * Reset when the mesh or armature entity set changes.
     */
    math::bbox culling_bounds_local_;

    /**
     * @brief Anchor entity for culling_bounds_local_: the topmost bone of the armature
     * (root motion baked into bone animation moves this), or null to fall back to the
     * owner transform for boneless armatures.
     */
    entt::handle bounds_anchor_;

    /**
     * @brief render_proxies_.version at the time the bounds captures above were taken. Used
     * to detect whether update_world_bounds sees fresh proxies or must re-anchor a capture.
     */
    uint64_t captured_proxies_version_{~0ULL};

    /**
     * @brief True when a pose refresh was skipped while armature transforms may have
     * changed (off-screen gate). Submit paths then get null proxies and fall back to
     * conservative per-submesh behavior until the next full refresh clears this.
     */
    bool render_proxies_stale_{false};

    /**
     * @brief Last frame this model was rendered.
     */
    uint64_t last_render_frame_{};

    /**
     * @brief Forces a full pose refresh on the next update_armature call regardless of
     * transform dirtiness. Starts true so the first update always populates the proxies.
     */
    bool pose_dirty_{true};

    /**
     * @brief Per-camera LOD state map.
     * Key: view identifier (camera pointer as uintptr_t)
     * Value: LOD state and last access frame for that camera
     * Automatically cleaned up when cameras become stale.
     */
    std::unordered_map<uintptr_t, per_camera_lod_state> per_camera_lod_data_;

    /**
     * @brief Velocity (motion vector) state: what the PREVIOUS frame rendered with.
     * prev_world_/prev_submesh_pose_/prev_skinning_pose_ feed u_prev_world in the velocity
     * pass; recorded_world_ is the two-slot staging for the world matrix (transforms are
     * already this frame's value at promotion time, unlike the pose caches).
     */
    math::mat4 prev_world_{1.0f};
    math::mat4 recorded_world_{1.0f};
    submesh_pose_mat4 prev_submesh_pose_;
    std::vector<pose_mat4> prev_skinning_pose_;

    /// Render frame of the last velocity promotion (multi-invoke guard).
    uint32_t velocity_state_frame_{0};

    /// False until the first promotion after (re-)enabling recording: the first frame uses
    /// prev = current so a spawn/stream-in never claims bogus motion.
    bool velocity_initialized_{false};

    /// This model moved since the last rendered frame (or is skinned).
    bool has_motion_{false};

    /// Last render frame (+1, 0 = never) any pipeline requested velocity recording for.
    static std::atomic<uint32_t> velocity_recording_request_frame_;
};

struct bone_component : public component_crtp<bone_component>
{
    uint32_t bone_index{};
};

/**
 * @struct submesh_entry
 * @brief Per-submesh render settings authored on the armature node entity that owns the submesh.
 */
struct submesh_entry
{
    /// Runtime index into the mesh submesh array. Re-resolved from the mesh asset on armature
    /// init; not stable across reimports (use stable_id for persistent references).
    uint32_t submesh_index{0};

    /// Import-stable submesh id (see mesh::submesh::stable_id). 0 = unknown; falls back to
    /// index-based matching during migration of legacy data.
    uint32_t stable_id{0};

    /// Whether this submesh instance casts shadows.
    bool casts_shadow{true};

    /// Whether this submesh instance is rendered at all.
    bool enabled{true};
};

struct submesh_component : public component_crtp<submesh_component>
{
    /// Per-submesh render settings for the submeshes affected by this node.
    /// Rebuilt from the mesh asset on armature init (see rebuild_submesh_entries), so the
    /// entry list itself is derived data - only the per-entry settings are authored.
    std::vector<submesh_entry> entries{};

    /**
     * @brief Migrates legacy serialized data that only stored bare submesh indices.
     * Creates default-valued entries for @p legacy_indices when no entries were loaded.
     */
    void migrate_legacy_indices(const std::vector<uint32_t>& legacy_indices)
    {
        if(!entries.empty() || legacy_indices.empty())
        {
            return;
        }

        entries.reserve(legacy_indices.size());
        for(uint32_t index : legacy_indices)
        {
            submesh_entry entry;
            entry.submesh_index = index;
            entries.emplace_back(entry);
        }
    }
};

} // namespace unravel
