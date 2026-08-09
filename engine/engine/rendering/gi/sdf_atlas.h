#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/mesh_sdf.h>

#include <graphics/graphics.h>
#include <graphics/texture.h>

#include <cstdint>
#include <vector>

namespace unravel
{

/**
 * @brief GPU residency for baked mesh distance fields.
 *
 * Holds three parallel resources the tracer samples:
 *   - a 3D brick atlas, one @ref mesh_sdf::brick_stride ^ 3 tile per resident brick;
 *   - an indirection buffer, holding every resident field's brick table back to back, with
 *     surface entries rewritten from mesh-local brick indices to absolute atlas slots;
 *   - a header buffer describing each field's bounds, voxel size and indirection offset.
 *
 * Bricks are allocated individually from a free list rather than in contiguous runs. Nothing
 * requires a field's bricks to be adjacent -- the indirection entry carries an absolute slot
 * -- and individual slots mean a field can always be uploaded as long as enough total bricks
 * are free, with no compaction pass and no fragmentation failure mode.
 */
class sdf_atlas
{
public:
    /// Returned by @ref upload when the field cannot be made resident.
    static constexpr uint32_t invalid_index = 0xFFFFFFFFu;

    /// Floats per header in @ref get_header_buffer, as vec4 elements.
    static constexpr uint32_t header_vec4_count = 3;

    struct settings
    {
        /// Atlas size in bricks per axis. 32 gives 32768 bricks in a 320^3 R8 texture,
        /// about 32 MB, which holds roughly a hundred typical props.
        /// Bricks per axis. Total capacity is the cube of this, and the backing texture is
        /// `atlas_brick_dim * brick_stride` voxels per axis (R8), so the memory cost is cubic
        /// too: 64 is a 640^3 texture at 262 MB, 72 is 720^3 at 373 MB.
        ///
        /// Sized for a whole SCENE, not a mesh. A city block of a model registers a field per
        /// submesh, and running out is silent in the image -- the geometry simply stops
        /// contributing to global illumination -- so it is worth leaving headroom.
        uint32_t atlas_brick_dim = 72;
        /// Brick bytes @ref upload may push in one frame before @ref has_upload_budget starts
        /// deferring whole fields to later frames. A large scene enabling GI otherwise uploads
        /// every field at once - hundreds of megabytes of per-brick texture updates in a single
        /// frame, which overruns the renderer's per-frame staging scratch (32 MB on the Vulkan
        /// backend) and degrades every further update to its own device allocation: the render
        /// thread stalls in allocateMemory while the main thread blocks on frame sync. Spreading
        /// the warmup over frames keeps each frame inside the scratch. Fields appear in GI over
        /// a short ramp instead of one monster frame.
        uint32_t max_upload_bytes_per_frame = 8u << 20u;
    };

    struct stats
    {
        uint32_t resident_fields = 0;
        uint32_t used_bricks = 0;
        uint32_t total_bricks = 0;
        uint32_t indirection_entries = 0;
        size_t atlas_bytes = 0;
    };

    auto init(const settings& settings) -> bool;
    void shutdown();

    auto is_valid() const -> bool
    {
        return static_cast<bool>(atlas_texture_);
    }

    /**
     * @brief Makes a baked field resident.
     * @return The header index the tracer uses to reference it, or @ref invalid_index when
     *         the field is unusable or the atlas has no room for it.
     */
    auto upload(const mesh_sdf& sdf) -> uint32_t;

    /**
     * @brief True while this frame's brick-upload budget still takes @p sdf whole.
     *
     * Fields upload all-or-nothing (see @ref upload), so the budget defers WHOLE fields to a
     * later frame. A deferral is NOT a refusal: the caller must simply retry next frame, and
     * must not cache it against @ref get_release_generation. The first field of a frame is
     * always allowed, so a field larger than the whole budget still loads.
     */
    auto has_upload_budget(const mesh_sdf& sdf) const -> bool;

    /**
     * @brief Releases a resident field, returning its bricks to the free list.
     */
    void release(uint32_t header_index);

    /**
     * @brief Pushes any pending header / indirection changes to the GPU.
     *
     * Called once per frame before the tracer runs. The brick atlas is written directly by
     * @ref upload, so only the two buffers need flushing here.
     */
    void flush();

    auto get_atlas_texture() const -> const gfx::texture::ptr&
    {
        return atlas_texture_;
    }

    /// Atlas size in bricks per axis; the tracer needs it to turn a slot into atlas coords.
    auto get_atlas_brick_dim() const -> uint32_t
    {
        return settings_.atlas_brick_dim;
    }

    /// Atlas size in voxels per axis, borders included.
    auto get_atlas_voxel_dim() const -> uint32_t
    {
        return settings_.atlas_brick_dim * mesh_sdf::brick_stride;
    }

    auto get_header_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return header_buffer_;
    }

    auto get_indirection_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return indirection_buffer_;
    }

    auto get_stats() const -> stats;

    /**
     * @brief Bumped every time @ref release returns bricks to the free list.
     *
     * Lets a caller that was refused for want of room know whether retrying could possibly help.
     * Retrying unconditionally is not viable: a scene that overruns the atlas refuses thousands of
     * meshes, and re-attempting all of them every frame costs real CPU and produces nothing.
     */
    auto get_release_generation() const -> uint32_t
    {
        return release_generation_;
    }

private:
    /// Bookkeeping for one resident field.
    struct resident_field
    {
        std::vector<uint32_t> brick_slots;
        uint32_t indirection_offset = 0;
        uint32_t indirection_count = 0;
        bool is_alive = false;
    };

    auto allocate_brick() -> uint32_t;
    /// Queues a field's bricks for @ref flush_pending_bricks. The voxels are COPIED: the
    /// pending queue must not reference the mesh asset, whose lifetime this class does not
    /// own, and the copy is bounded by settings::max_upload_bytes_per_frame.
    void queue_bricks(const std::vector<uint32_t>& slots, const mesh_sdf& sdf);
    /// Pushes the frame's queued bricks with as FEW texture updates as possible: contiguous
    /// slot runs become boxed region updates (partial row, then whole rows, then whole
    /// slabs). One update per BRICK melted the render thread on every backend that allocates
    /// staging per call - D3D12 creates a committed resource for EACH texture update, so a
    /// large scene's warmup (tens of thousands of bricks per frame) turned into seconds of
    /// driver allocation per frame. Boxing collapses that to a handful of calls per frame.
    /// Slots allocate mostly ascending on a fresh atlas, so the runs are long; a fragmented
    /// free list degrades gracefully toward smaller boxes.
    void flush_pending_bricks();
    auto allocate_indirection(uint32_t count) -> uint32_t;
    /// Widens the dirty indirection span to cover [offset, offset + count).
    void mark_indirection_dirty(uint32_t offset, uint32_t count);
    /// Recreates a buffer when the master copy has outgrown it. Dynamic buffers cannot be
    /// grown by update() alone -- a write past the allocated size is silently dropped, which
    /// leaves the shader reading zeros and is invisible until the traced result is wrong.
    void ensure_buffer_capacity();

    settings settings_{};
    gfx::texture::ptr atlas_texture_;
    /// vec4 elements; @ref header_vec4_count per field.
    gfx::dynamic_vertex_buffer_handle header_buffer_{bgfx::kInvalidHandle};
    /// uint32 elements.
    gfx::dynamic_index_buffer_handle indirection_buffer_{bgfx::kInvalidHandle};

    /// Master copies. Both buffers are small (kilobytes per field), so they are re-uploaded
    /// whole when dirty rather than tracked at sub-range granularity.
    std::vector<float> header_data_;
    std::vector<uint32_t> indirection_data_;
    /// Allocated GPU capacity, in elements (vec4 for headers, uint32 for indirection).
    /// Running totals for the atlas-full report, so the warning can name the shortfall rather
    /// than repeat itself once per refused mesh.
    /// 64-bit, and that is not paranoia: as 32-bit values these reached 3.4 billion in a scene that
    /// overruns the atlas, and the `total * 2` that schedules the next report then overflowed and
    /// wrapped to a small number -- so the condition was true again immediately and the warning
    /// printed on EVERY refusal, thousands per frame. A counter that only ever grows needs a type
    /// that cannot wrap, or the throttle built on it silently becomes the opposite of a throttle.
    uint64_t rejected_mesh_count_ = 0;
    uint64_t rejected_brick_total_ = 0;
    uint64_t next_rejection_report_ = 1;
    /// Incremented by @ref release. See @ref get_release_generation.
    uint32_t release_generation_ = 0;
    /// Brick bytes uploaded this frame; reset by @ref flush. See settings::max_upload_bytes_per_frame.
    uint32_t frame_upload_bytes_ = 0;
    /// A brick queued for upload: its atlas slot and its voxels' offset in @ref pending_brick_voxels_.
    struct pending_brick
    {
        uint32_t slot = 0;
        uint32_t data_offset = 0;
    };
    /// The frame's queued bricks and their copied voxels, drained by @ref flush_pending_bricks.
    std::vector<pending_brick> pending_bricks_;
    std::vector<uint8_t> pending_brick_voxels_;
    uint32_t header_capacity_vec4_ = 0;
    uint32_t indirection_capacity_ = 0;
    bool headers_dirty_ = false;
    /// Dirty SPAN of @ref indirection_data_ (entries, [min, max)), empty when min >= max.
    /// A range rather than a flag because the table reaches millions of entries on a big
    /// scene, and re-uploading all of it once per frame while fields stream in made the
    /// warmup's frames tens of megabytes heavier than the bricks they carried.
    uint32_t indirection_dirty_min_ = 0;
    uint32_t indirection_dirty_max_ = 0;

    /// A released field's indirection region, available for reuse.
    struct indirection_range
    {
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    std::vector<resident_field> fields_;
    std::vector<uint32_t> free_field_indices_;
    std::vector<uint32_t> free_brick_slots_;
    /// Deliberately not coalesced. A field released and re-uploaded asks for the same count, which
    /// is the case that matters (reloading a scene), and that hits an exact fit every time. Merging
    /// adjacent ranges would only help a workload that churns fields of many different sizes, which
    /// this does not have.
    std::vector<indirection_range> free_indirection_ranges_;
    uint32_t next_brick_slot_ = 0;
    uint32_t total_brick_slots_ = 0;
};

} // namespace unravel
