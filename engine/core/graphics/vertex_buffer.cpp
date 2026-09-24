#include "vertex_buffer.h"

namespace gfx
{

vertex_buffer::vertex_buffer(const bgfx::Memory* _mem,
                             const bgfx::VertexLayout& _decl,
                             std::uint16_t _flags /*= BGFX_BUFFER_NONE*/)
{
    if(_mem == nullptr)
    {
        return;
    }
    if(!gfx::eviction::is_supported())
    {
        handle_ = bgfx::createVertexBuffer(_mem, _decl, _flags);
        return;
    }
    eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
    handle_ = bgfx::createVertexBuffer(_mem, _decl, _flags);
    if(is_valid())
    {
        make_evictable(backing->size(),
                       [backing, decl = _decl, flags = _flags](vertex_buffer& self) -> bool
                       {
                           const bgfx::Memory* mem = eviction::make_backing_ref(backing);
                           self.handle_ = bgfx::createVertexBuffer(mem, decl, flags);
                           return self.is_valid();
                       });
    }
}
} // namespace gfx
