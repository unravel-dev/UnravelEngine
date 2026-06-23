#include "texture.h"
#include "utils/bgfx_utils.h"
#include <memory>
#include <string>
#include <vector>
namespace gfx
{
namespace
{
using backing_buffer = std::shared_ptr<std::vector<std::uint8_t>>;

auto make_backing(const void* data, std::uint32_t size) -> backing_buffer
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    return std::make_shared<std::vector<std::uint8_t>>(bytes, bytes + size);
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
            backing_buffer backing = make_backing(data, size);
            make_evictable(info.storageSize,
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
        backing_buffer backing = make_backing(_data, _size);
        std::string name = (_name != nullptr) ? _name : std::string{};
        make_evictable(info.storageSize,
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

    // GPU-produced textures (render targets / compute writes) have no CPU backing to evict, so make
    // headroom before allocating. They are created on the graphics API thread, so the synchronous
    // reclaim is safe and keeps large allocations from failing when near the GPU memory limit.
    if(is_gpu_generated())
    {
        gfx::eviction::reclaim_for(estimate_texture_gpu_size(info, _flags));
    }

    handle_ = create_texture_2d(_width, _height, _hasMips, _numLayers, _format, _flags, _mem);

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        backing_buffer backing = make_backing(_mem->data, _mem->size);
        make_evictable(estimate_texture_gpu_size(info, _flags),
                       [backing,
                        width = _width,
                        height = _height,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem =
                               gfx::copy(backing->data(), static_cast<std::uint32_t>(backing->size()));
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

    // GPU-produced textures (render targets / compute writes) have no CPU backing to evict, so make
    // headroom before allocating. They are created on the graphics API thread, so the synchronous
    // reclaim is safe and keeps large allocations from failing when near the GPU memory limit.
    if(is_gpu_generated())
    {
        gfx::eviction::reclaim_for(estimate_texture_gpu_size(info, _flags));
    }

    handle_ = create_texture_3d(_width, _height, _depth, _hasMips, _format, _flags, _mem);

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        backing_buffer backing = make_backing(_mem->data, _mem->size);
        make_evictable(estimate_texture_gpu_size(info, _flags),
                       [backing,
                        width = _width,
                        height = _height,
                        depth = _depth,
                        has_mips = _hasMips,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem =
                               gfx::copy(backing->data(), static_cast<std::uint32_t>(backing->size()));
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

    // GPU-produced textures (render targets / compute writes) have no CPU backing to evict, so make
    // headroom before allocating. They are created on the graphics API thread, so the synchronous
    // reclaim is safe and keeps large allocations from failing when near the GPU memory limit.
    if(is_gpu_generated())
    {
        gfx::eviction::reclaim_for(estimate_texture_gpu_size(info, _flags));
    }

    handle_ = create_texture_cube(_size, _hasMips, _numLayers, _format, _flags, _mem);

    if(_mem != nullptr && is_valid() && !is_gpu_generated() && gfx::eviction::is_supported())
    {
        backing_buffer backing = make_backing(_mem->data, _mem->size);
        make_evictable(estimate_texture_gpu_size(info, _flags),
                       [backing,
                        size = _size,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags](texture& self) -> bool
                       {
                           const memory_view* mem =
                               gfx::copy(backing->data(), static_cast<std::uint32_t>(backing->size()));
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
