#pragma once

#include "particle_cutout.h"
#include "particle_eval.h"
#include "particle_gpu_schema.h"
#include "particle_record.h"

#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/buffer_clear.h>
#include <engine/rendering/shader_buffer.h>

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
 * @brief Where the particle data lives on this device: storage buffers where it runs
 * render_feature::particle_storage_buffers; elsewhere textures, which the CPU fills, culling and sorting the particles.
 */
auto get_particle_data_storage() -> shader_buffer_storage;

/**
 * @brief The GPU side of the particles: one pool of particle records shared by every emitter, the
 * emitter tables, the curve-set atlas, and the per-view cull / sort / draw work.
 *
 * Everything the CPU writes goes through bgfx updates of CPU-written buffers (tables, curve atlas,
 * staging) at flush; the record pool is GPU-written, filled by the scatter kernel from the staging
 * buffer. bgfx applies a frame's updates before any of its views run, so each flush within a frame
 * stages into its own buffer.
 *
 * The collide kernel rewrites the records of particles that touch a scene surface (collide_view), so the pool holds
 * what no CPU copy does: a pool that grows is copied into the larger one on the GPU.
 *
 * The record pool, the emitter tables, the curve-set atlas and the CPU-sorted lists are shader buffers: where the
 * device has no storage buffer path (get_particle_data_storage) they are textures, the CPU writes the records into
 * the pool at flush, and culls and sorts every view. The cutout tables live in atlas blocks of their own, and the
 * GPU path's sorted list holds each batch's live count after its entries, so a draw binds no buffer for either.
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
    /// True once after the pool was reallocated without its records: every live record must be staged again.
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
    /// key. A table takes whole blocks of the curve-set atlas. Main thread.
    void acquire_cutout(uint64_t key);
    void release_cutout(uint64_t key);
    /// True while the table of @p key has no polygons yet.
    auto is_cutout_pending(uint64_t key) const -> bool;
    /// Fills the table of @p key (ignored once it is released).
    void set_cutout_polygons(uint64_t key, const std::vector<cutout_polygon>& polygons);
    /// The table's first vec4 in the curve-set atlas + 1, as the static row stores it; 0 while it has no polygons.
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
        /// Sort kind ribbon: the ribbons the emitter's particles form, and the longest age one of them reaches, in
        /// ticks. The key orders a ribbon after the one before it, and the particles of each by age.
        uint32_t ribbon_count = 1;
        uint32_t longest_age_ticks = 0;
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

    /// How one emitter's particles answer a contact with a scene surface (cs_particle_collide.eshader).
    struct collide_task
    {
        uint32_t pool_base = 0;
        uint32_t high_water = 0;
        collision_mode mode = collision_mode::kill;
        /// Bounces before a particle dies at its next contact; counted up to motion_offset_bounce_mask.
        uint32_t max_bounces = 0;
        /// A particle at rest lies flat on its surface.
        bool lies_flat = false;
        float restitution = 0.0f;
        float friction = 0.0f;
        float radius_scale = 1.0f;
        float rest_speed = 0.0f;
        float lifetime_loss = 0.0f;
        /// Radians.
        float bounce_scatter = 0.0f;
    };

    /// The scene a view's particles collide with.
    struct collide_scene
    {
        /// The view's opaque depth and its G-buffer's normals.
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle normals = BGFX_INVALID_HANDLE;
        /// The view's distance field with its level (one vec4 a level) and sampling uniforms, as their owner fills
        /// them: with all three the particles collide with the field instead of the depth.
        bgfx::TextureHandle distance_field = BGFX_INVALID_HANDLE;
        const float* distance_field_levels = nullptr;
        const float* distance_field_params = nullptr;
    };

    /// The scene collision kernel loaded, on the GPU cull and sort path: collide_view can run.
    auto can_collide() const -> bool;

    /**
     * @brief Tests every particle of @p tasks against @p scene and rewrites the records of the ones that touch it.
     * @p view carries the camera's matrices and view origin, and runs before the views that draw the particles.
     * @return The particle slots tested; 0 when the kernel did not run.
     */
    auto collide_view(bgfx::ViewId view, const std::vector<collide_task>& tasks, const collide_scene& scene) -> uint32_t;

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
        /// The table's atlas blocks: the first, and how many follow it in a row.
        uint32_t block = 0;
        uint32_t block_count = 0;
        /// Two polygon points per vec4, cutout_frame_vec4s vec4 per sheet frame, then zeros to the end of the table's
        /// last block; empty until the bake is in.
        std::vector<math::vec4> points;
        /// The atlas does not hold the points yet.
        bool needs_upload = false;
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
        /// Key / slot pairs as the GPU sort writes them, the keys left 0 (init_view_buffers).
        shader_buffer cpu_list{"s_particle_sorted", shader_buffer_element::uint1, shader_buffer_storage::buffer};
        uint32_t cpu_list_capacity = 0;
        uint32_t cpu_list_used = 0;
        std::vector<uint32_t> cpu_list_data;
    };

    struct staging_buffer
    {
        bgfx::DynamicIndexBufferHandle handle = BGFX_INVALID_HANDLE;
        uint32_t capacity = 0;
    };

    /// An empty record pool in @p storage.
    static auto make_record_pool(shader_buffer_storage storage) -> shader_buffer;
    /// Copies the records of the pool that grew into the pool that replaced it.
    void copy_grown_pool();
    auto allocate_lut_block() -> uint32_t;
    /// @p count atlas blocks in a row: free ones where a run of them exists, else new blocks at the atlas's end.
    auto allocate_lut_blocks(uint32_t count) -> uint32_t;
    void grow_pool(uint32_t required);
    void ensure_tables();
    void ensure_lut_atlas();
    void ensure_sort_buffers(uint32_t entries, uint32_t batches);
    void upload_tables();
    void upload_luts();
    /// Writes the cutout tables the atlas does not hold yet into their blocks.
    void upload_cutouts();
    /// Where the GPU path's sorted list holds its first batch count: after its entries.
    auto get_batch_counts_base() const -> uint32_t;
    /// Scatters the staged records into the pool; false when the scatter could not run (they stay staged).
    auto upload_staging(bgfx::ViewId view) -> bool;
    /// Writes the staged records into the pool texture, each run of consecutive slots in one update.
    void write_staged_records();
    void update_static_rows(uint32_t first, uint32_t count);
    void update_dynamic_rows(uint32_t count);
    /// Binds the particle data of a draw; false while some of it does not exist yet.
    auto bind_data(bool is_cpu_list) -> bool;
    auto uses_textures() const -> bool;
    /// Gives @p buffers a CPU list in the particle data's storage.
    void init_view_buffers(view_buffers& buffers) const;
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
    /// The collide kernel against a view's depth and normals, and against its distance field.
    std::shared_ptr<gpu_program> collide_depth_program_;
    std::shared_ptr<gpu_program> collide_field_program_;
    std::shared_ptr<gpu_program> pool_copy_program_;
    buffer_clear clear_;
    bool can_draw_ = false;
    bool can_sort_ = false;
    bool can_collide_ = false;
    bool can_collide_with_field_ = false;
    bool can_copy_pool_ = false;
    /// Where the particle data lives (get_particle_data_storage, at init).
    shader_buffer_storage storage_ = shader_buffer_storage::buffer;
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
    bgfx::UniformHandle u_pool_copy_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_collide_ = BGFX_INVALID_HANDLE;
    /// The scene a collide dispatch reads: the view's depth and normals, or the distance field with its uniforms.
    bgfx::UniformHandle u_scene_depth_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_scene_normals_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_distance_field_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_distance_field_levels_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_distance_field_params_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout quad_layout_;
    bgfx::VertexBufferHandle quad_vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle quad_indices_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle noise_texture_ = BGFX_INVALID_HANDLE;

    // Record pool: the scatter kernel writes it on the GPU path, and the collide kernel rewrites it.
    shader_buffer records_ = make_record_pool(shader_buffer_storage::buffer);
    /// The pool that grew, until flush copies its @ref grown_records_ records into records_.
    shader_buffer grown_from_ = make_record_pool(shader_buffer_storage::buffer);
    uint32_t grown_records_ = 0;
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
    shader_buffer static_table_{"s_particle_static", shader_buffer_element::float4, shader_buffer_storage::buffer};
    shader_buffer dynamic_table_{"s_particle_dynamic", shader_buffer_element::uint4, shader_buffer_storage::buffer};
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
    shader_buffer lut_atlas_{"s_particle_luts", shader_buffer_element::float4, shader_buffer_storage::buffer};
    std::unordered_map<uint64_t, cutout_entry> cutouts_;
    /// Some cutout table waits for its upload (cutout_entry::needs_upload).
    bool has_cutout_uploads_ = false;
    uint32_t lut_atlas_blocks_ = 0;

    // Per-view work.
    /// The view's sorted list: sort_capacity_ key / slot pairs, then batch_capacity_ live counts, one per batch of
    /// the last dispatch (cs_particle_draw_args.eshader), for the vertex shaders.
    bgfx::DynamicIndexBufferHandle sort_ = BGFX_INVALID_HANDLE;
    uint32_t sort_capacity_ = 0;
    bgfx::DynamicIndexBufferHandle counters_ = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle draw_args_ = BGFX_INVALID_HANDLE;
    uint32_t batch_capacity_ = 0;
    bool counters_need_clear_ = false;
    std::vector<view_buffers> view_buffers_;
    uint32_t views_this_frame_ = 0;
    uint32_t frame_ = 0xffffffffu;
    std::vector<uint32_t> task_data_;
    std::vector<uint32_t> batch_data_;
    /// The task tables of the frame's collide dispatches: one each, as a frame's buffer updates land before any of
    /// its views runs.
    std::vector<staging_buffer> collide_buffers_;
    uint32_t collides_this_frame_ = 0;
    std::vector<uint32_t> collide_data_;
};

} // namespace ps_soa
} // namespace unravel
