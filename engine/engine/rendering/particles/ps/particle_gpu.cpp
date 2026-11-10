#include "particle_gpu.h"
#include "particle_noise.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gpu_resource_utils.h>
#include <engine/rendering/render_feature_support.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace unravel
{
namespace ps_soa
{
namespace
{

constexpr uint32_t k_min_pool_capacity = 4096;
/// Most groups one dimension of a dispatch holds (the D3D11 and GL minimum).
constexpr uint32_t k_max_dispatch_groups = 65535;
constexpr uint32_t k_min_table_rows = 256;
constexpr uint32_t k_min_lut_blocks = 16;
/// The cutout buffer's first size, vec4 (always created: the vertex shaders bind it).
constexpr uint32_t k_min_cutout_vec4s = 256;
constexpr uint32_t k_min_staging_records = 256;
constexpr uint32_t k_min_tasks = 64;
constexpr uint32_t k_min_list_entries = 1024;
constexpr uint32_t k_min_batches = 64;
/// uints per staged record: the record and its destination slot.
constexpr uint32_t k_staging_uints_per_record = record_uints + 1;
/// uints per sorted-list entry: the key and the pool slot.
constexpr uint32_t k_list_uints_per_entry = 2;
constexpr uint32_t k_key_bits = 32;
/// uints per element of the uint4 shader buffers: records and dynamic rows fill whole elements.
constexpr uint32_t k_uints_per_uint4 = 4;
constexpr uint32_t k_record_uint4s = record_uints / k_uints_per_uint4;
constexpr uint32_t k_dynamic_row_uint4s = dynamic_row_uints / k_uints_per_uint4;
static_assert(record_uints % k_uints_per_uint4 == 0 && dynamic_row_uints % k_uints_per_uint4 == 0,
              "records and dynamic rows fill whole elements of their shader buffers");

struct quad_vertex
{
    float x, y, z, u, v;
};

/// Corner cut of the octagon along each side of the unit quad: 1 - 1 / sqrt(2), the regular octagon
/// whose inradius is the quad's, so it holds the quad's inscribed circle.
constexpr float k_octagon_cut = 0.29289322f;
constexpr float k_octagon_low = -0.5f + k_octagon_cut;
constexpr float k_octagon_high = 0.5f - k_octagon_cut;

/// The quad (vertices 0-3), the octagon (vertices 4-11), then the cutout (vertices 12-19: z = its vertex + 1, on
/// the quad's outline until the shader moves it onto the frame's cutout); uv = (x + 0.5, 0.5 - y). The trail strip
/// follows (make_particle_vertices).
const quad_vertex k_shape_vertices[20] = {
    {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
    {0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
    {0.5f, 0.5f, 0.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, 0.0f, 0.0f, 0.0f},
    {k_octagon_low, -0.5f, 0.0f, k_octagon_low + 0.5f, 1.0f},
    {k_octagon_high, -0.5f, 0.0f, k_octagon_high + 0.5f, 1.0f},
    {0.5f, k_octagon_low, 0.0f, 1.0f, 0.5f - k_octagon_low},
    {0.5f, k_octagon_high, 0.0f, 1.0f, 0.5f - k_octagon_high},
    {k_octagon_high, 0.5f, 0.0f, k_octagon_high + 0.5f, 0.0f},
    {k_octagon_low, 0.5f, 0.0f, k_octagon_low + 0.5f, 0.0f},
    {-0.5f, k_octagon_high, 0.0f, 0.0f, 0.5f - k_octagon_high},
    {-0.5f, k_octagon_low, 0.0f, 0.0f, 0.5f - k_octagon_low},
    {-0.5f, -0.5f, 1.0f, 0.0f, 1.0f},
    {0.0f, -0.5f, 2.0f, 0.5f, 1.0f},
    {0.5f, -0.5f, 3.0f, 1.0f, 1.0f},
    {0.5f, 0.0f, 4.0f, 1.0f, 0.5f},
    {0.5f, 0.5f, 5.0f, 1.0f, 0.0f},
    {0.0f, 0.5f, 6.0f, 0.5f, 0.0f},
    {-0.5f, 0.5f, 7.0f, 0.0f, 0.0f},
    {-0.5f, 0.0f, 8.0f, 0.0f, 0.5f},
};

/// The quad's two triangles, then the octagon and the cutout as fans around their first vertex (same winding).
const uint16_t k_shape_indices[quad_index_count + octagon_index_count + cutout_index_count] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 4, 6, 7, 4, 7, 8, 4, 8, 9, 4, 9, 10, 4, 10, 11,
    12, 13, 14, 12, 14, 15, 12, 15, 16, 12, 16, 17, 12, 17, 18, 12, 18, 19,
};

/// The shapes, then the trail strip: trail_segments + 1 pairs of vertices across it (x = -0.5, 0.5), y = 0 at
/// the particle to 1 at the tail.
auto make_particle_vertices() -> std::vector<quad_vertex>
{
    std::vector<quad_vertex> vertices(std::begin(k_shape_vertices), std::end(k_shape_vertices));
    for(uint32_t j = 0; j <= trail_segments; ++j)
    {
        const float along = float(j) / float(trail_segments);
        vertices.push_back({-0.5f, along, 0.0f, 0.0f, along});
        vertices.push_back({0.5f, along, 0.0f, 1.0f, along});
    }
    return vertices;
}

auto make_particle_indices() -> std::vector<uint16_t>
{
    std::vector<uint16_t> indices(std::begin(k_shape_indices), std::end(k_shape_indices));
    const uint16_t base = uint16_t(std::size(k_shape_vertices));
    for(uint16_t j = 0; j < trail_segments; ++j)
    {
        const uint16_t near_left = uint16_t(base + j * 2);
        const uint16_t near_right = uint16_t(near_left + 1);
        const uint16_t far_left = uint16_t(near_left + 2);
        const uint16_t far_right = uint16_t(near_left + 3);
        indices.insert(indices.end(), {near_left, near_right, far_right, far_right, far_left, near_left});
    }
    return indices;
}

auto next_pow2(uint32_t value) -> uint32_t
{
    uint32_t result = 1;
    while(result < value)
    {
        result <<= 1u;
    }
    return result;
}

auto ceil_log2(uint32_t value) -> uint32_t
{
    uint32_t bits = 0;
    while((1u << bits) < value)
    {
        ++bits;
    }
    return bits;
}

/// The program of a compute shader asset. Kept even while the shader does not compile: the program
/// rebuilds itself when the asset is recompiled (gpu_program::begin), so a fixed shader comes back
/// without a restart.
auto load_compute(asset_manager& assets, const char* key, const char* label) -> std::shared_ptr<gpu_program>
{
    auto shader = assets.get_asset<gfx::shader>(key);
    if(!shader)
    {
        APPLOG_WARNING("Particles: {} missing", label);
        return {};
    }
    auto program = std::make_shared<gpu_program>(shader);
    // On a device without the particle storage buffers the program is never built; the renderer's feature log says
    // so once.
    if(!program->is_valid() && get_render_feature_support().is_available(render_feature::particle_storage_buffers))
    {
        APPLOG_WARNING("Particles: {} does not compile yet", label);
    }
    return program;
}

/// Whether @p program is usable now, rebuilding it first if its shader was recompiled.
auto refresh(const std::shared_ptr<gpu_program>& program) -> bool
{
    if(!program)
    {
        return false;
    }
    const bool is_valid = program->begin();
    program->end();
    return is_valid;
}

using gpu_resource::destroy_handle;
using gpu_resource::grow_capacity;

/// Dispatches @p threads threads of @p program in groups of gpu_group_threads. Groups past one dimension's limit
/// fold into rows: the shader's thread index is gl_GlobalInvocationID.y x (k_max_dispatch_groups x
/// gpu_group_threads) + gl_GlobalInvocationID.x, and every shader bounds-checks its index.
void dispatch_threads(bgfx::ViewId view, bgfx::ProgramHandle program, uint32_t threads)
{
    const uint32_t groups = std::max((threads + gpu_group_threads - 1) / gpu_group_threads, 1u);
    const uint32_t rows = (groups + k_max_dispatch_groups - 1) / k_max_dispatch_groups;
    gfx::dispatch(view, program, rows > 1 ? k_max_dispatch_groups : groups, rows, 1);
}

} // namespace

auto get_particle_data_storage() -> shader_buffer_storage
{
    return get_render_feature_support().is_available(render_feature::particle_storage_buffers)
               ? shader_buffer_storage::buffer
               : shader_buffer_storage::texture;
}

void particle_gpu::init(rtti::context& ctx)
{
    quad_layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    const std::vector<quad_vertex> vertices = make_particle_vertices();
    const std::vector<uint16_t> indices = make_particle_indices();
    quad_vertices_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), uint32_t(vertices.size() * sizeof(quad_vertex))),
                                              quad_layout_);
    quad_indices_ = bgfx::createIndexBuffer(bgfx::copy(indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
    create_noise_texture();
    // Uniforms before programs: the GL renderer wires program uniforms from the registry at link time.
    u_scatter_ = bgfx::createUniform("u_particle_scatter", bgfx::UniformType::Vec4);
    u_view0_ = bgfx::createUniform("u_particle_view0", bgfx::UniformType::Vec4);
    u_view1_ = bgfx::createUniform("u_particle_view1", bgfx::UniformType::Vec4);
    u_view2_ = bgfx::createUniform("u_particle_view2", bgfx::UniformType::Vec4);
    u_frustum_ = bgfx::createUniform("u_particle_frustum", bgfx::UniformType::Vec4, 6);
    u_sort_ = bgfx::createUniform("u_particle_sort", bgfx::UniformType::Vec4);
    u_args_ = bgfx::createUniform("u_particle_args", bgfx::UniformType::Vec4);
    u_draw_ = bgfx::createUniform("u_particle_draw", bgfx::UniformType::Vec4);
    u_noise_ = bgfx::createUniform("s_particle_noise", bgfx::UniformType::Sampler);
    // The samplers of texture storage come before the programs too; the first view's buffers stand for every view's.
    storage_ = get_particle_data_storage();
    for(shader_buffer* data : {&records_, &static_table_, &dynamic_table_, &lut_atlas_, &cutout_buffer_})
    {
        data->set_storage(storage_);
        data->init();
    }
    init_view_buffers(view_buffers_.emplace_back());
    auto& assets = ctx.get_cached<asset_manager>();
    scatter_program_ = load_compute(assets, "engine:/data/shaders/particles/cs_particle_scatter.eshader", "record scatter");
    keygen_program_ = load_compute(assets, "engine:/data/shaders/particles/cs_particle_keygen.eshader", "cull / sort keys");
    sort_program_ = load_compute(assets, "engine:/data/shaders/particles/cs_particle_sort.eshader", "sort");
    args_program_ = load_compute(assets, "engine:/data/shaders/particles/cs_particle_draw_args.eshader", "draw arguments");
    clear_.init(ctx);
    refresh_programs();
    if(!can_draw())
    {
        APPLOG_ERROR("Particles: the record scatter cannot run. Particles do not draw.");
    }
    else if(!can_sort())
    {
        APPLOG_WARNING("Particles: GPU culling and sorting cannot run. Particles cull and sort on the CPU.");
    }
}

auto particle_gpu::uses_textures() const -> bool
{
    return storage_ == shader_buffer_storage::texture;
}

void particle_gpu::init_view_buffers(view_buffers& buffers) const
{
    buffers.cpu_list.set_storage(storage_);
    buffers.cpu_list.init();
}

void particle_gpu::create_noise_texture()
{
    const auto& voxels = get_particle_noise().get_voxels();
    const bgfx::Memory* memory = bgfx::alloc(uint32_t(voxels.size() * 4 * sizeof(uint16_t)));
    auto* texels = reinterpret_cast<uint16_t*>(memory->data);
    for(size_t i = 0; i < voxels.size(); ++i)
    {
        texels[i * 4] = bx::halfFromFloat(voxels[i].x);
        texels[i * 4 + 1] = bx::halfFromFloat(voxels[i].y);
        texels[i * 4 + 2] = bx::halfFromFloat(voxels[i].z);
        texels[i * 4 + 3] = 0;
    }
    // Filtered and wrapping: the noise tiles.
    noise_texture_ = bgfx::createTexture3D(uint16_t(particle_noise_size),
                                           uint16_t(particle_noise_size),
                                           uint16_t(particle_noise_size),
                                           false,
                                           bgfx::TextureFormat::RGBA16F,
                                           BGFX_TEXTURE_NONE,
                                           memory);
}

auto particle_gpu::get_noise_texture() const -> bgfx::TextureHandle
{
    return noise_texture_;
}

void particle_gpu::refresh_programs()
{
    const auto& support = get_render_feature_support();
    // Texture storage needs no kernel to draw: the CPU writes the records.
    can_draw_ = uses_textures() || refresh(scatter_program_);
    // The GPU path draws with indirect arguments; without indirect draws the CPU culls, sorts and counts.
    can_sort_ = can_draw_ && support.is_available(render_feature::gpu_particles_indirect) && refresh(keygen_program_) &&
                refresh(sort_program_) && refresh(args_program_);
}

void particle_gpu::shutdown()
{
    destroy_buffers();
    // The module lives in a static: everything it holds on the renderer goes before the renderer does.
    clear_.release();
    scatter_program_.reset();
    keygen_program_.reset();
    sort_program_.reset();
    args_program_.reset();
    destroy_handle(u_scatter_);
    destroy_handle(u_view0_);
    destroy_handle(u_view1_);
    destroy_handle(u_view2_);
    destroy_handle(u_frustum_);
    destroy_handle(u_sort_);
    destroy_handle(u_args_);
    destroy_handle(u_draw_);
    destroy_handle(u_noise_);
    destroy_handle(quad_vertices_);
    destroy_handle(quad_indices_);
    destroy_handle(noise_texture_);
    luts_.clear();
    lut_block_keys_.clear();
    free_lut_blocks_.clear();
    dirty_lut_blocks_.clear();
    lut_block_count_ = 0;
}

void particle_gpu::destroy_buffers()
{
    records_.destroy();
    static_table_.destroy();
    dynamic_table_.destroy();
    lut_atlas_.destroy();
    cutout_buffer_.destroy();
    cutout_buffer_capacity_ = 0;
    cutouts_dirty_ = true;
    destroy_handle(sort_);
    destroy_handle(counters_);
    destroy_handle(draw_args_);
    destroy_handle(batch_counts_);
    table_rows_ = 0;
    for(auto& buffer : staging_buffers_)
    {
        destroy_handle(buffer.handle);
    }
    staging_buffers_.clear();
    for(auto& buffers : view_buffers_)
    {
        destroy_handle(buffers.tasks);
        destroy_handle(buffers.batches);
        buffers.cpu_list.destroy();
    }
    view_buffers_.clear();
    pool_capacity_ = 0;
    free_ranges_.clear();
    staged_records_.clear();
    staged_slots_.clear();
    has_kept_staging_ = false;
    pool_reset_ = false;
    lut_atlas_blocks_ = 0;
    sort_capacity_ = 0;
    batch_capacity_ = 0;
    static_rows_.clear();
    dynamic_rows_.clear();
}

auto particle_gpu::can_draw() const -> bool
{
    return can_draw_;
}

auto particle_gpu::can_sort() const -> bool
{
    return can_sort_;
}

auto particle_gpu::reserve_range(uint32_t capacity) -> uint32_t
{
    if(capacity == 0)
    {
        return invalid_base;
    }
    for(auto it = free_ranges_.begin(); it != free_ranges_.end(); ++it)
    {
        if(it->size >= capacity)
        {
            const uint32_t base = it->base;
            it->base += capacity;
            it->size -= capacity;
            if(it->size == 0)
            {
                free_ranges_.erase(it);
            }
            return base;
        }
    }
    const uint32_t old_capacity = pool_capacity_;
    grow_pool(old_capacity + capacity);
    // The grown tail is one free range after the old end (merged with a free range ending there).
    return reserve_range(capacity);
}

void particle_gpu::release_range(uint32_t base, uint32_t capacity)
{
    if(base == invalid_base || capacity == 0)
    {
        return;
    }
    range released{base, capacity};
    auto it = std::lower_bound(free_ranges_.begin(),
                               free_ranges_.end(),
                               released,
                               [](const range& a, const range& b)
                               {
                                   return a.base < b.base;
                               });
    it = free_ranges_.insert(it, released);
    // Merge with the next, then the previous neighbour.
    auto next = it + 1;
    if(next != free_ranges_.end() && it->base + it->size == next->base)
    {
        it->size += next->size;
        free_ranges_.erase(next);
    }
    if(it != free_ranges_.begin())
    {
        auto previous = it - 1;
        if(previous->base + previous->size == it->base)
        {
            previous->size += it->size;
            free_ranges_.erase(it);
        }
    }
}

void particle_gpu::grow_pool(uint32_t required)
{
    const uint32_t old_capacity = pool_capacity_;
    const uint32_t new_capacity = grow_capacity(old_capacity, required, k_min_pool_capacity);
    records_.reserve(new_capacity * k_record_uint4s);
    pool_capacity_ = new_capacity;
    release_range(old_capacity, new_capacity - old_capacity);
    // The new buffer starts empty: everything staged against the old one is restaged by its emitter.
    staged_records_.clear();
    staged_slots_.clear();
    has_kept_staging_ = false;
    pool_reset_ = old_capacity > 0;
}

auto particle_gpu::take_pool_reset() -> bool
{
    const bool was_reset = pool_reset_;
    pool_reset_ = false;
    return was_reset;
}

auto particle_gpu::allocate_lut_block() -> uint32_t
{
    if(!free_lut_blocks_.empty())
    {
        const uint32_t block = free_lut_blocks_.back();
        free_lut_blocks_.pop_back();
        return block;
    }
    return lut_block_count_++;
}

void particle_gpu::release_lut(uint64_t key)
{
    std::lock_guard<std::mutex> lock(lut_mutex_);
    auto found = luts_.find(key);
    if(found == luts_.end())
    {
        return;
    }
    if(--found->second.references > 0)
    {
        return;
    }
    free_lut_blocks_.push_back(found->second.block);
    lut_block_keys_.erase(found->second.block);
    luts_.erase(found);
}

auto particle_gpu::get_lut(uint64_t key) const -> const lut_set*
{
    std::lock_guard<std::mutex> lock(lut_mutex_);
    auto found = luts_.find(key);
    return found != luts_.end() ? found->second.data.get() : nullptr;
}

void particle_gpu::acquire_cutout(uint64_t key)
{
    cutouts_[key].references++;
}

void particle_gpu::release_cutout(uint64_t key)
{
    auto found = cutouts_.find(key);
    if(found == cutouts_.end() || --found->second.references > 0)
    {
        return;
    }
    if(found->second.size > 0)
    {
        free_cutout_ranges_.push_back({found->second.base, found->second.size});
    }
    cutouts_.erase(found);
}

auto particle_gpu::is_cutout_pending(uint64_t key) const -> bool
{
    auto found = cutouts_.find(key);
    return found != cutouts_.end() && !found->second.has_polygons;
}

void particle_gpu::set_cutout_polygons(uint64_t key, const std::vector<cutout_polygon>& polygons)
{
    auto found = cutouts_.find(key);
    if(found == cutouts_.end() || found->second.has_polygons || polygons.empty())
    {
        return;
    }
    const uint32_t size = uint32_t(polygons.size()) * cutout_frame_vec4s;
    const uint32_t base = allocate_cutout_range(size);
    for(size_t frame = 0; frame < polygons.size(); ++frame)
    {
        const cutout_polygon& polygon = polygons[frame];
        for(uint32_t pair = 0; pair < cutout_frame_vec4s; ++pair)
        {
            cutout_data_[base + frame * cutout_frame_vec4s + pair] =
                math::vec4(polygon[pair * 2], polygon[pair * 2 + 1]);
        }
    }
    found->second.base = base;
    found->second.size = size;
    found->second.has_polygons = true;
    cutouts_dirty_ = true;
}

auto particle_gpu::get_cutout_table(uint64_t key) const -> float
{
    auto found = cutouts_.find(key);
    return found != cutouts_.end() && found->second.has_polygons ? float(found->second.base + 1) : 0.0f;
}

auto particle_gpu::allocate_cutout_range(uint32_t size) -> uint32_t
{
    for(auto free = free_cutout_ranges_.begin(); free != free_cutout_ranges_.end(); ++free)
    {
        if(free->size < size)
        {
            continue;
        }
        const uint32_t base = free->base;
        free->base += size;
        free->size -= size;
        if(free->size == 0)
        {
            free_cutout_ranges_.erase(free);
        }
        return base;
    }
    const uint32_t base = uint32_t(cutout_data_.size());
    cutout_data_.resize(cutout_data_.size() + size, math::vec4(0.0f));
    return base;
}

void particle_gpu::ensure_cutout_buffer()
{
    const uint32_t required = math::max(uint32_t(cutout_data_.size()), 1u);
    if(cutout_buffer_.is_valid() && cutout_buffer_capacity_ >= required)
    {
        return;
    }
    cutout_buffer_capacity_ = grow_capacity(cutout_buffer_capacity_, required, k_min_cutout_vec4s);
    cutout_buffer_.reserve(cutout_buffer_capacity_);
    cutouts_dirty_ = true;
}

void particle_gpu::upload_cutouts()
{
    ensure_cutout_buffer();
    if(!cutouts_dirty_ || cutout_data_.empty())
    {
        return;
    }
    cutout_buffer_.update(0, uint32_t(cutout_data_.size()), cutout_data_.data());
    cutouts_dirty_ = false;
}

void particle_gpu::set_static_row(uint16_t emitter, const static_row& row)
{
    if(emitter >= static_rows_.size())
    {
        const uint32_t rows = std::max<uint32_t>(next_pow2(uint32_t(emitter) + 1), k_min_table_rows);
        static_rows_.resize(rows);
        static_dirty_.resize(rows, 1);
        dynamic_rows_.resize(rows);
        tables_need_full_upload_ = true;
    }
    static_rows_[emitter] = row;
    static_dirty_[emitter] = 1;
}

void particle_gpu::set_dynamic_row(uint16_t emitter, const dynamic_row& row)
{
    if(emitter >= dynamic_rows_.size())
    {
        static_row empty{};
        set_static_row(emitter, empty);
    }
    dynamic_rows_[emitter] = row;
}

void particle_gpu::stage(uint32_t pool_slot, const particle_record& record)
{
    staged_records_.push_back(record);
    staged_slots_.push_back(pool_slot);
}

void particle_gpu::ensure_tables()
{
    const uint32_t rows = uint32_t(static_rows_.size());
    if(rows == 0)
    {
        return;
    }
    const uint32_t table_rows = static_table_.is_valid() ? table_rows_ : 0;
    if(table_rows >= rows)
    {
        return;
    }
    static_table_.reserve(rows * static_row_vec4s);
    dynamic_table_.reserve(rows * k_dynamic_row_uint4s);
    table_rows_ = rows;
    tables_need_full_upload_ = true;
}

void particle_gpu::update_static_rows(uint32_t first, uint32_t count)
{
    static_table_.update(first * static_row_vec4s, count * static_row_vec4s, &static_rows_[first]);
}

void particle_gpu::update_dynamic_rows(uint32_t count)
{
    dynamic_table_.update(0, count * k_dynamic_row_uint4s, dynamic_rows_.data());
}

void particle_gpu::upload_tables()
{
    const uint32_t rows = std::min<uint32_t>(uint32_t(static_rows_.size()), table_rows_);
    if(rows == 0)
    {
        return;
    }
    uint32_t run_start = rows;
    for(uint32_t row = 0; row <= rows; ++row)
    {
        const bool is_dirty = row < rows && (tables_need_full_upload_ || static_dirty_[row] != 0);
        if(is_dirty && run_start == rows)
        {
            run_start = row;
        }
        if(!is_dirty && run_start != rows)
        {
            update_static_rows(run_start, row - run_start);
            run_start = rows;
        }
        if(row < rows)
        {
            static_dirty_[row] = 0;
        }
    }
    tables_need_full_upload_ = false;
    update_dynamic_rows(rows);
}

void particle_gpu::ensure_lut_atlas()
{
    if(lut_atlas_blocks_ >= lut_block_count_ && lut_atlas_.is_valid())
    {
        return;
    }
    const uint32_t blocks = grow_capacity(lut_atlas_blocks_, lut_block_count_, k_min_lut_blocks);
    lut_atlas_.reserve(blocks * lut_set_vec4s);
    lut_atlas_blocks_ = blocks;
    // A new atlas holds nothing yet: upload every live block.
    dirty_lut_blocks_.clear();
    for(const auto& block_key : lut_block_keys_)
    {
        dirty_lut_blocks_.push_back(block_key.first);
    }
}

void particle_gpu::upload_luts()
{
    std::lock_guard<std::mutex> lock(lut_mutex_);
    if(lut_block_count_ == 0)
    {
        return;
    }
    ensure_lut_atlas();
    for(uint32_t block : dirty_lut_blocks_)
    {
        auto key = lut_block_keys_.find(block);
        if(key == lut_block_keys_.end())
        {
            continue;
        }
        const lut_set& data = *luts_.at(key->second).data;
        lut_atlas_.update(block * lut_set_vec4s, lut_set_vec4s, data.data.data());
    }
    dirty_lut_blocks_.clear();
}

auto particle_gpu::upload_staging(bgfx::ViewId view) -> bool
{
    const uint32_t count = uint32_t(staged_slots_.size());
    if(staging_used_this_frame_ >= staging_buffers_.size())
    {
        staging_buffers_.emplace_back();
    }
    staging_buffer& buffer = staging_buffers_[staging_used_this_frame_++];
    if(buffer.capacity < count)
    {
        destroy_handle(buffer.handle);
        buffer.capacity = grow_capacity(buffer.capacity, count, k_min_staging_records);
        buffer.handle = bgfx::createDynamicIndexBuffer(buffer.capacity * k_staging_uints_per_record,
                                                       BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    }
    const uint32_t record_bytes = count * uint32_t(sizeof(particle_record));
    const uint32_t slot_bytes = count * uint32_t(sizeof(uint32_t));
    const bgfx::Memory* memory = bgfx::alloc(record_bytes + slot_bytes);
    std::memcpy(memory->data, staged_records_.data(), record_bytes);
    std::memcpy(memory->data + record_bytes, staged_slots_.data(), slot_bytes);
    bgfx::update(buffer.handle, 0, memory);
    if(!scatter_program_->begin())
    {
        return false;
    }
    const float scatter[4] = {float(count), 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_scatter_, scatter);
    bgfx::setBuffer(0, buffer.handle, bgfx::Access::Read);
    records_.bind_compute(1, bgfx::Access::Write);
    dispatch_threads(view, scatter_program_->native_handle(), count);
    scatter_program_->end();
    return true;
}

void particle_gpu::write_staged_records()
{
    if(staged_slots_.empty() || !records_.is_valid())
    {
        return;
    }
    keep_latest_staged();
    std::vector<uint32_t> order(staged_slots_.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(),
              order.end(),
              [&](uint32_t a, uint32_t b)
              {
                  return staged_slots_[a] < staged_slots_[b];
              });
    std::vector<particle_record> run;
    run.reserve(order.size());
    for(size_t i = 0; i < order.size(); ++i)
    {
        run.push_back(staged_records_[order[i]]);
        const uint32_t slot = staged_slots_[order[i]];
        const bool is_run_end = i + 1 == order.size() || staged_slots_[order[i + 1]] != slot + 1;
        if(is_run_end)
        {
            const uint32_t first_slot = slot + 1 - uint32_t(run.size());
            records_.update(first_slot * k_record_uint4s, uint32_t(run.size()) * k_record_uint4s, run.data());
            run.clear();
        }
    }
}

void particle_gpu::keep_latest_staged()
{
    std::unordered_map<uint32_t, size_t> latest;
    latest.reserve(staged_slots_.size());
    for(size_t i = 0; i < staged_slots_.size(); ++i)
    {
        latest[staged_slots_[i]] = i;
    }
    if(latest.size() == staged_slots_.size())
    {
        return;
    }
    std::vector<particle_record> records;
    std::vector<uint32_t> slots;
    records.reserve(latest.size());
    slots.reserve(latest.size());
    for(size_t i = 0; i < staged_slots_.size(); ++i)
    {
        if(latest[staged_slots_[i]] == i)
        {
            records.push_back(staged_records_[i]);
            slots.push_back(staged_slots_[i]);
        }
    }
    staged_records_.swap(records);
    staged_slots_.swap(slots);
}

void particle_gpu::flush()
{
    begin_frame();
    if(!can_draw())
    {
        // Kept until the scatter can run (its shader may still be loading): those particles draw from then on.
        keep_latest_staged();
        has_kept_staging_ = !staged_slots_.empty();
        return;
    }
    APP_SCOPE_PERF("Particles/GPU Upload");
    ensure_tables();
    upload_tables();
    upload_luts();
    upload_cutouts();
    if(has_kept_staging_)
    {
        keep_latest_staged();
        has_kept_staging_ = false;
    }
    if(uses_textures())
    {
        write_staged_records();
    }
    else if(!staged_slots_.empty() && records_.is_valid())
    {
        gfx::render_pass pass("Particles/Upload");
        pass.touch();
        if(!upload_staging(pass.id))
        {
            keep_latest_staged();
            has_kept_staging_ = true;
            return;
        }
    }
    staged_records_.clear();
    staged_slots_.clear();
}

void particle_gpu::begin_frame()
{
    const uint32_t frame = uint32_t(gfx::get_render_frame());
    if(frame != frame_)
    {
        frame_ = frame;
        views_this_frame_ = 0;
        staging_used_this_frame_ = 0;
        refresh_programs();
    }
}

void particle_gpu::begin_view()
{
    begin_frame();
    if(views_this_frame_ >= view_buffers_.size())
    {
        init_view_buffers(view_buffers_.emplace_back());
    }
    view_buffers& buffers = view_buffers_[views_this_frame_++];
    buffers.cpu_list_used = 0;
    buffers.cpu_list_data.clear();
}

auto particle_gpu::current_view_buffers() -> view_buffers&
{
    return view_buffers_[views_this_frame_ - 1];
}

void particle_gpu::ensure_sort_buffers(uint32_t entries, uint32_t batches)
{
    if(sort_capacity_ < entries || !bgfx::isValid(sort_))
    {
        destroy_handle(sort_);
        sort_capacity_ = grow_capacity(sort_capacity_, entries, k_min_list_entries);
        sort_ = bgfx::createDynamicIndexBuffer(sort_capacity_ * k_list_uints_per_entry,
                                               BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
    }
    if(batch_capacity_ < batches || !bgfx::isValid(counters_))
    {
        destroy_handle(counters_);
        destroy_handle(draw_args_);
        destroy_handle(batch_counts_);
        batch_capacity_ = grow_capacity(batch_capacity_, batches, k_min_batches);
        counters_ = bgfx::createDynamicIndexBuffer(batch_capacity_, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
        batch_counts_ = bgfx::createDynamicIndexBuffer(batch_capacity_, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
        draw_args_ = bgfx::createIndirectBuffer(batch_capacity_);
        counters_need_clear_ = true;
    }
}

auto particle_gpu::dispatch_view(bgfx::ViewId view,
                                 const std::vector<view_task>& tasks,
                                 std::vector<view_batch>& batches,
                                 const math::vec3& eye,
                                 const math::vec3& forward,
                                 float camera_speed,
                                 const std::array<math::vec4, 6>& frustum_planes) -> view_dispatch_stats
{
    view_dispatch_stats stats;
    const uint32_t batch_count = uint32_t(batches.size());
    if(tasks.empty() || batch_count == 0 || !can_sort() || !records_.is_valid())
    {
        return stats;
    }
    APP_SCOPE_PERF("Particles/GPU View Dispatch");
    // Each batch's capacity is the slots of its tasks; sorted batches take the front of the list, in
    // order, then the power-of-two padding, then the unsorted batches.
    for(auto& batch : batches)
    {
        batch.capacity = 0;
    }
    for(const auto& task : tasks)
    {
        batches[task.batch].capacity += task.high_water;
    }
    uint32_t sorted_total = 0;
    uint32_t sorted_batches = 0;
    for(auto& batch : batches)
    {
        if(batch.is_sorted)
        {
            batch.first = sorted_total;
            sorted_total += batch.capacity;
            ++sorted_batches;
        }
    }
    const uint32_t sort_count = sorted_total > 0 ? std::max(next_pow2(sorted_total), gpu_sort_block) : 0;
    uint32_t list_end = sort_count;
    for(auto& batch : batches)
    {
        if(!batch.is_sorted)
        {
            batch.first = list_end;
            list_end += batch.capacity;
        }
    }
    const uint32_t batch_bits = sorted_batches > 1 ? ceil_log2(sorted_batches) : 0;
    const uint32_t distance_bits = k_key_bits - batch_bits;
    std::vector<uint32_t> sorted_ordinal(batch_count, 0);
    uint32_t ordinal = 0;
    for(uint32_t b = 0; b < batch_count; ++b)
    {
        if(batches[b].is_sorted)
        {
            sorted_ordinal[b] = ordinal++;
        }
    }
    // Task table: pool base, high water, thread offset, output base, batch, sort kind, batch key.
    task_data_.assign(tasks.size() * gpu_task_uints, 0u);
    std::vector<uint32_t> batch_fill(batch_count, 0);
    uint32_t thread_offset = 0;
    for(uint32_t t = 0; t < tasks.size(); ++t)
    {
        const view_task& task = tasks[t];
        const view_batch& batch = batches[task.batch];
        uint32_t* entry = &task_data_[t * gpu_task_uints];
        entry[0] = task.pool_base;
        entry[1] = task.high_water;
        entry[2] = thread_offset;
        entry[3] = batch.first + (batch.is_sorted ? batch_fill[task.batch] : 0u);
        entry[4] = task.batch;
        entry[5] = uint32_t(batch.is_sorted ? task.sort_kind : gpu_sort_kind::append);
        entry[6] = batch.is_sorted && batch_bits > 0 ? sorted_ordinal[task.batch] << distance_bits : 0u;
        batch_fill[task.batch] += task.high_water;
        thread_offset += task.high_water;
    }
    view_buffers& buffers = current_view_buffers();
    if(buffers.task_capacity < tasks.size() || !bgfx::isValid(buffers.tasks))
    {
        destroy_handle(buffers.tasks);
        buffers.task_capacity = grow_capacity(buffers.task_capacity, uint32_t(tasks.size()), k_min_tasks);
        buffers.tasks =
            bgfx::createDynamicIndexBuffer(buffers.task_capacity * gpu_task_uints, BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    }
    bgfx::update(buffers.tasks, 0, bgfx::copy(task_data_.data(), uint32_t(task_data_.size() * sizeof(uint32_t))));
    // Batch table: each batch's geometry (index count, first index) for its indirect draw.
    batch_data_.assign(size_t(batch_count) * gpu_batch_uints, 0u);
    for(uint32_t b = 0; b < batch_count; ++b)
    {
        batch_data_[b * gpu_batch_uints] = batches[b].index_count;
        batch_data_[b * gpu_batch_uints + 1] = batches[b].first_index;
    }
    if(buffers.batch_capacity < batch_count || !bgfx::isValid(buffers.batches))
    {
        destroy_handle(buffers.batches);
        buffers.batch_capacity = grow_capacity(buffers.batch_capacity, batch_count, k_min_batches);
        buffers.batches =
            bgfx::createDynamicIndexBuffer(buffers.batch_capacity * gpu_batch_uints, BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    }
    bgfx::update(buffers.batches, 0, bgfx::copy(batch_data_.data(), uint32_t(batch_data_.size() * sizeof(uint32_t))));
    ensure_sort_buffers(std::max(list_end, 1u), batch_count);
    if(counters_need_clear_)
    {
        clear_.dispatch(view, counters_, 0, batch_capacity_);
        counters_need_clear_ = false;
        ++stats.dispatches;
    }
    const uint32_t padding = sort_count - sorted_total;
    const uint32_t threads = thread_offset + padding;
    if(keygen_program_->begin())
    {
        const float view0[4] = {eye.x, eye.y, eye.z, float(tasks.size())};
        const float view1[4] = {forward.x, forward.y, forward.z, float(thread_offset)};
        const float view2[4] = {float(sorted_total), float(padding), float(distance_bits), camera_speed};
        bgfx::setUniform(u_view0_, view0);
        bgfx::setUniform(u_view1_, view1);
        bgfx::setUniform(u_view2_, view2);
        bgfx::setUniform(u_frustum_, frustum_planes.data(), 6);
        records_.bind_compute(0, bgfx::Access::Read);
        static_table_.bind_compute(1, bgfx::Access::Read);
        dynamic_table_.bind_compute(2, bgfx::Access::Read);
        lut_atlas_.bind_compute(3, bgfx::Access::Read);
        bgfx::setBuffer(4, buffers.tasks, bgfx::Access::Read);
        bgfx::setBuffer(5, sort_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(6, counters_, bgfx::Access::ReadWrite);
        // The cull places particles with their turbulence, as they draw.
        bgfx::setTexture(gpu_noise_stage, u_noise_, noise_texture_);
        dispatch_threads(view, keygen_program_->native_handle(), threads);
        keygen_program_->end();
        ++stats.dispatches;
    }
    if(sort_count > 1 && sort_program_->begin())
    {
        const uint32_t block_groups = sort_count / gpu_sort_block;
        const auto dispatch_sort = [&](float mode, uint32_t stage, uint32_t distance)
        {
            const float sort[4] = {mode, float(stage), float(distance), 0.0f};
            bgfx::setUniform(u_sort_, sort);
            bgfx::setBuffer(0, sort_, bgfx::Access::ReadWrite);
            gfx::dispatch(view, sort_program_->native_handle(), block_groups, 1, 1);
            ++stats.dispatches;
        };
        dispatch_sort(0.0f, 0, 0);
        for(uint32_t stage = gpu_sort_block * 2; stage <= sort_count; stage <<= 1u)
        {
            for(uint32_t distance = stage >> 1u; distance >= gpu_sort_block; distance >>= 1u)
            {
                dispatch_sort(1.0f, stage, distance);
            }
            dispatch_sort(2.0f, stage, 0);
        }
        sort_program_->end();
        stats.sorted_keys = sort_count;
    }
    if(args_program_->begin())
    {
        const float args[4] = {float(batch_count), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_args_, args);
        bgfx::setBuffer(0, counters_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(1, draw_args_, bgfx::Access::Write);
        bgfx::setBuffer(2, buffers.batches, bgfx::Access::Read);
        bgfx::setBuffer(3, batch_counts_, bgfx::Access::Write);
        gfx::dispatch(view, args_program_->native_handle(), (batch_count + gpu_group_threads - 1) / gpu_group_threads, 1, 1);
        args_program_->end();
        ++stats.dispatches;
    }
    return stats;
}

auto particle_gpu::upload_cpu_list(const std::vector<uint32_t>& slots) -> uint32_t
{
    view_buffers& buffers = current_view_buffers();
    const uint32_t first = buffers.cpu_list_used;
    for(uint32_t slot : slots)
    {
        // Key / slot pairs as the GPU sort writes them.
        buffers.cpu_list_data.push_back(0u);
        buffers.cpu_list_data.push_back(slot);
    }
    buffers.cpu_list_used += uint32_t(slots.size());
    return first;
}

void particle_gpu::upload_view_lists()
{
    view_buffers& buffers = current_view_buffers();
    if(buffers.cpu_list_used == 0)
    {
        return;
    }
    if(buffers.cpu_list_capacity < buffers.cpu_list_used || !buffers.cpu_list.is_valid())
    {
        buffers.cpu_list_capacity = grow_capacity(buffers.cpu_list_capacity, buffers.cpu_list_used, k_min_list_entries);
        buffers.cpu_list.reserve(buffers.cpu_list_capacity * k_list_uints_per_entry);
    }
    buffers.cpu_list.update(0, uint32_t(buffers.cpu_list_data.size()), buffers.cpu_list_data.data());
}

auto particle_gpu::bind_data(bool is_cpu_list) -> bool
{
    const shader_buffer& cpu_list = current_view_buffers().cpu_list;
    const bool has_list = is_cpu_list ? cpu_list.is_valid() : bgfx::isValid(sort_);
    if(!has_list || !records_.is_valid() || !static_table_.is_valid() || !lut_atlas_.is_valid())
    {
        return false;
    }
    records_.bind(gpu_records_stage);
    static_table_.bind(gpu_static_stage);
    dynamic_table_.bind(gpu_dynamic_stage);
    lut_atlas_.bind(gpu_luts_stage);
    if(is_cpu_list)
    {
        cpu_list.bind(gpu_sorted_stage);
    }
    else
    {
        bgfx::setBuffer(gpu_sorted_stage, sort_, bgfx::Access::Read);
    }
    // The GPU path's live counts; the texture variants draw CPU lists only and read none.
    if(bgfx::isValid(batch_counts_))
    {
        bgfx::setBuffer(gpu_batch_counts_stage, batch_counts_, bgfx::Access::Read);
    }
    else if(!uses_textures())
    {
        cpu_list.bind(gpu_batch_counts_stage);
    }
    ensure_cutout_buffer();
    cutout_buffer_.bind(gpu_cutout_stage);
    return true;
}

void particle_gpu::submit_draw(const draw_params& params)
{
    if(!bind_data(params.is_cpu_list))
    {
        return;
    }
    // y: the GPU batch whose live count the vertex shaders read; z: a CPU list's count (-1 on the GPU path).
    const float draw[4] = {float(params.first),
                           float(params.batch),
                           params.is_cpu_list ? float(params.instance_count) : -1.0f,
                           0.0f};
    bgfx::setUniform(u_draw_, draw);
    bgfx::setVertexBuffer(0, quad_vertices_);
    // Indirect draws take their index range from the draw arguments.
    bgfx::setIndexBuffer(quad_indices_, params.first_index, params.index_count);
    bgfx::setState(params.state, params.blend_rgba);
    if(params.is_cpu_list)
    {
        bgfx::setInstanceCount(params.instance_count);
        bgfx::submit(params.view, params.program);
        return;
    }
    gfx::submit_indirect(params.view, params.program, draw_args_, params.batch, 1);
}

} // namespace ps_soa
} // namespace unravel
