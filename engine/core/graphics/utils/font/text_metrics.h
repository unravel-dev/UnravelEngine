/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef TEXT_METRICS_H_HEADER_GUARD
#define TEXT_METRICS_H_HEADER_GUARD

#include "font_manager.h"

namespace gfx
{
class text_metrics
{
public:
    text_metrics(font_manager* manager);

    /// Append an ASCII/utf-8 string to the metrics helper.
    auto append_text(font_handle handle, const char* str, const char* end = nullptr) -> void;

    /// Append a wide char string to the metrics helper.
    auto append_text(font_handle handle, const wchar_t* str, const wchar_t* end = nullptr) -> void;

    /// Return the width of the measured text.
    auto get_width() const -> float
    {
        return width_;
    }

    /// Return the height of the measured text.
    auto get_height() const -> float
    {
        return height_;
    }

    /// Clear the width and height of the measured text.
    auto clear_text() -> void;

private:
    font_manager* manager_;
    float width_;
    float height_;
    float x_;
    float line_height_;
    float line_gap_;
};

/// Compute text crop area for text using a single font.
class text_line_metrics
{
public:
    text_line_metrics(const font_info& info);

    /// Return the height of a line of text using the given font.
    auto get_line_height() const -> float
    {
        return line_height_;
    }

    /// Return the number of text line in the given text.
    auto get_line_count(const bx::StringView& str) const -> uint32_t;

    /// Return the first and last character visible in the [first_line, last_line] range.
    void get_sub_text(const bx::StringView& str,
                      uint32_t first_line,
                      uint32_t last_line,
                      const char*& begin,
                      const char*& end);

private:
    float line_height_;
};
} // namespace gfx
#endif // TEXT_METRICS_H_HEADER_GUARD
