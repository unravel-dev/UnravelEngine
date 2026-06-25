#include "texture.h"
#include "utils/bgfx_utils.h"

#include <string>

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
/// Centralized helper for "I am about to allocate a GPU resource that has no CPU backing": the
/// eviction system gets a chance to free space and we surface its decision to the user. When
/// reclaim cannot find enough headroom we log a warning through the gfx logger (which the engine
/// routes to the same sink bgfx uses) and let the allocation proceed anyway: a bgfx-side OOM is
/// non-fatal (it returns an invalid handle and the engine continues) and we prefer that path over
/// swallowing the allocation here.
void make_room_for(std::uint64_t bytes, const char* what)
{
    const auto result = gfx::eviction::reclaim_for(bytes);
    if(result == gfx::eviction::reclaim_result::insufficient)
    {
        // Once per failure: do not spam, but tell the user clearly so they can correlate the
        // upcoming Vulkan / bgfx error with the eviction system's decision.
        const std::string msg = std::string("Eviction: Insufficient headroom for ") + what + " (" +
                                format_bytes(bytes) +
                                "); Allocation may fail.";
        gfx::log("warning", msg, __FILE__, __LINE__);
    }
}
} // namespace

texture::texture(const char* _path,
                 std::uint64_t _flags,
                 std::uint8_t _skip /*= 0 */,
                 texture_info* _info /*= nullptr*/)
{
    bx::Error err;
    std::uint32_t size = 0;
    void* data = load(bx::FilePath(_path), &size);
    if(data != nullptr)
    {
        handle_ = loadTexture(data, size, _flags, _skip, &info, nullptr, _path, &err);
        if(is_valid() && gfx::eviction::is_supported())
        {
            const std::uint64_t estimated_size = estimate_texture_gpu_size(info, _flags);
            eviction::backing_buffer backing = eviction::make_backing(data, size);
            make_evictable(estimated_size,
                           [backing, flags = _flags, skip = _skip](texture& self) -> bool
                           {
                               bx::Error e;
                               self.handle_ = loadTexture(backing->data(),
                                                          static_cast<std::uint32_t>(backing->size()),
                                                          flags,
                                                          skip,
                                                          &self.info,
                                                          nullptr,
                                                          nullptr,
                                                          &e);
                               return self.is_valid();
                           });
        }
        unload(data);
    }

    if(_info != nullptr)
    {
        *_info = info;
    }

    flags = _flags;
}

texture::texture(const void* _data,
                 std::uint32_t _size,
                 std::uint64_t _flags,
                 std::uint8_t _skip,
                 texture_info* _info,
                 const char* _name)
{
    bx::Error err;
    handle_ = loadTexture(_data, _size, _flags, _skip, &info, nullptr, _name, &err);

    if(is_valid() && gfx::eviction::is_supported())
    {
        const std::uint64_t estimated_size = estimate_texture_gpu_size(info, _flags);
        eviction::backing_buffer backing = eviction::make_backing(_data, _size);
        std::string name = (_name != nullptr) ? _name : std::string{};
        make_evictable(estimated_size,
                       [backing, flags = _flags, skip = _skip, name](texture& self) -> bool
                       {
                           bx::Error e;
                           self.handle_ = loadTexture(backing->data(),
                                                      static_cast<std::uint32_t>(backing->size()),
                                                      flags,
                                                      skip,
                                                      &self.info,
                                                      nullptr,
                                                      name.empty() ? nullptr : name.c_str(),
                                                      &e);
                           return self.is_valid();
                       });
    }

    if(_info != nullptr)
    {
        *_info = info;
    }

    flags = _flags;
}

texture::texture(std::uint16_t _width,
                 std::uint16_t _height,
                 bool _hasMips,
                 std::uint16_t _numLayers,
                 texture_format _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const memory_view* _mem /*= nullptr */)
    : flags(_flags)
{
    calc_texture_size(info, _width, _height, 1, false, _hasMips, _numLayers, _format);
    const std::uint64_t estimated_size = estimate_texture_gpu_size(info, _flags);

    // GPU-produced textures (render targets / compute writes) have no CPU backing to evict, so make
    // headroom before allocating. They are created on the graphics API thread, so the synchronous
    // reclaim is safe and keeps large allocations from failing when near the GPU memory limit.
    if(is_gpu_generated())
    {
        make_room_for(estimated_size, "render-target 2D texture");
    }

    handle_ = create_texture_2d(_width, _height, _hasMips, _numLayers, _format, _flags, _mem);
    if(is_valid() && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        make_evictable(estimated_size,
                       [backing,
                        width = _width,
                        height = _height,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem = eviction::make_backing_ref(backing);
                           self.handle_ =
                               create_texture_2d(width, height, has_mips, layers, format, flags, mem);
                           return self.is_valid();
                       });
    }
}

texture::texture(std::uint16_t _width,
                 std::uint16_t _height,
                 std::uint16_t _depth,
                 bool _hasMips,
                 texture_format _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const memory_view* _mem /*= nullptr */)
    : flags(_flags)
{
    calc_texture_size(info, _width, _height, _depth, false, _hasMips, 1, _format);
    const std::uint64_t estimated_size = estimate_texture_gpu_size(info, _flags);

    if(is_gpu_generated())
    {
        make_room_for(estimated_size, "render-target 3D texture");
    }

    handle_ = create_texture_3d(_width, _height, _depth, _hasMips, _format, _flags, _mem);
    if(is_valid() && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        make_evictable(estimated_size,
                       [backing,
                        width = _width,
                        height = _height,
                        depth = _depth,
                        has_mips = _hasMips,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem = eviction::make_backing_ref(backing);
                           self.handle_ =
                               create_texture_3d(width, height, depth, has_mips, format, flags, mem);
                           return self.is_valid();
                       });
    }
}

texture::texture(std::uint16_t _size,
                 bool _hasMips,
                 std::uint16_t _numLayers,
                 texture_format _format,
                 std::uint64_t _flags /*= BGFX_TEXTURE_NONE */,
                 const memory_view* _mem /*= nullptr */)
    : flags(_flags)
{
    calc_texture_size(info, _size, _size, _size, false, _hasMips, _numLayers, _format);
    const std::uint64_t estimated_size = estimate_texture_gpu_size(info, _flags);

    if(is_gpu_generated())
    {
        make_room_for(estimated_size, "render-target cube texture");
    }

    handle_ = create_texture_cube(_size, _hasMips, _numLayers, _format, _flags, _mem);
    if(is_valid() && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        make_evictable(estimated_size,
                       [backing,
                        size = _size,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem = eviction::make_backing_ref(backing);
                           self.handle_ = create_texture_cube(size, has_mips, layers, format, flags, mem);
                           return self.is_valid();
                       });
    }
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
    return 0 != (flags & (BGFX_TEXTURE_RT_MASK | BGFX_TEXTURE_COMPUTE_WRITE));
}
} // namespace gfx
