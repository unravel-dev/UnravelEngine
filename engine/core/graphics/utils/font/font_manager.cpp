/*
 * Copyright 2013 Jeremie Roy. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bx/bx.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../common.h"
#include <bgfx/bgfx.h>
#include <stb/stb_truetype.h>

BX_PRAGMA_DIAGNOSTIC_PUSH()
BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(
    4244) //  warning C4244: '=': conversion from 'double' to 'float', possible loss of data
BX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4701) //  warning C4701: potentially uninitialized local variable 'pt' used
#define SDF_IMPLEMENTATION
#include "sdf.h"
BX_PRAGMA_DIAGNOSTIC_POP()

#include <wchar.h> // wcslen

#include <tinystl/allocator.h>
#include <tinystl/unordered_map.h>
#include <tinystl/vector.h>
namespace stl = tinystl;

#include "../cube_atlas.h"
#include "font_manager.h"
#include "utf8.h"

namespace gfx
{

#define MAX_FONT_BUFFER_SIZE (512 * 4 * 512 * 4 * 4)

struct font_profile_scope
{
    font_profile_scope(const char* func) : func_(func)
    {
    }

    ~font_profile_scope()
    {
        int64_t now = bx::getHPCounter();
        int64_t frameTime = now - start;

        double freq = double(bx::getHPFrequency());

        // seconds:
        float deltaTimeS = float(frameTime / freq);

        // microseconds:
        float deltaTimeUs = float(frameTime * 1e6 / freq);

        bx::printf("--------------------------------------\n");
        bx::printf("%s took %.2f us\n", func_, deltaTimeUs);
        bx::printf("--------------------------------------\n");
    }

private:
    int64_t start = bx::getHPCounter();
    const char* func_{};
};

class true_type_font
{
public:
    /// Initialize from  an external buffer
    /// @remark The ownership of the buffer is external, and you must ensure it stays valid up to this object lifetime
    /// @return true if the initialization succeed
    auto init(const uint8_t* buffer,
              uint32_t buffer_size,
              int32_t font_index,
              uint32_t pixel_height,
              int16_t width_padding,
              int16_t height_padding) -> bool;

    /// return the font descriptor of the current font
    auto get_font_info() const -> font_info;

    /// raster a glyph as 8bit alpha to a memory buffer
    /// update the GlyphInfo according to the raster strategy
    /// @ remark buffer min size: glyphInfo.m_width * glyphInfo * height * sizeof(char)
    auto bake_glyph_alpha(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool;

    /// raster a glyph as 8bit signed distance to a memory buffer
    /// update the GlyphInfo according to the raster strategy
    /// @ remark buffer min size: glyphInfo.m_width * glyphInfo * height * sizeof(char)
    auto bake_glyph_distance(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool;
    auto bake_glyph_distance_fast(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool;
    auto bake_glyph_distance_stb(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool;

private:
    friend class font_manager;

    stbtt_fontinfo font_;
    float scale_{};

    int16_t width_padding_{};
    int16_t height_padding_{};

    int quality_{};

    stl::vector<uint8_t> scratch_buffer_;
    stl::vector<uint8_t> alpha_scratch_;
};

auto true_type_font::init(const uint8_t* buffer,
                          uint32_t buffer_size,
                          int32_t font_index,
                          uint32_t pixel_height,
                          int16_t width_padding,
                          int16_t height_padding) -> bool
{
    BX_WARN((buffer_size > 256 && buffer_size < 100000000),
            "(FontIndex %d) TrueType buffer size is suspicious (%d)",
            font_index,
            buffer_size);
    BX_WARN((pixel_height > 4 && pixel_height < 128),
            "(FontIndex %d) TrueType pixel height is suspicious (%d)",
            font_index,
            pixel_height);
    BX_UNUSED(buffer_size);

    int fonts = stbtt_GetNumberOfFonts(buffer);

    BX_WARN(fonts >= font_index, "(FontIndex %d) TrueType is not in range [0 - %d]", font_index, fonts);

    int offset = stbtt_GetFontOffsetForIndex(buffer, font_index);

    stbtt_InitFont(&font_, buffer, offset);

    float rasterizer_density = 1.0f;
    float pixel_height_f = float(pixel_height) * rasterizer_density;
    scale_ = (pixel_height > 0) ? stbtt_ScaleForPixelHeight(&font_, pixel_height_f)
                                : stbtt_ScaleForMappingEmToPixels(&font_, -pixel_height_f);

    width_padding_ = width_padding;
    height_padding_ = height_padding;
    return true;
}
auto true_type_font::get_font_info() const -> font_info
{
    int ascent{};
    int descent{};
    int lineGap{};
    stbtt_GetFontVMetrics(&font_, &ascent, &descent, &lineGap);

    float scale = scale_;

    int x0{}, y0{}, x1{}, y1{};
    stbtt_GetFontBoundingBox(&font_, &x0, &y0, &x1, &y1);

    font_info info;
    info.scale = 1.0f;
    info.ascender = bx::round(float(ascent) * scale);
    info.descender = bx::round(float(descent) * scale);
    info.line_gap = bx::round(float(lineGap) * scale);
    info.max_advance_width = bx::round(float(y1 - y0) * scale);

    info.capline = float(ascent);

    int caps[] = {'H', 'I'};
    for(int codepoint : caps)
    {
        int x0{}, x1{}, y0{}, y1{};
        if(stbtt_GetCodepointBox(&font_, codepoint, &x0, &y0, &x1, &y1))
        {
            info.capline = bx::floor(float(y1) * scale);
            break;
        }
    }

    info.xline = info.capline * 0.5f;

    int xh[] = {'x', 'z'};
    for(auto codepoint : xh)
    {
        int x0{}, x1{}, y0{}, y1{};
        if(stbtt_GetCodepointBox(&font_, codepoint, &x0, &y0, &x1, &y1))
        {
            info.xline = bx::floor(float(y1) * scale);
            break;
        }
    }

    info.underline_position = float(x1 - x0) * scale - float(ascent);
    info.underline_thickness = float(x1 - x0) * scale / 24.f;
    return info;
}

auto true_type_font::bake_glyph_alpha(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool
{
    // 1) Get vertical font metrics.
    int ascent{}, descent{}, line_gap{};
    stbtt_GetFontVMetrics(&font_, &ascent, &descent, &line_gap);

    // 2) Get horizontal metrics for this glyph.
    int advance{}, lsb{};
    stbtt_GetCodepointHMetrics(&font_, codepoint, &advance, &lsb);

    // 3) Compute un-padded bitmap bounding box.
    const float scale = scale_;
    int x0{}, y0{}, x1{}, y1{};
    stbtt_GetCodepointBitmapBox(&font_, codepoint, scale, scale, &x0, &y0, &x1, &y1);

    const int ww = x1 - x0;
    const int hh = y1 - y0;

    // 4) Record un-padded glyph info.
    out_info.offset_x = float(x0);
    out_info.offset_y = float(y0);
    out_info.width = float(ww);
    out_info.height = float(hh);
    out_info.advance_x = bx::round(float(advance) * scale);
    out_info.advance_y = bx::round(float(ascent - descent + line_gap) * scale);

    // 5) Render the grayscale bitmap into out_buffer.
    //    dst_pitch = bytes per row = glyph width.
    int bpp = 1;
    int dst_pitch = ww * bpp;
    stbtt_MakeCodepointBitmap(&font_, out_buffer, ww, hh, dst_pitch, scale, scale, codepoint);

    return ww * hh > 0;
}

auto true_type_font::bake_glyph_distance(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool
{
    if(quality_ == 0)
    {
        return bake_glyph_distance_fast(codepoint, out_info, out_buffer);
    }

    return bake_glyph_distance_stb(codepoint, out_info, out_buffer);
}

auto true_type_font::bake_glyph_distance_fast(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool
{
    // 1) Get vertical font metrics.
    int ascent{}, descent{}, line_gap{};
    stbtt_GetFontVMetrics(&font_, &ascent, &descent, &line_gap);

    // 2) Get horizontal metrics for this glyph.
    int advance{}, lsb{};
    stbtt_GetCodepointHMetrics(&font_, codepoint, &advance, &lsb);

    // 3) Compute un-padded bitmap bounding box.
    const float scale = scale_;
    int x0{}, y0{}, x1{}, y1{};
    stbtt_GetCodepointBitmapBox(&font_, codepoint, scale, scale, &x0, &y0, &x1, &y1);

    const int ww = x1 - x0;
    const int hh = y1 - y0;

    // 4) Record un-padded glyph info.
    out_info.offset_x = float(x0);
    out_info.offset_y = float(y0);
    out_info.width = float(ww);
    out_info.height = float(hh);
    out_info.advance_x = bx::round(float(advance) * scale);
    out_info.advance_y = bx::round(float(ascent - descent + line_gap) * scale);

    // 5) Render the grayscale bitmap into out_buffer.
    //    dst_pitch = bytes per row = glyph width.
    int bpp = 1;
    int dst_pitch = ww * bpp;
    stbtt_MakeCodepointBitmap(&font_, out_buffer, ww, hh, dst_pitch, scale, scale, codepoint);
    // 6) If there's any pixel data, build the SDF:
    if(ww > 0 && hh > 0)
    {
        // 6a) Compute padded dimensions.
        const int dw = width_padding_;
        const int dh = height_padding_;
        const int nw = ww + 2 * dw;
        const int nh = hh + 2 * dh;
        BX_ASSERT(nw * nh < 128 * 128, "Glyph too big");

        // 6b) Prepare (or resize) the alpha scratch buffer.
        size_t alpha_bytes = size_t(nw) * size_t(nh);
        if(alpha_scratch_.size() < alpha_bytes)
        {
            alpha_scratch_.resize(alpha_bytes);
        }
        uint8_t* alpha_img = alpha_scratch_.data();
        bx::memSet(alpha_img, 0, alpha_bytes);

        // 6c) Copy the rendered glyph into the center of the padded alpha image.
        for(int y = 0; y < hh; ++y)
        {
            bx::memCopy(alpha_img + size_t(y + dh) * nw + dw, out_buffer + size_t(y) * ww, size_t(ww));
        }

        // 6d) Prepare (or resize) the SDF scratch buffer (floats + SDFpoint structs).
        size_t temp_bytes = alpha_bytes * (sizeof(float) + sizeof(SDFpoint));
        if(scratch_buffer_.size() < temp_bytes)
        {
            scratch_buffer_.resize(temp_bytes);
        }
        uint8_t* temp = scratch_buffer_.data();

        // 6e) Run the no‐alloc distance‐transform, writing back into out_buffer.
        sdfBuildDistanceFieldNoAlloc(out_buffer, // output buffer (one byte per pixel, row-major)
                                     nw,         // outstride = bytes per row
                                     8.0f,       // radius
                                     alpha_img,  // input alpha image
                                     nw,
                                     nh,  // width, height of the padded image
                                     nw,  // stride = bytes per row of input
                                     temp // scratch buffer (must be distinct from alpha_img)
        );

        // 6f) Update glyph info to reflect padding.
        out_info.offset_x -= float(dw);
        out_info.offset_y -= float(dh);
        out_info.width = float(nw);
        out_info.height = float(nh);
    }

    return true;
}

auto true_type_font::bake_glyph_distance_stb(code_point codepoint, glyph_info& out_info, uint8_t* out_buffer) -> bool
{
    // 1) Get vertical font metrics.
    int ascent{}, descent{}, line_gap{};
    stbtt_GetFontVMetrics(&font_, &ascent, &descent, &line_gap);

    // 2) Get horizontal metrics for this glyph.
    int advance{}, lsb{};
    stbtt_GetCodepointHMetrics(&font_, codepoint, &advance, &lsb);

    // 3) Decide on SDF parameters
    float scale = scale_;         // your font‐to‐pixel scale
    int padding = width_padding_; // how many pixels to pad (radius)
    unsigned char on_edge = 128;  // “middle” of SDF range
    float pixel_dist_scale = float(on_edge) / float(padding);

    // 4) Call into stb to get a freshly‐malloc’d SDF bitmap
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap =
        stbtt_GetCodepointSDF(&font_, scale, codepoint, padding, on_edge, pixel_dist_scale, &w, &h, &xoff, &yoff);

    // 5) Fill out GlyphInfo
    out_info.offset_x = float(xoff);
    out_info.offset_y = float(yoff);
    out_info.width = float(w);
    out_info.height = float(h);
    out_info.advance_x = bx::round(advance * scale);
    out_info.advance_y = bx::round((ascent - descent + line_gap) * scale);

    if(bitmap)
    {
        // 6) Copy row-by-row into the caller’s buffer
        //    (assumes out_buffer has at least w*h bytes allocated)
        for(int y = 0; y < h; ++y)
        {
            bx::memCopy(out_buffer + size_t(y) * size_t(w), bitmap + size_t(y) * size_t(w), size_t(w));
        }

        // 7) Free the stb‐malloc’d bitmap
        stbtt_FreeSDF(bitmap, nullptr);
    }

    return true;
}

using glyph_lut_t = stl::unordered_map<code_point, glyph_info>;

// cache font data
struct font_manager::cached_font
{
    font_info info;
    glyph_lut_t cached_glyphs;
    true_type_font* font{};
    // an handle to a master font in case of sub distance field font
    font_handle master_font_handle = BGFX_INVALID_HANDLE;
    int16_t padding{};

    Atlas* atlas{};
    glyph_info white_glyph{};
};

auto add_white_glyph(Atlas* atlas, uint32_t size) -> glyph_info
{
    // Create filler rectangle
    stl::vector<uint8_t> buffer(size * size * 4, 255);

    glyph_info glyph{};
    glyph.width = size;
    glyph.height = size;

    /// make sure the white glyph doesn't bleed by using a one pixel inner outline
    glyph.region_index = atlas->addRegion(size, size, buffer.data(), AtlasRegion::TYPE_GRAY, 1);
    return glyph;
}

font_manager::font_manager(uint16_t texture_side_width) : atlas_size_(texture_side_width)
{
    init();
}

void font_manager::init()
{
    cached_files_ = new cached_file[MAX_OPENED_FILES];
    cached_fonts_ = new cached_font[MAX_OPENED_FONT];
    buffer_ = new uint8_t[MAX_FONT_BUFFER_SIZE];
}

font_manager::~font_manager()
{
    auto font_handles = font_handles_;

    for(uint16_t i = 0; i < font_handles.getNumHandles(); ++i)
    {
        if(font_handles.isValid(i))
        {
            font_handle handle{font_handles.getHandleAt(i)};
            destroy_font(handle);
        }
    }

    auto file_handles = file_handles_;

    for(uint16_t i = 0; i < file_handles.getNumHandles(); ++i)
    {
        if(file_handles.isValid(i))
        {
            true_type_handle handle{file_handles.getHandleAt(i)};
            destroy_ttf(handle);
        }
    }

    BX_ASSERT(font_handles_.getNumHandles() == 0, "All the fonts must be destroyed before destroying the manager");
    delete[] cached_fonts_;

    BX_ASSERT(file_handles_.getNumHandles() == 0, "All the font files must be destroyed before destroying the manager");
    delete[] cached_files_;

    delete[] buffer_;
}

auto font_manager::get_atlas(font_handle handle) const -> Atlas*
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];
    return font.atlas;
}

auto font_manager::create_ttf(const uint8_t* buffer, uint32_t size) -> true_type_handle
{
    uint16_t id = file_handles_.alloc();
    BX_ASSERT(id != bx::kInvalidHandle, "Invalid handle used");
    cached_files_[id].buffer = new uint8_t[size];
    cached_files_[id].buffer_size = size;
    bx::memCopy(cached_files_[id].buffer, buffer, size);

    true_type_handle ret = {id};
    return ret;
}

void font_manager::destroy_ttf(true_type_handle handle)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    delete[] cached_files_[handle.idx].buffer;
    cached_files_[handle.idx].buffer_size = 0;
    cached_files_[handle.idx].buffer = nullptr;
    file_handles_.free(handle.idx);
}

auto font_manager::create_font_by_pixel_size(true_type_handle ttf_handle,
                                             uint32_t typeface_index,
                                             uint32_t pixel_size,
                                             uint32_t font_type,
                                             uint16_t glyph_width_padding,
                                             uint16_t glyph_height_padding) -> font_handle
{
    BX_ASSERT(isValid(ttf_handle), "Invalid handle used");

    true_type_font* ttf = new true_type_font();
    if(!ttf->init(cached_files_[ttf_handle.idx].buffer,
                  cached_files_[ttf_handle.idx].buffer_size,
                  typeface_index,
                  pixel_size,
                  glyph_width_padding,
                  glyph_height_padding))
    {
        delete ttf;
        font_handle invalid = {bx::kInvalidHandle};
        return invalid;
    }

    uint16_t font_idx = font_handles_.alloc();
    BX_ASSERT(font_idx != bx::kInvalidHandle, "Invalid handle used");

    cached_font& font = cached_fonts_[font_idx];
    font.font = ttf;
    font.info = ttf->get_font_info();
    font.info.font_type = int16_t(font_type);
    font.info.pixel_size = uint16_t(pixel_size);
    font.cached_glyphs.clear();
    font.master_font_handle.idx = bx::kInvalidHandle;
    font.atlas = new Atlas(atlas_size_);
    font.white_glyph = add_white_glyph(font.atlas, 3);
    font_handle handle = {font_idx};
    return handle;
}

auto font_manager::create_scaled_font_to_pixel_size(font_handle base_font_handle, uint32_t pixel_size) -> font_handle
{
    BX_ASSERT(isValid(base_font_handle), "Invalid handle used");
    cached_font& base_font = cached_fonts_[base_font_handle.idx];
    font_info& info = base_font.info;

    font_info new_font_info = info;
    new_font_info.pixel_size = uint16_t(pixel_size);
    new_font_info.scale = (float)pixel_size / (float)info.pixel_size;
    new_font_info.ascender = (new_font_info.ascender * new_font_info.scale);
    new_font_info.descender = (new_font_info.descender * new_font_info.scale);
    new_font_info.capline = (new_font_info.capline * new_font_info.scale);
    new_font_info.xline = (new_font_info.xline * new_font_info.scale);
    new_font_info.line_gap = (new_font_info.line_gap * new_font_info.scale);
    new_font_info.max_advance_width = (new_font_info.max_advance_width * new_font_info.scale);
    new_font_info.underline_thickness = (new_font_info.underline_thickness * new_font_info.scale);
    new_font_info.underline_position = (new_font_info.underline_position * new_font_info.scale);

    uint16_t font_idx = font_handles_.alloc();
    BX_ASSERT(font_idx != bx::kInvalidHandle, "Invalid handle used");

    cached_font& font = cached_fonts_[font_idx];
    font.cached_glyphs.clear();
    font.info = new_font_info;
    font.font = nullptr;
    font.atlas = base_font.atlas;
    font.white_glyph = base_font.white_glyph;
    font.master_font_handle = base_font_handle;

    font_handle handle = {font_idx};
    return handle;
}

void font_manager::destroy_font(font_handle handle)
{
    BX_ASSERT(isValid(handle), "Invalid handle used");

    cached_font& font = cached_fonts_[handle.idx];

    if(font.font != nullptr)
    {
        delete font.font;
        font.font = nullptr;

        if(font.atlas != nullptr)
        {
            delete font.atlas;
            font.atlas = nullptr;
        }
    }

    font.cached_glyphs.clear();
    font_handles_.free(handle.idx);
}

auto font_manager::preload_glyph_ranges(font_handle handle, const code_point* ranges) -> bool
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];
    if(font.font == nullptr)
    {
        return false;
    }

    // font_profile_scope profile(__func__);
    // ranges is like { start0, end0, start1, end1, …, 0 }
    // Walk it two at a time until the 0 sentinel
    for(const code_point* p = ranges; *p != 0; p += 2)
    {
        code_point start = p[0];
        code_point end = p[1];
        // guard against malformed data
        BX_ASSERT(end >= start, "Range end before start");

        for(code_point cp = start; cp <= end; ++cp)
        {
            if(!preload_glyph(handle, cp))
            {
                return false;
            }
        }
    }

    return true;
}

auto font_manager::preload_glyph(font_handle handle, const char* str, const char* end) -> bool
{
    if(end == nullptr)
    {
        end = str + strlen(str);
    }
    BX_ASSERT(end >= str, "");

    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];

    if(nullptr == font.font)
    {
        return false;
    }

    uint32_t state = 0;

    for(; *str && str < end; ++str)
    {
        code_point codepoint = 0;
        if(utf8_decode(&state, &codepoint, *str) == UTF8_ACCEPT)
        {
            if(!preload_glyph(handle, codepoint))
            {
                return false;
            }
        }
    }

    BX_ASSERT(state == UTF8_ACCEPT, "The string is not well-formed");

    return true;
}

auto font_manager::preload_glyph(font_handle handle, const wchar_t* str, const wchar_t* end) -> bool
{
    if(end == nullptr)
    {
        end = str + wcslen(str);
    }
    BX_ASSERT(end >= str, "");

    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];

    if(nullptr == font.font)
    {
        return false;
    }

    for(const wchar_t* current = str; current < end; ++current)
    {
        code_point codepoint = *current;
        if(!preload_glyph(handle, codepoint))
        {
            return false;
        }
    }

    return true;
}

auto font_manager::preload_glyph(font_handle handle, code_point codepoint) -> bool
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];
    font_info& info = font.info;

    glyph_lut_t::iterator iter = font.cached_glyphs.find(codepoint);
    if(iter != font.cached_glyphs.end())
    {
        return true;
    }

    if(nullptr != font.font)
    {
        glyph_info glinfo;

        switch(font.info.font_type)
        {
            case FONT_TYPE_ALPHA:
                font.font->bake_glyph_alpha(codepoint, glinfo, buffer_);
                break;

            case FONT_TYPE_DISTANCE:
                font.font->bake_glyph_distance(codepoint, glinfo, buffer_);
                break;

            case FONT_TYPE_DISTANCE_SUBPIXEL:
                font.font->bake_glyph_distance(codepoint, glinfo, buffer_);
                break;

            case FONT_TYPE_DISTANCE_OUTLINE:
            case FONT_TYPE_DISTANCE_OUTLINE_IMAGE:
            case FONT_TYPE_DISTANCE_DROP_SHADOW:
            case FONT_TYPE_DISTANCE_DROP_SHADOW_IMAGE:
            case FONT_TYPE_DISTANCE_OUTLINE_DROP_SHADOW_IMAGE:
                font.font->bake_glyph_distance(codepoint, glinfo, buffer_);
                break;

            default:
                BX_ASSERT(false, "TextureType not supported yet");
        }

        if(!add_bitmap(font.atlas, glinfo, buffer_))
        {
            return false;
        }

        glinfo.advance_x = (glinfo.advance_x * info.scale);
        glinfo.advance_y = (glinfo.advance_y * info.scale);
        glinfo.offset_x = (glinfo.offset_x * info.scale);
        glinfo.offset_y = (glinfo.offset_y * info.scale);
        glinfo.height = (glinfo.height * info.scale);
        glinfo.width = (glinfo.width * info.scale);

        font.cached_glyphs[codepoint] = glinfo;
        return true;
    }

    if(isValid(font.master_font_handle) && preload_glyph(font.master_font_handle, codepoint))
    {
        const glyph_info* glyph = get_glyph_info(font.master_font_handle, codepoint);

        glyph_info glinfo = *glyph;
        glinfo.advance_x = (glinfo.advance_x * info.scale);
        glinfo.advance_y = (glinfo.advance_y * info.scale);
        glinfo.offset_x = (glinfo.offset_x * info.scale);
        glinfo.offset_y = (glinfo.offset_y * info.scale);
        glinfo.height = (glinfo.height * info.scale);
        glinfo.width = (glinfo.width * info.scale);

        font.cached_glyphs[codepoint] = glinfo;
        return true;
    }

    return false;
}

auto font_manager::add_glyph_bitmap(font_handle handle,
                                    code_point codepoint,
                                    uint16_t width,
                                    uint16_t height,
                                    uint16_t pitch,
                                    float extra_scale,
                                    const uint8_t* bitmap_buffer,
                                    float glyph_offset_x,
                                    float glyph_offset_y) -> bool
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    cached_font& font = cached_fonts_[handle.idx];

    glyph_lut_t::iterator iter = font.cached_glyphs.find(codepoint);
    if(iter != font.cached_glyphs.end())
    {
        return true;
    }

    glyph_info glinfo;

    float glyph_scale = extra_scale;
    glinfo.offset_x = glyph_offset_x * glyph_scale;
    glinfo.offset_y = glyph_offset_y * glyph_scale;
    glinfo.width = (float)width;
    glinfo.height = (float)height;
    glinfo.advance_x = (float)width * glyph_scale;
    glinfo.advance_y = (float)height * glyph_scale;
    glinfo.bitmap_scale = glyph_scale;

    uint32_t dst_pitch = width * 4;

    uint8_t* dst = buffer_;
    const uint8_t* src = bitmap_buffer;
    uint32_t src_pitch = pitch;

    for(int32_t ii = 0; ii < height; ++ii)
    {
        bx::memCopy(dst, src, dst_pitch);

        dst += dst_pitch;
        src += src_pitch;
    }

    auto atlas = get_atlas(handle);
    glinfo.region_index = atlas->addRegion((uint16_t)bx::ceil(glinfo.width),
                                           (uint16_t)bx::ceil(glinfo.height),
                                           buffer_,
                                           AtlasRegion::TYPE_BGRA8);

    font.cached_glyphs[codepoint] = glinfo;
    return true;
}

auto font_manager::get_font_info(font_handle handle) const -> const font_info&
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    return cached_fonts_[handle.idx].info;
}

auto font_manager::get_white_glyph(font_handle handle) const -> const glyph_info&
{
    BX_ASSERT(isValid(handle), "Invalid handle used");
    return cached_fonts_[handle.idx].white_glyph;
}

auto font_manager::get_kerning(font_handle handle, code_point prev_codepoint, code_point codepoint) -> float
{
    const cached_font& cfont = cached_fonts_[handle.idx];
    if(isValid(cfont.master_font_handle))
    {
        cached_font& master_font = cached_fonts_[cfont.master_font_handle.idx];
        return master_font.font->scale_ *
               stbtt_GetCodepointKernAdvance(&master_font.font->font_, prev_codepoint, codepoint) *
               cfont.info.scale;
    }

    return cfont.font->scale_ * stbtt_GetCodepointKernAdvance(&cfont.font->font_, prev_codepoint, codepoint);
}

auto font_manager::get_glyph_info(font_handle handle, code_point codepoint) -> const glyph_info*
{
    const glyph_lut_t& cached_glyphs = cached_fonts_[handle.idx].cached_glyphs;
    glyph_lut_t::const_iterator it = cached_glyphs.find(codepoint);

    if(it == cached_glyphs.end())
    {
        if(!preload_glyph(handle, codepoint))
        {
            return nullptr;
        }

        it = cached_glyphs.find(codepoint);
    }

    BX_ASSERT(it != cached_glyphs.end(), "Failed to preload glyph.");
    return &it->second;
}

auto font_manager::add_bitmap(Atlas* atlas, glyph_info& glinfo, const uint8_t* data) -> bool
{
    glinfo.region_index = atlas->addRegion((uint16_t)bx::ceil(glinfo.width),
                                           (uint16_t)bx::ceil(glinfo.height),
                                           data,
                                           AtlasRegion::TYPE_GRAY);
    return true;
}

} // namespace gfx
