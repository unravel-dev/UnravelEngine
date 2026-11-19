#include "texture.h"
#include "utils/bgfx_utils.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <string>
#include <utility>

namespace gfx
{

struct texture::image_source
{
    /// The parsed image, shared with the bgfx references of copies still uploading.
    ImagePtr image;
    /// Creation flags with the container's sRGB view applied.
    std::uint64_t flags{};
    /// Top mips the texture never has: the skip it was loaded with.
    std::uint8_t base_skip{};
    /// Debug name of every GPU copy.
    std::string name;
};

namespace
{

auto format_bytes(std::uint64_t bytes) -> std::string
{
    const bx::FixedStringT<32> formatted = bx::toHuman<32>(bytes, bx::Units::KibiByte, 2);
    return formatted.getCPtr();
}

auto is_gpu_generated_flags(std::uint64_t flags) -> bool
{
    return 0 != (flags & (BGFX_TEXTURE_RT_MASK | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_BLIT_DST));
}

/// Returns whether the caller should proceed with the GPU allocation.
/// Evictable (CPU-backed loads): @ref eviction::would_allocation_fit only - no sweep.
/// Immediate (render targets): full @ref eviction::reclaim_for with sweep and flush.
auto try_make_room_for(std::uint64_t bytes, const char* what, eviction::reclaim_kind kind) -> bool
{
    if(bytes == 0 || !eviction::is_supported())
    {
        return true;
    }
    if(kind == eviction::reclaim_kind::evictable)
    {
        if(eviction::would_allocation_fit(bytes))
        {
            return true;
        }
        const std::string msg = std::string("The ") + what + " (" + format_bytes(bytes) +
                                ") does not fit in GPU memory now. It is created when a draw first needs it.";
        gfx::log("warning", msg, __FILE__, __LINE__);
        return false;
    }
    const auto result = eviction::reclaim_for(bytes, kind);
    if(result != eviction::reclaim_result::insufficient)
    {
        return true;
    }
    const std::string msg = std::string("Eviction could not make room for the ") + what + " (" + format_bytes(bytes) +
                            "). Creating it may fail.";
    gfx::log("warning", msg, __FILE__, __LINE__);
    return true;
}

auto texture_reclaim_kind(std::uint64_t flags) -> eviction::reclaim_kind
{
    return is_gpu_generated_flags(flags) ? eviction::reclaim_kind::immediate : eviction::reclaim_kind::evictable;
}

/// The 16-bit dimension bgfx takes for @p value.
auto to_dimension(std::uint32_t value) -> std::uint16_t
{
    assert(value <= std::numeric_limits<std::uint16_t>::max() && "Texture dimension exceeds 16 bits");
    return static_cast<std::uint16_t>(value);
}

/// What the eviction log calls a texture of @p kind.
auto get_kind_label(texture_kind kind) -> const char*
{
    switch(kind)
    {
        case texture_kind::tex_3d:
            return "3D texture";
        case texture_kind::tex_cube:
            return "cube texture";
        case texture_kind::tex_2d:
            break;
    }
    return "2D texture";
}

/// Sizes @p info the way bgfx sizes the texture @p desc describes.
void calc_desc_info(bgfx::TextureInfo& info, const texture_desc& desc)
{
    const std::uint16_t width = to_dimension(desc.size.width);
    const std::uint16_t height = to_dimension(desc.size.height);
    switch(desc.kind)
    {
        case texture_kind::tex_3d:
            bgfx::calcTextureSize(info, width, height, desc.depth, false, desc.has_mips, 1, desc.format);
            return;
        case texture_kind::tex_cube:
            // Six width x width faces at depth 1. Describing a width^3 volume would inflate the
            // eviction estimate (a mipped 256 RGBA16F cube: ~153 MB instead of ~4 MB) and leave
            // info.cubeMap false.
            bgfx::calcTextureSize(info, width, width, 1, true, desc.has_mips, desc.layers, desc.format);
            return;
        case texture_kind::tex_2d:
            break;
    }
    bgfx::calcTextureSize(info, width, height, 1, false, desc.has_mips, desc.layers, desc.format);
}

/// Creates the bgfx texture @p desc describes, with @p mem as its initial contents (may be null).
auto create_desc_handle(const texture_desc& desc, const bgfx::Memory* mem) -> bgfx::TextureHandle
{
    const std::uint16_t width = to_dimension(desc.size.width);
    const std::uint16_t height = to_dimension(desc.size.height);
    switch(desc.kind)
    {
        case texture_kind::tex_3d:
            return bgfx::createTexture3D(width, height, desc.depth, desc.has_mips, desc.format, desc.flags, mem);
        case texture_kind::tex_cube:
            return bgfx::createTextureCube(width, desc.has_mips, desc.layers, desc.format, desc.flags, mem);
        case texture_kind::tex_2d:
            break;
    }
    return bgfx::createTexture2D(width, height, desc.has_mips, desc.layers, desc.format, desc.flags, mem);
}

/// The desc of a texture loaded from a file or memory, read from what the load reported.
auto describe_loaded(const bgfx::TextureInfo& info, std::uint64_t flags) -> texture_desc
{
    const texture_kind kind =
        info.cubeMap ? texture_kind::tex_cube : (info.depth > 1 ? texture_kind::tex_3d : texture_kind::tex_2d);
    return texture_desc{.size = {info.width, info.height},
                        .format = info.format,
                        .flags = flags,
                        .has_mips = info.numMips > 1,
                        .layers = info.numLayers,
                        .depth = info.depth,
                        .kind = kind};
}

void name_handle(bgfx::TextureHandle handle, const std::string& name)
{
    if(bgfx::isValid(handle) && !name.empty())
    {
        bgfx::setName(handle, name.c_str(), static_cast<std::int32_t>(name.size()));
    }
}

/// Whether a loaded texture of @p info can keep a tail: one 2D side with mips.
auto is_single_2d_with_mips(const bgfx::TextureInfo& info) -> bool
{
    return !info.cubeMap && info.numLayers <= 1 && info.depth <= 1 && info.numMips > 1;
}
} // namespace

texture::texture(const char* _path,
                 std::uint64_t _flags,
                 std::uint8_t _skip /*= 0 */,
                 bgfx::TextureInfo* _info /*= nullptr*/)
{
    std::uint32_t size = 0;
    void* data = load(bx::FilePath(_path), &size);
    if(data != nullptr)
    {
        load_image(data, size, _flags, _skip, _path);
        unload(data);
    }

    if(_info != nullptr)
    {
        *_info = info;
    }

    flags = _flags;
    desc_ = describe_loaded(info, _flags);
}

texture::texture(const void* _data,
                 std::uint32_t _size,
                 std::uint64_t _flags,
                 std::uint8_t _skip,
                 bgfx::TextureInfo* _info,
                 const char* _name)
{
    load_image(_data, _size, _flags, _skip, _name != nullptr ? _name : "memory texture");

    if(_info != nullptr)
    {
        *_info = info;
    }

    flags = _flags;
    desc_ = describe_loaded(info, _flags);
}

texture::texture(std::uint16_t _width,
                 std::uint16_t _height,
                 bool _hasMips,
                 std::uint16_t _numLayers,
                 bgfx::TextureFormat::Enum _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const bgfx::Memory* _mem /*= nullptr */)
    : texture(texture_desc{.size = {_width, _height},
                           .format = _format,
                           .flags = _flags,
                           .has_mips = _hasMips,
                           .layers = _numLayers,
                           .kind = texture_kind::tex_2d},
              _mem)
{
}

texture::texture(std::uint16_t _width,
                 std::uint16_t _height,
                 std::uint16_t _depth,
                 bool _hasMips,
                 bgfx::TextureFormat::Enum _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const bgfx::Memory* _mem /*= nullptr */)
    : texture(texture_desc{.size = {_width, _height},
                           .format = _format,
                           .flags = _flags,
                           .has_mips = _hasMips,
                           .depth = _depth,
                           .kind = texture_kind::tex_3d},
              _mem)
{
}

texture::texture(std::uint16_t _size,
                 bool _hasMips,
                 std::uint16_t _numLayers,
                 bgfx::TextureFormat::Enum _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const bgfx::Memory* _mem /*= nullptr */)
    : texture(texture_desc{.size = {_size, _size},
                           .format = _format,
                           .flags = _flags,
                           .has_mips = _hasMips,
                           .layers = _numLayers,
                           .kind = texture_kind::tex_cube},
              _mem)
{
}

texture::texture(const texture_desc& desc, const bgfx::Memory* mem) : flags(desc.flags), desc_(desc)
{
    assert((desc.kind == texture_kind::tex_3d || desc.depth == 1) && "Only a 3D texture has depth slices");
    assert((desc.kind != texture_kind::tex_3d || desc.layers == 1) && "A 3D texture has no array layers");
    assert((desc.kind != texture_kind::tex_cube || desc.size.width == desc.size.height) && "Cube faces are square");
    calc_desc_info(info, desc);
    const std::uint64_t estimated_size = estimate_texture_gpu_size(info, desc.flags);
    if(is_tracked_desc(desc, mem))
    {
        create_tracked(desc, mem, estimated_size);
        return;
    }
    const eviction::reclaim_kind kind = texture_reclaim_kind(desc.flags);
    if(try_make_room_for(estimated_size, get_kind_label(desc.kind), kind))
    {
        handle_ = create_desc_handle(desc, mem);
    }
    if(has_gpu_copy() && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }
}

auto texture::is_tracked_desc(const texture_desc& desc, const bgfx::Memory* mem) -> bool
{
    // The GPU writes a render target or a compute or blit destination after creation, so its initial
    // contents could not bring back what it holds: it stays resident.
    return mem != nullptr && eviction::is_supported() && !is_gpu_generated_flags(desc.flags);
}

void texture::create_tracked(const texture_desc& desc, const bgfx::Memory* mem, std::uint64_t estimated_size)
{
    // The backing copies the contents before mem goes to bgfx, which frees it once uploaded.
    eviction::backing_buffer backing = eviction::make_backing(mem->data, mem->size);
    // Created whether or not its bytes fit: bgfx frees mem only through the create it goes to, and the
    // frame pass gives the memory back from idle resources.
    const eviction::admission admitted = eviction::admit_allocation(estimated_size, eviction::admit_limit::hard_limit);
    if(!admitted.admitted)
    {
        gfx::log("warning",
                 std::string("The ") + get_kind_label(desc.kind) + " (" + format_bytes(estimated_size) +
                     ") is created past the GPU memory budget because its contents come with it.",
                 __FILE__,
                 __LINE__);
    }
    handle_ = create_desc_handle(desc, mem);
    auto restore = [backing, desc](texture& self) -> bool
    {
        self.handle_ = create_desc_handle(desc, eviction::make_backing_ref(backing));
        return self.has_gpu_copy();
    };
    if(has_gpu_copy())
    {
        make_evictable(estimated_size, std::move(restore), evict_class::evictable, admitted.reserved_bytes);
    }
    else
    {
        make_evictable_deferred(estimated_size, std::move(restore), evict_class::evictable, admitted.reserved_bytes);
    }
}

texture::~texture()
{
    leave_eviction();
}

void texture::load_image(const void* data,
                         std::uint32_t size,
                         std::uint64_t load_flags,
                         std::uint8_t skip,
                         const char* name)
{
    bx::Error err;
    ImagePtr image = imageParseShared(data, size, &err);
    if(!image)
    {
        return;
    }
    const auto base_skip = std::min<std::uint8_t>(skip, static_cast<std::uint8_t>(image->m_numMips - 1));
    if(!imageGetTextureInfo(*image, base_skip, info))
    {
        return;
    }
    const std::uint64_t create_flags = imageGetTextureFlags(*image, load_flags);
    const std::string label = name != nullptr ? name : "";
    if(!eviction::is_supported())
    {
        // Nothing keeps the image: bgfx frees it once uploaded.
        handle_ = createTextureFromImage(image, create_flags, base_skip);
        name_handle(handle_, label);
        return;
    }

    auto source = std::make_shared<image_source>();
    source->image = std::move(image);
    source->flags = create_flags;
    source->base_skip = base_skip;
    source->name = label;
    source_ = std::move(source);

    level_count_ = static_cast<std::uint8_t>(std::min<std::size_t>(info.numMips, level_bytes_.size()));
    for(std::uint8_t level = 0; level < level_count_; ++level)
    {
        bgfx::TextureInfo level_info;
        if(imageGetTextureInfo(*source_->image, static_cast<std::uint8_t>(base_skip + level), level_info))
        {
            level_bytes_[level] = estimate_texture_gpu_size(level_info, load_flags);
        }
    }
    can_reduce_ = is_single_2d_with_mips(info) && !is_gpu_generated_flags(load_flags);
    adopt_image_eviction(label.c_str());
}

void texture::adopt_image_eviction(const char* name)
{
    const std::string what = std::string("texture ") + name;
    if(is_gpu_generated_flags(source_->flags))
    {
        // The GPU writes it after creation, so the image could not bring back what it holds: it stays
        // resident and untracked, and makes room now as a render target does.
        try_make_room_for(level_bytes_[0], what.c_str(), eviction::reclaim_kind::immediate);
        handle_ = create_level(0);
        if(has_gpu_copy())
        {
            eviction::note_pending_allocation(level_bytes_[0]);
        }
        source_.reset();
        return;
    }

    gpu_size_ = level_bytes_[0];
    evict_class_ = evict_class::evictable;
    const load_start start = admit_load_start(what);
    if(start.create)
    {
        handle_ = create_level(start.level);
    }
    if(has_gpu_copy())
    {
        eviction::register_resource(this, start.level, start.reserved_bytes);
    }
    else
    {
        eviction::register_evicted_resource(this, start.reserved_bytes);
    }
}

auto texture::admit_load_start(const std::string& what) const -> load_start
{
    const std::uint8_t tail = get_tail_level(eviction::get_load_tail_size());
    const auto full_limit = tail != 0 ? eviction::admit_limit::soft_budget : eviction::admit_limit::hard_limit;
    const eviction::admission full = eviction::admit_allocation(level_bytes_[0], full_limit);
    if(full.admitted)
    {
        return {0, true, full.reserved_bytes};
    }
    if(tail != 0)
    {
        const eviction::admission tail_only = eviction::admit_allocation(level_bytes_[tail], eviction::admit_limit::hard_limit);
        if(tail_only.admitted)
        {
            return {tail, true, tail_only.reserved_bytes};
        }
    }
    gfx::log("warning",
             "The " + what + " (" + format_bytes(level_bytes_[0]) +
                 ") does not fit in GPU memory now. It is created when a draw first needs it.",
             __FILE__,
             __LINE__);
    return {};
}

auto texture::create_level(std::uint8_t level) const -> bgfx::TextureHandle
{
    if(!source_ || level >= level_count_)
    {
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::TextureHandle created =
        createTextureFromImage(source_->image, source_->flags, static_cast<std::uint8_t>(source_->base_skip + level));
    name_handle(created, source_->name);
    return created;
}

auto texture::on_set_level(std::uint8_t level) -> bool
{
    if(!source_)
    {
        return handle_impl::on_set_level(level);
    }
    const bgfx::TextureHandle created = create_level(level);
    if(!bgfx::isValid(created))
    {
        return false;
    }
    // bgfx runs destroys after the frame's draws and frees handles at frame end, so a draw this frame
    // that bound the old copy still reads it.
    if(has_gpu_copy())
    {
        bgfx::destroy(handle_);
    }
    handle_ = created;
    return true;
}

auto texture::gpu_size_at(std::uint8_t level) const -> std::uint64_t
{
    if(!source_)
    {
        return handle_impl::gpu_size_at(level);
    }
    return level < level_count_ ? level_bytes_[level] : 0;
}

auto texture::get_tail_level(std::uint32_t tail_size) const -> std::uint8_t
{
    if(!can_reduce_)
    {
        return 0;
    }
    // Direct3D wants whole blocks in the top mip of a BC texture, its only block-compressed kind (4x4).
    // ASTC's other blocks run on Vulkan and Metal, which take any size.
    const bimg::ImageBlockInfo& block = bimg::getBlockInfo(bimg::TextureFormat::Enum(info.format));
    const bool has_4x4_blocks = block.blockWidth == 4 && block.blockHeight == 4;
    const std::uint32_t block_side = has_4x4_blocks ? 4 : 1;
    return residency::calc_tail_level(info.width, info.height, level_count_, tail_size, block_side, block_side);
}

auto texture::get_desc() const -> const texture_desc&
{
    return desc_;
}

auto texture::get_size() const -> usize32_t
{
    return {static_cast<std::uint32_t>(info.width), static_cast<std::uint32_t>(info.height)};
}

auto texture::get_normal_map_channels() const -> normal_map_channels
{
    return gfx::get_normal_map_channels(info.format, is_normal_map_);
}

void texture::set_normal_map(bool is_normal_map)
{
    is_normal_map_ = is_normal_map;
}

auto texture::is_render_target() const -> bool
{
    return 0 != (flags & BGFX_TEXTURE_RT_MASK);
}

auto texture::is_gpu_generated() const -> bool
{
    return is_gpu_generated_flags(flags);
}

auto texture::fallback_handle() const -> bgfx::TextureHandle
{
    return gfx::fallback_texture();
}

auto texture::get_estimated_gpu_size() const -> std::uint64_t
{
    return estimate_texture_gpu_size(info, flags);
}
} // namespace gfx
