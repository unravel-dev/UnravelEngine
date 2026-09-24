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
    if(is_valid())
    {
        make_evictable(backing->size(),
                       [backing, flags = _flags](index_buffer& self) -> bool
                       {
                           const bgfx::Memory* mem = eviction::make_backing_ref(backing);
                           self.handle_ = bgfx::createIndexBuffer(mem, flags);
                           return self.is_valid();
                       });
    }
}
} // namespace gfx
