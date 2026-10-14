#pragma once

#include "particle_cutout.h"
#include "particle_eval.h"
#include "particle_gpu_schema.h"
#include "particle_record.h"

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/buffer_clear.h>

#include <bgfx/bgfx.h>
#include <context/context.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace unravel
{
namespace ps_soa
{

/**
 * @brief The GPU side of the particles: one pool of particle records shared by every emitter, the
 * emitter tables, the curve-set atlas, and the per-view cull / sort / draw work.
 *
 * Everything the CPU writes goes through bgfx updates of CPU-written buffers (tables, curve atlas,
 * staging) at flush; the record pool is GPU-written, filled by the scatter kernel from the staging
 * buffer. bgfx applies a frame's updates before any of its views run, so each flush within a frame
 * stages into its own buffer.
 */
class particle_gpu
{
public:
    static constexpr uint32_t invalid_base = 0xffffffffu;

    void init(rtti::context& ctx);
    void shutdown();

    /// The record scatter and the particle programs loaded: particles can draw.
    auto can_draw() const -> bool;
    /// The cull / sort / argument kernels loaded too: views cull and sort on the GPU.
    auto can_sort() const -> bool;

    /// Reserves @p capacity pool slots; returns the first or invalid_base.
    auto reserve_range(uint32_t capacity) -> uint32_t;
    void release_range(uint32_t base, uint32_t capacity);
    /// True once after the pool was reallocated: every live record must be staged again.
    auto take_pool_reset() -> bool;

    /**
     * @brief The atlas block holding the curve set @p key, baking it with @p bake when no emitter uses
     * it yet. Thread safe.
     */
    template<typename Baker>
    auto acquire_lut(uint64_t key, Baker&& bake) -> uint32_t
    {
        std::lock_guard<std::mutex> lock(lut_mutex_);
        auto found = luts_.find(key);
        if(found != luts_.end())
        {
            ++found->second.references;
            return found->second.block;
        }
        lut_entry entry;
        entry.block = allocate_lut_block();
        entry.references = 1;
        entry.data = std::make_unique<lut_set>();
        bake(*entry.data);
        dirty_lut_blocks_.push_back(entry.block);
        lut_block_keys_[entry.block] = key;
        const uint32_t block = entry.block;
        luts_.emplace(key, std::move(entry));
        return block;
    }
    void release_lut(uint64_t key);
    auto get_lut(uint64_t key) const -> const lut_set*;

    /// Cutout tables: each sheet frame's cutout_polygon (cutout_frame_vec4s vec4), shared by every emitter of one
    /// key. Main thread.
    void acquire_cutout(uint64_t key);
    void release_cutout(uint64_t key);
    /// True while the table of @p key has no polygons yet.
    auto is_cutout_pending(uint64_t key) const -> bool;
    /// Fills the table of @p key (ignored once it is released).
    void set_cutout_polygons(uint64_t key, const std::vector<cutout_polygon>& polygons);
    /// The table's first vec4 + 1, as the static row stores it; 0 while it has no polygons.
    auto get_cutout_table(uint64_t key) const -> float;

    /// The turbulence noise volume (particle_noise.h) the vertex shaders sample.
    auto get_noise_texture() const -> bgfx::TextureHandle;

    /// Rows of emitter @p emitter (its handle index). Each emitter writes only its own rows.
    void set_static_row(uint16_t emitter, const static_row& row);
    void set_dynamic_row(uint16_t emitter, const dynamic_row& row);

    /// Queues a record for its pool slot @p pool_slot. Main thread.
    void stage(uint32_t pool_slot, const particle_record& record);

    /// Uploads everything staged and changed since the last flush. Main thread.
    void flush();

    struct view_task
    {
        uint32_t pool_base = 0;
        uint32_t high_water = 0;
        uint32_t batch = 0;
        gpu_sort_kind sort_kind = gpu_sort_kind::append;
    };

    struct view_batch
    {
        /// First entry of the batch in the sorted list and how many entries it may fill.
        uint32_t first = 0;
        uint32_t capacity = 0;
        bool is_sorted = false;
        /// The batch's geometry in the particle index buffer.
        uint32_t index_count = quad_index_count;
        uint32_t first_index = 0;
    };

    struct view_dispatch_stats
    {
        uint32_t dispatches = 0;
        uint32_t sorted_keys = 0;
    };

    /**
     * @brief Culls and sorts @p tasks into this view's list (GPU path): fills @p batches' first entries
     * and writes their draw arguments. Sorted batches come first in @p batches' list order.
     */
    auto dispatch_view(bgfx::ViewId view,
                       const std::vector<view_task>& tasks,
                       std::vector<view_batch>& batches,
                       const math::vec3& eye,
                       const math::vec3& forward,
                       float camera_speed,
                       const std::array<math::vec4, 6>& frustum_planes) -> view_dispatch_stats;

    /// Appends a CPU-sorted list of pool slots to this view's list; returns its first entry.
    auto upload_cpu_list(const std::vector<uint32_t>& slots) -> uint32_t;
    /// Uploads the view's CPU-sorted lists. Call after the last upload_cpu_list, before the draws.
    void upload_view_lists();

    struct draw_params
    {
        bgfx::ViewId view = 0;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        uint64_t state = 0;
        /// bgfx::setState's second argument: the other render targets' blend with BGFX_STATE_BLEND_INDEPENDENT.
        uint32_t blend_rgba = 0;
        uint32_t first = 0;
        /// GPU batch index (indirect arguments) or, with a CPU list, the instance count.
        uint32_t batch = 0;
        uint32_t instance_count = 0;
        bool is_cpu_list = false;
        /// The geometry in the particle index buffer.
        uint32_t index_count = quad_index_count;
        uint32_t first_index = 0;
    };

    /// Binds the particle buffers and submits one batch. The caller sets textures and uniforms first.
    void submit_draw(const draw_params& params);

    /// Takes this view's buffer slot (each view of a frame has its own CPU-written buffers).
    void begin_view();

private:
    struct cutout_entry
    {
        uint32_t references = 0;
        uint32_t base = 0;
        uint32_t size = 0;
        bool has_polygons = false;
    };

    struct lut_entry
    {
        uint32_t block = 0;
        uint32_t references = 0;
        std::unique_ptr<lut_set> data;
    };

    struct range
    {
        uint32_t base = 0;
        uint32_t size = 0;
    };

    struct view_buffers
    {
        bgfx::DynamicIndexBufferHandle tasks = BGFX_INVALID_HANDLE;
        uint32_t task_capacity = 0;
        bgfx::DynamicIndexBufferHandle batches = BGFX_INVALID_HANDLE;
        uint32_t batch_capacity = 0;
        bgfx::DynamicIndexBufferHandle cpu_list = BGFX_INVALID_HANDLE;
        uint32_t cpu_list_capacity = 0;
        uint32_t cpu_list_used = 0;
        std::vector<uint32_t> cpu_list_data;
    };

    struct staging_buffer
    {
        bgfx::DynamicIndexBufferHandle handle = BGFX_INVALID_HANDLE;
        uint32_t capacity = 0;
    };

    auto allocate_lut_block() -> uint32_t;
    void grow_pool(uint32_t required);
    void ensure_tables();
    void ensure_lut_atlas();
    void ensure_sort_buffers(uint32_t entries, uint32_t batches);
    void upload_tables();
    void upload_luts();
    /// Grows the cutout buffer to hold every table (it always exists: the vertex shaders read it).
    void ensure_cutout_buffer();
    void upload_cutouts();
    /// First-fit range of @p size vec4 in the cutout tables.
    auto allocate_cutout_range(uint32_t size) -> uint32_t;
    /// Scatters the staged records into the pool; false when the scatter could not run (they stay staged).
    auto upload_staging(bgfx::ViewId view) -> bool;
    /// Keeps only the last record staged for each slot: records kept over frames may stage a slot again.
    void keep_latest_staged();
    auto current_view_buffers() -> view_buffers&;
    /// Starts the per-frame buffer slots over when the render frame changed.
    void begin_frame();
    /// Rebuilds programs whose shaders were recompiled and records which kernels can run.
    void refresh_programs();
    void create_noise_texture();
    void destroy_buffers();

    std::shared_ptr<gpu_program> scatter_program_;
    std::shared_ptr<gpu_program> keygen_program_;
    std::shared_ptr<gpu_program> sort_program_;
    std::shared_ptr<gpu_program> args_program_;
    buffer_clear clear_;
    bool can_draw_ = false;
    bool can_sort_ = false;
    bgfx::UniformHandle u_scatter_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_view0_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_view1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_view2_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_frustum_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sort_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_args_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_draw_ = BGFX_INVALID_HANDLE;
    /// The turbulence noise volume's sampler: the cull pass places particles with their turbulence.
    bgfx::UniformHandle u_noise_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout vec4_layout_;
    bgfx::VertexLayout quad_layout_;
    bgfx::VertexBufferHandle quad_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle quad_indices_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle noise_texture_ = BGFX_INVALID_HANDLE;

    // Record pool.
    bgfx::DynamicIndexBufferHandle records_ = BGFX_INVALID_HANDLE;
    uint32_t pool_capacity_ = 0;
    std::vector<range> free_ranges_;
    bool pool_reset_ = false;
    std::vector<particle_record> staged_records_;
    std::vector<uint32_t> staged_slots_;
    /// Records stayed staged because the scatter could not run: a slot may be staged more than once.
    bool has_kept_staging_ = false;
    std::vector<staging_buffer> staging_buffers_;
    uint32_t staging_used_this_frame_ = 0;

    // Emitter tables.
    bgfx::DynamicVertexBufferHandle static_table_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle dynamic_table_ = BGFX_INVALID_HANDLE;
    std::vector<static_row> static_rows_;
    std::vector<dynamic_row> dynamic_rows_;
    std::vector<uint8_t> static_dirty_;
    uint32_t table_rows_ = 0;
    bool tables_need_full_upload_ = true;

    // Curve-set atlas.
    mutable std::mutex lut_mutex_;
    std::unordered_map<uint64_t, lut_entry> luts_;
    std::unordered_map<uint32_t, uint64_t> lut_block_keys_;
    std::vector<uint32_t> free_lut_blocks_;
    std::vector<uint32_t> dirty_lut_blocks_;
    uint32_t lut_block_count_ = 0;
    bgfx::DynamicVertexBufferHandle lut_atlas_ = BGFX_INVALID_HANDLE;
    std::unordered_map<uint64_t, cutout_entry> cutouts_;
    /// The tables' vec4, their free ranges, and whether the buffer needs them again.
    std::vector<math::vec4> cutout_data_;
    std::vector<range> free_cutout_ranges_;
    bool cutouts_dirty_ = false;
    bgfx::DynamicVertexBufferHandle cutout_buffer_ = BGFX_INVALID_HANDLE;
    uint32_t cutout_buffer_capacity_ = 0;
    uint32_t lut_atlas_blocks_ = 0;

    // Per-view work.
    bgfx::DynamicIndexBufferHandle sort_ = BGFX_INVALID_HANDLE;
    uint32_t sort_capacity_ = 0;
    bgfx::DynamicIndexBufferHandle counters_ = BGFX_INVALID_HANDLE;
    /// Each batch's live count of the last dispatch (cs_particle_draw_args.sc), for the vertex shaders.
    bgfx::DynamicIndexBufferHandle batch_counts_ = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle draw_args_ = BGFX_INVALID_HANDLE;
    uint32_t batch_capacity_ = 0;
    bool counters_need_clear_ = false;
    std::vector<view_buffers> view_buffers_;
    uint32_t views_this_frame_ = 0;
    uint32_t frame_ = 0xffffffffu;
    std::vector<uint32_t> task_data_;
    std::vector<uint32_t> batch_data_;
};

} // namespace ps_soa
} // namespace unravel
