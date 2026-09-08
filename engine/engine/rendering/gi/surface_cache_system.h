#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>
#include <engine/rendering/gi/sdf_atlas.h>
#include <engine/rendering/gi/sdf_instance_grid.h>
// For material::sptr, which is a nested typedef and so needs the complete type.
#include <engine/rendering/material.h>
#include <engine/rendering/gpu_light_buffer.h>

#include <context/context.hpp>
#include <hpp/uuid.hpp>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace unravel
{

class scene;
class mesh;
class model;
class model_component;

/**
 * @brief World state for surface cache global illumination.
 *
 * Owns the distance field residency shared by every camera, which is why it lives in
 * @c rtti::context rather than in a @c gfx::render_view: the fields describe the world, not
 * a view of it, and a scene rendered by three cameras must not upload them three times.
 *
 * Responsibilities in this phase:
 *   - make a mesh's baked field resident on first use, keyed by asset uid;
 *   - rebuild the per-frame instance list the tracer transforms rays through.
 */
class surface_cache_system
{
public:
    /// One resident field placed in the world. Mirrors the GPU instance layout.
    struct instance
    {
        ///< World to local transform, so a ray can be moved into field space.
        math::mat4 world_to_local{1.0f};
        ///< Local to world, for turning a local hit back into a world position.
        math::mat4 local_to_world{1.0f};
        ///< World-space bounds of the field, for broad-phase rejection.
        math::bbox world_bounds{};
        ///< Index into the atlas header buffer.
        uint32_t header_index = sdf_atlas::invalid_index;
        ///< Uniform scale factor applied to distances sampled in local space. Non-uniform
        ///< scale uses the smallest axis, which keeps the field conservative (a sphere trace
        ///< under-steps rather than overshooting through geometry).
        float local_to_world_scale = 1.0f;
        ///< World displacement of the field's bounds centre since the placement's previous
        ///< frame (zero for a static placement or a first sighting). The gather's temporal
        ///< shortens a receiver's history by the fraction of its rays that hit MOVING
        ///< instances - the receivers of a mover's shadow and bounce, which the per-pixel
        ///< velocity buffer cannot see.
        math::vec3 velocity{0.0f};
        ///< The largest displacement of any corner of the field's local bounds over the same
        ///< frame: a spinning placement moves its surface while its centre stays put.
        float max_corner_displacement = 0.0f;
        ///< Diffuse colour of this placement's material, and its emission.
        ///
        ///< A distance field carries geometry only, so a cell first discovered by a BOUNCE ray
        ///< has no material and has to fall back to a neutral grey until an on-screen pixel
        ///< registers one -- which never happens for anything the camera does not look at. The
        ///< instance is where the material can be recovered without storing material voxels: a
        ///< submesh is drawn with exactly one material, and one field is baked per submesh, so
        ///< the mapping is already one to one.
        ///
        ///< This is the base colour FACTOR; the attribute composer multiplies it on the GPU by
        ///< the texture mean at @ref mean_slot, so a texture-dominated material bounces its
        ///< true average reflectance rather than its (usually white) tint.
        math::vec3 albedo{0.5f, 0.5f, 0.5f};
        math::vec3 emissive{0.0f, 0.0f, 0.0f};
        ///< Material metalness factor. The light lattice stores diffuse bounce only, which a
        ///< metal has none of; the reflection tier blends a hit's lattice answer toward its
        ///< base colour over the receiver's irradiance by this (gi_reflection_kernel.sh).
        float metalness = 0.0f;
        ///< Slot of the material's colour map in the texture-mean buffer; 0 is reserved white.
        uint32_t mean_slot = 0;
        ///< Whether that mean has been captured (fingerprint input; see global_sdf_instance).
        bool mean_captured = false;
        ///< The same pair for the material's EMISSIVE map. Emission is a source, so its mean
        ///< is what decides how much energy a textured emitter actually puts into the scene:
        ///< without it a sign bounces its colour factor over its whole silhouette.
        uint32_t emissive_mean_slot = 0;
        bool emissive_mean_captured = false;
    };

    /// One texture whose mean is waiting to be captured on the GPU.
    struct texture_mean_capture
    {
        gfx::texture::ptr texture;
        uint32_t slot = 0;
    };

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Rebuilds the world-space state for this frame and flushes pending atlas uploads.
     *
     * Walks every model in the scene, not just the visible set: geometry behind the camera
     * still bounces light, and excluding it would reintroduce exactly the offscreen blindness
     * the screen-space path suffers from.
     *
     * Takes no camera, and that is the point of the split. Everything here is a function of the
     * WORLD, so it is identical for every camera and is skipped after the first call in a frame --
     * with two cameras it used to rebuild the same instance list and re-upload the same grid twice.
     * The camera-dependent half is @ref surface_cache_view.
     */
    void update_world(scene& scn);

    /// Placements the cascade composes from. Rebuilt by @ref update_world; borrowed by each
    /// camera's @ref surface_cache_view, which must therefore compose within the same frame.
    auto get_clipmap_instances() const -> const std::vector<global_sdf_instance>&
    {
        return clipmap_instances_;
    }

    /**
     * @brief A world region whose accumulated lighting went stale recently: the union of the
     *        bounds a changed placement occupied within the last GI_TEMPORAL_DIRTY_HOLD_FRAMES
     *        frames (it moved, appeared, vanished or changed material).
     *
     * The temporal accumulators localise their fast-flush window to these regions instead of
     * dropping the whole screen to the fast cap whenever anything anywhere moves - which is
     * what one oscillating cube in a far cell used to do to every pixel of a still shot.
     */
    struct dirty_region
    {
        math::bbox bounds{};
        /// Frame of the most recent change inside the region.
        uint64_t last_change_frame = 0;
    };

    /// The regions changed within the hold window, rebuilt by @ref update_world.
    auto get_dirty_regions() const -> const std::vector<dirty_region>&
    {
        return dirty_regions_;
    }

    /**
     * @brief Packs the dirty regions as (min, max) vec4 pairs for the shader uniform of
     *        gi_dirty_regions.sh, most recent first so a fixed budget keeps the regions
     *        still flushing.
     * @return The number of regions packed (at most @p max_regions); more regions than that
     *         exist when get_dirty_regions().size() exceeds it.
     */
    auto pack_dirty_regions(float* out_bounds, uint32_t max_regions) const -> uint32_t;

    auto get_atlas() -> sdf_atlas&
    {
        return atlas_;
    }

    auto get_light_buffer() const -> const gpu_light_buffer&
    {
        return light_buffer_;
    }


    /// Per-frame instance list packed for the tracer. Owned here rather than by a pass, since
    /// every pass that traces needs the same one and packing it twice would be wasted work.
    auto get_instance_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return instance_buffer_;
    }

    /// One emissive instance the probes sample explicitly (GI S4, next-event estimation):
    /// the bounding sphere of its placed bounds, its emitted radiance and its power.
    struct emitter
    {
        math::vec3 center{0.0f, 0.0f, 0.0f};
        float radius = 0.0f;
        math::vec3 radiance{0.0f, 0.0f, 0.0f};
        /// Luminance x emitting surface area (gi::emitter_selection_weight), the ordering key
        /// for the cap. CPU only: the upload packs @ref extent into this lane instead, and the
        /// shader rebuilds the same weight from the extent it decodes.
        float power = 0.0f;
        /// The axis-aligned extent of this piece, metres (at most GI_EMISSIVE_NEE_SEGMENT
        /// per axis) - the reflection tier's near-field term samples the piece along its
        /// longest axis. Packed 8 bits per axis into the power lane, negated and offset by
        /// one as the marker an older upload cannot produce.
        math::vec3 extent{0.0f, 0.0f, 0.0f};
    };

    /// This frame's emitter table (rebuilt by update_world, capped by power).
    auto get_emitters() const -> const std::vector<emitter>&
    {
        return emitters_;
    }

    /// vec4 elements per emitter in the table appended to the instance buffer. Must match
    /// SDF_EMITTER_STRIDE in gi/gi_emissive_nee.sh: (center, radius), (radiance, power).
    static constexpr uint32_t emitter_vec4_stride = 2;

    /// vec4 elements per packed instance. Must match SDF_INSTANCE_STRIDE in gi/sdf_common.sh.
    /// Two of the eleven carry the material; emission is HDR, so it gets its own vec4 rather
    /// than being packed into a spare component; the eleventh is the instance velocity.
    static constexpr uint32_t instance_vec4_stride = 11;

    /// Slot capacity of the mean buffer (16 KiB of vec4s). Overflow falls back to the white
    /// slot with a one-time warning rather than growing - a scene with a thousand distinct
    /// colour maps has bigger problems than its bounce tint. PUBLIC because the reflection
    /// pass stages the whole buffer into its trace list (GI_REFLECTION_MEAN_SLOTS must equal
    /// this; static_assert at the staging site).
    static constexpr uint32_t texture_mean_capacity = 1024;
    /// Radix packing the colour and emissive mean slots into one float lane of the instance
    /// record (see upload_instances). A power of two, so the shader's divide is an exponent
    /// shift. MIRROR OF SDF_MEAN_SLOT_RADIX in sdf_common.sh.
    static constexpr uint32_t mean_slot_radix = 2048;

    /// The per-texture mean buffer the attribute composer reads (slot 0 = white).
    auto get_texture_mean_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return texture_mean_buffer_;
    }

    /**
     * @brief Hands up to @p budget pending mean captures to the caller for dispatch this frame.
     *
     * Marks them captured, which flips the content fingerprint of every instance using the slot
     * on the NEXT world update - one frame after the capture dispatch executed, so the
     * recompose that publishes the mean always reads a written value.
     */
    auto take_texture_mean_captures(uint32_t budget) -> std::vector<texture_mean_capture>;

    /// Mean-capture dispatches per frame. Each is a 1x1x1 dispatch of 64 texture taps, so the
    /// bound exists to pace the recompose wave a fresh scene triggers, not the GPU cost.
    static constexpr uint32_t max_texture_mean_captures_per_frame = 4;

    /**
     * @brief Monotonic revision of everything a clipmap-level fingerprint can depend on.
     *
     * Bumped when the packed instance bytes change, when a texture-mean capture lands, and
     * when the atlas residency moves (a field uploaded or released). While it holds still and
     * a level's target origin holds still, that level's content fingerprint is necessarily
     * unchanged - which is what lets global_sdf_clipmap::update skip re-walking every
     * instance for every level on every frame.
     */
    auto get_content_revision() const -> uint64_t
    {
        return content_revision_;
    }

    /// CSR offsets of the instance cull grid, one entry per cell plus a terminator.
    auto get_grid_offset_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return grid_offset_buffer_;
    }

    /// Instance indices the cull grid's cells refer to.
    auto get_grid_instance_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return grid_instance_buffer_;
    }

    auto get_instance_grid() const -> const sdf_instance_grid&
    {
        return grid_;
    }

    /**
     * @brief The two vec4s every tracer binds to address the cull grid.
     *
     * [0] = grid origin xyz, cell size w. [1] = cell counts xyz, non-zero w when the grid is
     * usable. Built here for the same reason the clipmap's sampling parameters are: several
     * passes traverse this grid and any disagreement between them changes which instances a ray
     * finds, which does not fail loudly -- it just means some geometry stops occluding for one
     * pass and not another.
     */
    auto get_grid_params() const -> const float*
    {
        return grid_params_.data();
    }

    auto get_instances() const -> const std::vector<instance>&
    {
        return instances_;
    }

    auto is_enabled() const -> bool
    {
        return supported_ && enabled_ && atlas_.is_valid();
    }

    /// Whether the renderer backend can run GI at all (compute support, checked at init).
    /// Distinct from @ref is_enabled: a user toggle cannot switch an unsupported backend on.
    auto is_supported() const -> bool
    {
        return supported_;
    }

    void set_enabled(bool enabled)
    {
        enabled_ = enabled;
    }

private:
    /// Generation value no atlas ever reports, marking a field that has never been attempted so the
    /// first try always happens regardless of what the atlas has released.
    static constexpr uint32_t never_attempted = 0xFFFFFFFFu;

    /// Residency record for one mesh asset.
    struct mesh_residency
    {
        uint32_t header_index = sdf_atlas::invalid_index;
        ///< Level of the mip chain actually made resident. Everything downstream -- the instance
        ///< bounds, the level fingerprint -- has to describe the level the atlas took, not the
        ///< finest one the mesh owns, or the placement will not match what the tracer samples.
        uint32_t resident_mip = 0;
        ///< Set when the mesh has no baked field at all. PERMANENT, and deliberately distinct from
        ///< the atlas refusing an upload for want of room: that is a statement about the atlas at
        ///< one moment, not about the mesh, and it stops being true as soon as anything is
        ///< released. Conflating the two is what left a scene's meshes excluded from GI forever
        ///< after a busier scene had filled the atlas once.
        bool has_no_field = false;
        ///< World frame this was last asked for. Anything not asked for in the current frame is
        ///< released, which is the only thing that ever returns bricks to the atlas.
        uint64_t last_used_frame = 0;
        ///< One-time "this field is a phantom" diagnostic fired; see acquire_field.
        bool thickness_warned = false;
        ///< Atlas release generation at the last upload attempt, or @ref never_attempted.
        ///
        ///< A refusal for want of room is retried only once the atlas has actually freed something,
        ///< because nothing else can change the answer. Retrying every frame instead is not merely
        ///< wasteful: a scene that overruns the atlas refuses thousands of meshes, so it re-attempts
        ///< thousands of doomed uploads per frame, and the refusal counters climb into the billions.
        uint32_t attempt_generation = never_attempted;
    };

    /**
     * @brief Returns the header index for a mesh, uploading its field on first use.
     *
     * Also marks the record as used this frame, which is what keeps it out of the sweep at the end
     * of @ref update_world.
     */
    /// What a submesh got from the atlas: a header index, and which level of its chain is behind
    /// it. @ref sdf_atlas::invalid_index means no field this frame.
    struct acquired_field
    {
        uint32_t header_index = sdf_atlas::invalid_index;
        uint32_t mip_level = 0;
    };

    auto acquire_field(const hpp::uuid& mesh_uid,
                       const mesh& m,
                       uint32_t submesh_index,
                       uint32_t wanted_mip) -> acquired_field;

    /**
     * @brief Level a placement deserves, from how far it is from the nearest camera.
     *
     * UE bands this on ABSOLUTE distance -- its Mip1 box is the outermost global distance field
     * clipmap extent and its Mip2 box a middle one -- and takes the finest any view wants. That
     * cannot be copied directly: those extents belong to a view's clipmap, and update_world is
     * deliberately camera-agnostic. Absolute distances would also be wrong here in a way they are
     * not for UE, because a project's world scale is not fixed: the same numbers that band a
     * metre-scale prop put an entire centimetre-scale building in the coarsest level.
     *
     * Banded on distance RELATIVE TO THE PLACEMENT'S OWN SIZE instead, which is scale free and
     * needs no per-project tuning. It also reproduces the part of UE's rule that matters: their
     * test is box against box and so includes the object's extent, which is exactly why a large
     * object keeps its detail from further away.
     */
    auto compute_wanted_mip(const math::bbox& world_bounds) const -> uint32_t;

    ///< Every active camera's position this frame, gathered at the top of update_world.
    ///
    ///< Residency is SHARED by every camera, so it must not depend on which one is rendering --
    ///< that split is the whole reason update_world takes no camera. Taking the finest level any
    ///< camera wants keeps it a function of the world: the set of cameras is scene state, and two
    ///< cameras produce one answer rather than fighting over it. Same resolution UE reaches with
    ///< its InterlockedMax across views.
    std::vector<math::vec3> camera_positions_;

    /// Drops every field to a coarser level once the atlas has run out, and re-places them.
    void apply_atlas_pressure();

    ///< Level every field STARTS its residency walk at, raised when the atlas runs out.
    ///
    ///< Without it the walk is greedy and first-come-first-served: while the atlas has room every
    ///< field takes its finest level, so the first arrivals spend the whole atlas and the fallback
    ///< only engages for the stragglers -- by which point nothing fits, not even their coarsest
    ///< level. Measured on Bistro: 1291 submeshes at their finest level filled 373,248 bricks
    ///< exactly, and the remaining 204 were refused outright. Biasing the START of the walk is
    ///< what turns "the last ones lose" into "everyone is a little coarser".
    uint32_t global_mip_bias_ = 0;
    ///< Rejected-brick total at the last bias decision, so a bump happens once per overrun rather
    ///< than once per refused mesh.
    uint64_t acknowledged_rejected_bricks_ = 0;

    /**
     * @brief Releases every field nothing referenced this frame.
     *
     * The atlas is keyed by mesh ASSET and has no other reclamation path, so without this it only
     * ever grows: loading a second scene keeps the first scene's bricks resident, the new scene's
     * meshes are refused for want of room, and GI silently does not run at all -- every field is
     * refused, so the instance list comes out empty and every pass early-outs.
     */
    void release_unused_fields();

    /**
     * @brief Appends one placement of a resident field to this frame's instance list.
     * @param local_to_world The transform the RENDERER draws the geometry with, which for a
     *        model with submesh nodes is the node's transform, not the model root's.
     * @param mat The material this submesh is DRAWN with. Null falls back to a
     *        neutral albedo; it must resolve the same way the renderer does, or a bounce would
     *        tint light with a colour the surface is not actually painted.
     */
    /// Per-placement motion tracking behind @ref get_dirty_regions, and the cache of the
    /// pose-derived values @ref add_instance would otherwise recompute every frame. `history`
    /// holds the (frame, bounds) pairs the placement occupied within the hold window, newest
    /// last; a placement that vanished keeps its entry until that history ages out.
    struct tracked_placement
    {
        /// FNV over the pose and the baked material (see compute_placement_hash).
        uint64_t placement_hash = 0;
        /// The region bounds (emissive-inflated) and the raw world bounds as of the last frame.
        math::bbox bounds{};
        math::bbox field_bounds{};
        /// Pose-derived cache: valid while @ref pose_key matches the frame's (transform, field
        /// bounds) - the inverse and the transformed corners are pure functions of those, so
        /// a static placement reuses them instead of paying an inverse per frame.
        uint64_t pose_key = 0;
        bool has_pose = false;
        math::mat4 world_to_local{1.0f};
        float local_to_world_scale = 1.0f;
        /// The placement's transform as of the previous frame, for the instance velocity
        /// (the bounds centre's delta and the largest corner displacement).
        math::mat4 last_local_to_world{1.0f};
        bool has_last_pose = false;
        uint64_t seen_frame = 0;
        /// Set once the sweep recorded the placement's disappearance; cleared if it returns.
        bool swept = false;
        struct history_entry
        {
            uint64_t frame = 0;
            math::bbox bounds{};
        };
        std::vector<history_entry> history;
    };

    /// FNV-1a over the pose and the material the attribute voxels bake, component by
    /// component (never over sizeof: math::vec3 carries indeterminate padding bytes).
    static auto compute_placement_hash(const math::mat4& local_to_world,
                                       const math::vec3& albedo,
                                       const math::vec3& emissive) -> uint64_t;

    /**
     * @brief Records a placement's pose for @ref get_dirty_regions: a new, moved, re-materialed
     *        or vanished placement adds the bounds it occupied to its region history.
     *
     * @param identity Stable key of the placement (entity, submesh, placement index), so a pose
     *        can be compared against the same placement's previous frame.
     * @param placement_hash The frame's compute_placement_hash of the placement.
     * @param field_bounds The placement's raw world bounds; the region bounds are derived here
     *        (an emissive placement inflates by its light's reach).
     * @return The placement's record, for the pose cache.
     */
    auto track_placement(uint64_t identity,
                         uint64_t placement_hash,
                         const math::vec3& emissive,
                         const math::bbox& field_bounds) -> tracked_placement&;

    /// Sweeps placements not seen this frame (their last bounds go stale too), drops history
    /// older than the hold window and rebuilds @ref dirty_regions_.
    void rebuild_dirty_regions();

    void add_instance(uint64_t identity,
                      uint32_t header_index,
                      const mesh_sdf& sdf,
                      const math::mat4& local_to_world,
                      const std::shared_ptr<mesh>& owner,
                      const material::sptr& mat);

    /**
     * @brief The material a submesh is drawn with: the model material of its data group.
     *
     * Mirrors what the submit paths in model.cpp bind. The renderer is the authority on what
     * colour a surface actually is, so bouncing light off a different one would tint the scene
     * with a material nothing on screen is painted with.
     */
    static auto resolve_submesh_material(const model& mdl, const mesh& m, uint32_t submesh_index)
        -> material::sptr;

    /**
     * @brief Slot of a colour map's mean in the GPU texture-mean buffer, allocating on first sight.
     *
     * The factor alone is white on every textured material, which drives the bounce gain to the
     * GI_MAX_ALBEDO cap instead of the surface's true reflectance and over-brightens every
     * bounce. The mean itself never touches the CPU: a one-time cs_gi_texture_mean dispatch
     * (run by the compose pass via @ref take_texture_mean_captures) samples the texture's own
     * mip tail into the buffer, and the attribute composer multiplies factor x mean. Until a
     * texture is loaded and captured its slot reads the seeded white, and @p out_captured flips
     * the content fingerprint once when the capture lands, recomposing the affected levels.
     */
    auto acquire_texture_mean_slot(const asset_handle<gfx::texture>& color_map, bool& out_captured)
        -> uint32_t;

    /**
     * @brief Packs the instance list into the layout SdfLoadInstance expects and uploads it.
     *
     * Transforms are written as the three rows of an affine 3x4 rather than as a mat4, so the
     * GPU side has no matrix-convention ambiguity to get wrong. See the note in sdf_common.sh.
     */
    void upload_instances();

    /// Rebuilds the emitter table from this frame's instances (bounding spheres of the
    /// emissive ones), capped at GI_EMISSIVE_NEE_MAX_EMITTERS by power.
    void rebuild_emitters();

    sdf_atlas atlas_;
    gpu_light_buffer light_buffer_;
    std::vector<emitter> emitters_;
    /// Identifies one submesh's field. Residency is per SUBMESH, not per mesh: each submesh has
    /// its own field and is uploaded to the atlas independently.
    struct field_key
    {
        hpp::uuid mesh_uid{};
        uint32_t submesh_index{};

        auto operator==(const field_key& other) const -> bool
        {
            return mesh_uid == other.mesh_uid && submesh_index == other.submesh_index;
        }
    };

    struct field_key_hash
    {
        auto operator()(const field_key& key) const -> size_t
        {
            const size_t uid_hash = std::hash<hpp::uuid>{}(key.mesh_uid);
            return uid_hash ^ (size_t(key.submesh_index) * 0x9e3779b97f4a7c15ull);
        }
    };

    std::unordered_map<field_key, mesh_residency, field_key_hash> residency_;
    /// Bookkeeping for one colour map's slot in the GPU mean buffer.
    struct texture_mean_entry
    {
        uint32_t slot = 0;
        ///< Queued into @ref pending_texture_means_ (a texture still streaming waits here).
        bool queued = false;
        ///< Handed to the compose pass for capture; mirrored into every instance that uses the
        ///< slot, where the fingerprint reads it.
        bool captured = false;
    };
    std::unordered_map<hpp::uuid, texture_mean_entry> texture_mean_slots_;
    std::vector<texture_mean_capture> pending_texture_means_;
    /// vec4 per slot, seeded white; written only by cs_gi_texture_mean dispatches.
    gfx::dynamic_vertex_buffer_handle texture_mean_buffer_{bgfx::kInvalidHandle};
    uint32_t next_texture_mean_slot_ = 1;
    bool texture_mean_overflow_warned_ = false;
    std::vector<instance> instances_;
    /// Clipmap composition input, rebuilt each frame alongside @ref instances_.
    std::vector<global_sdf_instance> clipmap_instances_;
    std::unordered_map<uint64_t, tracked_placement> tracked_placements_;
    std::vector<dirty_region> dirty_regions_;
    /// Keeps every mesh referenced by @ref clipmap_instances_ alive for the duration of
    /// composition. The composer borrows raw mesh_sdf pointers, so an asset unloading
    /// mid-compose would otherwise dangle.
    std::vector<std::shared_ptr<mesh>> clipmap_keepalive_;
    /**
     * @brief Rebuilds the instance cull grid and uploads it.
     *
     * Rebuilt in full whenever the instance fingerprint changes. The grid holds no state worth
     * carrying forward, so there is nothing to invalidate: an identical instance set means an
     * identical grid, and anything else rebuilds from scratch.
     */
    void upload_instance_grid();

    /// Packed instance data and its GPU mirror, rebuilt each frame. Uploaded only when the
    /// fingerprint over the packed bytes changes: a static scene keeps it byte-identical, and
    /// re-staging megabytes per frame anyway kept the Vulkan backend allocating continuously.
    gfx::dynamic_vertex_buffer_handle instance_buffer_{bgfx::kInvalidHandle};
    uint32_t instance_buffer_capacity_ = 0;
    std::vector<float> instance_data_;
    /// FNV-1a over @ref instance_data_ as last packed (the light buffer's convention).
    uint64_t instance_fingerprint_ = 0;
    /// See @ref get_content_revision. Starts at 1 so a zero can mean "no revision known".
    uint64_t content_revision_ = 1;
    /// The instance fingerprint the grid was last built and uploaded for.
    uint64_t grid_uploaded_fingerprint_ = 0;
    /// Broad-phase over @ref instances_, so a ray tests the instances near it rather than all of
    /// them. Rebuilt whenever the instance fingerprint changes.
    sdf_instance_grid grid_;
    std::vector<math::bbox> grid_bounds_;
    gfx::dynamic_index_buffer_handle grid_offset_buffer_{bgfx::kInvalidHandle};
    gfx::dynamic_index_buffer_handle grid_instance_buffer_{bgfx::kInvalidHandle};
    uint32_t grid_offset_capacity_ = 0;
    uint32_t grid_instance_capacity_ = 0;
    std::array<float, 8> grid_params_{};
    bool enabled_ = true;
    /// Backend capability, decided once at init: without compute shaders nothing here can run.
    bool supported_ = false;
    /// Frame the world state was last rebuilt in, so several cameras in one frame share one
    /// rebuild. Starts at a value no frame counter produces, so the first call always runs.
    uint64_t world_frame_ = std::numeric_limits<uint64_t>::max();
};

} // namespace unravel
