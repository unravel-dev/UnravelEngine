/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "../common.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include <cmath>
#include <stddef.h> // offsetof
#include <wchar.h>  // wcslen

#include "../cube_atlas.h"
#include "text_buffer_manager.h"
#include "utf8.h"

#include <tinystl/allocator.h>
#include <tinystl/unordered_map.h>
#include <tinystl/vector.h>
namespace stl = tinystl;

#include "fs_font_basic.bin.h"
#include "fs_font_distance_field.bin.h"
#include "fs_font_distance_field_drop_shadow.bin.h"
#include "fs_font_distance_field_drop_shadow_image.bin.h"
#include "fs_font_distance_field_outline.bin.h"
#include "fs_font_distance_field_outline_drop_shadow_image.bin.h"
#include "fs_font_distance_field_outline_image.bin.h"
#include "fs_font_distance_field_subpixel.bin.h"
#include "vs_font_basic.bin.h"
#include "vs_font_distance_field.bin.h"
#include "vs_font_distance_field_drop_shadow.bin.h"
#include "vs_font_distance_field_drop_shadow_image.bin.h"
#include "vs_font_distance_field_outline.bin.h"
#include "vs_font_distance_field_outline_drop_shadow_image.bin.h"
#include "vs_font_distance_field_outline_image.bin.h"
#include "vs_font_distance_field_subpixel.bin.h"

namespace gfx
{

static const bgfx::EmbeddedShader s_embedded_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_font_basic),
    BGFX_EMBEDDED_SHADER(fs_font_basic),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_subpixel),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_subpixel),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_outline),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_outline),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_outline_image),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_outline_image),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_drop_shadow),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_drop_shadow),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_drop_shadow_image),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_drop_shadow_image),
    BGFX_EMBEDDED_SHADER(vs_font_distance_field_outline_drop_shadow_image),
    BGFX_EMBEDDED_SHADER(fs_font_distance_field_outline_drop_shadow_image),

    BGFX_EMBEDDED_SHADER_END()};

class text_buffer
{
public:
    text_buffer(font_manager* manager) : font_manager_(manager)
    {
        size_t initial(8192 - 5);
        resize_buffers(initial);
    }

    void resize_buffers(size_t max_buffered_characters)
    {
        vertex_buffer_.resize(max_buffered_characters * 4);
        index_buffer_.resize(max_buffered_characters * 6);
        style_buffer_.resize(max_buffered_characters * 4);
    }

    auto get_max_buffered_characters() const -> size_t
    {
        return vertex_buffer_.size() / 4;
    }

    auto get_outline_color() -> uint32_t
    {
        return outline_color_;
    }

    auto get_outline_width() -> float
    {
        return outline_width_;
    }

    auto get_drop_shadow_color() -> uint32_t
    {
        return drop_shadow_color_;
    }

    auto get_drop_shadow_offset_u() -> float
    {
        return drop_shadow_offset_[0];
    }

    auto get_drop_shadow_offset_v() -> float
    {
        return drop_shadow_offset_[1];
    }

    auto get_drop_shadow_softener() -> float
    {
        return drop_shadow_softener_;
    }

    void set_style(uint32_t flags = style_normal)
    {
        style_flags_ = flags;
    }

    void set_text_color(uint32_t rgba = 0xff000000)
    {
        text_color_ = rgba;
    }

    void set_background_color(uint32_t rgba = 0xff000000)
    {
        background_color_ = rgba;
    }

    void set_foreground_color(uint32_t rgba = 0xff000000)
    {
        foreground_color_ = rgba;
    }

    void set_overline_color(uint32_t rgba = 0xff000000)
    {
        overline_color_ = rgba;
    }

    void set_underline_color(uint32_t rgba = 0xff000000)
    {
        underline_color_ = rgba;
    }

    void set_strike_through_color(uint32_t rgba = 0xff000000)
    {
        strike_through_color_ = rgba;
    }

    void set_outline_color(uint32_t rgba = 0xff000000)
    {
        outline_color_ = rgba;
    }

    void set_outline_width(float outline_width = 3.0f)
    {
        outline_width_ = outline_width;
    }

    void set_drop_shadow_color(uint32_t rgba = 0xff000000)
    {
        drop_shadow_color_ = rgba;
    }

    void set_drop_shadow_offset(float u, float v)
    {
        drop_shadow_offset_[0] = u;
        drop_shadow_offset_[1] = v;
    }

    void set_drop_shadow_softener(float smoother)
    {
        drop_shadow_softener_ = smoother;
    }

    void set_pen_position(float x, float y)
    {
        pen_x_ = x;
        pen_y_ = y;
    }

    void get_pen_position(float* x, float* y)
    {
        *x = pen_x_;
        *y = pen_y_;
    }

    void set_pen_origin(float x, float y)
    {
        origin_x_ = x;
        origin_y_ = y;
    }

    void set_apply_kerning(bool apply_kerning)
    {
        apply_kerning_ = apply_kerning;
    }

    void append_text(font_handle font_handle, const char* str, const char* end = nullptr);

    void append_text(font_handle font_handle, const wchar_t* str, const wchar_t* end = nullptr);

    void append_atlas_face(font_handle font_handle, uint16_t face_index);

    void clear_text_buffer();

    auto get_vertex_buffer() -> const uint8_t*
    {
        return (uint8_t*)vertex_buffer_.data();
    }

    auto get_vertex_count() const -> uint32_t
    {
        return vertex_count_;
    }

    auto get_vertex_size() const -> uint32_t
    {
        return sizeof(text_vertex);
    }

    auto get_index_buffer() const -> const uint16_t*
    {
        return index_buffer_.data();
    }

    auto get_buffers_dirty() const -> bool
    {
        return buffers_dirty_;
    }

    void set_buffers_dirty(bool dirty)
    {
        buffers_dirty_ = dirty;
    }

    auto get_index_count() const -> uint32_t
    {
        return index_count_;
    }

    auto get_index_size() const -> uint32_t
    {
        return sizeof(uint16_t);
    }

    auto get_text_color() const -> uint32_t
    {
        return text_color_;
    }

    auto get_rectangle() const -> text_rectangle
    {
        return rectangle_;
    }

private:
    void append_glyph(font_handle handle, code_point codepoint, bool shadow);
    void vertical_center_last_line(float dy, float top, float bottom);

    static auto to_abgr(uint32_t rgba) -> uint32_t
    {
        return (((rgba >> 0) & 0xff) << 24) | (((rgba >> 8) & 0xff) << 16) | (((rgba >> 16) & 0xff) << 8) |
               (((rgba >> 24) & 0xff) << 0);
    }

    void set_vertex(uint32_t i, float x, float y, uint32_t rgba, uint8_t style = style_normal)
    {
        vertex_buffer_[i].x = x;
        vertex_buffer_[i].y = y;
        vertex_buffer_[i].rgba = rgba;
        style_buffer_[i] = style;
    }

    void set_outline_color(uint32_t i, uint32_t rgba_outline)
    {
        vertex_buffer_[i].rgba_outline = rgba_outline;
    }

    struct text_vertex
    {
        float x{}, y{};
        int16_t u{}, v{}, w{}, t{};
        int16_t u1{}, v1{}, w1{}, t1{};
        int16_t u2{}, v2{}, w2{}, t2{};
        uint32_t rgba{};
        uint32_t rgba_outline{};
    };

    uint32_t style_flags_{style_normal};

    uint32_t text_color_{0xffffffff};
    uint32_t background_color_{0x00000000};
    uint32_t foreground_color_{0x00000000};
    uint32_t overline_color_{0xffffffff};
    uint32_t underline_color_{0xffffffff};
    uint32_t strike_through_color_{0xffffffff};

    float outline_width_{0.0f};
    uint32_t outline_color_{0xff000000};

    float drop_shadow_offset_[2] = {0.00f, 0.00f};
    uint32_t drop_shadow_color_{0xff000000};
    float drop_shadow_softener_{1.0f};

    bool apply_kerning_{true};
    float pen_x_{};
    float pen_y_{};

    float origin_x_{};
    float origin_y_{};

    float line_ascender_{};
    float line_descender_{};
    float line_gap_{};

    code_point previous_code_point_{};

    text_rectangle rectangle_{};
    font_manager* font_manager_;

    stl::vector<text_vertex> vertex_buffer_;
    stl::vector<uint16_t> index_buffer_;
    stl::vector<uint8_t> style_buffer_;

    bool buffers_dirty_{};

    uint32_t index_count_{};
    uint32_t line_start_index_{};
    uint16_t vertex_count_{};
};


void text_buffer::append_text(font_handle handle, const char* str, const char* end)
{
    if(vertex_count_ == 0)
    {
        line_descender_ = 0;
        line_ascender_ = 0;
        line_gap_ = 0;
        previous_code_point_ = 0;
    }

    code_point codepoint = 0;
    uint32_t state = 0;

    if(end == nullptr)
    {
        end = str + bx::strLen(str);
    }
    BX_ASSERT(end >= str, "");

    const font_info& font = font_manager_->get_font_info(handle);
    if(font.font_type & FONT_TYPE_MASK_DISTANCE_DROP_SHADOW)
    {
        float save_pen_x = pen_x_;
        float save_pen_y = pen_y_;
        code_point save_previous_code_point = previous_code_point_;
        text_rectangle save_rectangle = rectangle_;

        const char* orig_string = str;
        for(; *str && str < end; ++str)
        {
            if(utf8_decode(&state, &codepoint, *str) == UTF8_ACCEPT)
            {
                append_glyph(handle, codepoint, true);
            }
        }
        str = orig_string;

        pen_x_ = save_pen_x;
        pen_y_ = save_pen_y;
        previous_code_point_ = save_previous_code_point;
        rectangle_ = save_rectangle;
    }

    for(; *str && str < end; ++str)
    {
        if(utf8_decode(&state, &codepoint, *str) == UTF8_ACCEPT)
        {
            append_glyph(handle, codepoint, false);
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");
}

void text_buffer::append_text(font_handle handle, const wchar_t* str, const wchar_t* end)
{
    if(vertex_count_ == 0)
    {
        line_descender_ = 0;
        line_ascender_ = 0;
        line_gap_ = 0;
        previous_code_point_ = 0;
    }

    if(end == nullptr)
    {
        end = str + wcslen(str);
    }
    BX_ASSERT(end >= str, "");

    const font_info& font = font_manager_->get_font_info(handle);
    if(font.font_type & FONT_TYPE_MASK_DISTANCE_DROP_SHADOW)
    {
        float save_pen_x = pen_x_;
        float save_pen_y = pen_y_;
        code_point save_previous_code_point = previous_code_point_;
        text_rectangle save_rectangle = rectangle_;

        for(const wchar_t* current = str; current < end; ++current)
        {
            code_point code_point = *current;
            append_glyph(handle, code_point, true);
        }

        pen_x_ = save_pen_x;
        pen_y_ = save_pen_y;
        previous_code_point_ = save_previous_code_point;
        rectangle_ = save_rectangle;
    }

    for(const wchar_t* current = str; current < end; ++current)
    {
        code_point code_point = *current;
        append_glyph(handle, code_point, false);
    }
}

void text_buffer::append_atlas_face(font_handle handle, uint16_t face_index)
{
    if(vertex_count_ / 4 >= get_max_buffered_characters())
    {
        //background
        size_t max_quads_per_glyph = 1;
        size_t capacity_growth = 10;
        resize_buffers(get_max_buffered_characters() + max_quads_per_glyph * capacity_growth);
    }

    const Atlas* atlas = font_manager_->get_atlas(handle);

    float x0 = pen_x_;
    float y0 = pen_y_;
    float x1 = x0 + (float)atlas->getTextureSize();
    float y1 = y0 + (float)atlas->getTextureSize();

    atlas->packFaceLayerUV(face_index,
                           (uint8_t*)vertex_buffer_.data(),
                           sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                           sizeof(text_vertex));

    set_vertex(vertex_count_ + 0, x0, y0, background_color_);
    set_vertex(vertex_count_ + 1, x0, y1, background_color_);
    set_vertex(vertex_count_ + 2, x1, y1, background_color_);
    set_vertex(vertex_count_ + 3, x1, y0, background_color_);

    index_buffer_[index_count_ + 0] = vertex_count_ + 0;
    index_buffer_[index_count_ + 1] = vertex_count_ + 1;
    index_buffer_[index_count_ + 2] = vertex_count_ + 2;
    index_buffer_[index_count_ + 3] = vertex_count_ + 0;
    index_buffer_[index_count_ + 4] = vertex_count_ + 2;
    index_buffer_[index_count_ + 5] = vertex_count_ + 3;
    vertex_count_ += 4;
    index_count_ += 6;
    buffers_dirty_ = true;

    pen_x_ += x1 - x0;
}

void text_buffer::clear_text_buffer()
{
    pen_x_ = 0;
    pen_y_ = 0;
    origin_x_ = 0;
    origin_y_ = 0;

    vertex_count_ = 0;
    index_count_ = 0;
    line_start_index_ = 0;
    line_ascender_ = 0;
    line_descender_ = 0;
    line_gap_ = 0;
    previous_code_point_ = 0;
    rectangle_.width = 0;
    rectangle_.height = 0;
}

void text_buffer::append_glyph(font_handle handle, code_point codepoint, bool shadow)
{
    if(codepoint == L'\t')
    {
        for(uint32_t ii = 0; ii < 4; ++ii)
        {
            append_glyph(handle, L' ', shadow);
        }
        return;
    }

    const glyph_info* glyph = font_manager_->get_glyph_info(handle, codepoint);
    BX_WARN(NULL != glyph, "Glyph not found (font handle %d, code point %d)", handle.idx, codepoint);
    if(nullptr == glyph)
    {
        previous_code_point_ = 0;
        return;
    }

    if(vertex_count_ / 4 >= get_max_buffered_characters())
    {
        // background, shadow, underline, overline, glyph, foreground
        size_t max_quads_per_glyph = 6;
        size_t capacity_growth = 100;
        resize_buffers(get_max_buffered_characters() + max_quads_per_glyph * capacity_growth);
    }

    const font_info& font = font_manager_->get_font_info(handle);

    if(codepoint == L'\n')
    {
        line_gap_ = font.line_gap;
        line_descender_ = font.descender;
        line_ascender_ = font.ascender;
        line_start_index_ = vertex_count_;
        previous_code_point_ = 0;
        pen_x_ = origin_x_;
        pen_y_ += line_gap_ + line_ascender_ - line_descender_;
        return;
    }

    // is there a change of font size that require the text on the left to be centered again ?
    if(font.ascender > line_ascender_ || (font.descender < line_descender_))
    {
        if(font.descender < line_descender_)
        {
            line_descender_ = font.descender;
            line_gap_ = font.line_gap;
        }

        float txtDecals = (font.ascender - line_ascender_);
        line_ascender_ = font.ascender;
        line_gap_ = font.line_gap;
        vertical_center_last_line((txtDecals),
                                  (pen_y_ - line_ascender_),
                                  (pen_y_ + line_ascender_ - line_descender_ + line_gap_));
    }

    float kerning = 0.0f;
    if(apply_kerning_)
    {
        kerning = font_manager_->get_kerning(handle, previous_code_point_, codepoint);
        pen_x_ += kerning;
    }

    const glyph_info& whiteGlyph = font_manager_->get_white_glyph(handle);
    const Atlas* atlas = font_manager_->get_atlas(handle);
    const AtlasRegion& atlasRegion = atlas->getRegion(glyph->region_index);

    const bool is_drop_shadow_font =
        (font_manager_->get_font_info(handle).font_type & FONT_TYPE_MASK_DISTANCE_DROP_SHADOW) != 0;

    if((shadow || !is_drop_shadow_font) && style_flags_ & style_background && background_color_ & 0xff000000)
    {
        float x0 = pen_x_ - kerning;
        float y0 = pen_y_;
        float x1 = x0 + glyph->advance_x;
        float y1 = pen_y_ + line_ascender_ - line_descender_ + line_gap_;

        atlas->packUV(whiteGlyph.region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        set_vertex(vertex_count_ + 0, x0, y0, background_color_, style_background);
        set_vertex(vertex_count_ + 1, x0, y1, background_color_, style_background);
        set_vertex(vertex_count_ + 2, x1, y1, background_color_, style_background);
        set_vertex(vertex_count_ + 3, x1, y0, background_color_, style_background);

        index_buffer_[index_count_ + 0] = vertex_count_ + 0;
        index_buffer_[index_count_ + 1] = vertex_count_ + 1;
        index_buffer_[index_count_ + 2] = vertex_count_ + 2;
        index_buffer_[index_count_ + 3] = vertex_count_ + 0;
        index_buffer_[index_count_ + 4] = vertex_count_ + 2;
        index_buffer_[index_count_ + 5] = vertex_count_ + 3;
        vertex_count_ += 4;
        index_count_ += 6;
        buffers_dirty_ = true;

    }

    if(shadow)
    {
        if(atlasRegion.getType() != AtlasRegion::TYPE_BGRA8)
        {
            float extra_x_offset = drop_shadow_offset_[0] * font.scale;
            float extra_y_offset = drop_shadow_offset_[1] * font.scale;

            uint32_t adjusted_drop_shadow_color =
                ((((drop_shadow_color_ & 0xff000000) >> 8) * (text_color_ >> 24)) & 0xff000000) |
                (drop_shadow_color_ & 0x00ffffff);

            const uint8_t shadowA = (drop_shadow_color_ >> 24) & 0xFF;

            if(shadowA > 0 || std::fabs(extra_x_offset) > 1e-6f || std::fabs(extra_y_offset) > 1e-6f)
            {
                float x0 = pen_x_ + glyph->offset_x + extra_x_offset;
                float y0 = pen_y_ + line_ascender_ + glyph->offset_y + extra_y_offset;
                float x1 = x0 + glyph->width;
                float y1 = y0 + glyph->height;

                bx::memSet(&vertex_buffer_[vertex_count_], 0, sizeof(text_vertex) * 4);

                atlas->packUV(glyph->region_index,
                              (uint8_t*)vertex_buffer_.data(),
                              sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u2),
                              sizeof(text_vertex));

                set_vertex(vertex_count_ + 0, x0, y0, adjusted_drop_shadow_color);
                set_vertex(vertex_count_ + 1, x0, y1, adjusted_drop_shadow_color);
                set_vertex(vertex_count_ + 2, x1, y1, adjusted_drop_shadow_color);
                set_vertex(vertex_count_ + 3, x1, y0, adjusted_drop_shadow_color);

                index_buffer_[index_count_ + 0] = vertex_count_ + 0;
                index_buffer_[index_count_ + 1] = vertex_count_ + 1;
                index_buffer_[index_count_ + 2] = vertex_count_ + 2;
                index_buffer_[index_count_ + 3] = vertex_count_ + 0;
                index_buffer_[index_count_ + 4] = vertex_count_ + 2;
                index_buffer_[index_count_ + 5] = vertex_count_ + 3;
                vertex_count_ += 4;
                index_count_ += 6;
                buffers_dirty_ = true;

            }
        }

        pen_x_ += glyph->advance_x;

        float lineWidth = pen_x_ - origin_x_;
        if(lineWidth > rectangle_.width)
        {
            rectangle_.width = lineWidth;
        }

        float lineHeight = pen_y_ + line_ascender_ - line_descender_ + line_gap_;
        if(lineHeight > rectangle_.height)
        {
            rectangle_.height = lineHeight;
        }

        previous_code_point_ = codepoint;

        return;
    }

    if(style_flags_ & style_underline && underline_color_ & 0xFF000000)
    {
        float x0 = pen_x_ - kerning;
        float y0 = pen_y_ + line_ascender_ - line_descender_ * 0.5f;
        float x1 = x0 + glyph->advance_x;
        float y1 = y0 + font.underline_thickness;

        atlas->packUV(whiteGlyph.region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        set_vertex(vertex_count_ + 0, x0, y0, underline_color_, style_underline);
        set_vertex(vertex_count_ + 1, x0, y1, underline_color_, style_underline);
        set_vertex(vertex_count_ + 2, x1, y1, underline_color_, style_underline);
        set_vertex(vertex_count_ + 3, x1, y0, underline_color_, style_underline);

        index_buffer_[index_count_ + 0] = vertex_count_ + 0;
        index_buffer_[index_count_ + 1] = vertex_count_ + 1;
        index_buffer_[index_count_ + 2] = vertex_count_ + 2;
        index_buffer_[index_count_ + 3] = vertex_count_ + 0;
        index_buffer_[index_count_ + 4] = vertex_count_ + 2;
        index_buffer_[index_count_ + 5] = vertex_count_ + 3;
        vertex_count_ += 4;
        index_count_ += 6;
        buffers_dirty_ = true;

    }

    if(style_flags_ & style_overline && overline_color_ & 0xFF000000)
    {
        float x0 = pen_x_ - kerning;
        float y0 = pen_y_;
        float x1 = x0 + glyph->advance_x;
        float y1 = y0 + font.underline_thickness;

        atlas->packUV(whiteGlyph.region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        set_vertex(vertex_count_ + 0, x0, y0, overline_color_, style_overline);
        set_vertex(vertex_count_ + 1, x0, y1, overline_color_, style_overline);
        set_vertex(vertex_count_ + 2, x1, y1, overline_color_, style_overline);
        set_vertex(vertex_count_ + 3, x1, y0, overline_color_, style_overline);

        index_buffer_[index_count_ + 0] = vertex_count_ + 0;
        index_buffer_[index_count_ + 1] = vertex_count_ + 1;
        index_buffer_[index_count_ + 2] = vertex_count_ + 2;
        index_buffer_[index_count_ + 3] = vertex_count_ + 0;
        index_buffer_[index_count_ + 4] = vertex_count_ + 2;
        index_buffer_[index_count_ + 5] = vertex_count_ + 3;
        vertex_count_ += 4;
        index_count_ += 6;
        buffers_dirty_ = true;

    }

    if(!shadow && atlasRegion.getType() == AtlasRegion::TYPE_BGRA8)
    {
        bx::memSet(&vertex_buffer_[vertex_count_], 0, sizeof(text_vertex) * 4);

        atlas->packUV(glyph->region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u1),
                      sizeof(text_vertex));

        float glyph_scale = glyph->bitmap_scale;
        float glyph_width = glyph->width * glyph_scale;
        float glyph_height = glyph->height * glyph_scale;
        float x0 = pen_x_ + glyph->offset_x;
        float y0 = pen_y_ + (font.ascender + -font.descender - glyph_height) / 2;
        float x1 = x0 + glyph_width;
        float y1 = y0 + glyph_height;

        set_vertex(vertex_count_ + 0, x0, y0, text_color_);
        set_vertex(vertex_count_ + 1, x0, y1, text_color_);
        set_vertex(vertex_count_ + 2, x1, y1, text_color_);
        set_vertex(vertex_count_ + 3, x1, y0, text_color_);
    }
    else if(!shadow)
    {
        bx::memSet(&vertex_buffer_[vertex_count_], 0, sizeof(text_vertex) * 4);

        atlas->packUV(glyph->region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        float x0 = pen_x_ + glyph->offset_x;
        float y0 = pen_y_ + line_ascender_ + glyph->offset_y;
        float x1 = x0 + glyph->width;
        float y1 = y0 + glyph->height;

        set_vertex(vertex_count_ + 0, x0, y0, text_color_);
        set_vertex(vertex_count_ + 1, x0, y1, text_color_);
        set_vertex(vertex_count_ + 2, x1, y1, text_color_);
        set_vertex(vertex_count_ + 3, x1, y0, text_color_);

        set_outline_color(vertex_count_ + 0, outline_color_);
        set_outline_color(vertex_count_ + 1, outline_color_);
        set_outline_color(vertex_count_ + 2, outline_color_);
        set_outline_color(vertex_count_ + 3, outline_color_);
    }

    index_buffer_[index_count_ + 0] = vertex_count_ + 0;
    index_buffer_[index_count_ + 1] = vertex_count_ + 1;
    index_buffer_[index_count_ + 2] = vertex_count_ + 2;
    index_buffer_[index_count_ + 3] = vertex_count_ + 0;
    index_buffer_[index_count_ + 4] = vertex_count_ + 2;
    index_buffer_[index_count_ + 5] = vertex_count_ + 3;
    vertex_count_ += 4;
    index_count_ += 6;
    buffers_dirty_ = true;


    if(style_flags_ & style_foreground && foreground_color_ & 0xff000000)
    {
        float x0 = pen_x_ - kerning;
        float y0 = pen_y_;
        float x1 = x0 + glyph->advance_x;
        float y1 = pen_y_ + line_ascender_ - line_descender_ + line_gap_;

        atlas->packUV(whiteGlyph.region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        set_vertex(vertex_count_ + 0, x0, y0, foreground_color_, style_foreground);
        set_vertex(vertex_count_ + 1, x0, y1, foreground_color_, style_foreground);
        set_vertex(vertex_count_ + 2, x1, y1, foreground_color_, style_foreground);
        set_vertex(vertex_count_ + 3, x1, y0, foreground_color_, style_foreground);

        index_buffer_[index_count_ + 0] = vertex_count_ + 0;
        index_buffer_[index_count_ + 1] = vertex_count_ + 1;
        index_buffer_[index_count_ + 2] = vertex_count_ + 2;
        index_buffer_[index_count_ + 3] = vertex_count_ + 0;
        index_buffer_[index_count_ + 4] = vertex_count_ + 2;
        index_buffer_[index_count_ + 5] = vertex_count_ + 3;
        vertex_count_ += 4;
        index_count_ += 6;
        buffers_dirty_ = true;

    }

    if(style_flags_ & style_strike_through && strike_through_color_ & 0xFF000000)
    {
        float x0 = pen_x_ - kerning;
        float y0 = pen_y_ + 0.666667f * font.ascender;
        float x1 = x0 + glyph->advance_x;
        float y1 = y0 + font.underline_thickness;

        atlas->packUV(whiteGlyph.region_index,
                      (uint8_t*)vertex_buffer_.data(),
                      sizeof(text_vertex) * vertex_count_ + offsetof(text_vertex, u),
                      sizeof(text_vertex));

        set_vertex(vertex_count_ + 0, x0, y0, strike_through_color_, style_strike_through);
        set_vertex(vertex_count_ + 1, x0, y1, strike_through_color_, style_strike_through);
        set_vertex(vertex_count_ + 2, x1, y1, strike_through_color_, style_strike_through);
        set_vertex(vertex_count_ + 3, x1, y0, strike_through_color_, style_strike_through);

        index_buffer_[index_count_ + 0] = vertex_count_ + 0;
        index_buffer_[index_count_ + 1] = vertex_count_ + 1;
        index_buffer_[index_count_ + 2] = vertex_count_ + 2;
        index_buffer_[index_count_ + 3] = vertex_count_ + 0;
        index_buffer_[index_count_ + 4] = vertex_count_ + 2;
        index_buffer_[index_count_ + 5] = vertex_count_ + 3;
        vertex_count_ += 4;
        index_count_ += 6;
        buffers_dirty_ = true;

    }

    pen_x_ += glyph->advance_x;

    float line_width = pen_x_ - origin_x_;
    if(line_width > rectangle_.width)
    {
        rectangle_.width = line_width;
    }

    float line_height = pen_y_ + line_ascender_ - line_descender_ + line_gap_;
    if(line_height > rectangle_.height)
    {
        rectangle_.height = line_height;
    }

    previous_code_point_ = codepoint;
}

void text_buffer::vertical_center_last_line(float dy, float top, float bottom)
{
    for(uint32_t ii = line_start_index_; ii < vertex_count_; ii += 4)
    {
        if(style_buffer_[ii] == style_background)
        {
            vertex_buffer_[ii + 0].y = top;
            vertex_buffer_[ii + 1].y = bottom;
            vertex_buffer_[ii + 2].y = bottom;
            vertex_buffer_[ii + 3].y = top;
        }
        else
        {
            vertex_buffer_[ii + 0].y += dy;
            vertex_buffer_[ii + 1].y += dy;
            vertex_buffer_[ii + 2].y += dy;
            vertex_buffer_[ii + 3].y += dy;
        }
    }
}

text_buffer_manager::text_buffer_manager(font_manager* manager) : font_manager_(manager)
{
    bgfx::RendererType::Enum type = bgfx::getRendererType();

    basic_program_ = bgfx::createProgram(bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_basic"),
                                         bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_basic"),
                                         true);

    distance_program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field"),
                            bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field"),
                            true);

    distance_subpixel_program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_subpixel"),
                            bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_subpixel"),
                            true);

    distance_drop_shadow_program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_drop_shadow"),
                            bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_drop_shadow"),
                            true);

    distance_drop_shadow_image_program_ = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_drop_shadow_image"),
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_drop_shadow_image"),
        true);

    distance_outline_program_ =
        bgfx::createProgram(bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_outline"),
                            bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_outline"),
                            true);

    distance_outline_image_program_ = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_outline_image"),
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_outline_image"),
        true);

    distance_outline_drop_shadow_image_program_ = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "vs_font_distance_field_outline_drop_shadow_image"),
        bgfx::createEmbeddedShader(s_embedded_shaders, type, "fs_font_distance_field_outline_drop_shadow_image"),
        true);

    vertex_layout_.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Int16, true)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Int16, true)
        .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Int16, true)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
        .end();

    s_tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    u_drop_shadow_color_ = bgfx::createUniform("u_dropShadowColor", bgfx::UniformType::Vec4);
    u_params_ = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
}

text_buffer_manager::~text_buffer_manager()
{
    BX_ASSERT(text_buffer_handles_.getNumHandles() == 0,
              "All the text buffers must be destroyed before destroying the manager");
    delete[] text_buffers_;

    bgfx::destroy(u_params_);

    bgfx::destroy(u_drop_shadow_color_);
    bgfx::destroy(s_tex_color_);

    bgfx::destroy(basic_program_);
    bgfx::destroy(distance_program_);
    bgfx::destroy(distance_subpixel_program_);
    bgfx::destroy(distance_outline_program_);
    bgfx::destroy(distance_outline_image_program_);
    bgfx::destroy(distance_drop_shadow_program_);
    bgfx::destroy(distance_drop_shadow_image_program_);
    bgfx::destroy(distance_outline_drop_shadow_image_program_);
}

auto text_buffer_manager::create_text_buffer(uint32_t type, buffer_type::Enum btype) -> text_buffer_handle
{
    uint16_t text_idx = text_buffer_handles_.alloc();
    buffer_cache& bc = text_buffers_[text_idx];

    bc.buffer = new text_buffer(font_manager_);
    bc.font_type = type;
    bc.type = btype;
    bc.index_buffer_handle_idx = bgfx::kInvalidHandle;
    bc.vertex_buffer_handle_idx = bgfx::kInvalidHandle;

    text_buffer_handle ret = {text_idx};
    return ret;
}

void text_buffer_manager::destroy_text_buffer(text_buffer_handle handle)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");

    buffer_cache& bc = text_buffers_[handle.idx];
    text_buffer_handles_.free(handle.idx);
    delete bc.buffer;
    bc.buffer = nullptr;

    if(bc.vertex_buffer_handle_idx == bgfx::kInvalidHandle)
    {
        return;
    }

    switch(bc.type)
    {
        case buffer_type::Static:
        {
            bgfx::IndexBufferHandle ibh;
            bgfx::VertexBufferHandle vbh;
            ibh.idx = bc.index_buffer_handle_idx;
            vbh.idx = bc.vertex_buffer_handle_idx;
            bgfx::destroy(ibh);
            bgfx::destroy(vbh);
        }

        break;

        case buffer_type::Dynamic:
            bgfx::DynamicIndexBufferHandle ibh;
            bgfx::DynamicVertexBufferHandle vbh;
            ibh.idx = bc.index_buffer_handle_idx;
            vbh.idx = bc.vertex_buffer_handle_idx;
            bgfx::destroy(ibh);
            bgfx::destroy(vbh);

            break;

        case buffer_type::Transient: // destroyed every frame
            break;
    }
}

void text_buffer_manager::submit_text_buffer(text_buffer_handle handle,
                                             font_handle fhandle,
                                             bgfx::ViewId id,
                                             uint64_t state,
                                             int32_t depth)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");

    buffer_cache& bc = text_buffers_[handle.idx];

    uint32_t index_size = bc.buffer->get_index_count() * bc.buffer->get_index_size();
    uint32_t vertex_size = bc.buffer->get_vertex_count() * bc.buffer->get_vertex_size();

    if(0 == index_size || 0 == vertex_size)
    {
        return;
    }

    bgfx::setTexture(0, s_tex_color_, font_manager_->get_atlas(fhandle)->getTextureHandle());

    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    switch(bc.font_type)
    {
        case FONT_TYPE_ALPHA:
            program = basic_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            break;

        case FONT_TYPE_DISTANCE:
        {
            program = distance_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            float params[4] = {0.0f, (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        case FONT_TYPE_DISTANCE_SUBPIXEL:
            program = distance_subpixel_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                               BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_INV_SRC_COLOR),
                           bc.buffer->get_text_color());
            break;

        case FONT_TYPE_DISTANCE_OUTLINE:
        {
            program = distance_outline_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            float params[4] = {0.0f,
                               (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f,
                               0.0f,
                               bc.buffer->get_outline_width()};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        case FONT_TYPE_DISTANCE_OUTLINE_IMAGE:
        {
            program = distance_outline_image_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            float params[4] = {0.0f,
                               (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f,
                               0.0f,
                               bc.buffer->get_outline_width()};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        case FONT_TYPE_DISTANCE_DROP_SHADOW:
        {
            program = distance_drop_shadow_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            uint32_t drop_shadow_color = bc.buffer->get_drop_shadow_color();
            float drop_shadow_color_vec[4] = {((drop_shadow_color >> 16) & 0xff) / 255.0f,
                                              ((drop_shadow_color >> 8) & 0xff) / 255.0f,
                                              (drop_shadow_color & 0xff) / 255.0f,
                                              (drop_shadow_color >> 24) / 255.0f};
            bgfx::setUniform(u_drop_shadow_color_, &drop_shadow_color_vec);

            float params[4] = {0.0f,
                               (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f,
                               bc.buffer->get_drop_shadow_softener(),
                               0.0};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        case FONT_TYPE_DISTANCE_DROP_SHADOW_IMAGE:
        {
            program = distance_drop_shadow_image_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            uint32_t drop_shadow_color = bc.buffer->get_drop_shadow_color();
            float drop_shadow_color_vec[4] = {((drop_shadow_color >> 16) & 0xff) / 255.0f,
                                              ((drop_shadow_color >> 8) & 0xff) / 255.0f,
                                              (drop_shadow_color & 0xff) / 255.0f,
                                              (drop_shadow_color >> 24) / 255.0f};
            bgfx::setUniform(u_drop_shadow_color_, &drop_shadow_color_vec);

            float params[4] = {0.0f,
                               (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f,
                               bc.buffer->get_drop_shadow_softener(),
                               0.0};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        case FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE:
        {
            program = distance_outline_drop_shadow_image_program_;
            bgfx::setState(state | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_RGB |
                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));

            uint32_t drop_shadow_color = bc.buffer->get_drop_shadow_color();
            float drop_shadow_color_vec[4] = {((drop_shadow_color >> 16) & 0xff) / 255.0f,
                                              ((drop_shadow_color >> 8) & 0xff) / 255.0f,
                                              (drop_shadow_color & 0xff) / 255.0f,
                                              (drop_shadow_color >> 24) / 255.0f};
            bgfx::setUniform(u_drop_shadow_color_, &drop_shadow_color_vec);

            float params[4] = {0.0f,
                               (float)font_manager_->get_atlas(fhandle)->getTextureSize() / 512.0f,
                               bc.buffer->get_drop_shadow_softener(),
                               bc.buffer->get_outline_width()};
            bgfx::setUniform(u_params_, &params);
            break;
        }

        default:
            break;
    }

    switch(bc.type)
    {
        case buffer_type::Static:
        {
            bgfx::IndexBufferHandle ibh;
            bgfx::VertexBufferHandle vbh;

            if(bgfx::kInvalidHandle == bc.vertex_buffer_handle_idx)
            {
                ibh = bgfx::createIndexBuffer(bgfx::copy(bc.buffer->get_index_buffer(), index_size));

                vbh = bgfx::createVertexBuffer(bgfx::copy(bc.buffer->get_vertex_buffer(), vertex_size), vertex_layout_);

                bc.vertex_buffer_handle_idx = vbh.idx;
                bc.index_buffer_handle_idx = ibh.idx;
            }
            else
            {
                vbh.idx = bc.vertex_buffer_handle_idx;
                ibh.idx = bc.index_buffer_handle_idx;
            }

            bgfx::setVertexBuffer(0, vbh, 0, bc.buffer->get_vertex_count());
            bgfx::setIndexBuffer(ibh, 0, bc.buffer->get_index_count());
        }
        break;

        case buffer_type::Dynamic:
        {
            bgfx::DynamicIndexBufferHandle ibh;
            bgfx::DynamicVertexBufferHandle vbh;

            if(bgfx::kInvalidHandle == bc.vertex_buffer_handle_idx)
            {
                ibh = bgfx::createDynamicIndexBuffer(bgfx::copy(bc.buffer->get_index_buffer(), index_size),
                                                     BGFX_BUFFER_ALLOW_RESIZE);

                vbh = bgfx::createDynamicVertexBuffer(bgfx::copy(bc.buffer->get_vertex_buffer(), vertex_size),
                                                      vertex_layout_,
                                                      BGFX_BUFFER_ALLOW_RESIZE);

                bc.index_buffer_handle_idx = ibh.idx;
                bc.vertex_buffer_handle_idx = vbh.idx;
            }
            else if(bc.buffer->get_buffers_dirty())
            {
                ibh.idx = bc.index_buffer_handle_idx;
                vbh.idx = bc.vertex_buffer_handle_idx;

                bgfx::update(ibh, 0, bgfx::copy(bc.buffer->get_index_buffer(), index_size));

                bgfx::update(vbh, 0, bgfx::copy(bc.buffer->get_vertex_buffer(), vertex_size));
            }

            ibh.idx = bc.index_buffer_handle_idx;
            vbh.idx = bc.vertex_buffer_handle_idx;

            bgfx::setVertexBuffer(0, vbh, 0, bc.buffer->get_vertex_count());
            bgfx::setIndexBuffer(ibh, 0, bc.buffer->get_index_count());
        }
        break;

        case buffer_type::Transient:
        {
            bgfx::TransientIndexBuffer tib;
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientIndexBuffer(&tib, bc.buffer->get_index_count());
            bgfx::allocTransientVertexBuffer(&tvb, bc.buffer->get_vertex_count(), vertex_layout_);
            bx::memCopy(tib.data, bc.buffer->get_index_buffer(), index_size);
            bx::memCopy(tvb.data, bc.buffer->get_vertex_buffer(), vertex_size);
            bgfx::setVertexBuffer(0, &tvb, 0, bc.buffer->get_vertex_count());
            bgfx::setIndexBuffer(&tib, 0, bc.buffer->get_index_count());
        }
        break;
    }

    bgfx::submit(id, program, depth);

    bc.buffer->set_buffers_dirty(false);
}

void text_buffer_manager::set_style(text_buffer_handle handle, uint32_t flags)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_style(flags);
}

void text_buffer_manager::set_text_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_text_color(rgba);
}

void text_buffer_manager::set_background_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_background_color(rgba);
}

void text_buffer_manager::set_foreground_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_foreground_color(rgba);
}

void text_buffer_manager::set_overline_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_overline_color(rgba);
}

void text_buffer_manager::set_underline_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_underline_color(rgba);
}

void text_buffer_manager::set_strike_through_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_strike_through_color(rgba);
}

void text_buffer_manager::set_outline_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_outline_color(rgba);
}

void text_buffer_manager::set_outline_width(text_buffer_handle handle, float outline_width)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_outline_width(outline_width);
}

void text_buffer_manager::set_drop_shadow_color(text_buffer_handle handle, uint32_t rgba)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_drop_shadow_color(rgba);
}

void text_buffer_manager::set_drop_shadow_offset(text_buffer_handle handle, float u, float v)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_drop_shadow_offset(u, v);
}

void text_buffer_manager::set_drop_shadow_softener(text_buffer_handle handle, float smoother)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_drop_shadow_softener(smoother);
}

void text_buffer_manager::set_pen_position(text_buffer_handle handle, float x, float y)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_pen_position(x, y);
}

void text_buffer_manager::set_pen_origin(text_buffer_handle handle, float x, float y)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_pen_origin(x, y);
}

void text_buffer_manager::get_pen_position(text_buffer_handle handle, float* x, float* y)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->get_pen_position(x, y);
}

void text_buffer_manager::set_apply_kerning(text_buffer_handle handle, bool apply_kerning)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->set_apply_kerning(apply_kerning);
}

void text_buffer_manager::append_text(text_buffer_handle handle,
                                      font_handle fhandle,
                                      const char* _string,
                                      const char* _end)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->append_text(fhandle, _string, _end);
}

void text_buffer_manager::append_text(text_buffer_handle handle,
                                      font_handle fhandle,
                                      const wchar_t* _string,
                                      const wchar_t* _end)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->append_text(fhandle, _string, _end);
}

void text_buffer_manager::append_atlas_face(text_buffer_handle handle, font_handle fhandle, uint16_t _faceIndex)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->append_atlas_face(fhandle, _faceIndex);
}

void text_buffer_manager::clear_text_buffer(text_buffer_handle handle)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    bc.buffer->clear_text_buffer();
}

auto text_buffer_manager::get_rectangle(text_buffer_handle handle) const -> text_rectangle
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    buffer_cache& bc = text_buffers_[handle.idx];
    return bc.buffer->get_rectangle();
}

} // namespace gfx
