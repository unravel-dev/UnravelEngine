#include "vertex_buffer.h"

#include <memory>
#include <vector>

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
    const auto* bytes = static_cast<const std::uint8_t*>(_mem->data);
    auto backing = std::make_shared<std::vector<std::uint8_t>>(bytes, bytes + _mem->size);
    handle_ = create_vertex_buffer(_mem, _decl, _flags);
    if(is_valid())
    {
        make_evictable(backing->size(),
                       [backing, decl = _decl, flags = _flags](vertex_buffer& self) -> bool
                       {
                           const memory_view* mem =
                               gfx::copy(backing->data(), static_cast<std::uint32_t>(backing->size()));
                           self.handle_ = create_vertex_buffer(mem, decl, flags);
                           return self.is_valid();
                       });
    }
}
} // namespace gfx
