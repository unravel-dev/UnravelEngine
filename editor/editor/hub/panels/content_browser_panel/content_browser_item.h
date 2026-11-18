#pragma once
#include "content_browser_item_details.h"

#include <filesystem/cache.hpp>
#include <graphics/texture.h>
#include <hpp/string_view.hpp>
#include <imgui/imgui.h>

#include <functional>
#include <string>

namespace unravel
{

/**
 * @brief What a card shows. The content browser's entries and the asset picker's tiles are the same cards; the views
 * point into strings that outlive the draw call.
 */
struct content_card_view
{
    /// Centered under the thumbnail on up to two lines.
    hpp::string_view name;
    /// Display name of the type ("Mesh"): the color of the strip under the thumbnail, and the pill under the name
    /// unless a caption takes its place. Empty for files of no asset type.
    hpp::string_view type;
    /// A muted line in place of the type's pill, such as the folder an asset is in; empty for the pill.
    hpp::string_view caption;
    /// A short type-specific fact badged in the thumbnail's bottom-right corner; empty for none.
    hpp::string_view summary;
    ImTextureID texture{};
    /// Size of the texture in pixels, for its aspect.
    ImVec2 texture_size{};
    /// Drawn in the middle of the thumbnail while there is no texture; null for nothing.
    const char* glyph{};
    bool is_folder{};
    /// An asset an import writes, such as a model's default material: badged in the thumbnail's top-left corner.
    bool is_generated{};
    /// Such an asset was edited since its import wrote it, so imports keep it; its badge says so.
    bool is_generated_edited{};
    bool is_loading{};
    bool is_selected{};
    bool is_focused{};
    /// What a field holds now: a check badged in the thumbnail's top-right corner.
    bool is_current{};
    /// Width of the card in pixels.
    float size{};
};

/**
 * @brief Draws a card as a single ImGui item.
 *
 * The thumbnail sits in a well with the summary badged in its bottom-right corner, an import mark in its top-left one
 * and the current mark in its top-right one; a strip in the type's accent color runs under it, then the name centered
 * on up to two lines and the caption, or the type in bold on a centered pill of the accent color. A loading card shows
 * a spinner in place of its thumbnail; selection, focus and the pointer draw rings around the card.
 * @param card What the card shows.
 * @param id The item's id.
 * @return True when the card was pressed.
 */
auto draw_content_card(const content_card_view& card, ImGuiID id) -> bool;

/// Height of a card of the given width in pixels, for a host that lays out rows of cards.
auto get_content_card_height(float width) -> float;

/// The accent color of an asset type ("Material"), as its cards show it; a neutral gray for types without one.
auto get_asset_type_accent(hpp::string_view type) -> ImU32;

/// The type in bold on a pill of its accent color as an item at the cursor, then the summary beside it in dim text,
/// as a card's tooltip shows them. The pill is ellipsized to max_width.
void draw_asset_type_line(hpp::string_view type, hpp::string_view summary, float max_width);

/// Titled sections of labeled values, every value on one column, as a card's tooltip shows them.
void draw_content_sections(const content_item_sections& sections, float content_width);

/// An arc turning in the middle of the rectangle, as a card shows while its asset loads.
void draw_loading_spinner(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max);

/**
 * @brief One entry of the content browser grid as drawn this frame: what its card and tooltip show, and what the
 * entry does when acted on.
 */
struct content_browser_item
{
    using on_action_t = std::function<void()>;
    using on_rename_t = std::function<void(const std::string&)>;
    /// Appends the type-specific tooltip sections; runs only while the tooltip is shown.
    using collect_details_t = std::function<void(content_item_sections&)>;

    /**
     * @brief Creates the item of a directory cache entry; the type is derived from the entry.
     * @param e The entry, which must outlive the item.
     */
    explicit content_browser_item(const fs::directory_cache::cache_entry& e);

    /// Whether the entry is a folder.
    auto is_folder() const -> bool;

    const fs::directory_cache::cache_entry& entry;
    /// Display name of the entry's type ("Mesh", "Folder"); empty for files of no asset type.
    const std::string& type;

    on_action_t on_click;
    on_action_t on_double_click;
    on_action_t on_delete;
    on_action_t on_cancel;
    on_rename_t on_rename;
    collect_details_t collect_details;

    gfx::texture::ptr icon;
    /// The asset's uid; nil for folders and files of no asset type.
    hpp::uuid uid;
    /// A short type-specific fact badged on the card's thumbnail, such as a triangle count; empty for none.
    std::string summary;
    /// An asset an import writes, such as a model's default material: badged in the thumbnail's top-left corner.
    bool is_generated{};
    /// Such an asset was edited since its import wrote it, so imports keep it; its badge says so.
    bool is_generated_edited{};
    bool is_loading{};
    bool is_selected{};
    bool is_focused{};
    /// Width of the card in pixels.
    float size{};
};

/**
 * @brief Draws the card of an item as a single ImGui item.
 *
 * The card shows the thumbnail in a well with the summary badged in its bottom-right corner and, for an asset an
 * import writes, an import mark in its top-left one; a strip in the type's accent color, the name centered on up to
 * two lines, and the type in bold on a centered pill of the accent color. A loading item shows a spinner in place of
 * its thumbnail; selection and focus draw rings around the card.
 * @param item The item to draw.
 * @return True when the card was pressed.
 */
auto draw_content_card(const content_browser_item& item) -> bool;

/**
 * @brief Draws the tooltip of the card drawn last while it is hovered: the item's file facts and type-specific
 * details, or a large thumbnail preview while Shift is held.
 * @param item The item whose card was drawn last.
 */
void draw_content_tooltip(const content_browser_item& item);

} // namespace unravel
