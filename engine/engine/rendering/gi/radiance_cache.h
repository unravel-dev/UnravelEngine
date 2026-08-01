#pragma once

#include <engine/engine_export.h>

#include <math/math.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief World-anchored spatial hash holding cached radiance for surfaces.
 *
 * This is the structure that makes the lighting world stable. An entry is addressed purely by
 * WHERE a surface point is and WHICH WAY it faces, never by anything derived from the camera,
 * so the same point resolves to the same entry no matter where it is viewed from or whether it
 * is on screen at all. Screen-space techniques cannot do this: their history is indexed by
 * pixel, so it is destroyed whenever the camera moves.
 *
 * Storing radiance here also makes bounces free. A ray that hits a surface reads the cached
 * value, which already includes the light that surface received from everywhere else, so the
 * result converges toward many bounces without ever tracing a second one.
 *
 * The layout is deliberately the one the GPU can implement: fixed-size, open addressed with a
 * short probe chain, and evicted by age. There is no free list and no compaction pass -- a
 * ring buffer would need per-frame defragmentation, which is a cost this avoids entirely.
 */
class radiance_cache
{
public:
    /// Probe chain length. A key hashes to one slot and may occupy any of the next few; beyond
    /// that the oldest is evicted. Short chains keep the GPU lookup a bounded, coherent read.
    ///
    /// Must equal GI_CACHE_PROBE_LENGTH in `radiance_cache.sh`. A reader probing a shorter chain
    /// than the writer used silently fails to find entries that are present.
    static constexpr uint32_t probe_length = 8;
    /// Default entry count, shared with the GPU mirror so the two cannot drift.
    ///
    /// Sized for the demand, which is dominated by the far field rather than by anything near
    /// the camera: cells grow with distance, but area grows faster, so most of the table is
    /// spent on the coarse levels. A ground plane out to `insert_max_distance` alone accounts
    /// for tens of thousands of cells. Under-sizing does not degrade gracefully -- an open
    /// addressed table near saturation drops most inserts AND fails most lookups, which reads
    /// as a cache that is populated yet never hit.
    static constexpr uint32_t default_capacity = 1u << 19;
    /// Sentinel for an unoccupied slot. A real key is remapped away from this value.
    static constexpr uint32_t empty_key = 0u;
    static constexpr uint32_t invalid_slot = 0xFFFFFFFFu;

    struct settings
    {
        ///< Entries, rounded up to a power of two so the modulo is a mask.
        uint32_t capacity = default_capacity;
        ///< Edge length of a level-0 cell, in world units.
        float base_cell_size = 0.25f;
        ///< Distance from the camera at which cells start growing.
        ///
        /// Generous, because cell size is the dominant control on LIGHT LEAKING. Everything
        /// inside one cell that shares a normal bin shares a single entry, so two parallel walls
        /// closer together than a cell are served each other's light. Keeping cells fine out to a
        /// useful distance costs entries, which the table has room for, and buys leak-freedom
        /// that no amount of tracing accuracy can recover once the key has merged two surfaces.
        float base_distance = 16.0f;
        ///< Cell size doubles every level; this caps how coarse it can get.
        ///
        /// The cap matters more than it looks. At level 6 a cell is 0.25 * 64 = 16 m across --
        /// larger than most rooms -- so distant geometry merges wholesale and small emitters are
        /// averaged away to nothing. Capping at 3 keeps the coarsest cell to 2 m, which is under
        /// the spacing of typical wall geometry.
        uint32_t max_level = 3;
    };

    /// One cached surface element.
    struct entry
    {
        ///< Full hash of the key, used to detect that a probe landed on a different cell.
        uint32_t key = empty_key;
        ///< Frame this entry was last touched, for age-based eviction.
        uint32_t frame_touched = 0;
        ///< Running mean of radiance arriving at this cell.
        math::vec3 radiance{0.0f};
        ///< Samples accumulated, saturating at the configured maximum.
        uint32_t sample_count = 0;
    };

    void init(const settings& settings);
    void clear();

    /**
     * @brief Level of detail for a point, from its distance to the camera.
     *
     * Cells grow with distance so they stay roughly constant in screen size, which is what
     * bounds the cache's size independently of how large the world is. Deliberately a step
     * function: within a level the cell size is constant, so a moving camera does not
     * continuously reshape the grid underneath the cached values.
     */
    auto compute_level(const math::vec3& position, const math::vec3& camera_position) const -> uint32_t;

    auto get_cell_size(uint32_t level) const -> float;

    /**
     * @brief The key for a surface point.
     *
     * Depends only on the quantised position, the level, and the facing direction. The normal
     * is part of the key so that the lit and unlit sides of a wall never share an entry, which
     * is the first line of defence against light leaking through it.
     */
    auto compute_key(const math::vec3& position, const math::vec3& normal, uint32_t level) const -> uint32_t;

    /// @brief As @ref compute_key, with the facing already quantised to a cube face.
    auto compute_key_for_face(const math::vec3& position, uint32_t face, uint32_t level) const -> uint32_t;

    /**
     * @brief Finds the slot for a surface, tolerating a facing that sits on a quantisation
     *        boundary.
     *
     * A writer and a reader derive the normal from different sources -- the G-buffer on one side,
     * the field gradient on the other -- so a surface whose normal falls between two cube faces
     * can be binned differently by each, and would then never be found. When the dominant face
     * misses, the runner up is tried: near a tie both sides agree on the SET of the top two
     * faces even when they disagree on the order, so this always covers the writer's choice.
     */
    auto find_surface(const math::vec3& position, const math::vec3& normal, uint32_t level) const -> uint32_t;

    /**
     * @brief Finds a key's slot, inserting it when absent.
     * @return The slot, or @ref invalid_slot when the probe chain is full of newer entries.
     */
    auto insert(uint32_t key, uint32_t frame) -> uint32_t;

    /**
     * @brief Finds a key's slot without inserting.
     * @return The slot, or @ref invalid_slot when the key is not resident.
     */
    auto find(uint32_t key) const -> uint32_t;

    /**
     * @brief Blends a radiance sample into a slot as a running mean.
     *
     * The blend weight falls as 1/n while the entry is young, so it converges quickly, then
     * floors at @p min_alpha so it keeps responding to change instead of freezing.
     */
    void accumulate(uint32_t slot, const math::vec3& radiance, uint32_t frame, float min_alpha,
                    uint32_t max_samples);

    auto get_entry(uint32_t slot) const -> const entry&
    {
        return entries_[slot];
    }

    auto get_settings() const -> const settings&
    {
        return settings_;
    }

    auto get_capacity() const -> uint32_t
    {
        return uint32_t(entries_.size());
    }

    /// Occupied entries. Linear in capacity; for diagnostics, not per-frame use.
    auto count_occupied() const -> uint32_t;

private:
    settings settings_{};
    std::vector<entry> entries_;
    uint32_t capacity_mask_ = 0;
};

/**
 * @brief Quantises a normal to one of 24 directions: a cube face, subdivided 2x2.
 *
 * Coarse on purpose. The direction only has to separate surfaces that face meaningfully
 * differently -- the two sides of a wall, the floor from the ceiling -- while still letting
 * everything on one flat surface share entries. Finer quantisation would fragment a flat
 * surface across many entries and slow convergence for no benefit.
 */
/**
 * @brief Quantises a normal to one of the 6 cube faces.
 *
 * Six, not a finer subdivision. A 2x2 split per face puts a bin boundary exactly through the
 * face CENTRE -- that is, through the axis-aligned directions, which are by far the most common
 * normals in a built scene. A floor at (0, 1, 0) then has its bin decided by the sign of two
 * components that are both zero, so the tiniest disagreement between two sources of that normal
 * yields a different bin, and every large flat surface becomes a permanent cache miss. With six
 * faces the axis directions sit at bin CENTRES instead, where they are maximally robust.
 *
 * Coarse is also sufficient: the facing only has to separate surfaces that face meaningfully
 * differently -- the two sides of a wall, the floor from the ceiling -- which six faces do.
 */
auto quantize_normal(const math::vec3& normal) -> uint32_t;

/// @brief The runner up to @ref quantize_normal, used to tolerate a facing near a bin boundary.
auto quantize_normal_second(const math::vec3& normal) -> uint32_t;

/// @brief Unit direction of a cube face produced by @ref quantize_normal.
auto face_direction(uint32_t face) -> math::vec3;

} // namespace unravel
