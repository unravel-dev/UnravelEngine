#include "uniform.h"

namespace gfx
{
namespace
{
    struct ref_counted_handle
    {
        bgfx::UniformHandle handle = {bgfx::kInvalidHandle};
        uint64_t ref_count{};
    };
    struct uniform_cache
    {
        using cache_t =
            std::unordered_map<std::string, std::unordered_map<bgfx::UniformType::Enum, ref_counted_handle>>;
        cache_t cache;
        // std::unordered_map<uint16_t, ref_counted_handle*> lut;
    };

    auto get_uniform_cache() -> uniform_cache&
    {
        static uniform_cache cache;
        return cache;
    }

    auto aquire(const std::string& _name, bgfx::UniformType::Enum _type, std::uint16_t _num) -> bgfx::UniformHandle
    {
        // auto& cache = get_uniform_cache();
        // auto& by_name = cache.cache[_name];
        // BX_ASSERT(by_name.size() <= 1, "Uniform %s has different types in cache", _name.c_str());
   
        // auto& by_type = by_name[_type];
        // auto& counted_uniform = by_type;

        // if(counted_uniform.ref_count == 0)
        // {
        //     counted_uniform.handle = bgfx::createUniform(_name.c_str(), _type, _num);
        //     // cache.lut[counted_uniform.handle.idx] = &counted_uniform;
        // }

        // counted_uniform.ref_count++;

        // return counted_uniform.handle;
        return bgfx::createUniform(_name.c_str(), _type, _num);
    }

    void release(bgfx::UniformHandle _handle)
    {
        // auto& cache = get_uniform_cache();
        // auto& counted_uniform = cache.lut[_handle.idx];
        // counted_uniform->ref_count--;
        // if(counted_uniform->ref_count == 0)
        // {
        //     bgfx::destroy(counted_uniform->handle);
        //     cache.lut.erase(counted_uniform->handle.idx);
        // }

    }
}

void deinit_uniform_cache()
{
    auto& cache = get_uniform_cache();

    for(auto& [name, type_map] : cache.cache)
    {
        for(auto& [type, handle] : type_map)
        {
            bgfx::destroy(handle.handle);
        }
    }
    // BX_ASSERT(cache.lut.empty(), "Uniform cache is not empty");
    // cache.lut.clear();
    cache.cache.clear();
}

uniform::uniform(const std::string& _name, bgfx::UniformType::Enum _type, std::uint16_t _num /*= 1*/)
{
    // handle_ = bgfx::createUniform(_name.c_str(), _type, _num);
    handle_ = aquire(_name, _type, _num);
    bgfx::getUniformInfo(handle_, info);
}

uniform::uniform(handle_type_t _handle)
{
    bgfx::getUniformInfo(_handle, info);
    // handle_ = bgfx::createUniform(info.name, info.type, info.num);
    handle_ = aquire(info.name, info.type, info.num);
}

uniform::~uniform()
{
    if(is_valid())
    {
        release(handle_);
    }
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

    bgfx::setTexture(_stage, native_handle(), frameBuffer->get_texture(_attachment)->native_handle(), _flags);
}

void uniform::set_texture(uint8_t _stage,
                          const gfx::texture* _texture,
                          uint32_t _flags /*= std::numeric_limits<uint32_t>::max()*/)
{
    if(_texture == nullptr)
    {
        return;
    }

    bgfx::setTexture(_stage, native_handle(), _texture->native_handle(), _flags);
}

void uniform::set_uniform(const void* _value, uint16_t _num)
{
    bgfx::setUniform(native_handle(), _value, _num);
}
} // namespace gfx
