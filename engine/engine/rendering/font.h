#pragma once
#include <engine/engine_export.h>

#include <graphics/graphics.h>

#include <graphics/utils/font/font_manager.h>
#include <graphics/utils/font/text_buffer_manager.h>
#include <graphics/utils/font/text_metrics.h>
#include <engine/assets/asset_handle.h>

namespace unravel
{

void init_fonts();
void deinit_fonts();

struct base_font
{
    base_font();
    ~base_font();

    auto get_info() const -> const gfx::font_info&;
    auto get_line_height() const -> float;
    auto is_valid() const -> bool;

    gfx::font_handle handle{gfx::invalid_handle};
};

struct scaled_font : base_font
{
};

struct font : base_font
{
    font();
    font(const char* path,
         uint32_t typeface_index,
         uint32_t pixel_ize,
         uint32_t font_type = FONT_TYPE_DISTANCE,
         uint16_t glyph_width_padding = 8,
         uint16_t glyph_height_padding = 8);
    ~font();

    auto get_scaled_font(uint32_t pixel_size) const -> std::shared_ptr<scaled_font>;

    static auto default_thin() -> asset_handle<font>&;
    static auto default_extra_light() -> asset_handle<font>&;
    static auto default_light() -> asset_handle<font>&;
    static auto default_medium() -> asset_handle<font>&;
    static auto default_regular() -> asset_handle<font>&;
    static auto default_semi_bold() -> asset_handle<font>&;
    static auto default_bold() -> asset_handle<font>&;
    static auto default_heavy() -> asset_handle<font>&;
    static auto default_black() -> asset_handle<font>&;

    gfx::true_type_handle ttf_handle{gfx::invalid_handle};
};


struct text_buffer
{
    gfx::text_buffer_handle handle{gfx::invalid_handle};
};

struct text_buffer_builder
{
    text_buffer_builder();
    ~text_buffer_builder();

    void destroy_buffers();

    std::vector<text_buffer> buffers;
    gfx::text_buffer_manager manager;
};

struct text_metrics
{
    text_metrics();

    gfx::text_metrics metrics;
};

} // namespace unravel
