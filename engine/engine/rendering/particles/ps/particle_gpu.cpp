#include "particle_gpu.h"
#include "particle_noise.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
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
/// the quad's outline until the shader moves it onto the frame's cutout); uv = (x + 0.5, 0.5 - y). The strip of
/// trails and ribbons follows (make_particle_vertices).
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

/// The shapes, then the strip: strip_max_segments + 1 pairs of vertices across it (x = -0.5, 0.5), y = 0 at its
/// start (a trail's particle, the older particle of a ribbon's stretch) to 1 at its end.
auto make_particle_vertices() -> std::vector<quad_vertex>
{
    std::vector<quad_vertex> vertices(std::begin(k_shape_vertices), std::end(k_shape_vertices));
    for(uint32_t j = 0; j <= strip_max_segments; ++j)
    {
        const float along = float(j) / float(strip_max_segments);
        vertices.push_back({-0.5f, along, 0.0f, 0.0f, along});
        vertices.push_back({0.5f, along, 0.0f, 1.0f, along});
    }
    return vertices;
}

/// The shapes' indices, then the strip's ranges: 1 quad, 2, 4 ... strip_max_segments, each over every pair of
/// vertices its quads end on (get_strip_first_index).
auto make_particle_indices() -> std::vector<uint16_t>
{
    std::vector<uint16_t> indices(std::begin(k_shape_indices), std::end(k_shape_indices));
    const uint16_t base = uint16_t(std::size(k_shape_vertices));
    for(uint32_t segments = 1; segments <= strip_max_segments; segments *= 2)
    {
        const uint32_t pairs_per_quad = strip_max_segments / segments;
        for(uint32_t quad = 0; quad < segments; ++quad)
        {
            const uint16_t near_left = uint16_t(base + quad * pairs_per_quad * 2);
            const uint16_t near_right = uint16_t(near_left + 1);
            const uint16_t far_left = uint16_t(near_left + pairs_per_quad * 2);
            const uint16_t far_right = uint16_t(far_left + 1);
            indices.insert(indices.end(), {near_left, near_right, far_right, far_right, far_left, near_left});
        }
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
auto load_compute(asset_manager& assets,
                  const char* key,
                  const char* label,
                  const std::string& variant = gfx::shader::DEFAULT_VARIANT) -> std::shared_ptr<gpu_program>
{
    auto shader = assets.get_asset<gfx::shader>(key);
    if(!shader)
    {
        APPLOG_WARNING("Particles: {} missing", label);
        return {};
    }
    auto program = std::make_shared<gpu_program>(shader, variant);
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
    u_pool_copy_ = bgfx::createUniform("u_particle_pool_copy", bgfx::UniformType::Vec4);
    u_collide_ = bgfx::createUniform("u_particle_collide", bgfx::UniformType::Vec4);
    u_scene_depth_ = bgfx::createUniform("s_particle_scene_depth", bgfx::UniformType::Sampler);
    u_scene_normals_ = bgfx::createUniform("s_particle_scene_normals", bgfx::UniformType::Sampler);
    // The distance field's own names (gi/sdf_clipmap.sh): the collide kernel samples it as every reader does.
    u_distance_field_ = bgfx::createUniform("s_sdf_clipmap", bgfx::UniformType::Sampler);
    u_distance_field_levels_ =
        bgfx::createUniform("u_sdf_clipmap_levels", bgfx::UniformType::Vec4, uint16_t(global_sdf_clipmap::level_count));
    u_distance_field_params_ = bgfx::createUniform("u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    // The samplers of texture storage come before the programs too; the first view's buffers stand for every view's.
    storage_ = get_particle_data_storage();
    for(shader_buffer* data : {&records_, &static_table_, &dynamic_table_, &lut_atlas_})
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
    pool_copy_program_ = load_compute(assets, "engine:/data/shaders/particles/cs_particle_pool_copy.eshader", "pool copy");
    const char* collide_key = "engine:/data/shaders/particles/cs_particle_collide.eshader";
    collide_depth_program_ = load_compute(assets, collide_key, "scene collision");
    collide_field_program_ = load_compute(assets, collide_key, "scene collision with the distance field", "distance_field");
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
    can_copy_pool_ = can_draw_ && !uses_textures() && refresh(pool_copy_program_);
    // Collided records are known to the GPU alone, so they need the path that culls and sorts there.
    can_collide_ = can_sort_ && refresh(collide_depth_program_);
    can_collide_with_field_ = can_collide_ && refresh(collide_field_program_);
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
    collide_depth_program_.reset();
    collide_field_program_.reset();
    pool_copy_program_.reset();
    destroy_handle(u_pool_copy_);
    destroy_handle(u_collide_);
    destroy_handle(u_scene_depth_);
    destroy_handle(u_scene_normals_);
    destroy_handle(u_distance_field_);
    destroy_handle(u_distance_field_levels_);
    destroy_handle(u_distance_field_params_);
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
    // The cutout tables' blocks go with the atlas.
    cutouts_.clear();
    has_cutout_uploads_ = false;
    lut_block_count_ = 0;
}

void particle_gpu::destroy_buffers()
{
    records_.destroy();
    grown_from_.destroy();
    grown_records_ = 0;
    for(auto& buffer : collide_buffers_)
    {
        destroy_handle(buffer.handle);
    }
    collide_buffers_.clear();
    static_table_.destroy();
    dynamic_table_.destroy();
    lut_atlas_.destroy();
    destroy_handle(sort_);
    destroy_handle(counters_);
    destroy_handle(draw_args_);
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

auto particle_gpu::can_collide() const -> bool
{
    return can_collide_;
}

auto particle_gpu::make_record_pool(shader_buffer_storage storage) -> shader_buffer
{
    return shader_buffer("s_particle_records", shader_buffer_element::uint4, storage, BGFX_BUFFER_COMPUTE_READ_WRITE);
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
    // The pool holds what scene collisions wrote, which no CPU copy has: it is kept until flush copies it into the
    // larger one. A pool that grows again before that still copies from the one that holds the records.
    const bool keeps_records = old_capacity > 0 && records_.is_valid() && can_copy_pool_;
    if(keeps_records && !grown_from_.is_valid())
    {
        grown_from_ = std::move(records_);
        grown_records_ = old_capacity;
        records_ = make_record_pool(storage_);
    }
    records_.reserve(new_capacity * k_record_uint4s);
    pool_capacity_ = new_capacity;
    release_range(old_capacity, new_capacity - old_capacity);
    if(keeps_records)
    {
        // What is staged keeps its slots in the larger pool, and is scattered after the copy.
        return;
    }
    // The new buffer starts empty: everything staged against the old one is restaged by its emitter, and a pool
    // still kept for its copy is let go.
    grown_from_.destroy();
    grown_records_ = 0;
    staged_records_.clear();
    staged_slots_.clear();
    has_kept_staging_ = false;
    pool_reset_ = old_capacity > 0;
}

void particle_gpu::copy_grown_pool()
{
    gfx::render_pass pass("Particles/Pool Copy");
    pass.touch();
    if(records_.is_valid() && pool_copy_program_->begin())
    {
        const float copy[4] = {float(grown_records_), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_pool_copy_, copy);
        grown_from_.bind_compute(0, bgfx::Access::Read);
        records_.bind_compute(1, bgfx::Access::Write);
        dispatch_threads(pass.id, pool_copy_program_->native_handle(), grown_records_);
        pool_copy_program_->end();
    }
    else
    {
        // The copy cannot run after all: the emitters stage their records again, as the CPU knows them.
        pool_reset_ = true;
    }
    grown_from_.destroy();
    grown_records_ = 0;
}

auto particle_gpu::take_pool_reset() -> bool
{
    // A pool that grew waits for flush to copy it. Where that copy cannot run after all (its shader was recompiled
    // and failed), the emitters stage their records again now, before any view reads the pool that holds none.
    if(grown_from_.is_valid() && !refresh(pool_copy_program_))
    {
        grown_from_.destroy();
        grown_records_ = 0;
        pool_reset_ = true;
    }
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

auto particle_gpu::allocate_lut_blocks(uint32_t count) -> uint32_t
{
    if(count <= 1)
    {
        return allocate_lut_block();
    }
    std::sort(free_lut_blocks_.begin(), free_lut_blocks_.end());
    size_t run_start = 0;
    for(size_t i = 0; i < free_lut_blocks_.size(); ++i)
    {
        if(i > 0 && free_lut_blocks_[i] != free_lut_blocks_[i - 1] + 1)
        {
            run_start = i;
        }
        if(i - run_start + 1 == count)
        {
            const uint32_t first = free_lut_blocks_[run_start];
            free_lut_blocks_.erase(free_lut_blocks_.begin() + ptrdiff_t(run_start),
                                   free_lut_blocks_.begin() + ptrdiff_t(i + 1));
            return first;
        }
    }
    const uint32_t first = lut_block_count_;
    lut_block_count_ += count;
    return first;
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
    {
        std::lock_guard<std::mutex> lock(lut_mutex_);
        for(uint32_t block = 0; block < found->second.block_count; ++block)
        {
            free_lut_blocks_.push_back(found->second.block + block);
        }
    }
    cutouts_.erase(found);
}

auto particle_gpu::is_cutout_pending(uint64_t key) const -> bool
{
    auto found = cutouts_.find(key);
    return found != cutouts_.end() && found->second.points.empty();
}

void particle_gpu::set_cutout_polygons(uint64_t key, const std::vector<cutout_polygon>& polygons)
{
    auto found = cutouts_.find(key);
    if(found == cutouts_.end() || !found->second.points.empty() || polygons.empty())
    {
        return;
    }
    cutout_entry& entry = found->second;
    entry.points.reserve(polygons.size() * cutout_frame_vec4s);
    for(const cutout_polygon& polygon : polygons)
    {
        for(uint32_t pair = 0; pair < cutout_frame_vec4s; ++pair)
        {
            entry.points.emplace_back(polygon[pair * 2], polygon[pair * 2 + 1]);
        }
    }
    entry.block_count = (uint32_t(entry.points.size()) + lut_set_vec4s - 1) / lut_set_vec4s;
    // The rest of the last block reads as collapsed polygons, never as what the block held before.
    entry.points.resize(size_t(entry.block_count) * lut_set_vec4s, math::vec4(0.0f));
    {
        std::lock_guard<std::mutex> lock(lut_mutex_);
        entry.block = allocate_lut_blocks(entry.block_count);
    }
    entry.needs_upload = true;
    has_cutout_uploads_ = true;
}

auto particle_gpu::get_cutout_table(uint64_t key) const -> float
{
    auto found = cutouts_.find(key);
    const bool has_table = found != cutouts_.end() && !found->second.points.empty();
    return has_table ? float(found->second.block * lut_set_vec4s + 1) : 0.0f;
}

void particle_gpu::upload_cutouts()
{
    if(!has_cutout_uploads_ || !lut_atlas_.is_valid())
    {
        return;
    }
    has_cutout_uploads_ = false;
    for(auto& [key, entry] : cutouts_)
    {
        if(!entry.needs_upload)
        {
            continue;
        }
        const bool is_in_atlas = entry.block + entry.block_count <= lut_atlas_blocks_;
        if(!is_in_atlas)
        {
            has_cutout_uploads_ = true;
            continue;
        }
        lut_atlas_.update(entry.block * lut_set_vec4s, uint32_t(entry.points.size()), entry.points.data());
        entry.needs_upload = false;
    }
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
    // A new atlas holds nothing yet: upload every live block and every cutout table.
    dirty_lut_blocks_.clear();
    for(const auto& block_key : lut_block_keys_)
    {
        dirty_lut_blocks_.push_back(block_key.first);
    }
    for(auto& [key, entry] : cutouts_)
    {
        entry.needs_upload = !entry.points.empty();
        has_cutout_uploads_ = has_cutout_uploads_ || entry.needs_upload;
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
    if(grown_from_.is_valid())
    {
        copy_grown_pool();
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
        collides_this_frame_ = 0;
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

auto particle_gpu::get_batch_counts_base() const -> uint32_t
{
    return sort_capacity_ * k_list_uints_per_entry;
}

void particle_gpu::ensure_sort_buffers(uint32_t entries, uint32_t batches)
{
    const bool needs_batches = batch_capacity_ < batches || !bgfx::isValid(counters_);
    if(needs_batches)
    {
        destroy_handle(counters_);
        destroy_handle(draw_args_);
        batch_capacity_ = grow_capacity(batch_capacity_, batches, k_min_batches);
        counters_ = bgfx::createDynamicIndexBuffer(batch_capacity_, BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
        draw_args_ = bgfx::createIndirectBuffer(batch_capacity_);
        counters_need_clear_ = true;
    }
    // The list holds the batch counts after its entries, so it is created again when either capacity grows.
    const bool needs_entries = sort_capacity_ < entries;
    if(needs_entries || needs_batches || !bgfx::isValid(sort_))
    {
        destroy_handle(sort_);
        if(needs_entries)
        {
            sort_capacity_ = grow_capacity(sort_capacity_, entries, k_min_list_entries);
        }
        sort_ = bgfx::createDynamicIndexBuffer(get_batch_counts_base() + batch_capacity_,
                                               BGFX_BUFFER_COMPUTE_READ_WRITE | BGFX_BUFFER_INDEX32);
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
    // Task table: pool base, high water, thread offset, output base, batch, sort kind, batch key, ribbon key bits.
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
        const bool is_ribbon = batch.is_sorted && task.sort_kind == gpu_sort_kind::ribbon;
        entry[7] = is_ribbon ? pack_ribbon_key_bits(task.ribbon_count, task.longest_age_ticks, distance_bits) : 0u;
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
        const float args[4] = {float(batch_count), float(get_batch_counts_base()), 0.0f, 0.0f};
        bgfx::setUniform(u_args_, args);
        bgfx::setBuffer(0, counters_, bgfx::Access::ReadWrite);
        bgfx::setBuffer(1, draw_args_, bgfx::Access::Write);
        bgfx::setBuffer(2, buffers.batches, bgfx::Access::Read);
        bgfx::setBuffer(3, sort_, bgfx::Access::Write);
        gfx::dispatch(view, args_program_->native_handle(), (batch_count + gpu_group_threads - 1) / gpu_group_threads, 1, 1);
        args_program_->end();
        ++stats.dispatches;
    }
    return stats;
}

auto particle_gpu::collide_view(bgfx::ViewId view, const std::vector<collide_task>& tasks, const collide_scene& scene) -> uint32_t
{
    begin_frame();
    const bool uses_field = can_collide_with_field_ && bgfx::isValid(scene.distance_field) &&
                            scene.distance_field_levels != nullptr && scene.distance_field_params != nullptr;
    const bool has_depth = bgfx::isValid(scene.depth) && bgfx::isValid(scene.normals);
    const bool has_data = records_.is_valid() && static_table_.is_valid() && lut_atlas_.is_valid();
    if(tasks.empty() || !can_collide() || !has_data || (!uses_field && !has_depth))
    {
        return 0;
    }
    APP_SCOPE_PERF("Particles/GPU Scene Collision");
    // Task table (gpu_collide_task_slot).
    collide_data_.assign(tasks.size() * gpu_collide_task_uints, 0u);
    uint32_t thread_offset = 0;
    for(size_t t = 0; t < tasks.size(); ++t)
    {
        const collide_task& task = tasks[t];
        uint32_t* entry = &collide_data_[t * gpu_collide_task_uints];
        const auto set_float = [entry](gpu_collide_task_slot slot, float value)
        {
            std::memcpy(entry + slot, &value, sizeof(value));
        };
        entry[gpu_collide_task_pool_base] = task.pool_base;
        entry[gpu_collide_task_high_water] = task.high_water;
        entry[gpu_collide_task_thread_offset] = thread_offset;
        entry[gpu_collide_task_answer] = uint32_t(task.mode) |
                                         (std::min(task.max_bounces, motion_offset_bounce_mask) << gpu_collide_bounces_shift) |
                                         (task.lies_flat ? gpu_collide_flat_bit : 0u);
        set_float(gpu_collide_task_restitution, task.restitution);
        set_float(gpu_collide_task_friction, task.friction);
        set_float(gpu_collide_task_radius_scale, task.radius_scale);
        set_float(gpu_collide_task_rest_speed, task.rest_speed);
        set_float(gpu_collide_task_lifetime_loss, task.lifetime_loss);
        set_float(gpu_collide_task_bounce_scatter, task.bounce_scatter);
        thread_offset += task.high_water;
    }
    if(thread_offset == 0)
    {
        return 0;
    }
    if(collides_this_frame_ >= collide_buffers_.size())
    {
        collide_buffers_.emplace_back();
    }
    staging_buffer& buffer = collide_buffers_[collides_this_frame_++];
    if(buffer.capacity < tasks.size() || !bgfx::isValid(buffer.handle))
    {
        destroy_handle(buffer.handle);
        buffer.capacity = grow_capacity(buffer.capacity, uint32_t(tasks.size()), k_min_tasks);
        buffer.handle = bgfx::createDynamicIndexBuffer(buffer.capacity * gpu_collide_task_uints,
                                                       BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_INDEX32);
    }
    bgfx::update(buffer.handle, 0, bgfx::copy(collide_data_.data(), uint32_t(collide_data_.size() * sizeof(uint32_t))));
    const std::shared_ptr<gpu_program>& program = uses_field ? collide_field_program_ : collide_depth_program_;
    if(!program->begin())
    {
        return 0;
    }
    const float collide[4] = {float(tasks.size()), float(thread_offset), 0.0f, 0.0f};
    bgfx::setUniform(u_collide_, collide);
    records_.bind_compute(gpu_records_stage, bgfx::Access::ReadWrite);
    static_table_.bind_compute(gpu_static_stage, bgfx::Access::Read);
    dynamic_table_.bind_compute(gpu_dynamic_stage, bgfx::Access::Read);
    lut_atlas_.bind_compute(gpu_luts_stage, bgfx::Access::Read);
    bgfx::setBuffer(gpu_collide_tasks_stage, buffer.handle, bgfx::Access::Read);
    // The particles are tested where they draw, turbulence included.
    bgfx::setTexture(gpu_noise_stage, u_noise_, noise_texture_);
    if(uses_field)
    {
        bgfx::setUniform(u_distance_field_levels_, scene.distance_field_levels, uint16_t(global_sdf_clipmap::level_count));
        bgfx::setUniform(u_distance_field_params_, scene.distance_field_params);
        bgfx::setTexture(gpu_collide_distance_field_stage, u_distance_field_, scene.distance_field);
    }
    else
    {
        // The depth and the normal of one pixel: no filtering across an edge.
        const uint32_t point_clamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
        bgfx::setTexture(gpu_collide_depth_stage, u_scene_depth_, scene.depth, point_clamp);
        bgfx::setTexture(gpu_collide_normals_stage, u_scene_normals_, scene.normals, point_clamp);
    }
    dispatch_threads(view, program->native_handle(), thread_offset);
    program->end();
    return thread_offset;
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
    return true;
}

void particle_gpu::submit_draw(const draw_params& params)
{
    if(!bind_data(params.is_cpu_list))
    {
        return;
    }
    // y: the GPU batch whose live count the vertex shaders read; z: a CPU list's count (-1 on the GPU path); w: where
    // the GPU list holds its first batch count.
    const float draw[4] = {float(params.first),
                           float(params.batch),
                           params.is_cpu_list ? float(params.instance_count) : -1.0f,
                           params.is_cpu_list ? 0.0f : float(get_batch_counts_base())};
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
