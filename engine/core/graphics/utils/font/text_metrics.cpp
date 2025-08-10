/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "text_metrics.h"
#include "utf8.h"

namespace gfx
{
text_metrics::text_metrics(font_manager* manager) : manager_(manager)
{
    clear_text();
}

void text_metrics::clear_text()
{
    width_ = height_ = x_ = line_height_ = line_gap_ = 0;
}
void text_metrics::append_text(font_handle font_handle, const char* string, const char* end)
{
    if(end == nullptr)
    {
        end = string + bx::strLen(string);
    }
    BX_ASSERT(end >= string, "");

    const font_info& font = manager_->get_font_info(font_handle);

    if(font.line_gap > line_gap_)
    {
        line_gap_ = font.line_gap;
    }

    if((font.ascender - font.descender) > line_height_)
    {
        height_ -= line_height_;
        line_height_ = font.ascender - font.descender;
        height_ += line_height_;
    }

    code_point codepoint = 0;
    code_point previous_codepoint = 0;

    uint32_t state = 0;

    for(; *string && string < end; ++string)
    {
        if(!utf8_decode(&state, &codepoint, *string))
        {
            const glyph_info* glyph = manager_->get_glyph_info(font_handle, codepoint);
            if(nullptr != glyph)
            {
                if(codepoint == L'\n')
                {
                    height_ += line_gap_ + font.ascender - font.descender;
                    line_gap_ = font.line_gap;
                    line_height_ = font.ascender - font.descender;
                    x_ = 0;
                }

                float kerning = manager_->get_kerning(font_handle, previous_codepoint, codepoint);
                x_ += kerning + glyph->advance_x;
                if(x_ > width_)
                {
                    width_ = x_;
                }
            }
            else
            {
                BX_ASSERT(false, "Glyph not found");
            }

            previous_codepoint = codepoint;
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");
}

text_line_metrics::text_line_metrics(const font_info& info)
    : line_height_(info.ascender - info.descender + info.line_gap)
{
}

auto text_line_metrics::get_line_count(const bx::StringView& str) const -> uint32_t
{
    code_point codepoint = 0;
    uint32_t state = 0;
    uint32_t line_count = 1;
    for(const char* ptr = str.getPtr(); ptr != str.getTerm(); ++ptr)
    {
        if(utf8_decode(&state, &codepoint, *ptr) == UTF8_ACCEPT)
        {
            if(codepoint == L'\n')
            {
                ++line_count;
            }
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");
    return line_count;
}

void text_line_metrics::get_sub_text(const bx::StringView& str,
                                     uint32_t first_line,
                                     uint32_t last_line,
                                     const char*& begin,
                                     const char*& end)
{
    code_point codepoint = 0;
    uint32_t state = 0;
    uint32_t current_line = 0;

    const char* ptr = str.getPtr();

    while(ptr != str.getTerm() && (current_line < first_line))
    {
        for(; ptr != str.getTerm(); ++ptr)
        {
            if(utf8_decode(&state, &codepoint, *ptr) == UTF8_ACCEPT)
            {
                if(codepoint == L'\n')
                {
                    ++current_line;
                    ++ptr;
                    break;
                }
            }
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");
    begin = ptr;

    while(ptr != str.getTerm() && (current_line < last_line))
    {
        for(; ptr != str.getTerm(); ++ptr)
        {
            if(utf8_decode(&state, &codepoint, *ptr) == UTF8_ACCEPT)
            {
                if(codepoint == L'\n')
                {
                    ++current_line;
                    ++ptr;
                    break;
                }
            }
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");
    end = ptr;
}

} // namespace gfx
