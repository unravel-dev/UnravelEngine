#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/mesh_sdf.h>

#include <math/math.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace unravel
{

/**
 * @brief One placement of a baked field in the world, as the clipmap composer consumes it.
 *
 * Deliberately a plain struct rather than surface_cache_system::instance so the composer can
 * be built and validated without the ECS, the asset layer, or a GPU.
 */
struct global_sdf_instance
{
    ///< The field being placed. Borrowed; must outlive composition.
    const mesh_sdf* sdf = nullptr;
    ///< The chain's coarsest level when resident beside a finer @ref sdf, else null. The GPU composes the GI
    ///< cascade from it beyond @ref sdf's band; hashed into the level fingerprint so its arrival recomposes.
    const mesh_sdf* coarse_sdf = nullptr;
    ///< World to local, for sampling the field at a world position.
    math::mat4 world_to_local{1.0f};
    ///< World-space bounds of the field, for culling.
    math::bbox world_bounds{};
    ///< Smallest scale axis: converts a local-space distance to a conservative world distance.
    float local_to_world_scale = 1.0f;
    ///< World length of each local axis, for the per-axis box bounds of sample_instance_distance.
    math::vec3 axis_scale{1.0f};
};

/**
 * @brief Camera-centred cascade of coarse distance fields covering the whole scene.
 *
 * This is what makes offscreen geometry contribute to global illumination. A per-instance
 * field only answers questions about the mesh it was baked from, and finding which instance a
 * ray might hit costs a search; the clipmap answers "how far is the nearest surface, anywhere"
 * in one lookup, at a resolution that falls off with distance from the camera.
 *
 * WORLD STABILITY. Each level's origin is snapped to a whole multiple of that level's voxel
 * size, so it is a step function of camera position rather than a continuous one. Two cameras
 * anywhere within the same voxel produce a bit-identical clipmap, which is what stops the
 * lighting from crawling as the camera moves. Never remove the snapping to "reduce popping" --
 * it is the popping that is correct, and it is invisible because the field is only ever
 * consumed as a low-frequency occlusion signal.
 *
 * CONSERVATIVENESS. A voxel stores the minimum over every instance that reaches it, and every
 * per-instance sample is itself a conservative under-estimate, so the composed result is too.
 * A sphere trace against the clipmap therefore never overshoots through geometry.
 */
class global_sdf_clipmap
{
public:
    /// Cascades. Four covers a wide range without the memory of a finer ladder; each level is
    /// `level_scale` times larger than the one before it.
    static constexpr uint32_t level_count = 4;

    struct settings
    {
        ///< Voxels per axis in every level. Memory is level_count * resolution^3 bytes.
        ///
        /// The STRUCT default stays 64 -- the value the CPU composer, the tests and any headless
        /// consumer can afford, since composition work is cubic in it. The runtime uses the GI's
        /// layout (lumen_constants.h LUMEN_GLOBAL_SDF_RESOLUTION) with GPU composition.
        uint32_t resolution = 64;
        ///< World-space extent covered by level 0.
        float base_extent = 16.0f;
        ///< Extent multiplier between consecutive levels.
        ///
        /// A tracer stops within `surface_bias` VOXELS of a surface, so a coarse level makes a
        /// floor appear to float: at 8 m voxels, half a voxel is 4 m of visible offset, and the
        /// level boundaries show up as steps. Doubling per level rather than quadrupling keeps
        /// the far cascades fine enough for that error to stay small, at the cost of total
        /// range (16/32/64/128 m rather than 8/32/128/512 m).
        ///
        /// Range is the cheaper thing to give up here: GI rays are bounded well inside the outermost
        /// cascade anyway.
        float level_scale = 2.0f;
        ///< Distance encoded before the R8 storage saturates, in voxels of that level. Matches
        ///< the mesh field's convention so the two decode identically.
        float encode_range = mesh_sdf::encode_range;
        ///< Width of the cross-fade into the next level, in VOXELS of the level fading out.
        ///
        /// Levels are composed independently, so their isosurfaces sit up to about one coarse
        /// voxel apart. The band has to be wider than that displacement for the fade to hide it;
        /// the next level's voxel is `level_scale` of these, so a few voxels is the right order.
        /// Zero restores the hard switch.
        float blend_voxels = 4.0f;
        ///< Bin the instances into a grid over the level and test only the ones a voxel can
        ///< reach, instead of every instance the level as a whole overlaps.
        ///
        /// Pure acceleration: composing with it off must produce byte-identical voxels, which is
        /// what `test_clipmap_culled_composition_matches_brute_force` asserts. Present as a
        /// setting so that comparison can be made without a second code path to drift.
        bool cull_composition = true;
        ///< Leave the VOXELS to a compute dispatch, doing only the bookkeeping here.
        ///
        /// Everything that decides WHICH levels to rebuild -- snapping, fingerprinting, staleness
        /// ageing, the budget -- is subtle, tested, and identical either way, so it stays on the CPU
        /// and only the per-voxel loop moves. `update` then reports the same dirty mask and the
        /// dispatch composes exactly those levels.
        ///
        /// The CPU composer remains the REFERENCE: `sample` and `sample_ex` read `level::voxels`,
        /// and the bake tests are their only consumers, so they keep working against a
        /// CPU-composed cascade while the runtime uses the GPU one.
        ///
        /// False HERE because a true default silently leaves every headless consumer -- the tests
        /// above all -- with a cascade nothing composes. The runtime opts in wherever the compose
        /// program loaded, which is also what makes the GI's resolution affordable.
        bool compose_on_gpu = false;
        ///< Levels recomposed per update, at most. Composition touches every voxel of a level,
        ///< so recomposing all of them in the frame the camera crosses a voxel boundary would
        ///< hitch. Levels are considered finest first, which is also the order they go stale in
        ///< (level 0 has the smallest voxels, so its origin re-snaps most often).
        uint32_t max_levels_per_update = 1;
        ///< Scale of the smallest object the GPU's GI cascade composition keeps (cs_gi_clipmap_compose.sc,
        ///< SdfLumenCascadeKeepsInstance): 1 / the scene detail. It is part of every level's fingerprint, so a
        ///< change recomposes the levels within the update budget.
        float object_radius_scale = 1.0f;
        ///< Levels that were composed before update PARTIALLY (level::is_partial).
        ///< False recomposes whole levels for every change, under the level budget and the edit throttle.
        bool partial_updates = true;
    };

    /// A box of one level's voxels: [min, min + size) per axis, in level voxel coordinates.
    struct voxel_box
    {
        math::ivec3 min{0};
        math::ivec3 size{0};
    };

    /// One instance a level was composed from: its entry hash (placement and identity) and its world bounds.
    struct composed_entry
    {
        uint64_t hash = 0;
        math::bbox bounds{};
    };

    struct level
    {
        ///< World-space minimum corner, snapped to a whole multiple of @ref voxel_size.
        math::vec3 origin{0.0f};
        ///< World-space edge length of one voxel.
        float voxel_size = 0.0f;
        ///< resolution^3 voxels, x-major (`x + y * res + z * res * res`), R8 encoded: what the CPU composer writes
        ///< and the CPU samplers read. Empty when the GPU composes the level (settings::compose_on_gpu).
        std::vector<uint8_t> voxels;
        ///< Fingerprint of the instances THIS level was composed from. Identity and placement,
        ///< order independent. Compared per level rather than globally so a moved instance only
        ///< invalidates the levels it actually reaches.
        uint64_t content_fingerprint = 0;
        ///< Updates this level has waited while stale. Drives the recomposition order, which is
        ///< what stops a level that keeps losing the budget race from starving: a fast camera
        ///< re-snaps the finest level almost every frame, and a strictly finest-first policy
        ///< would then never recompose the coarse ones at all.
        uint32_t stale_updates = 0;
        ///< The instances the voxels were composed from, sorted by hash; valid while @ref has_composed_entries (after
        ///< a compose, until a setting changes what is composed). A later update composes only where this set and
        ///< the current one differ (a partial update).
        std::vector<composed_entry> composed_entries;
        bool has_composed_entries = false;
        ///< PARTIAL recompose: the last recompose rewrote only part of the window. The composed value of a voxel
        ///< is a function of its world centre and the instances within reach alone, and the origin moves by whole
        ///< snap cells, so the old window's overlap with the new one, moved by @ref scroll_shift, holds the bytes a
        ///< recompose writes except within reach of an instance that moved, appeared or left: those voxels are
        ///< @ref partial_boxes, composed together with the slabs a scroll exposes (compute_scroll_boxes). False for
        ///< a full recompose.
        bool is_partial = false;
        ///< The origin's move of a partial recompose, in this level's voxels: the new window's voxel v holds what
        ///< the old window held at v + scroll_shift. Zero when the origin held still.
        math::ivec3 scroll_shift{0};
        ///< The voxels of a partial recompose within reach of a changed instance, in the new window's voxels,
        ///< aligned to GI_CLIPMAP_PARTIAL_BOX_ALIGNMENT.
        std::vector<voxel_box> partial_boxes;
        ///< Counts this level's recomposes: a consumer that mirrored recompose n can mirror a partial recompose n + 1
        ///< by its boxes alone.
        uint64_t compose_serial = 0;

        /// The level exists: its planning (snapping, fingerprints, the recompose budget) runs whichever composer
        /// fills it.
        auto is_valid() const -> bool
        {
            return voxel_size > 0.0f;
        }

        /// The CPU composer fills this level, so the CPU samplers can read it.
        auto has_voxels() const -> bool
        {
            return voxel_size > 0.0f && !voxels.empty();
        }
    };

    void init(const settings& settings);

    /**
     * @brief Applies settings that may change while running.
     *
     * Re-initialises when the change alters the STORAGE or the geometry of the cascade -- resolution,
     * base extent, level scale -- because those change what a voxel means, so the composed contents
     * are not reinterpretable and every level has to be rebuilt. That discards the cascade for a few
     * frames, which is why it is conditional rather than unconditional: the knobs a person actually
     * sweeps while looking at a scene (the blend band, the per-update budget, CPU versus GPU
     * composition) all take effect on the next composition without throwing anything away.
     *
     * @return true when the cascade was re-initialised and its contents discarded.
     */
    auto apply_settings(const settings& new_settings) -> bool;

    /**
     * @brief Recomposes the levels whose snapped origin moved or whose contents changed.
     *
     * A level goes stale for two independent reasons: its snapped origin drifted, or the set of
     * instances reaching it changed. Both are detected here, per level -- the caller does not
     * have to tell the cascade that something moved, and could not tell it WHICH levels care.
     *
     * A level that was composed before updates PARTIALLY (level::is_partial): only the voxels within reach of an
     * instance that moved, appeared or left, and the slabs a re-snapped origin exposes, on the level's staggered
     * cadence (is_partial_update_due). A level with no composed set, a move past its window,
     * or changes beyond GI_CLIPMAP_MAX_PARTIAL_INSTANCES or GI_CLIPMAP_MAX_PARTIAL_FRACTION of its voxels
     * recomposes in full, and full recomposes are BUDGETED: composing a level is expensive enough to be a visible
     * hitch. Staleness age drives their order, so no level can starve.
     *
     * @param instances Every resident field placement in the world, NOT only visible ones.
     * @param camera_position Centre of the cascade.
     * @param instances_revision Monotonic revision of everything the level fingerprints can
     *        depend on (surface_cache_system::get_content_revision). While it and a level's
     *        target origin both hold still, that level's fingerprint is recalled from cache
     *        instead of re-walking every instance. 0 means "unknown", which disables the cache
     *        and recomputes every fingerprint on every update.
     * @return The number of levels recomposed, for budgeting and diagnostics.
     */
    auto update(const std::vector<global_sdf_instance>& instances,
                const math::vec3& camera_position,
                uint64_t instances_revision = 0) -> uint32_t;

    /// Levels currently waiting to be recomposed, for diagnostics. Persistently non-zero means
    /// the budget is not keeping up with how fast the scene or the camera is changing.
    auto get_stale_level_count() const -> uint32_t;

    /// Returned wherever no level answers. Large and positive so a trace keeps marching rather
    /// than stopping at the edge of the world.
    static constexpr float outside_distance = 1e6f;

    /**
     * @brief Samples ONE level at a world position, in world units.
     *
     * @return @ref outside_distance when that level does not cover the position, including the
     *         outermost half voxel, which trilinear filtering cannot address.
     */
    auto sample_level(uint32_t index, const math::vec3& world_position) const -> float;

    /**
     * @brief Index of the finest level covering a world position, and how far into its blend
     *        band the position lies.
     *
     * @param out_blend 0 where the level answers alone, rising to 1 at the outer edge of its
     *                  coverage, where the next level has fully taken over. Exposed so a debug
     *                  view can show the handover the same way the sampler computes it.
     * @return level_count when no level covers the position.
     */
    auto find_level(const math::vec3& world_position, float& out_blend) const -> uint32_t;

    /**
     * @brief Samples the cascade at a world position, in world units.
     *
     * Uses the finest level containing @p world_position, CROSS-FADED into the next level over
     * a band at the edge of its coverage. Levels are composed independently at different voxel
     * sizes, so their isosurfaces do not coincide; switching between them abruptly makes the
     * field discontinuous exactly where two consumers are most likely to disagree about where a
     * surface is. The blend is what makes every consumer quote ONE function.
     *
     * Stays conservative: a convex combination of two under-estimates is an under-estimate, so
     * the blended value can never exceed the true distance either.
     *
     * Reference implementation of what the tracing shader performs.
     */
    auto sample(const math::vec3& world_position) const -> float;

    /// @brief As @ref sample, also reporting the voxel size of the cascade that answered.
    ///
    /// Anything scaled to "a voxel" is meaningless without this, because the levels differ in voxel
    /// size by orders of magnitude. Inside a cross-fade band the answer is a mixture of two levels,
    /// so the reported size is the same mixture.
    auto sample_ex(const math::vec3& world_position, float& out_voxel_size) const -> float;

    auto get_level(uint32_t index) const -> const level&
    {
        return levels_[index];
    }

    /// Levels whose contents changed in the last @ref update, as a bit per level. The GPU
    /// mirror uploads only these.
    auto get_dirty_levels() const -> uint32_t
    {
        return dirty_levels_;
    }

    void clear_dirty_levels()
    {
        dirty_levels_ = 0;
    }

    auto get_settings() const -> const settings&
    {
        return settings_;
    }

    /// World-space extent covered by a level.
    auto get_level_extent(uint32_t index) const -> float;

    /**
     * @brief Appends @p boxes to a dispatch's brick list, the unit of the compose and object grid dispatches: one
     *        group per brick of @p brick_edge^3 voxels (or cells), the bricks of a box x fastest, boxes in order.
     *
     * Two vec4s per box: xyz = its first voxel and w = its first brick counted from the first box of @p table; xyz = its
     * size in voxels. @p first_brick is where the appended boxes' bricks start.
     * @return The bricks of the appended boxes.
     */
    static auto append_brick_boxes(const std::vector<voxel_box>& boxes,
                                   int brick_edge,
                                   uint32_t first_brick,
                                   std::vector<math::vec4>& table) -> uint32_t;

    /// True when level @p index takes its partial updates on update @p update_index, staggered so about
    /// GI_CLIPMAP_PARTIAL_UPDATES_PER_FRAME levels update per frame: the first levels every update, the others at
    /// halving frequencies with distinct phases.
    static auto is_partial_update_due(uint32_t index, uint64_t update_index) -> bool;

    /**
     * @brief The decomposition of a scrolled partial recompose (see level::is_partial).
     *
     * @param shift The origin's move in voxels (level::scroll_shift).
     * @param resolution Voxels per axis.
     * @param out_overlap The voxels the new window shares with the old one, in NEW window
     *        coordinates; their source in the old window is min + shift.
     * @param out_exposed The voxels the new window exposes, as up to three DISJOINT slabs (one
     *        per moved axis, each trimmed to the ranges the earlier slabs did not cover) that
     *        together with the overlap tile the whole window exactly.
     * @return The number of exposed slabs; 0 when the shift is zero (nothing to do) or when
     *         the windows do not overlap at all (a full recompose is the only option).
     */
    static auto compute_scroll_boxes(const math::ivec3& shift,
                                     uint32_t resolution,
                                     voxel_box& out_overlap,
                                     std::array<voxel_box, 3>& out_exposed) -> uint32_t;

    /// Origin snap granularity, in voxels of each level: a whole number of the GPU coverage's texels (two voxels), so
    /// a scroll-only recompose moves the coverage by whole texels. Raising it trades a fraction of guaranteed level
    /// coverage at the window edge (half a snap, absorbed by the cross-fade and the next level) for proportionally
    /// fewer recomposes while the camera moves.
    static constexpr uint32_t origin_snap_voxels = 16;

    auto get_memory_usage() const -> size_t;

private:
    /// What an update does to one level.
    struct level_plan
    {
        bool is_partial = false;
        math::ivec3 scroll_shift{0};
        std::vector<voxel_box> partial_boxes;
    };

    /// Composes the voxels of @p boxes of one level (the whole level when empty) from the instances reaching it.
    void compose_level(uint32_t index,
                       const std::vector<global_sdf_instance>& instances,
                       const std::vector<voxel_box>& boxes = {});

    /// Merges every two boxes whose bounding box holds no more voxels than the two do apart (an instance's old and
    /// new reach, mostly): fewer, larger boxes for the same voxels.
    static void merge_overlapping_boxes(std::vector<voxel_box>& boxes);

    /// The CPU composer's half of a partial recompose: moves the old window's overlap into place.
    void scroll_level_voxels(uint32_t index, const math::ivec3& shift);

    /// Recomposes level @p index for @p target_origin under @p plan (CPU voxels unless compose_on_gpu) and records
    /// the instance set @p entries it was composed from.
    void apply_level_plan(uint32_t index,
                          const level_plan& plan,
                          const math::vec3& target_origin,
                          uint64_t target_fingerprint,
                          const std::vector<composed_entry>& entries,
                          const std::vector<global_sdf_instance>& instances);

    /// The partial recompose that brings level @p index from its composed set to @p entries at @p target_origin, or
    /// none (full recompose) when the level has no composed set, the move leaves no overlap, or the changes exceed the
    /// partial limits.
    auto plan_partial_update(uint32_t index, const math::vec3& target_origin, const std::vector<composed_entry>& entries)
        const -> std::optional<level_plan>;

    /// World-space region a level covers, given its origin.
    auto compute_level_bounds(uint32_t index, const math::vec3& origin) const -> math::bbox;

    /// Distance beyond a level's bounds at which an instance can still write into it.
    auto compute_level_reach(uint32_t index) const -> float;

    /// Distance beyond an instance's bounds within which it can change a level's voxels or coverage: the larger of
    /// the encode range and the GI cascade's coverage band, in the level's voxels.
    auto compute_level_influence(uint32_t index) const -> float;

    /// The instances that compose a level covering @p bounds, sorted by entry hash: sampleable, with bounds within
    /// @p reach of the level. Must select exactly the set compose_level does, or a change that alters the
    /// composition could leave the fingerprint equal and the level would never be rebuilt.
    auto collect_level_entries(const math::bbox& bounds,
                               float reach,
                               const std::vector<global_sdf_instance>& instances) const -> std::vector<composed_entry>;

    /// Order-independent hash of a level's instance set (collect_level_entries).
    auto compute_entries_fingerprint(const std::vector<composed_entry>& entries) const -> uint64_t;

    /// One instance's contribution to a level fingerprint: placement and identity.
    static auto compute_instance_entry_hash(const global_sdf_instance& instance) -> uint64_t;

    /// Recomputes @ref instance_entry_hashes_ when the content revision moved (or is unknown).
    void refresh_instance_entry_hashes(const std::vector<global_sdf_instance>& instances,
                                       uint64_t instances_revision);

    settings settings_{};
    std::array<level, level_count> levels_{};
    /// The fingerprint cache update() recalls when neither the instances revision nor a
    /// level's target origin moved. Revision 0 = nothing cached.
    std::array<math::vec3, level_count> cached_target_origin_{};
    std::array<uint64_t, level_count> cached_target_fingerprint_{};
    std::array<std::vector<composed_entry>, level_count> cached_target_entries_{};
    uint64_t cached_instances_revision_ = 0;
    /// Per-instance entry hashes (compute_instance_entry_hash) for the instance list of
    /// @ref instance_entry_hash_revision_, parallel to that list; the level fingerprints sum
    /// them instead of re-hashing every instance per level per frame. A non-sampleable
    /// instance carries 0 and a cleared sampleable flag.
    std::vector<uint64_t> instance_entry_hashes_;
    std::vector<uint8_t> instance_entry_sampleable_;
    uint64_t instance_entry_hash_revision_ = 0;
    /// Edit coalescing (GI_CLIPMAP_EDIT_THROTTLE_FRAMES), LEADING-EDGE: the first edit after
    /// a quiet stretch recomposes immediately (the pinned editor behaviour), and only a
    /// CONTINUOUS stream of edits - a drag re-fingerprinting its levels every frame -
    /// coalesces to one recompose per window. The pending diff persists either way, so the
    /// final state lands within one window of the stream ending; origin re-snaps never
    /// throttle. update_counter_ ticks once per update(); the arrays hold each level's last
    /// recompose tick and last observed content change (signed, seeded to -window in init so
    /// the very first edit reads as idle-started).
    int64_t update_counter_ = 0;
    std::array<int64_t, level_count> last_compose_counter_{};
    std::array<int64_t, level_count> last_content_seen_{};
    /// One bit per level, set when that level's voxels were rewritten and the GPU mirror is
    /// therefore stale. Cleared by the owner once it has uploaded.
    uint32_t dirty_levels_ = 0;
};

} // namespace unravel
