#pragma once

#include <audit/audit_catalog.h>
#include <audit/audit_types.h>

#include <imgui/imgui.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// The pieces the Project Audit window is drawn from: severity styles, cards of counts, bars, callouts, chips and
/// the labels of areas, rule sources and platforms.
namespace unravel::audit_widgets
{

/// How a severity is drawn.
struct severity_style
{
    const char* icon;
    const char* name;
    /// What a count of it is titled: "Errors", "Warnings", "Info".
    const char* plural;
    ImU32 color;
};

/// From the most important, as the filters and the summary list them.
constexpr std::array<audit::severity, 3> SEVERITY_ORDER{audit::severity::error, audit::severity::warning, audit::severity::info};

constexpr ImU32 IGNORED_COLOR = IM_COL32(150, 150, 150, 255);
constexpr ImU32 FIXED_COLOR = IM_COL32(110, 190, 130, 255);
constexpr ImU32 AREA_COLOR = IM_COL32(170, 170, 190, 255);
constexpr ImU32 PROFILE_COLOR = IM_COL32(150, 180, 210, 255);
/// A size that grew, and one that shrank.
constexpr ImU32 GROWTH_COLOR = IM_COL32(240, 130, 110, 255);
constexpr ImU32 SHRINK_COLOR = IM_COL32(110, 190, 130, 255);

auto get_severity_style(audit::severity level) -> const severity_style&;
auto get_area_title(audit::area value) -> const char*;
auto get_area_icon(audit::area value) -> const char*;
auto get_rule_source_title(audit::rule_source source) -> const char*;
auto get_rule_source_icon(audit::rule_source source) -> const char*;
/// The glyph of a platform by its title ("Desktop", "Android", "Web").
auto get_platform_icon(const std::string& platform_title) -> const char*;

/// A color with its alpha scaled.
auto with_alpha(ImU32 color, float alpha) -> ImU32;
/// A size in font units, in pixels.
auto to_pixels(float font_units) -> float;
/// The part of a key after its last slash.
auto get_file_name(const std::string& key) -> std::string;
/// "0.4 s", "12 s", "2 min 05 s".
auto format_duration(double seconds) -> std::string;
/// The local date and time of a Unix time, "2026-11-18 21:40".
auto format_time(std::int64_t seconds) -> std::string;

/// A setting's place in Project Settings ("Graphics > Shadows > Resolution"), by its settings path.
auto get_setting_label(const std::string& path) -> std::string;
/// The Project Settings page a settings path is on; empty when none.
auto get_settings_category(const std::string& path) -> std::string;
/// The subject in full, as the details show it.
auto describe_subject(const audit::subject& where) -> std::string;
/// The subject in a few words, as a list row shows it.
auto describe_subject_short(const audit::subject& where) -> std::string;
/// The issue as text to paste: severity, rule, where, message, details, call path, profiles, rule and id.
auto make_issue_text(const audit::issue& item) -> std::string;

/// A section's title, semi-bold, with muted text at the right end of its line.
void draw_section_header(const char* title, const char* right_text = nullptr);
/// A section's title with a link at the right end of its line. @return True when the link is clicked.
auto draw_section_header_link(const char* title, const char* id, const char* link_text, const char* tooltip) -> bool;

/// A card of a count: a stripe of its color, its icon and label, the value large and a caption under it.
struct stat_card
{
    const char* icon;
    const char* label;
    std::string value;
    std::string caption;
    ImU32 color;
    /// A zero count: the value is muted and the card does not react.
    bool is_muted;
    const char* tooltip;
};
/// @return True when the card is clicked.
auto draw_stat_card(const char* id, const stat_card& card, float width) -> bool;
/// The height of a stat card.
auto get_stat_card_height() -> float;

/// A part of a stacked bar.
struct bar_segment
{
    float value;
    ImU32 color;
};
/// A row of a bar chart: an icon and a label, a bar of stacked segments scaled to `scale`, and a count at the right.
/// @return True when the row is clicked.
auto draw_bar_row(const char* id,
                  const char* icon,
                  ImU32 icon_color,
                  const char* label,
                  const std::vector<bar_segment>& segments,
                  float scale,
                  const char* right_text,
                  const char* tooltip) -> bool;

/// A box of advice tinted by its color: an icon, a bold title and wrapped text. Items added between begin_callout and
/// end_callout go under the text.
void begin_callout(const char* id, const char* icon, const char* title, const char* text, ImU32 color);
void end_callout();
/// Text inside a callout, wrapped at its padding; with `is_bullet`, a bullet before it.
void draw_callout_text(const char* text, bool is_bullet = false);

/// Draws a chip at a point without adding an item. @return The size it takes in the line.
auto draw_chip_at(const ImVec2& min, const char* text, ImU32 color, bool is_hovered = false) -> ImVec2;
/// A chip: an optional icon and a short text on a soft fill of its color.
void draw_chip(const char* text, ImU32 color, const char* tooltip = nullptr);
/// A chip that acts as a link. @return True when clicked.
auto draw_chip_button(const char* id, const char* text, ImU32 color, const char* tooltip) -> bool;
/// Starts the next chip on this line, or on the next one when it would not fit.
void same_line_or_wrap(float next_width);
auto calc_chip_width(const char* text) -> float;

/// A square button of the frame's height with an icon centered on it.
auto draw_icon_button(const char* id, const char* icon, const char* tooltip, bool is_enabled = true) -> bool;
/// A button filled with the theme's accent: the one action a place is about.
auto draw_primary_button(const char* label, const char* tooltip, bool is_enabled = true) -> bool;

/// A label of the details' label column, with the cursor left where the value starts.
void draw_field_label(const char* label, float column_width);

/// A key as the project's files show it: relative to the data folder for an app:/data key, the key itself otherwise.
auto get_display_path(const std::string& key) -> std::string;
/// A key's folder, muted, then its file name, as one item; the full key is in its tooltip. @return True when clicked,
/// when `is_link`.
auto draw_path_item(const char* id, const std::string& key, bool is_link, const char* tooltip) -> bool;

/**
 * @brief Two panes that share the space left in the window, side by side or one over the other, with a handle
 * between them the user drags.
 *
 * The caller keeps `share`, the first pane's share of the space. Each pane keeps its minimum size, so neither can be
 * pushed out of sight, whatever the window's size was when the split was last dragged. Usage: construct, begin the
 * first pane with get_first_size(), end it, call draw_handle(), then begin the second pane with size (0, 0).
 */
class pane_split
{
public:
    pane_split(const char* id, bool is_side_by_side, float& share, float min_first, float min_second);

    /// The size to give the first pane's BeginChild.
    auto get_first_size() const -> ImVec2;
    /// Draws the handle after the first pane and leaves the cursor where the second pane starts.
    void draw_handle();

private:
    const char* id_;
    bool is_side_by_side_;
    float& share_;
    float min_first_;
    float min_second_;
    ImVec2 start_{};
    ImVec2 avail_{};
    float first_{};
};

/// A turning arc: work of unknown length in progress.
void draw_spinner(const ImVec2& center, float radius, ImU32 color);

} // namespace unravel::audit_widgets
