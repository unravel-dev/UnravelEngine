#pragma once

#include "handle_impl.h"

namespace gfx
{
struct index_buffer : public handle_impl<index_buffer, bgfx::IndexBufferHandle>
{
    index_buffer() = default;
    //-----------------------------------------------------------------------------
    //  Name : index_buffer ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    index_buffer(const bgfx::Memory* _mem, std::uint16_t _flags = BGFX_BUFFER_NONE);

    /// Creates the buffer from @p _backing without copying it. bgfx reads it on its render thread a frame or two
    /// later and an eviction restore reads it again; each keeps its own reference, so the caller may drop its
    /// own at any time.
    index_buffer(const eviction::backing_buffer& _backing, std::uint16_t _flags = BGFX_BUFFER_NONE);

private:
    /// Registers with the eviction system, recreating the buffer from @p backing on restore.
    void make_evictable_from(const eviction::backing_buffer& backing, std::uint16_t flags);
};
} // namespace gfx
