#include "uniform.h"

namespace gfx
{
namespace
{
    struct ref_counted_handle
    {
        gfx::uniform_handle handle = {bgfx::kInvalidHandle};
        uint64_t ref_count{};
    };
    struct uniform_cache
    {
        using cache_t = std::unordered_map<std::string, std::unordered_map<gfx::uniform_type, ref_counted_handle>>;
        cache_t cache;
        // std::unordered_map<uint16_t, ref_counted_handle*> lut;
    };

    auto get_uniform_cache() -> uniform_cache&
    {
        static uniform_cache cache;
        return cache;
    }

    auto aquire(const std::string& _name, gfx::uniform_type _type, std::uint16_t _num) -> gfx::uniform_handle
    {
        // auto& cache = get_uniform_cache();
        // auto& by_name = cache.cache[_name];
        // BX_ASSERT(by_name.size() <= 1, "Uniform %s has different types in cache", _name.c_str());
   
        // auto& by_type = by_name[_type];
        // auto& counted_uniform = by_type;

        // if(counted_uniform.ref_count == 0)
        // {
        //     counted_uniform.handle = gfx::create_uniform(_name.c_str(), _type, _num);
        //     // cache.lut[counted_uniform.handle.idx] = &counted_uniform;
        // }

        // counted_uniform.ref_count++;

        // return counted_uniform.handle;
        return gfx::create_uniform(_name.c_str(), _type, _num);
    }

    void release(gfx::uniform_handle _handle)
    {
        // auto& cache = get_uniform_cache();
        // auto& counted_uniform = cache.lut[_handle.idx];
        // counted_uniform->ref_count--;
        // if(counted_uniform->ref_count == 0)
        // {
        //     gfx::destroy(counted_uniform->handle);
        //     cache.lut.erase(counted_uniform->handle.idx);
        // }
    }
}

void deinit_uniform_cache()
{
    auto& cache = get_uniform_cache();
    // BX_ASSERT(cache.lut.empty(), "Uniform cache is not empty");
    // cache.lut.clear();
    cache.cache.clear();
}

uniform::uniform(const std::string& _name, uniform_type _type, std::uint16_t _num /*= 1*/)
{
    // handle_ = gfx::create_uniform(_name.c_str(), _type, _num);
    handle_ = aquire(_name, _type, _num);
    gfx::get_uniform_info(handle_, info);
}

uniform::uniform(handle_type_t _handle)
{
    gfx::get_uniform_info(_handle, info);
    // handle_ = gfx::create_uniform(info.name, info.type, info.num);
    handle_ = aquire(info.name, info.type, info.num);
}

uniform::~uniform()
{
    if(is_valid())
    {
        release(handle_);
    }
    handle_ = invalid_handle();
}


void uniform::set_texture(uint8_t _stage,
                          const gfx::frame_buffer* frameBuffer,
                          uint8_t _attachment /*= 0 */,
                          uint32_t _flags /*= std::numeric_limits<uint32_t>::max()*/)
{
    if(frameBuffer == nullptr)
    {
        return;
    }

    gfx::set_texture(_stage, native_handle(), frameBuffer->get_texture(_attachment)->native_handle(), _flags);
}

void uniform::set_texture(uint8_t _stage,
                          const gfx::texture* _texture,
                          uint32_t _flags /*= std::numeric_limits<uint32_t>::max()*/)
{
    if(_texture == nullptr)
    {
        return;
    }

    gfx::set_texture(_stage, native_handle(), _texture->native_handle(), _flags);
}

void uniform::set_uniform(const void* _value, uint16_t _num)
{
    gfx::set_uniform(native_handle(), _value, _num);
}
} // namespace gfx
