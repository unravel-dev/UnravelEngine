#pragma once
#include "frame_buffer.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace gfx
{
struct render_pass
{
    static void push_scope(const char* name);
    static void pop_scope();

    struct scope
    {
        scope(const char* name)
        {
            push_scope(name);
        }
        ~scope()
        {
            pop_scope();
        }
    };

    //-----------------------------------------------------------------------------
    //  Name : render_pass ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    render_pass(const char* name);
    render_pass(bgfx::ViewId id, const char* name);


    //-----------------------------------------------------------------------------
    //  Name : bind ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void bind(const frame_buffer* fb = nullptr) const;
    void touch() const;
    //-----------------------------------------------------------------------------
    //  Name : clear ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void clear(uint16_t _flags, uint32_t _rgba = 0x000000ff, float _depth = 1.0f, uint8_t _stencil = 0) const;

    //-----------------------------------------------------------------------------
    //  Name : clear ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void clear() const;

    //-----------------------------------------------------------------------------
    //  Name : set_view_proj ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void set_view_proj(const float* v, const float* p);

    //-----------------------------------------------------------------------------
    //  Name : set_view_scissor ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void set_view_scissor(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height);
    void set_view_rect(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height);
    //-----------------------------------------------------------------------------
    //  Name : reset ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    static void reset();

    //-----------------------------------------------------------------------------
    //  Name : get_pass ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    static auto get_max_pass_id() -> bgfx::ViewId;

    static auto get_last_frame_max_pass_id() -> bgfx::ViewId;

    /// The render pass ids this frame may still hand out before it runs out of them and is flushed part-way.
    static auto get_passes_left() -> bgfx::ViewId;

    /// The render pass ids this frame has handed out so far.
    static auto get_pass_count() -> bgfx::ViewId;
    ///
    bgfx::ViewId id;
};
} // namespace gfx
