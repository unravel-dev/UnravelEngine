#include "texture.h"
#include "utils/bgfx_utils.h"

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
                 texture_info* _info /*= nullptr*/)
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
}

texture::texture(const void* _data,
                 std::uint32_t _size,
                 std::uint64_t _flags,
                 std::uint8_t _skip,
                 texture_info* _info,
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
    const eviction::reclaim_kind kind = texture_reclaim_kind(_flags);
    const bool can_allocate = try_make_room_for(estimated_size, "2D texture", kind);

    if(can_allocate)
    {
        handle_ = create_texture_2d(_width, _height, _hasMips, _numLayers, _format, _flags, _mem);
    }
    const bool valid = is_valid();
    if(valid && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        auto restore = [backing,
                        width = _width,
                        height = _height,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags,
                        estimated_size](texture& self) -> bool
        {
            const memory_view* mem = eviction::make_backing_ref(backing);
            self.handle_ = create_texture_2d(width, height, has_mips, layers, format, flags, mem);
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
    const eviction::reclaim_kind kind = texture_reclaim_kind(_flags);
    const bool can_allocate = try_make_room_for(estimated_size, "3D texture", kind);

    if(can_allocate)
    {
        handle_ = create_texture_3d(_width, _height, _depth, _hasMips, _format, _flags, _mem);
    }

    const bool valid = is_valid();
    if(valid && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        auto restore = [backing,
                        width = _width,
                        height = _height,
                        depth = _depth,
                        has_mips = _hasMips,
                        format = _format,
                        flags = _flags,
                        estimated_size](texture& self) -> bool
        {
            const memory_view* mem = eviction::make_backing_ref(backing);
            self.handle_ = create_texture_3d(width, height, depth, has_mips, format, flags, mem);
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
    const eviction::reclaim_kind kind = texture_reclaim_kind(_flags);
    const bool can_allocate = try_make_room_for(estimated_size, "cube texture", kind);

    if(can_allocate)
    {
        handle_ = create_texture_cube(_size, _hasMips, _numLayers, _format, _flags, _mem);
    }
    const bool valid = is_valid();
    if(valid && is_gpu_generated())
    {
        gfx::eviction::note_pending_allocation(estimated_size);
    }

    if(_mem != nullptr && gfx::eviction::is_supported())
    {
        eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
        auto restore = [backing,
                        size = _size,
                        has_mips = _hasMips,
                        layers = _numLayers,
                        format = _format,
                        flags = _flags,
                        estimated_size](texture& self) -> bool
        {
            const memory_view* mem = eviction::make_backing_ref(backing);
            self.handle_ = create_texture_cube(size, has_mips, layers, format, flags, mem);
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

auto texture::fallback_handle() const -> texture_handle
{
    return gfx::fallback_texture();
}

auto texture::get_estimated_gpu_size() const -> std::uint64_t
{
    return estimate_texture_gpu_size(info, flags);
}
} // namespace gfx
