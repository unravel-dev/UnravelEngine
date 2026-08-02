#include "sdf_atlas.h"

#include <cmath>

#include <engine/profiler/profiler.h>

#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// Voxels along one atlas axis for a given brick count, border included.
auto compute_atlas_voxel_dim(uint32_t atlas_brick_dim) -> uint32_t
{
    return atlas_brick_dim * mesh_sdf::brick_stride;
}

/// Layout of the header buffer: a flat array of vec4, which is what BUFFER_RO(_, vec4, _)
/// expects on every backend (a typed Buffer<float4> on D3D, a StructuredBuffer elsewhere).
auto get_vec4_buffer_layout() -> const gfx::vertex_layout&
{
    static const gfx::vertex_layout layout = []()
    {
        gfx::vertex_layout decl;
        decl.begin().add(gfx::attribute::TexCoord0, 4, gfx::attribute_type::Float).end();
        return decl;
    }();
    return layout;
}

} // namespace

auto sdf_atlas::init(const settings& settings) -> bool
{
    shutdown();
    settings_ = settings;
    const uint32_t brick_dim = std::max(settings_.atlas_brick_dim, 1u);
    settings_.atlas_brick_dim = brick_dim;
    const uint32_t voxel_dim = compute_atlas_voxel_dim(brick_dim);
    if(voxel_dim > 2048u)
    {
        APPLOG_ERROR("[SurfaceCache] Atlas brick dimension {} needs a {}^3 texture, which exceeds the "
                     "2048 limit. Reduce it.",
                     brick_dim,
                     voxel_dim);
        return false;
    }
    // Hardware trilinear (the default filter) is safe here precisely because of the brick
    // border: a sample anywhere in a brick's interior has all eight of its filter taps inside
    // that brick's own tile, so the filter can never blend in a neighbouring and unrelated
    // brick. Remove the border and this stops being true. Clamping is belt and braces -- the
    // tracer never addresses outside the atlas.
    const uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    atlas_texture_ = std::make_shared<gfx::texture>(static_cast<uint16_t>(voxel_dim),
                                                    static_cast<uint16_t>(voxel_dim),
                                                    static_cast<uint16_t>(voxel_dim),
                                                    false,
                                                    gfx::texture_format::R8,
                                                    flags);
    if(!atlas_texture_ || !atlas_texture_->is_valid())
    {
        APPLOG_ERROR("[SurfaceCache] Failed to create the {}^3 SDF brick atlas.", voxel_dim);
        atlas_texture_.reset();
        return false;
    }
    total_brick_slots_ = brick_dim * brick_dim * brick_dim;
    next_brick_slot_ = 0;
    // One header worth of zeros so the handles are always valid to bind before any field is
    // resident. ensure_buffer_capacity grows them from here.
    header_data_.assign(4u * header_vec4_count, 0.0f);
    indirection_data_.assign(1, 0u);
    ensure_buffer_capacity();
    APPLOG_INFO("[SurfaceCache] SDF brick atlas ready: {}^3 bricks ({}^3 voxels, {} MB).",
                brick_dim,
                voxel_dim,
                (size_t(voxel_dim) * voxel_dim * voxel_dim) / (1024 * 1024));
    return true;
}

void sdf_atlas::shutdown()
{
    if(bgfx::isValid(header_buffer_))
    {
        gfx::destroy(header_buffer_);
        header_buffer_ = {bgfx::kInvalidHandle};
    }
    if(bgfx::isValid(indirection_buffer_))
    {
        gfx::destroy(indirection_buffer_);
        indirection_buffer_ = {bgfx::kInvalidHandle};
    }
    atlas_texture_.reset();
    header_data_.clear();
    indirection_data_.clear();
    fields_.clear();
    free_field_indices_.clear();
    free_brick_slots_.clear();
    next_brick_slot_ = 0;
    total_brick_slots_ = 0;
    header_capacity_vec4_ = 0;
    indirection_capacity_ = 0;
    headers_dirty_ = false;
    indirection_dirty_ = false;
}

auto sdf_atlas::allocate_brick() -> uint32_t
{
    if(!free_brick_slots_.empty())
    {
        const uint32_t slot = free_brick_slots_.back();
        free_brick_slots_.pop_back();
        return slot;
    }
    if(next_brick_slot_ >= total_brick_slots_)
    {
        return invalid_index;
    }
    return next_brick_slot_++;
}

void sdf_atlas::upload_brick(uint32_t slot, const uint8_t* voxels)
{
    const uint32_t brick_dim = settings_.atlas_brick_dim;
    const uint32_t bx = slot % brick_dim;
    const uint32_t by = (slot / brick_dim) % brick_dim;
    const uint32_t bz = slot / (brick_dim * brick_dim);
    const uint16_t stride = static_cast<uint16_t>(mesh_sdf::brick_stride);
    gfx::update_texture_3d(atlas_texture_->native_handle(),
                           0,
                           static_cast<uint16_t>(bx * mesh_sdf::brick_stride),
                           static_cast<uint16_t>(by * mesh_sdf::brick_stride),
                           static_cast<uint16_t>(bz * mesh_sdf::brick_stride),
                           stride,
                           stride,
                           stride,
                           gfx::copy(voxels, mesh_sdf::brick_voxel_count));
}

auto sdf_atlas::allocate_indirection(uint32_t count) -> uint32_t
{
    // First fit over the regions release() gave back, before growing. Reuse is what keeps this
    // bounded: abandoning a released field's region was harmless while fields only died with the
    // asset, but they are now released as soon as nothing references them, so without this a scene
    // reload would grow the table by the whole scene's worth every time.
    for(size_t i = 0; i < free_indirection_ranges_.size(); ++i)
    {
        auto& range = free_indirection_ranges_[i];
        if(range.count < count)
        {
            continue;
        }
        const uint32_t offset = range.offset;
        if(range.count == count)
        {
            free_indirection_ranges_.erase(free_indirection_ranges_.begin() + std::ptrdiff_t(i));
        }
        else
        {
            range.offset += count;
            range.count -= count;
        }
        // Cleared to match what the growing path below hands back, so a caller that writes fewer
        // entries than it asked for cannot read a previous field's slots.
        std::fill(indirection_data_.begin() + std::ptrdiff_t(offset),
                  indirection_data_.begin() + std::ptrdiff_t(offset + count),
                  0u);
        return offset;
    }
    const uint32_t offset = uint32_t(indirection_data_.size());
    indirection_data_.resize(size_t(offset) + count, 0u);
    return offset;
}

auto sdf_atlas::upload(const mesh_sdf& sdf) -> uint32_t
{
    APP_SCOPE_PERF("GI/SurfaceCache/Upload SDF");
    if(!is_valid() || !sdf.is_valid())
    {
        return invalid_index;
    }
    const uint32_t surface_bricks = sdf.get_surface_brick_count();
    const uint32_t brick_count = uint32_t(sdf.indirection.size());
    // Reserve every brick up front. A partially resident field would trace as if the missing
    // bricks were empty space, which silently turns solid geometry transparent -- far worse
    // than refusing the upload and leaving the mesh out of GI entirely.
    std::vector<uint32_t> slots;
    slots.reserve(surface_bricks);
    for(uint32_t i = 0; i < surface_bricks; ++i)
    {
        const uint32_t slot = allocate_brick();
        if(slot == invalid_index)
        {
            for(uint32_t allocated : slots)
            {
                free_brick_slots_.push_back(allocated);
            }
            // Accumulated and reported on a doubling schedule rather than per mesh. One line per
            // refused mesh is both unusable -- a scene overruns by hundreds of them at once -- and
            // uninformative: the number that says how much to raise the atlas by is the RUNNING
            // TOTAL, which no individual line carries.
            ++rejected_mesh_count_;
            rejected_brick_total_ += surface_bricks;
            if(rejected_brick_total_ >= next_rejection_report_)
            {
                // Doubling, so a scene that overruns by thousands of meshes reports a handful of
                // times rather than once per mesh. Saturating rather than wrapping: this used to be
                // 32-bit, and once the total passed two billion the doubling wrapped to a small
                // number, which made the throttle fire on every refusal instead of suppressing it.
                next_rejection_report_ = rejected_brick_total_ > (UINT64_MAX / 2u)
                                             ? UINT64_MAX
                                             : rejected_brick_total_ * 2u;
                APPLOG_WARNING("[SurfaceCache] SDF atlas is full ({0} bricks). {1} meshes refused so "
                               "far, needing {2} bricks in total, so they do not contribute to global "
                               "illumination. Raise sdf_atlas::settings::atlas_brick_dim past {3} "
                               "(memory is cubic in it), or lower the mesh importer's Max Total "
                               "Voxels to make each field cheaper.",
                               total_brick_slots_,
                               rejected_mesh_count_,
                               rejected_brick_total_,
                               uint32_t(std::ceil(std::cbrt(double(total_brick_slots_ + rejected_brick_total_)))));
            }
            return invalid_index;
        }
        slots.push_back(slot);
    }
    // Copy the indirection table, rewriting surface entries from mesh-local brick indices to
    // absolute atlas slots. Empty entries carry a distance, not an index, and pass through.
    const uint32_t indirection_offset = allocate_indirection(brick_count);
    for(uint32_t i = 0; i < brick_count; ++i)
    {
        const uint32_t entry = sdf.indirection[i];
        if(is_sdf_empty_entry(entry))
        {
            indirection_data_[size_t(indirection_offset) + i] = entry;
            continue;
        }
        indirection_data_[size_t(indirection_offset) + i] = make_sdf_surface_entry(slots[entry]);
    }
    for(uint32_t i = 0; i < surface_bricks; ++i)
    {
        upload_brick(slots[i], sdf.brick_voxels.data() + size_t(i) * mesh_sdf::brick_voxel_count);
    }
    uint32_t header_index = 0;
    if(!free_field_indices_.empty())
    {
        header_index = free_field_indices_.back();
        free_field_indices_.pop_back();
    }
    else
    {
        header_index = uint32_t(fields_.size());
        fields_.emplace_back();
        header_data_.resize(size_t(fields_.size()) * 4u * header_vec4_count, 0.0f);
    }
    auto& field = fields_[header_index];
    field.brick_slots = std::move(slots);
    field.indirection_offset = indirection_offset;
    field.indirection_count = brick_count;
    field.is_alive = true;
    // Header layout, mirrored by gi/sdf_common.sh. Keep the two in sync.
    //   [0] xyz = local bounds min, w = voxel size
    //   [1] xyz = brick dim (as float), w = indirection offset (as float)
    //   [2] x   = two-sided shell thickness (0 when the field is signed)
    //       yzw = grid dim (as float)
    float* header = header_data_.data() + size_t(header_index) * 4u * header_vec4_count;
    header[0] = sdf.bounds.min.x;
    header[1] = sdf.bounds.min.y;
    header[2] = sdf.bounds.min.z;
    header[3] = sdf.voxel_size;
    header[4] = float(sdf.brick_dim.x);
    header[5] = float(sdf.brick_dim.y);
    header[6] = float(sdf.brick_dim.z);
    header[7] = float(indirection_offset);
    header[8] = sdf.is_two_sided ? sdf.two_sided_thickness : 0.0f;
    header[9] = float(sdf.grid_dim.x);
    header[10] = float(sdf.grid_dim.y);
    header[11] = float(sdf.grid_dim.z);
    headers_dirty_ = true;
    indirection_dirty_ = true;
    return header_index;
}

void sdf_atlas::release(uint32_t header_index)
{
    if(header_index >= fields_.size() || !fields_[header_index].is_alive)
    {
        return;
    }
    auto& field = fields_[header_index];
    for(uint32_t slot : field.brick_slots)
    {
        free_brick_slots_.push_back(slot);
    }
    field.brick_slots.clear();
    field.is_alive = false;
    // Returned for reuse IN PLACE, never compacted: moving it would shift every later field's
    // offset and require rewriting all their headers. Reuse used to be skipped entirely on the
    // grounds that fields only died when an asset unloaded, so the waste was bounded by asset
    // churn -- that stopped being true once fields are released as soon as nothing references
    // them, which happens on every scene change.
    if(field.indirection_count > 0)
    {
        free_indirection_ranges_.push_back({field.indirection_offset, field.indirection_count});
    }
    field.indirection_count = 0;
    free_field_indices_.push_back(header_index);
    float* header = header_data_.data() + size_t(header_index) * 4u * header_vec4_count;
    std::fill(header, header + 4u * header_vec4_count, 0.0f);
    headers_dirty_ = true;
    // The refusal tally describes a MOMENT, not the session. Freeing bricks changes what will fit,
    // so a total accumulated before that says nothing about what is short now -- and leaving the
    // doubling schedule where it was would silence the report right through the next genuine
    // shortfall, which is when it is worth reading.
    rejected_mesh_count_ = 0;
    rejected_brick_total_ = 0;
    next_rejection_report_ = 1;
    // Freeing room is the ONLY thing that can turn a previous refusal into a success, so this is
    // what tells a refused caller that retrying is worth anything. See get_release_generation.
    ++release_generation_;
}

void sdf_atlas::ensure_buffer_capacity()
{
    const uint32_t required_headers = uint32_t(header_data_.size() / 4u);
    if(!bgfx::isValid(header_buffer_) || required_headers > header_capacity_vec4_)
    {
        if(bgfx::isValid(header_buffer_))
        {
            gfx::destroy(header_buffer_);
        }
        // Grow with slack so a scene loading many distinct meshes does not recreate the
        // buffer once per mesh.
        header_capacity_vec4_ = required_headers + required_headers / 2u + 64u;
        header_buffer_ = gfx::create_dynamic_vertex_buffer(header_capacity_vec4_,
                                                           get_vec4_buffer_layout(),
                                                           BGFX_BUFFER_COMPUTE_READ);
        headers_dirty_ = true;
    }
    const uint32_t required_indirection = uint32_t(indirection_data_.size());
    if(!bgfx::isValid(indirection_buffer_) || required_indirection > indirection_capacity_)
    {
        if(bgfx::isValid(indirection_buffer_))
        {
            gfx::destroy(indirection_buffer_);
        }
        indirection_capacity_ = required_indirection + required_indirection / 2u + 4096u;
        indirection_buffer_ = gfx::create_dynamic_index_buffer(indirection_capacity_,
                                                               BGFX_BUFFER_COMPUTE_READ |
                                                                   BGFX_BUFFER_INDEX32);
        indirection_dirty_ = true;
    }
}

void sdf_atlas::flush()
{
    if(!is_valid())
    {
        return;
    }
    ensure_buffer_capacity();
    if(headers_dirty_ && !header_data_.empty())
    {
        gfx::update(header_buffer_,
                    0,
                    gfx::copy(header_data_.data(), uint32_t(header_data_.size() * sizeof(float))));
        headers_dirty_ = false;
    }
    if(indirection_dirty_ && !indirection_data_.empty())
    {
        gfx::update(indirection_buffer_,
                    0,
                    gfx::copy(indirection_data_.data(), uint32_t(indirection_data_.size() * sizeof(uint32_t))));
        indirection_dirty_ = false;
    }
}

auto sdf_atlas::get_stats() const -> stats
{
    stats result;
    result.total_bricks = total_brick_slots_;
    result.used_bricks = next_brick_slot_ - uint32_t(free_brick_slots_.size());
    result.indirection_entries = uint32_t(indirection_data_.size());
    for(const auto& field : fields_)
    {
        if(field.is_alive)
        {
            ++result.resident_fields;
        }
    }
    const uint32_t voxel_dim = compute_atlas_voxel_dim(settings_.atlas_brick_dim);
    result.atlas_bytes = size_t(voxel_dim) * voxel_dim * voxel_dim;
    return result;
}

} // namespace unravel
