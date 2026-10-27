#pragma once

#include "handle_impl.h"

namespace gfx
{
struct vertex_buffer : public handle_impl<vertex_buffer, bgfx::VertexBufferHandle>
{
    vertex_buffer() = default;
    //-----------------------------------------------------------------------------
    //  Name : vertex_buffer ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    vertex_buffer(const bgfx::Memory* _mem, const bgfx::VertexLayout& _decl, std::uint16_t _flags = BGFX_BUFFER_NONE);

    /// Creates the buffer from @p _backing without copying it. bgfx reads it on its render thread a frame or two
    /// later and an eviction restore reads it again; each keeps its own reference, so the caller may drop its
    /// own at any time.
    vertex_buffer(const eviction::backing_buffer& _backing,
                  const bgfx::VertexLayout& _decl,
                  std::uint16_t _flags = BGFX_BUFFER_NONE);

private:
    /// Registers with the eviction system, recreating the buffer from @p backing on restore.
    void make_evictable_from(const eviction::backing_buffer& backing, const bgfx::VertexLayout& decl, std::uint16_t flags);
};
} // namespace gfx
