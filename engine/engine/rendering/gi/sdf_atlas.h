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
    void upload_brick(uint32_t slot, const uint8_t* voxels);
    auto allocate_indirection(uint32_t count) -> uint32_t;
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
    uint32_t rejected_mesh_count_ = 0;
    uint32_t rejected_brick_total_ = 0;
    uint32_t next_rejection_report_ = 1;
    uint32_t header_capacity_vec4_ = 0;
    uint32_t indirection_capacity_ = 0;
    bool headers_dirty_ = false;
    bool indirection_dirty_ = false;

    std::vector<resident_field> fields_;
    std::vector<uint32_t> free_field_indices_;
    std::vector<uint32_t> free_brick_slots_;
    uint32_t next_brick_slot_ = 0;
    uint32_t total_brick_slots_ = 0;
};

} // namespace unravel
