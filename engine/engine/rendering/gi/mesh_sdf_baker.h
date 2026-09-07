#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/mesh_sdf.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief Triangle soup the SDF baker consumes.
 *
 * Kept separate from @ref mesh so the baker can be driven directly from unit tests and
 * from the GI validation harness without constructing an asset.
 */
struct sdf_source_geometry
{
    ///< Local-space vertex positions.
    std::vector<math::vec3> positions;
    ///< Triangle corner indices into @ref positions, three per triangle.
    std::vector<uint32_t> indices;
    ///< Local-space bounds of @ref positions.
    math::bbox bounds{};
    ///< Triangles dropped during extraction for carrying no surface: zero-area slivers and
    ///< non-finite positions. Reported so an asset whose bounds were being set by junk geometry
    ///< is diagnosable at import time rather than by looking at the field it produced.
    uint32_t discarded_triangles = 0;

    auto get_triangle_count() const -> uint32_t
    {
        return uint32_t(indices.size() / 3);
    }

    auto is_valid() const -> bool
    {
        return !positions.empty() && indices.size() >= 3 && (indices.size() % 3) == 0;
    }
};

/**
 * @brief How a triangle soup is distributed inside its own bounds.
 *
 * A field's voxel size comes from its BOUNDS while its useful content is the geometry, and those two
 * only agree when the geometry fills the bounds. A submesh holding several small parts scattered far
 * apart -- street lamps, signage, bolts, anything an artist grouped by material rather than by
 * location -- is mostly empty space, so it gets a voxel sized to the SPREAD rather than to the parts.
 * The parts then fall below one voxel and the field cannot represent them at all.
 *
 * Nothing about that is visible in the viewport: the submesh renders perfectly, the field is sized
 * correctly to the submesh, and every bake setting is being honoured. It is only detectable by
 * comparing the extent of the geometry to the extent of the space it is spread over.
 */
struct sdf_component_summary
{
    ///< Connected pieces, where two triangles are connected when they share a welded position.
    uint32_t component_count = 0;
    ///< Longest axis of the largest single piece -- the scale the field actually has to resolve.
    float largest_component_extent = 0.0f;
    ///< Longest axis of the whole soup -- the scale that picks the voxel size.
    float bounds_extent = 0.0f;

    /**
     * @brief Ratio of the space spread over to the largest piece in it. 1 means solid, high is sparse.
     *
     * This is the number that predicts an unusable field: it is very nearly how many times too coarse
     * the voxel is, since the voxel is the bounds divided by a fixed count.
     */
    auto get_sparsity() const -> float
    {
        return largest_component_extent > 0.0f ? bounds_extent / largest_component_extent : 0.0f;
    }
};

/**
 * @brief Measures how a triangle soup is distributed across its bounds.
 *
 * Connectivity is judged on WELDED positions rather than on vertex indices, because a seam duplicates
 * a position for a different normal or UV -- keyed by index, one solid part reads as several pieces
 * and a scattered submesh looks exactly like a normal one.
 */
auto summarize_connected_components(const sdf_source_geometry& geometry) -> sdf_component_summary;

/**
 * @brief Controls for a single SDF bake. Mirrored per asset in the mesh importer meta.
 */
struct mesh_sdf_bake_settings
{
    ///< THE knob. Edge length of one voxel, in local units; 0 means derive it from
    ///< @ref resolution, which is what every existing asset does.
    ///<
    ///< This is the quantity everything else is a function of, which is why it is the one worth
    ///< setting directly:
    ///<   - detail: the field resolves features about this size, and cannot represent thinner
    ///<     geometry at all;
    ///<   - range: the narrow band reaches @ref mesh_sdf::encode_range voxels, so four times
    ///<     this, and past it a stored brick saturates;
    ///<   - memory and bake time: a surface is two-dimensional, so both go as the inverse SQUARE
    ///<     -- halving this costs about four times as much, not eight.
    ///<
    ///< Finer is not simply better, and the range line above is why: see the sizing comment in
    ///< bake_mesh_sdf for the measurement that settles it.
    ///<
    ///< The caps only ever coarsen, so a request can be refused but never exceeded. When one
    ///< does refuse, the asset compiler says so rather than leaving the setting looking ignored.
    float target_voxel_size = 0.0f;
    ///< Fallback used when @ref target_voxel_size is 0: target voxel count along the longest
    ///< BOUNDS axis. A poor proxy for quality, because it is relative to the mesh rather than to
    ///< the world -- a 10 m wall and a 1 m prop at 64 differ seventeenfold in the size they
    ///< actually resolve. Kept so existing assets bake exactly as they did.
    uint32_t resolution = 64;
    ///< Lower clamp on the derived voxel size, in local units. Stops tiny props from
    ///< producing needlessly dense fields.
    float min_voxel_size = 0.01f;
    ///< Upper clamp on the derived voxel size, in local units. Stops large meshes from
    ///< producing fields too coarse to occlude anything.
    float max_voxel_size = 1.0f;
    ///< Hard ceiling on grid voxels per axis, after clamping. Bounds the SHAPE of a field, not
    ///< its cost: see @ref max_total_voxels.
    uint32_t max_resolution = 256;
    ///< Hard ceiling on TOTAL grid voxels in one field. THE cost budget, and the only one:
    ///< both bake time and atlas footprint scale with voxel count, and voxel count is cubic in
    ///< resolution, so a per-axis cap of 256 still permits 16.7M voxels in a single field --
    ///< far more than the whole scene's atlas holds. Enforced by growing the voxel size, so the
    ///< field always still covers the whole mesh; the cost is detail, never coverage.
    ///<
    ///< The default is what a cubic mesh at @ref resolution 64 asks for, so it does not bite on
    ///< the nominal case and only catches fields that would otherwise run away. In practice it
    ///< is this rather than @ref resolution that settles the size of a compact mesh.
    ///<
    ///< It measures the DENSE grid, which is admittedly the wrong shape: a surface is
    ///< two-dimensional, so this charges a hollow or flat mesh for space it never stores.
    ///< Budgeting stored bricks instead was built and reverted -- see the sizing comment in
    ///< bake_mesh_sdf. If it returns it replaces this setting rather than joining it; one field
    ///< should not need two budgets to describe its cost.
    uint64_t max_total_voxels = 262144;
    ///< Bake an unsigned shell instead of a signed field. Required for foliage cards and
    ///< any other geometry that is not a closed surface, where the inside/outside test is
    ///< meaningless and would produce randomly signed voxels.
    bool two_sided = false;
    ///< Local-space half-thickness given to the shell when @ref two_sided is set.
    float two_sided_thickness = 0.05f;
    ///< Refuse to bake when the geometry is spread over more than this many times its largest
    ///< connected piece.
    ///<
    ///< The voxel size comes from the BOUNDS while the content is the geometry, so a submesh holding
    ///< small parts scattered far apart -- lamps, signage, bolts, anything grouped by material rather
    ///< than by location -- gets a voxel sized to the gaps. Past a point the parts fall below one
    ///< voxel and the field cannot represent them at all; what it produces instead is a blob the size
    ///< of the spread, which traces as solid geometry that is not there.
    ///<
    ///< Refusing is strictly better than that. A skipped field means the parts do not contribute to
    ///< GI, and they were too small to contribute usefully anyway; a phantom field occludes a whole
    ///< neighbourhood. Set to 0 to disable the check.
    ///<
    ///< Deliberately far above normal authoring: a handful of parts side by side measures a few x,
    ///< while the cases that break tracing measure in the hundreds or thousands.
    float max_component_spread = 32.0f;
};

/**
 * @brief Whether a single bake may spread its voxel work over the shared thread pool.
 *
 * The choice belongs to the caller because it depends on what the caller is doing, not on the
 * asset: one large field wants the pool to itself, while a model made of many small fields is
 * better parallelised one field per thread.
 */
enum class sdf_bake_threading : uint8_t
{
    /// Spread the voxel work across the shared pool. Right when this bake is the only work
    /// in flight.
    parallel,
    /// Keep the whole bake on the calling thread. REQUIRED when the caller is itself running
    /// on the shared pool: poolstl's parallel algorithms submit their chunks to that pool and
    /// then block the calling thread until the chunks finish, so a pool thread that starts a
    /// nested parallel algorithm ends up waiting on work queued behind itself. With every pool
    /// thread doing that, nothing can drain the queue and the bake deadlocks.
    serial,
};

/**
 * @brief Bakes a sparse signed distance field from a triangle soup.
 *
 * Sign comes from the angle-weighted pseudonormal of the closest surface feature, which is
 * exact for closed manifold meshes and is the reason @ref mesh_sdf_bake_settings::two_sided
 * exists for meshes that are not.
 *
 * Safe to call from a worker thread; touches no GPU state and no shared engine state. Callers
 * already running on the shared pool must pass @ref sdf_bake_threading::serial.
 *
 * @return false when the geometry is unusable (degenerate bounds, no triangles).
 */
auto bake_mesh_sdf(const sdf_source_geometry& geometry,
                   const mesh_sdf_bake_settings& settings,
                   mesh_sdf& out,
                   sdf_bake_threading threading = sdf_bake_threading::parallel) -> bool;

/**
 * @brief Bakes a field and its coarser levels, finest first.
 *
 * Each level doubles the voxel of the one before it, so it costs about a quarter as much: a
 * surface is two-dimensional, and stored bricks track the surface. A three-level chain is
 * therefore roughly a third more work than the finest level alone, not three times.
 *
 * Levels are baked INDEPENDENTLY rather than downsampled from the level above. Resampling a
 * stored field would be cheaper, but the stored field is a saturating narrow band and a
 * conservative under-estimate; interpolating it does not reliably stay conservative, and "never
 * over-estimate" is the one property sphere tracing cannot survive losing. UE rebuilds each mip
 * from the source geometry for the same reason.
 *
 * A level that cannot be produced ends the chain rather than failing the bake, so the result is
 * always usable and never longer than @p mip_count.
 *
 * @return false only when the finest level itself could not be baked.
 */
auto bake_mesh_sdf_mips(const sdf_source_geometry& geometry,
                        const mesh_sdf_bake_settings& settings,
                        std::vector<mesh_sdf>& out,
                        uint32_t mip_count = mesh_sdf::mip_count,
                        sdf_bake_threading threading = sdf_bake_threading::parallel) -> bool;

/**
 * @brief Samples a baked field at a local-space point, in local units.
 *
 * Reference implementation of the addressing and decoding the tracing shader performs.
 * The validation harness compares the two to catch CPU/GPU divergence.
 */
auto sample_mesh_sdf(const mesh_sdf& sdf, const math::vec3& local_position) -> float;

} // namespace unravel
