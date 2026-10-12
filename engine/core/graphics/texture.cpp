#include "texture.h"
#include "utils/bgfx_utils.h"

#include <cassert>
#include <limits>
#include <string>
#include <utility>

namespace gfx
{
namespace
{

auto clamp_non_negative(std::int64_t bytes) -> std::uint64_t
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, bytes));
}

void write_formatted(std::uint64_t bytes, char* out, std::size_t out_size, std::uint8_t num_frac)
{
    if(out == nullptr || out_size == 0)
    {
        return;
    }
    if(num_frac == 0)
    {
        bx::prettify(out, static_cast<int32_t>(out_size), bytes, bx::Units::KibiByte);
        return;
    }
    const bx::FixedStringT<32> formatted = bx::toHuman<32>(bytes, bx::Units::KibiByte, num_frac);
    bx::strCopy(out, static_cast<int32_t>(out_size), formatted.getCPtr());
}

auto format_bytes(std::uint64_t bytes, std::uint8_t num_frac = 2) -> std::string
{
    std::array<char, 32> buffer{};
    write_formatted(bytes, buffer.data(), buffer.size(), num_frac);
    return buffer.data();
}

auto format_bytes(std::int64_t bytes, std::uint8_t num_frac = 2) -> std::string
{
    return format_bytes(clamp_non_negative(bytes), num_frac);
}

void format_bytes(std::uint64_t bytes, char* out, std::size_t out_size, std::uint8_t num_frac)
{
    write_formatted(bytes, out, out_size, num_frac);
}
/// Returns whether the caller should proceed with the GPU allocation.
/// Evictable (CPU-backed loads): @ref eviction::would_allocation_fit only — no sweep.
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
        const std::string msg = std::string("Eviction: Insufficient headroom for ") + what + " (" +
                                format_bytes(bytes) + "); Skipping allocation (fallback texture).";
        gfx::log("warning", msg, __FILE__, __LINE__);
        return false;
    }
    const auto result = eviction::reclaim_for(bytes, kind);
    if(result != eviction::reclaim_result::insufficient)
    {
        return true;
    }
    const std::string msg = std::string("Eviction: Insufficient headroom for ") + what + " (" +
                            format_bytes(bytes) + "); Allocation may fail.";
    gfx::log("warning", msg, __FILE__, __LINE__);
    return true;
}

auto texture_reclaim_kind(std::uint64_t flags) -> eviction::reclaim_kind
{
    const bool gpu_generated = 0 != (flags & (BGFX_TEXTURE_RT_MASK | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_BLIT_DST));
    return gpu_generated ? eviction::reclaim_kind::immediate : eviction::reclaim_kind::evictable;
}

auto make_load_precreate(std::uint64_t flags, const char* what) -> TexturePreCreateFn
{
    const eviction::reclaim_kind kind = texture_reclaim_kind(flags);
    return [flags, what, kind](const bgfx::TextureInfo& info) -> bool
    {
        return try_make_room_for(estimate_texture_gpu_size(info, flags), what, kind);
    };
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
} // namespace

void texture::adopt_loaded_eviction(eviction::backing_buffer backing,
                                      std::uint64_t estimated_size,
                                      std::uint64_t load_flags,
                                      std::uint8_t skip,
                                      const char* label)
{
    auto restore = [backing, load_flags, skip, label = std::string(label)](texture& self) -> bool
    {
        bx::Error e;
        self.handle_ = loadTexture(backing->data(),
                                   static_cast<std::uint32_t>(backing->size()),
                                   load_flags,
                                   skip,
                                   &self.info,
                                   nullptr,
                                   label.c_str(),
                                   &e);
        return self.is_valid();
    };
    if(is_valid())
    {
        make_evictable(estimated_size, std::move(restore));
    }
    else
    {
        make_evictable_deferred(estimated_size, std::move(restore));
    }
}

texture::texture(const char* _path,
                 std::uint64_t _flags,
                 std::uint8_t _skip /*= 0 */,
                 bgfx::TextureInfo* _info /*= nullptr*/)
{
    bx::Error err;
    std::uint32_t size = 0;
    void* data = load(bx::FilePath(_path), &size);
    if(data != nullptr)
    {
        if(gfx::eviction::is_supported())
        {
            eviction::backing_buffer backing = eviction::make_backing(data, size);
            handle_ = loadTexture(data, size, _flags, _skip, &info, nullptr, _path, &err, make_load_precreate(_flags, _path));
            adopt_loaded_eviction(backing,
                                  estimate_texture_gpu_size(info, _flags),
                                  _flags,
                                  _skip,
                                  _path);
        }
        else
        {
            handle_ = loadTexture(data, size, _flags, _skip, &info, nullptr, _path, &err);
        }
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
    bx::Error err;
    const char* label = (_name != nullptr) ? _name : "memory texture";
    if(gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_data, _size);
        handle_ = loadTexture(_data, _size, _flags, _skip, &info, nullptr, _name, &err, make_load_precreate(_flags, label));
        adopt_loaded_eviction(backing, estimate_texture_gpu_size(info, _flags), _flags, _skip, label);
    }
    else
    {
        handle_ = loadTexture(_data, _size, _flags, _skip, &info, nullptr, _name, &err);
    }

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
    const eviction::reclaim_kind kind = texture_reclaim_kind(desc.flags);
    const bool can_allocate = try_make_room_for(estimated_size, get_kind_label(desc.kind), kind);
    if(can_allocate)
    {
        handle_ = create_desc_handle(desc, mem);
    }
    const bool valid = is_valid();
    if(valid && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }
    if(mem == nullptr || !gfx::eviction::is_supported())
    {
        return;
    }
    eviction::backing_buffer backing = eviction::make_backing(mem->data, mem->size);
    auto restore = [backing, desc](texture& self) -> bool
    {
        self.handle_ = create_desc_handle(desc, eviction::make_backing_ref(backing));
        return self.is_valid();
    };
    if(valid)
    {
        make_evictable(estimated_size, std::move(restore));
    }
    else
    {
        make_evictable_deferred(estimated_size, std::move(restore));
    }
}

auto texture::get_desc() const -> const texture_desc&
{
    return desc_;
}

auto texture::get_size() const -> usize32_t
{
    return {static_cast<std::uint32_t>(info.width), static_cast<std::uint32_t>(info.height)};
}

auto texture::is_render_target() const -> bool
{
    return 0 != (flags & BGFX_TEXTURE_RT_MASK);
}

auto texture::is_gpu_generated() const -> bool
{
    return 0 != (flags & (BGFX_TEXTURE_RT_MASK | BGFX_TEXTURE_COMPUTE_WRITE | BGFX_TEXTURE_BLIT_DST));
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
