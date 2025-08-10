/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef TEXT_BUFFER_MANAGER_H_HEADER_GUARD
#define TEXT_BUFFER_MANAGER_H_HEADER_GUARD

#include "font_manager.h"

namespace gfx
{
BGFX_HANDLE(text_buffer_handle)

#define MAX_TEXT_BUFFER_COUNT 1024

/// type of vertex and index buffer to use with a TextBuffer
struct buffer_type
{
    enum Enum
    {
        Static,
        Dynamic,
        Transient,
    };
};

/// special style effect (can be combined)
enum text_style_flags
{
    style_normal = 0,
    style_overline = 1,
    style_underline = 1 << 1,
    style_strike_through = 1 << 2,
    style_background = 1 << 3,
    style_foreground = 1 << 4,
};

struct text_rectangle
{
    float width{}, height{};
};

class text_buffer;
class text_buffer_manager
{
public:
    text_buffer_manager(font_manager* font_manager);
    ~text_buffer_manager();

    auto create_text_buffer(uint32_t type, buffer_type::Enum btype) -> text_buffer_handle;
    void destroy_text_buffer(text_buffer_handle handle);
    void submit_text_buffer(text_buffer_handle handle,
                            font_handle fhandle,
                            bgfx::ViewId id,
                            uint64_t state = 0,
                            int32_t depth = 0);

    void set_style(text_buffer_handle handle, uint32_t flags = style_normal);
    void set_text_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);
    void set_background_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);
    void set_foreground_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);

    void set_overline_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);
    void set_underline_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);
    void set_strike_through_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);

    void set_outline_width(text_buffer_handle handle, float outline_width = 3.0f);
    void set_outline_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);

    void set_drop_shadow_offset(text_buffer_handle handle, float u, float v);
    void set_drop_shadow_color(text_buffer_handle handle, uint32_t rgba = 0x000000FF);
    void set_drop_shadow_softener(text_buffer_handle handle, float smoother = 1.0f);

    void set_pen_origin(text_buffer_handle handle, float x, float y);
    void set_pen_position(text_buffer_handle handle, float x, float y);
    void get_pen_position(text_buffer_handle handle, float* x, float* y);

    void set_apply_kerning(text_buffer_handle handle, bool apply_kerning);

    /// Append an ASCII/utf-8 string to the buffer using current pen position and color.
    void append_text(text_buffer_handle handle, font_handle fhandle, const char* string, const char* end = nullptr);

    /// Append a wide char unicode string to the buffer using current pen position and color.
    void append_text(text_buffer_handle handle,
                     font_handle fhandle,
                     const wchar_t* string,
                     const wchar_t* end = nullptr);

    /// Append a whole face of the atlas cube, mostly used for debugging and visualizing atlas.
    void append_atlas_face(text_buffer_handle handle, font_handle fhandle, uint16_t face_index);

    /// Clear the text buffer and reset its state (pen/color).
    void clear_text_buffer(text_buffer_handle handle);

    /// Return the rectangular size of the current text buffer (including all its content).
    auto get_rectangle(text_buffer_handle handle) const -> text_rectangle;

private:
    struct buffer_cache
    {
        uint16_t index_buffer_handle_idx{};
        uint16_t vertex_buffer_handle_idx{};
        text_buffer* buffer{};
        buffer_type::Enum type{};
        uint32_t font_type{};
    };

    buffer_cache* text_buffers_ = new buffer_cache[MAX_TEXT_BUFFER_COUNT];
    bx::HandleAllocT<MAX_TEXT_BUFFER_COUNT> text_buffer_handles_;
    font_manager* font_manager_{};
    bgfx::VertexLayout vertex_layout_;
    bgfx::UniformHandle s_tex_color_;
    bgfx::UniformHandle u_drop_shadow_color_;
    bgfx::UniformHandle u_params_;
    bgfx::ProgramHandle basic_program_;
    bgfx::ProgramHandle distance_program_;
    bgfx::ProgramHandle distance_subpixel_program_;
    bgfx::ProgramHandle distance_outline_program_;
    bgfx::ProgramHandle distance_outline_image_program_;
    bgfx::ProgramHandle distance_drop_shadow_program_;
    bgfx::ProgramHandle distance_drop_shadow_image_program_;
    bgfx::ProgramHandle distance_outline_drop_shadow_image_program_;
};
} // namespace gfx
#endif // TEXT_BUFFER_MANAGER_H_HEADER_GUARD
