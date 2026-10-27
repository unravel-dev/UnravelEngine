#include "index_buffer.h"

namespace gfx
{

index_buffer::index_buffer(const bgfx::Memory* _mem, std::uint16_t _flags /*= BGFX_BUFFER_NONE*/)
{
    if(_mem == nullptr)
    {
        return;
    }
    if(!gfx::eviction::is_supported())
    {
        handle_ = bgfx::createIndexBuffer(_mem, _flags);
        return;
    }
    eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
    handle_ = bgfx::createIndexBuffer(_mem, _flags);
    make_evictable_from(backing, _flags);
}

index_buffer::index_buffer(const eviction::backing_buffer& _backing, std::uint16_t _flags /*= BGFX_BUFFER_NONE*/)
{
    if(!_backing)
    {
        return;
    }
    handle_ = bgfx::createIndexBuffer(eviction::make_backing_ref(_backing), _flags);
    make_evictable_from(_backing, _flags);
}

void index_buffer::make_evictable_from(const eviction::backing_buffer& backing, std::uint16_t flags)
{
    if(!is_valid())
    {
        return;
    }
    make_evictable(backing->size(),
                   [backing, flags](index_buffer& self) -> bool
                   {
                       const bgfx::Memory* mem = eviction::make_backing_ref(backing);
                       self.handle_ = bgfx::createIndexBuffer(mem, flags);
                       return self.is_valid();
                   });
}
} // namespace gfx
