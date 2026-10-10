#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>
#include <engine/rendering/gi/lumen_card_library.h>
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
#include <utility>
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
 * Responsibilities:
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
        ///< The chain's coarsest level when it is resident beside a finer @ref header_index, else @ref header_index:
        ///< the GI cascade reads distances beyond the finer level's band from it (SdfInstanceStandaloneDistance in
        ///< gi/sdf_common.sh).
        uint32_t coarse_header_index = sdf_atlas::invalid_index;
        ///< Uniform scale factor applied to distances sampled in local space. Non-uniform
        ///< scale uses the smallest axis, which keeps the field conservative (a sphere trace
        ///< under-steps rather than overshooting through geometry).
        float local_to_world_scale = 1.0f;
        ///< World length of each local axis: the composed fields bound a non-uniformly scaled
        ///< placement per axis (sample_instance_distance) instead of by the smallest axis alone.
        math::vec3 axis_scale{1.0f};
        ///< The material renders both faces (cull none): the GI global SDF's coverage leaves space near only such
        ///< placements uncovered, which the march expands less and dithers through.
        bool is_two_sided = false;
        ///< The material emits (its emissive luminance reaches GI_EMISSIVE_LIGHT_SOURCE_MIN_LUMINANCE): an
        ///< emissive light source, derived from the material rather than authored. The GI keeps its cards resident
        ///< down to one texel and composes it into the global SDF however small. Cleared for an emitter inside its
        ///< own housing (clear_enclosed_emissive_light_sources).
        bool is_emissive_light_source = false;
    };

    /// What the surface cache needs to capture one placement, parallel to the instance list
    /// (the same index is the placement's GPU instance index).
    struct lumen_source
    {
        ///< Stable across frames: entity, submesh and drawn instance.
        uint64_t identity = 0;
        ///< This frame's GI instance index (the GPU instance record).
        uint32_t instance_index = 0;
        std::shared_ptr<mesh> owner;
        uint32_t submesh_index = 0;
        material::sptr material;
        math::mat4 local_to_world{1.0f};
        ///< Null until the submesh's cards are built.
        std::shared_ptr<const lumen_mesh_cards> cards;
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
     * two cameras do not rebuild the same instance list or re-upload the same grid twice.
     * The camera-dependent half is @ref surface_cache_view.
     */
    void update_world(scene& scn);

    /// Placements the cascade composes from. Rebuilt by @ref update_world; borrowed by each
    /// camera's @ref surface_cache_view, which must therefore compose within the same frame.
    auto get_clipmap_instances() const -> const std::vector<global_sdf_instance>&
    {
        return clipmap_instances_;
    }

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
    auto get_instance_buffer() const -> bgfx::DynamicVertexBufferHandle
    {
        return instance_buffer_;
    }

    /// vec4 elements per packed instance. Must match SDF_INSTANCE_STRIDE in gi/sdf_common.sh: the two affine
    /// transforms (three rows each), the bounds with the header and the scale, the flags with the axis scales, and
    /// the coarse header.
    static constexpr uint32_t instance_vec4_stride = 10;

    /**
     * @brief Monotonic revision of everything a clipmap-level fingerprint can depend on.
     *
     * Bumped when the packed instance bytes change and when the atlas residency moves (a field
     * uploaded or released). While it holds still and
     * a level's target origin holds still, that level's content fingerprint is necessarily
     * unchanged - which is what lets global_sdf_clipmap::update skip re-walking every
     * instance for every level on every frame.
     */
    auto get_content_revision() const -> uint64_t
    {
        return content_revision_;
    }

    /**
     * @brief Hash of this frame's instance list IN ORDER (the placement identities).
     *
     * The instance indices the GPU buffers carry are positions in a list rebuilt every frame from a
     * traversal with no guaranteed order, and the content fingerprints deliberately ignore order. A
     * structure that stores instance indices across frames (the GI object grid) is valid only while
     * this holds still.
     */
    auto get_instance_order_hash() const -> uint64_t
    {
        return instance_order_hash_;
    }

    /// The instance cull grid as the tracers bind it (sdf_common.sh stage 12): the CSR offsets
    /// (one per cell plus a terminator) followed by the instance indices the cells refer to;
    /// the instance base rides get_grid_params()[7].
    auto get_grid_buffer() const -> bgfx::DynamicIndexBufferHandle
    {
        return grid_buffer_;
    }

    /**
     * @brief The two vec4s every tracer binds to address the cull grid.
     *
     * [0] = grid origin xyz, cell size w. [1] = cell counts xyz, w = the instance list's base
     * entry in the grid buffer (the offset count), non-zero when the grid is usable. Built here
     * for the same reason the clipmap's sampling parameters are: several
     * passes traverse this grid and any disagreement between them changes which instances a ray
     * finds, which does not fail loudly -- it just means some geometry stops occluding for one
     * pass and not another.
     */
    auto get_grid_params() const -> const float*
    {
        return grid_params_.data();
    }

    /**
     * @brief Runtime experiment flags the GI passes read on the CPU: two code paths in one build,
     *        alternated inside ONE editor launch for A/B comparisons - the variance between launches
     *        hides small effects. Zero in production; set by the editor MCP tool gi_set_experiment_flags.
     */
    void set_experiment_flags(uint64_t flags)
    {
        experiment_flags_ = flags;
    }

    auto get_experiment_flags() const -> uint64_t
    {
        return experiment_flags_;
    }

    /**
     * @brief This frame's lighting changed globally: the first directional light's or the sky's brightest channel
     *        moved more than global_lighting_change_ratio either way since the last world update. The GI then rebuilds
     *        its radiance cache and starts its gather history over instead of converging at the budgeted rates.
     */
    auto has_global_lighting_change() const -> bool
    {
        return has_global_lighting_change_;
    }

    auto get_instances() const -> const std::vector<instance>&
    {
        return instances_;
    }

    auto get_lumen_sources() const -> const std::vector<lumen_source>&
    {
        return lumen_sources_;
    }

    /// The recorded build of @p source's card set (lumen_card_library::acquire_build_debug), null while it runs.
    auto acquire_lumen_card_build_debug(const lumen_source& source) -> std::shared_ptr<const lumen_card_build_debug>
    {
        const bool two_sided = source.material && source.material->get_cull_type() == cull_type::none;
        return lumen_cards_.acquire_build_debug(source.owner, source.submesh_index, two_sided);
    }

    /// Drops the recorded card builds (lumen_card_library::release_build_debug).
    void release_lumen_card_build_debug()
    {
        lumen_cards_.release_build_debug();
    }

    /// The backend runs compute (checked at init) and the atlas exists.
    auto is_enabled() const -> bool
    {
        return supported_ && atlas_.is_valid();
    }

private:
    /// Generation value no atlas ever reports, marking a field that has never been attempted so the
    /// first try always happens regardless of what the atlas has released.
    static constexpr uint32_t never_attempted = 0xFFFFFFFFu;

    /// Residency record for one mesh asset.
    struct mesh_residency
    {
        uint32_t header_index = sdf_atlas::invalid_index;
        ///< The chain's coarsest level, resident beside a finer @ref header_index (see instance::coarse_header_index);
        ///< invalid while @ref header_index is itself the coarsest or the upload has not happened yet.
        uint32_t coarse_header_index = sdf_atlas::invalid_index;
        ///< Atlas release generation at the last coarse upload attempt (as @ref attempt_generation).
        uint32_t coarse_attempt_generation = never_attempted;
        ///< Level of the mip chain actually made resident. Everything downstream -- the instance
        ///< bounds, the level fingerprint -- has to describe the level the atlas took, not the
        ///< finest one the mesh owns, or the placement will not match what the tracer samples.
        uint32_t resident_mip = 0;
        ///< Set when the mesh has no baked field at all. PERMANENT, and deliberately distinct from
        ///< the atlas refusing an upload for want of room: that is a statement about the atlas at
        ///< one moment, not about the mesh, and it stops being true as soon as anything is
        ///< released. Conflating the two would leave a scene's meshes excluded from GI forever
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
        ///< The coarsest level resident beside it, or invalid (see mesh_residency::coarse_header_index).
        uint32_t coarse_header_index = sdf_atlas::invalid_index;
        uint32_t coarse_mip_level = 0;
    };

    /// @ref acquire_field's answer for a record holding a level: also makes the chain's coarsest level resident
    /// beside it when it is finer.
    auto complete_acquired_field(mesh_residency& record, const mesh& m, uint32_t submesh_index) -> acquired_field;

    auto acquire_field(const hpp::uuid& mesh_uid,
                       const mesh& m,
                       uint32_t submesh_index,
                       uint32_t wanted_mip) -> acquired_field;

    /**
     * @brief Level a placement deserves, from how far it is from the nearest camera.
     *
     * Banded on distance RELATIVE TO THE PLACEMENT'S OWN SIZE, not on absolute distance. Absolute
     * bands would have to come from a view's clipmap extents, and update_world is deliberately
     * camera-agnostic. They would also depend on the project's world scale, which is not fixed:
     * the same numbers that band a metre-scale prop put an entire centimetre-scale building in
     * the coarsest level.
     *
     * The relative band is scale free and needs no per-project tuning. The distance is measured
     * to the placement's box and so includes the object's extent, which is exactly why a large
     * object keeps its detail from further away.
     */
    auto compute_wanted_mip(const math::bbox& world_bounds) const -> uint32_t;

    ///< Every active camera's position this frame, gathered at the top of update_world.
    ///
    ///< Residency is SHARED by every camera, so it must not depend on which one is rendering --
    ///< that split is the whole reason update_world takes no camera. Taking the finest level any
    ///< camera wants keeps it a function of the world: the set of cameras is scene state, and two
    ///< cameras produce one answer rather than fighting over it.
    std::vector<math::vec3> camera_positions_;

    /// Drops every field to a coarser level once the atlas has run out, and re-places them.
    void apply_atlas_pressure();

    ///< Level every field STARTS its residency walk at, raised when the atlas runs out.
    ///
    ///< Without it the walk is greedy and first-come-first-served: while the atlas has room every
    ///< field takes its finest level, so the first arrivals spend the whole atlas and the fallback
    ///< only engages for the stragglers -- by which point nothing fits, not even their coarsest
    ///< level. Biasing the START of the walk is what turns "the last ones lose" into "everyone is
    ///< a little coarser".
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

    /// The cache of the pose-derived values @ref add_instance would otherwise recompute every frame, kept per
    /// placement identity: the inverse, the scales and the transformed bounds are pure functions of the transform
    /// and the field's local bounds, so a static placement reuses them.
    struct tracked_placement
    {
        /// Valid while it matches the frame's (transform, field bounds) key.
        uint64_t pose_key = 0;
        bool has_pose = false;
        math::mat4 world_to_local{1.0f};
        float local_to_world_scale = 1.0f;
        math::vec3 axis_scale{1.0f};
        math::bbox world_bounds{};
        /// The last world frame the placement was drawn in.
        uint64_t seen_frame = 0;
    };

    /// Drops the records of placements not drawn this frame.
    void sweep_tracked_placements();

    /**
     * @brief What the walk needs from a material, decoded once per material per frame.
     *
     * Casting a material and decoding its emission give the same answers for every placement in the frame, so they
     * are memoised by material pointer for the duration of the walk (see @ref summarize_material) rather than
     * repeated for every placement that shares the material.
     */
    struct material_summary
    {
        bool is_pbr = false;
        bool is_blended = false;
        /// Luminance of the emissive colour times its intensity (instance::is_emissive_light_source).
        float emissive_luminance = 0.0f;
        /// The material culls no face (instance::is_two_sided).
        bool is_two_sided = false;
    };

    /// The summary of @p mat for this frame, decoded on first sight (see material_summary).
    auto summarize_material(const material::sptr& mat) -> const material_summary&;

    /// The scene walk of @ref update_world: every drawn submesh of every active model places its field, when it has
    /// one.
    void walk_scene(scene& scn);

    /// A placement's resident field: the level traced and, when finer than its chain's coarsest, that coarsest
    /// level too (null / invalid otherwise).
    struct placed_field
    {
        uint32_t header_index = sdf_atlas::invalid_index;
        const mesh_sdf* sdf = nullptr;
        uint32_t coarse_header_index = sdf_atlas::invalid_index;
        const mesh_sdf* coarse_sdf = nullptr;
    };

    /**
     * @brief Appends one placement of a resident field to this frame's instance list.
     * @param local_to_world The transform the RENDERER draws the geometry with, which for a
     *        model with submesh nodes is the node's transform, not the model root's.
     * @param material The summary of the material this submesh is DRAWN with (its sidedness and emission).
     */
    void add_instance(uint64_t identity,
                      const placed_field& field,
                      const math::mat4& local_to_world,
                      const std::shared_ptr<mesh>& owner,
                      const material_summary& material);

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
     * @brief Packs the instance list into the layout SdfLoadInstance expects and uploads it.
     *
     * Transforms are written as the three rows of an affine 3x4 rather than as a mat4, so the
     * GPU side has no matrix-convention ambiguity to get wrong. See the note in sdf_common.sh.
     */
    void upload_instances();
    /// Clears instance::is_emissive_light_source on emitters enclosed by another placement (a bulb in its lamp glass).
    void clear_enclosed_emissive_light_sources();

    sdf_atlas atlas_;
    gpu_light_buffer light_buffer_;
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
    std::vector<instance> instances_;
    /// Surface cache capture inputs, rebuilt each frame alongside @ref instances_ (same order).
    std::vector<lumen_source> lumen_sources_;
    lumen_card_library lumen_cards_;
    /// Clipmap composition input, rebuilt each frame alongside @ref instances_.
    std::vector<global_sdf_instance> clipmap_instances_;
    /// Pose caches by placement identity (entity, submesh, placement index), swept of the placements not drawn.
    std::unordered_map<uint64_t, tracked_placement> tracked_placements_;
    /// Per-frame memo behind summarize_material, keyed by material pointer; cleared each walk.
    std::unordered_map<const material*, material_summary> material_summaries_;
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
    /// re-staging megabytes per frame anyway would keep the Vulkan backend allocating continuously.
    bgfx::DynamicVertexBufferHandle instance_buffer_{bgfx::kInvalidHandle};
    uint32_t instance_buffer_capacity_ = 0;
    std::vector<float> instance_data_;
    /// FNV-1a over @ref instance_data_ as last packed (the light buffer's convention).
    uint64_t instance_fingerprint_ = 0;
    /// See @ref get_content_revision. Starts at 1 so a zero can mean "no revision known".
    uint64_t content_revision_ = 1;
    /// See @ref get_instance_order_hash.
    uint64_t instance_order_hash_ = 0;
    /// The instance fingerprint the grid was last built and uploaded for.
    uint64_t grid_uploaded_fingerprint_ = 0;
    /// Broad-phase over @ref instances_, so a ray tests the instances near it rather than all of
    /// them. Rebuilt whenever the instance fingerprint changes.
    sdf_instance_grid grid_;
    std::vector<math::bbox> grid_bounds_;
    /// Non-emissive placement bounds of this frame (clear_enclosed_emissive_light_sources), kept to reuse the allocation.
    std::vector<math::bbox> enclosure_candidate_bounds_;
    bgfx::DynamicIndexBufferHandle grid_buffer_{bgfx::kInvalidHandle};
    uint32_t grid_capacity_ = 0;
    /// The offsets and instance indices concatenated for the one-buffer upload.
    std::vector<uint32_t> grid_upload_;
    std::array<float, 4u * gi::GI_SDF_GRID_PARAMS_VEC4> grid_params_{};
    uint64_t experiment_flags_ = 0;

    /// A change of the sun or sky by more than this factor either way is global.
    static constexpr float global_lighting_change_ratio = 4.0f;
    /// The floor of the ratio's terms, so a light switching on or off counts as a change.
    static constexpr float global_lighting_epsilon = 1e-5f;
    /// See has_global_lighting_change.
    void update_global_lighting_state(scene& scn);
    float global_sun_ = 0.0f;
    float global_sky_ = 0.0f;
    bool has_global_lighting_change_ = false;
    /// Backend capability, decided once at init: without compute shaders nothing here can run.
    bool supported_ = false;
    /// Frame the world state was last rebuilt in, so several cameras in one frame share one
    /// rebuild. Starts at a value no frame counter produces, so the first call always runs.
    uint64_t world_frame_ = std::numeric_limits<uint64_t>::max();
};

} // namespace unravel
