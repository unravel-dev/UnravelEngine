#include "vertex_buffer.h"

namespace gfx
{

vertex_buffer::vertex_buffer(const memory_view* _mem,
                             const vertex_layout& _decl,
                             std::uint16_t _flags /*= BGFX_BUFFER_NONE*/)
{
    if(_mem == nullptr)
    {
        return;
    }
    if(!gfx::eviction::is_supported())
    {
        handle_ = create_vertex_buffer(_mem, _decl, _flags);
        return;
    }
    eviction::backing_buffer backing = eviction::make_backing(_mem->data, _mem->size);
    handle_ = create_vertex_buffer(_mem, _decl, _flags);
    if(is_valid())
    {
        make_evictable(backing->size(),
                       [backing, decl = _decl, flags = _flags](vertex_buffer& self) -> bool
                       {
                           const memory_view* mem = eviction::make_backing_ref(backing);
                           self.handle_ = create_vertex_buffer(mem, decl, flags);
                           return self.is_valid();
                       });
    }
}
} // namespace gfx
