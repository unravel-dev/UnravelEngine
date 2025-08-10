#pragma once
#include <base/basetypes.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/font.h>
#include <hpp/small_vector.hpp>
#include <math/math.h>

namespace unravel
{

enum align : uint32_t
{
    invalid = 0,
    // Horizontal align (general)
    left = 1 << 0,
    center = 1 << 1,
    right = 1 << 2,
    horizontal_mask = left | center | right,

    // Vertical align (general)
    top = 1 << 3,
    middle = 1 << 4,
    bottom = 1 << 5,
    vertical_mask = top | middle | bottom,

    // Vertical align (text only)
    capline = 1 << 6,   // capline of first line
    midline = 1 << 7,   // half distance between cap_height and baseline
    baseline = 1 << 10, // baseline of last line
    typographic_mask = capline | midline | baseline,

    vertical_text_mask = vertical_mask | typographic_mask
};

struct alignment
{
    uint32_t flags{align::left | align::top};
};

template<typename T, size_t StaticCapacity = 16>
using text_vector = hpp::small_vector<T, StaticCapacity>;


struct text_style_flags
{
    uint32_t flags = gfx::style_normal;
};

struct text_style
{
    float opacity = 1.0f;
    uint32_t text_color = math::color::white();
    uint32_t background_color = math::color::transparent();
    uint32_t foreground_color = math::color::transparent();
    uint32_t overline_color = math::color::white();
    uint32_t underline_color = math::color::white();
    uint32_t strike_color = math::color::white();
    uint32_t outline_color = math::color::black();
    float outline_width = 0.0f;
    math::vec2 shadow_offsets{0.0f, 0.0f};
    uint32_t shadow_color = math::color::black();
    float shadow_softener = 1.0f;
    uint32_t style_flags = gfx::style_normal;

    void set_opacity(float opacity) { this->opacity = opacity; }
    auto get_opacity() const -> float { return opacity; }

    void set_text_color(math::color color) { text_color = static_cast<uint32_t>(color); }
    auto get_text_color() const -> math::color { return math::color(text_color); }

    void set_background_color(math::color color) { background_color = static_cast<uint32_t>(color); }
    auto get_background_color() const -> math::color { return math::color(background_color); }

    void set_foreground_color(math::color color) { foreground_color = static_cast<uint32_t>(color); }
    auto get_foreground_color() const -> math::color { return math::color(foreground_color); }

    void set_overline_color(math::color color) { overline_color = static_cast<uint32_t>(color); }
    auto get_overline_color() const -> math::color { return math::color(overline_color); }

    void set_underline_color(math::color color) { underline_color = static_cast<uint32_t>(color); }
    auto get_underline_color() const -> math::color { return math::color(underline_color); }

    void set_strike_color(math::color color) { strike_color = static_cast<uint32_t>(color); }
    auto get_strike_color() const -> math::color { return math::color(strike_color); }

    void set_outline_color(math::color color) { outline_color = static_cast<uint32_t>(color); }
    auto get_outline_color() const -> math::color { return math::color(outline_color); }

    void set_shadow_color(math::color color) { shadow_color = static_cast<uint32_t>(color); }
    auto get_shadow_color() const -> math::color { return math::color(shadow_color); }

    void set_style_flags(text_style_flags flags) { style_flags = flags.flags; }
    auto get_style_flags() const -> text_style_flags { return {style_flags}; }

    auto operator==(const text_style& other) const -> bool
    {
        constexpr float epsilon = 0.0001f;
        return std::fabs(opacity - other.opacity) < epsilon &&
               text_color == other.text_color &&
               background_color == other.background_color &&
               foreground_color == other.foreground_color &&
               overline_color == other.overline_color &&
               underline_color == other.underline_color &&
               strike_color == other.strike_color &&
               outline_color == other.outline_color &&
               std::fabs(outline_width - other.outline_width) < epsilon &&
               shadow_offsets == other.shadow_offsets &&
               shadow_color == other.shadow_color &&
               std::fabs(shadow_softener - other.shadow_softener) < epsilon &&
               style_flags == other.style_flags;
    }
};

// All the per buffer style state
struct rich_state
{
    text_style style{};
    bool no_break{};
};

enum class break_type : uint8_t
{
    nobreak,   // normal
    mustbreak, // LINEBREAK_MUSTBREAK immediately after
    allowbreak // punctuation/blank: never wrap before
};

struct word_frag
{
    std::string_view txt;
    std::string_view brk_symbol;
    rich_state state;
    break_type brk;     // one of none, must_break, no_wrap_before
    float base_width;   // measured at base size
    float scaled_width; // base_width * scale
};

using fragment_list = text_vector<word_frag>;

struct frag_atom
{
    float width = 0;
    break_type brk = break_type::allowbreak;
    text_vector<word_frag> parts;
};

struct rich_segment
{
    hpp::string_view text;
    rich_state state;
};
using segment_list = text_vector<rich_segment>;

struct wrapped_line
{
    text_vector<rich_segment> segments;
    hpp::string_view brk_symbol;
    float width{};
};
using text_layout = text_vector<wrapped_line>;

struct scratch_cache
{
    text_vector<char, 256> lb;
    text_vector<char, 256> wb;
    text_vector<size_t> offsets;

    fragment_list frags;
    text_vector<frag_atom> atoms;

    text_layout layout;
};

struct text_line
{
    std::string line;
    std::string break_symbol;
};

/**
 * @class model_component
 * @brief Class that contains core data for meshes.
 */
class text_component : public component_crtp<text_component>
{
public:
    enum class buffer_type : uint32_t
    {
        static_buffer,
        dynamic_buffer,
        transient_buffer
    };

    enum class overflow_type : uint32_t
    {
        none,
        word,
        grapheme
    };

    /**
     * @brief Sets the text content to be rendered
     * @param text The string content to be displayed
     */
    void set_text(const std::string& text);

    /**
     * @brief Gets the current text content
     * @return The current text string being rendered
     */
    auto get_text() const -> const std::string&;

    /**
     * @brief Sets the text styling properties
     * @param style The text_style object containing all styling properties
     */
    void set_style(const text_style& style);

    /**
     * @brief Gets the current text style settings
     * @return The current text_style object
     */
    auto get_style() const -> const text_style&;

    /**
     * @brief Sets the buffer type for text rendering
     * @param type The buffer_type to use (static, dynamic, or transient)
     */
    void set_buffer_type(const buffer_type& type);

    /**
     * @brief Gets the current buffer type
     * @return The current buffer_type being used
     */
    auto get_buffer_type() const -> const buffer_type&;

    /**
     * @brief Sets how text should overflow when it exceeds its bounds
     * @param type The overflow_type to use (none, word, or grapheme)
     */
    void set_overflow_type(const overflow_type& type);

    /**
     * @brief Gets the current overflow handling type
     * @return The current overflow_type being used
     */
    auto get_overflow_type() const -> const overflow_type&;

    /**
     * @brief Sets the font to be used for rendering text
     * @param font Asset handle to the font resource
     */
    void set_font(const asset_handle<font>& font);

    /**
     * @brief Gets the current font
     * @return Asset handle to the current font
     */
    auto get_font() const -> const asset_handle<font>&;

    /**
     * @brief Sets the font size in pixels
     * @param font_size The size in pixels
     */
    void set_font_size(uint32_t font_size);

    /**
     * @brief Gets the current font size
     * @return The current font size in pixels
     */
    auto get_font_size() const -> uint32_t;

    /**
     * @brief Enables or disables automatic font sizing
     * @param auto_size True to enable auto-sizing, false to disable
     */
    void set_auto_size(bool auto_size);

    /**
     * @brief Checks if auto-sizing is enabled
     * @return True if auto-sizing is enabled, false otherwise
     */
    auto get_auto_size() const -> bool;

    /**
     * @brief Gets the actual font size being used for rendering
     * @return The calculated font size in pixels
     */
    auto get_render_font_size() const -> uint32_t;

    /**
     * @brief Sets the area bounds for text rendering
     * @param area The width and height of the text area
     */
    void set_area(const fsize_t& area);

    /**
     * @brief Gets the current text area bounds
     * @return The current width and height of the text area
     */
    auto get_area() const -> const fsize_t&;

    /**
     * @brief Sets the range for automatic font sizing
     * @param range The minimum and maximum font sizes allowed
     */
    void set_auto_size_range(const urange32_t& range);

    /**
     * @brief Gets the current auto-size range
     * @return The current minimum and maximum font sizes for auto-sizing
     */
    auto get_auto_size_range() const -> const urange32_t&;

    /**
     * @brief Enables or disables rich text processing
     * @param is_rich True to enable rich text processing, false for plain text
     */
    void set_is_rich_text(bool is_rich);

    /**
     * @brief Checks if rich text processing is enabled
     * @return True if rich text is enabled, false otherwise
     */
    auto get_is_rich_text() const -> bool;

    /**
     * @brief Enables or disables kerning in text rendering
     * @param apply_kerning True to enable kerning, false to disable
     */
    void set_apply_kerning(bool apply_kerning);

    /**
     * @brief Checks if kerning is enabled
     * @return True if kerning is enabled, false otherwise
     */
    auto get_apply_kerning() const -> bool;

    /**
     * @brief Sets the text alignment properties
     * @param align The alignment flags for horizontal and vertical positioning
     */
    void set_alignment(const alignment& align);

    /**
     * @brief Gets the current text alignment settings
     * @return The current alignment flags
     */
    auto get_alignment() const -> const alignment&;

    /**
     * @brief Gets the scaled font instance used for rendering
     * @return Reference to the current scaled font
     */
    auto get_scaled_font() const -> const scaled_font&;

    /**
     * @brief Gets the actual area used for rendering
     * @return The calculated render area dimensions
     */
    auto get_render_area() const -> fsize_t;

    /**
     * @brief Checks if the text can be rendered
     * @return True if the text can be rendered, false otherwise
     */
    auto can_be_rendered() const -> bool;

    /**
     * @brief Gets the bounding box of the text
     * @return The bounding box in local space
     */
    auto get_bounds() const -> math::bbox;

    /**
     * @brief Gets the bounding box used for rendering
     * @return The bounding box in render space
     */
    auto get_render_bounds() const -> math::bbox;

    /**
     * @brief Gets the number of render buffers being used
     * @return The count of render buffers
     */
    auto get_render_buffers_count() const -> size_t;

    /**
     * @brief Gets the text content split into lines
     * @param include_breaks Whether to include line break symbols in the output
     * @return Vector of text lines with their break symbols
     */
    auto get_lines(bool include_breaks = true) const -> text_vector<text_line>;

    /**
     * @brief Converts meters to pixels based on current font metrics
     * @param meters The value in meters to convert
     * @return The equivalent value in pixels
     */
    auto meters_to_px(float meters) const -> float;

    /**
     * @brief Converts pixels to meters based on current font metrics
     * @param px The value in pixels to convert
     * @return The equivalent value in meters
     */
    auto px_to_meters(float px) const -> float;

    /**
     * @brief Submits the text for rendering
     * @param id The view ID for rendering
     * @param world The world transform matrix
     * @param state The rendering state flags
     */
    void submit(gfx::view_id id, const math::transform& world, uint64_t state);

private:
    auto get_builder() const -> text_buffer_builder&;
    void recreate_scaled_font() const;
    void recreate_text() const;

    asset_handle<font> font_ = font::default_regular();

    mutable uintptr_t font_version_{};
    mutable bool scaled_font_dirty_{true};

    mutable std::shared_ptr<scaled_font> scaled_font_;

    bool is_rich_{true};
    bool apply_kerning_{true};

    std::string text_;
    mutable bool text_dirty_{true};

    uint32_t font_size_{36};
    mutable uint32_t calculated_font_size_{};
    fsize_t area_{20.0f, 10.0f};
    fsize_t render_area_;

    buffer_type type_{buffer_type::static_buffer};
    overflow_type overflow_type_{overflow_type::word};

    alignment align_{};
    mutable bool align_dirty_{true};

    bool auto_size_{};
    urange32_t auto_size_font_range_{18, 72};

    text_style style_{};

    std::shared_ptr<text_buffer_builder> builder_ = std::make_shared<text_buffer_builder>();
    std::shared_ptr<text_buffer_builder> debug_builder_ = std::make_shared<text_buffer_builder>();

    mutable scratch_cache scratch_{};
};

} // namespace unravel
