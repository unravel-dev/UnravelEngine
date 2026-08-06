#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/mesh_sdf.h>

#include <math/math.h>

#include <array>
#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief One placement of a baked field in the world, as the clipmap composer consumes it.
 *
 * Deliberately a plain struct rather than surface_cache_service::instance so the composer can
 * be built and validated without the ECS, the asset layer, or a GPU.
 */
struct global_sdf_instance
{
    ///< The field being placed. Borrowed; must outlive composition.
    const mesh_sdf* sdf = nullptr;
    ///< World to local, for sampling the field at a world position.
    math::mat4 world_to_local{1.0f};
    ///< World-space bounds of the field, for culling.
    math::bbox world_bounds{};
    ///< Smallest scale axis: converts a local-space distance to a conservative world distance.
    float local_to_world_scale = 1.0f;
    ///< Surface properties for the attribute voxels (GI v2 plan 3.1): what the winning instance
    ///< at a surface voxel looks like. This is what lets a cascade hit be attributed to a
    ///< material, which the distance field alone cannot do.
    math::vec3 albedo{0.5f};
    math::vec3 emissive{0.0f};
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
        /// consumer can afford, since composition work is cubic in it. The RUNTIME defaults to
        /// 128 with GPU composition instead (see gi_settings), which halves the level-0 voxel and
        /// with it the scale of everything the cascade gets wrong: thin-geometry leaks, isosurface
        /// displacement, and the acne both produce.
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
        /// Range is the cheaper thing to give up here: the near field is covered by per-instance
        /// fields out to sdf_debug_pass::settings::near_field_distance, and GI rays are bounded
        /// well inside the outermost cascade anyway.
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
        /// The CPU composer remains the REFERENCE: `sample`, `sample_ex` and `resolve_surface_point`
        /// read `level::voxels`, and the bake tests are their only consumers, so they keep working
        /// against a CPU-composed cascade while the runtime uses the GPU one.
        ///
        /// False HERE because a true default silently leaves every headless consumer -- the tests
        /// above all -- with a cascade nothing composes. The RUNTIME opts in through gi_settings,
        /// where a GPU is guaranteed; that is also what makes its 128 resolution affordable.
        bool compose_on_gpu = false;
        ///< Levels recomposed per update, at most. Composition touches every voxel of a level,
        ///< so recomposing all of them in the frame the camera crosses a voxel boundary would
        ///< hitch. Levels are considered finest first, which is also the order they go stale in
        ///< (level 0 has the smallest voxels, so its origin re-snaps most often).
        uint32_t max_levels_per_update = 1;
    };

    /**
     * @brief What the last composition actually did, for diagnosing cost in a REAL scene.
     *
     * The shape of the work depends entirely on how the instances are distributed, and a
     * synthetic fixture can be made to show almost any answer. These are the numbers that
     * distinguish the possible causes from each other: too many instances reaching a level, too
     * many sharing a cull cell, or the cheap reject failing so that most candidates are sampled
     * in full.
     */
    struct compose_stats
    {
        uint32_t level = 0;
        ///< Instances whose bounds reach this level at all, after the per-level cull.
        uint32_t relevant_instances = 0;
        uint32_t cull_cells = 0;
        ///< Instance-in-cell entries. Divided by cells, the mean candidates a voxel considers.
        uint32_t cull_references = 0;
        ///< Worst cell. A scene-spanning instance lands in EVERY cell, so a high floor here
        ///< means the grid cannot help however fine the cells get.
        uint32_t max_candidates_in_cell = 0;
        ///< Candidates considered, and how many survived the cheap bounds reject to be sampled.
        ///< The ratio is what says whether the remaining cost is rejects or real field lookups.
        uint64_t candidate_tests = 0;
        uint64_t field_samples = 0;
    };

    auto get_last_compose_stats() const -> const compose_stats&
    {
        return last_compose_stats_;
    }

    struct level
    {
        ///< World-space minimum corner, snapped to a whole multiple of @ref voxel_size.
        math::vec3 origin{0.0f};
        ///< World-space edge length of one voxel.
        float voxel_size = 0.0f;
        ///< resolution^3 voxels, x-major (`x + y * res + z * res * res`), R8 encoded.
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
        ///< Attribute voxels at half the distance resolution (GI v2 plan 3.1), recomposed with
        ///< the level. RGBA8 packed (r,g,b = winning instance albedo, a = 255 where the voxel is
        ///< SURFACE - within GI_SURFACE_VOXEL_BAND attribute voxels of the composed isosurface -
        ///< and 0 everywhere else). (attr_resolution)^3, x-major like @ref voxels.
        std::vector<uint32_t> attr_albedo;
        ///< Winning instance emissive per attribute voxel, radiance units. Zero off-surface.
        std::vector<math::vec3> attr_emissive;
        ///< The surface-voxel list: packed attribute-voxel coordinates (see pack_surface_voxel)
        ///< of every voxel whose albedo alpha is 255. CPU reference of the GPU append buffer
        ///< that drives the light-voxel update's indirect dispatch.
        std::vector<uint32_t> attr_surface_list;

        auto is_valid() const -> bool
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
     * Recomposition is BUDGETED even when instances moved. Composing a level is expensive enough
     * to be a visible hitch, and doing all four in the frame something moved is what made any
     * animation in the scene stutter. The cost of budgeting is that a moved object keeps
     * occluding from its old position for a few frames rather than one; that is bounded and
     * eventually consistent, where the failure this replaces -- never noticing at all -- was
     * permanent. Staleness age drives the order, so no level can starve.
     *
     * @param instances Every resident field placement in the world, NOT only visible ones.
     * @param camera_position Centre of the cascade.
     * @return The number of levels recomposed, for budgeting and diagnostics.
     */
    auto update(const std::vector<global_sdf_instance>& instances, const math::vec3& camera_position)
        -> uint32_t;

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

    /// Attribute voxels per axis: half the distance resolution. Halving is the memory/coverage
    /// point the plan's section 6 budget is computed at; the light voxels this feeds live at the
    /// same resolution.
    static constexpr uint32_t attr_downsample = 2;

    auto get_attr_resolution() const -> uint32_t
    {
        return settings_.resolution / attr_downsample;
    }

    /// Packs an attribute-voxel coordinate + level into one uint for the surface-voxel list.
    /// 8 bits per axis (attribute resolutions through 256) + 2 bits of level.
    static auto pack_surface_voxel(uint32_t x, uint32_t y, uint32_t z, uint32_t level) -> uint32_t
    {
        return x | (y << 8u) | (z << 16u) | (level << 24u);
    }

    auto get_memory_usage() const -> size_t;

private:
    /// Composes one level's voxels from the instances reaching it.
    void compose_level(uint32_t index, const std::vector<global_sdf_instance>& instances);

    /**
     * @brief Composes one level's ATTRIBUTE voxels (albedo, emissive, surface list) from the
     *        instances, after the distance voxels are current.
     *
     * REFERENCE IMPLEMENTATION of cs_gi_clipmap_attributes.sc, in the same relationship as
     * compose_level is to cs_gi_clipmap_compose.sc.
     *
     * A voxel is SURFACE when the composed field at its centre lies within
     * GI_SURFACE_VOXEL_BAND attribute voxels of zero. The field is the judge - not the raw
     * per-instance samples - because mesh fields saturate at mesh scale, so deep interiors of
     * thick objects read as "near a surface" to them, while the composed level saturates in
     * LEVEL voxels and excludes interiors correctly.
     *
     * The winner is the instance with the smallest |distance| at the voxel centre, ties broken
     * by the smaller GLOBAL instance index. The tie-break is load bearing: the CPU and GPU walks
     * visit candidates in different orders, and argmin without a deterministic tie rule would
     * make the two composers disagree on exactly the voxels where two surfaces meet.
     */
    void compose_level_attributes(uint32_t index, const std::vector<global_sdf_instance>& instances);

    /// World-space region a level covers, given its origin.
    auto compute_level_bounds(uint32_t index, const math::vec3& origin) const -> math::bbox;

    /// Distance beyond a level's bounds at which an instance can still write into it.
    auto compute_level_reach(uint32_t index) const -> float;

    /// Order-independent hash of the instances that would compose a level covering @p bounds.
    /// Must select exactly the set compose_level does, or a change that alters the composition
    /// could leave the fingerprint equal and the level would never be rebuilt.
    auto compute_level_fingerprint(const math::bbox& bounds,
                                   float reach,
                                   const std::vector<global_sdf_instance>& instances) const -> uint64_t;

    settings settings_{};
    compose_stats last_compose_stats_{};
    std::array<level, level_count> levels_{};
    /// One bit per level, set when that level's voxels were rewritten and the GPU mirror is
    /// therefore stale. Cleared by the owner once it has uploaded.
    uint32_t dirty_levels_ = 0;
};

} // namespace unravel
