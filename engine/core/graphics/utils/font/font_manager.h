/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef FONT_MANAGER_H_HEADER_GUARD
#define FONT_MANAGER_H_HEADER_GUARD

#include <bgfx/bgfx.h>
#include <bx/handlealloc.h>
#include <bx/string.h>

namespace gfx
{
class Atlas;

#define MAX_OPENED_FILES 512
#define MAX_OPENED_FONT  512

#define FONT_TYPE_ALPHA UINT32_C(0x00000100) // L8
// #define FONT_TYPE_LCD               UINT32_C(0x00000200) // BGRA8
// #define FONT_TYPE_RGBA              UINT32_C(0x00000300) // BGRA8
#define FONT_TYPE_DISTANCE                           UINT32_C(0x00000400) // L8
#define FONT_TYPE_DISTANCE_SUBPIXEL                  UINT32_C(0x00000500) // L8
#define FONT_TYPE_DISTANCE_OUTLINE                   UINT32_C(0x00000600) // L8
#define FONT_TYPE_DISTANCE_OUTLINE_IMAGE             UINT32_C(0x00001600) // L8 + BGRA8
#define FONT_TYPE_DISTANCE_DROP_SHADOW               UINT32_C(0x00002700) // L8
#define FONT_TYPE_DISTANCE_DROP_SHADOW_IMAGE         UINT32_C(0x00003800) // L8 + BGRA8
#define FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE UINT32_C(0x00003900) // L8 + BGRA8
#define FONT_TYPE_MASK_DISTANCE_IMAGE                UINT32_C(0x00001000)
#define FONT_TYPE_MASK_DISTANCE_DROP_SHADOW          UINT32_C(0x00002000)

struct font_info
{
    /// The font height in pixel.
    uint16_t pixel_size{};
    /// Rendering type used for the font.
    int16_t font_type{};

    /// The pixel extents above the baseline in pixels (typically positive).
    float ascender{};
    /// The extents below the baseline in pixels (typically negative).
    float descender{};
    /// The spacing in pixels between one row's descent and the next row's ascent.
    float line_gap{};
    /// The extends above the baseline representing of the capital letters.
    float capline{};
    /// The extends above the baseline representing of the small letters.
    float xline{};
    /// This field gives the maximum horizontal cursor advance for all glyphs in the font.
    float max_advance_width{};
    /// The thickness of the under/hover/strike-trough line in pixels.
    float underline_thickness{};
    /// The position of the underline relatively to the baseline.
    float underline_position{};
    /// Scale to apply to glyph data.
    float scale{};
};

// Glyph metrics:
// --------------
//
//                       xmin                     xmax
//                        |                         |
//                        |<-------- width -------->|
//                        |                         |
//              |         +-------------------------+----------------- ymax
//              |         |    ggggggggg   ggggg    |     ^        ^
//              |         |   g:::::::::ggg::::g    |     |        |
//              |         |  g:::::::::::::::::g    |     |        |
//              |         | g::::::ggggg::::::gg    |     |        |
//              |         | g:::::g     g:::::g     |     |        |
//    offset_x -|-------->| g:::::g     g:::::g     |  offset_y    |
//              |         | g:::::g     g:::::g     |     |        |
//              |         | g::::::g    g:::::g     |     |        |
//              |         | g:::::::ggggg:::::g     |     |        |
//              |         |  g::::::::::::::::g     |     |      height
//              |         |   gg::::::::::::::g     |     |        |
//  baseline ---*---------|---- gggggggg::::::g-----*--------      |
//            / |         |             g:::::g     |              |
//     origin   |         | gggggg      g:::::g     |              |
//              |         | g:::::gg   gg:::::g     |              |
//              |         |  g::::::ggg:::::::g     |              |
//              |         |   gg:::::::::::::g      |              |
//              |         |     ggg::::::ggg        |              |
//              |         |         gggggg          |              v
//              |         +-------------------------+----------------- ymin
//              |                                   |
//              |------------- advance_x ---------->|

/// Unicode value of a character
using code_point = uint32_t;

/// A structure that describe a glyph.
struct glyph_info
{
    /// Index for faster retrieval.
    int32_t glyph_index{};

    /// Glyph's width in pixels.
    float width{};

    /// Glyph's height in pixels.
    float height{};

    /// Glyph's left offset in pixels
    float offset_x{};

    /// Glyph's top offset in pixels.
    ///
    /// @remark This is the distance from the baseline to the top-most glyph
    ///   scan line, upwards y coordinates being positive.
    float offset_y{};

    /// For horizontal text layouts, this is the unscaled horizontal
    /// distance in pixels used to increment the pen position when the
    /// glyph is drawn as part of a string of text.
    float advance_x{};

    /// For vertical text layouts, this is the unscaled vertical distance
    /// in pixels used to increment the pen position when the glyph is
    /// drawn as part of a string of text.
    float advance_y{};

    /// Amount to scale a bitmap image glyph.
    float bitmap_scale{};

    /// Region index in the atlas storing textures.
    uint16_t region_index{};
};

BGFX_HANDLE(true_type_handle)
BGFX_HANDLE(font_handle)

class font_manager
{
public:
/// Create the font manager and create the texture cube as BGRA8 with
    /// linear filtering.
    font_manager(uint16_t texture_side_width = 512);

    ~font_manager();

    /// Retrieve the atlas used by the font manager (e.g. to add stuff to it)
    auto get_atlas(font_handle handle) const -> Atlas*;

    /// Load a TrueType font from a given buffer. The buffer is copied and
    /// thus can be freed or reused after this call.
    ///
    /// @return invalid handle if the loading fail
    auto create_ttf(const uint8_t* buffer, uint32_t size) -> true_type_handle;

    /// Unload a TrueType font (free font memory) but keep loaded glyphs.
    void destroy_ttf(true_type_handle handle);

    /// Return a font whose height is a fixed pixel size.
    auto create_font_by_pixel_size(true_type_handle handle,
                                   uint32_t typeface_index,
                                   uint32_t pixel_size,
                                   uint32_t font_type = FONT_TYPE_ALPHA,
                                   uint16_t glyph_width_padding = 8,
                                   uint16_t glyph_height_padding = 8) -> font_handle;

    /// Return a scaled child font whose height is a fixed pixel size.
    auto create_scaled_font_to_pixel_size(font_handle base_font_handle, uint32_t pixel_size) -> font_handle;

    /// destroy a font (truetype or baked)
    void destroy_font(font_handle handle);

    /// Preload a set of glyphs from a TrueType file.
    ///
    /// @return True if every glyph could be preloaded, false otherwise if
    ///   the Font is a baked font, this only do validation on the characters.
    auto preload_glyph(font_handle handle, const wchar_t* string, const wchar_t* end = nullptr) -> bool;
    auto preload_glyph(font_handle handle, const char* string, const char* end = nullptr) -> bool;

    /// Preload a single glyph, return true on success.
    auto preload_glyph(font_handle handle, code_point character) -> bool;
    auto preload_glyph_ranges(font_handle handle, const code_point* ranges) -> bool;

    auto add_glyph_bitmap(font_handle handle,
                          code_point character,
                          uint16_t width,
                          uint16_t height,
                          uint16_t pitch,
                          float extra_scale,
                          const uint8_t* bitmap_buffer,
                          float glyph_offset_x,
                          float glyph_offset_y) -> bool;

    /// Return the font descriptor of a font.
    ///
    /// @remark the handle is required to be valid
    auto get_font_info(font_handle handle) const -> const font_info&;

    /// Return the rendering information about the glyph region. Load the
    /// glyph from a TrueType font if possible
    ///
    auto get_glyph_info(font_handle handle, code_point code_point) -> const glyph_info*;

    auto get_kerning(font_handle handle, code_point prev_code_point, code_point code_point) -> float;

    auto get_white_glyph(font_handle handle) const -> const glyph_info&;

private:
    struct cached_font;
    struct cached_file
    {
        uint8_t* buffer;
        uint32_t buffer_size;
    };

    void init();
    auto add_bitmap(Atlas* atlas, glyph_info& glyph_info, const uint8_t* data) -> bool;
    uint16_t atlas_size_;

    bx::HandleAllocT<MAX_OPENED_FONT> font_handles_;
    cached_font* cached_fonts_;

    bx::HandleAllocT<MAX_OPENED_FILES> file_handles_;
    cached_file* cached_files_;

    // temporary buffer to raster glyph
    uint8_t* buffer_;
};
} // namespace gfx
#endif // FONT_MANAGER_H_HEADER_GUARD
