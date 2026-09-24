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
};
} // namespace gfx
