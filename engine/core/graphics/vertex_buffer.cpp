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
    make_evictable_from(backing, _decl, _flags);
}

vertex_buffer::vertex_buffer(const eviction::backing_buffer& _backing,
                             const bgfx::VertexLayout& _decl,
                             std::uint16_t _flags /*= BGFX_BUFFER_NONE*/)
{
    if(!_backing)
    {
        return;
    }
    handle_ = bgfx::createVertexBuffer(eviction::make_backing_ref(_backing), _decl, _flags);
    make_evictable_from(_backing, _decl, _flags);
}

void vertex_buffer::make_evictable_from(const eviction::backing_buffer& backing,
                                        const bgfx::VertexLayout& decl,
                                        std::uint16_t flags)
{
    if(!is_valid())
    {
        return;
    }
    make_evictable(backing->size(),
                   [backing, decl, flags](vertex_buffer& self) -> bool
                   {
                       const bgfx::Memory* mem = eviction::make_backing_ref(backing);
                       self.handle_ = bgfx::createVertexBuffer(mem, decl, flags);
                       return self.is_valid();
                   });
}
} // namespace gfx
