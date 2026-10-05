#pragma once
#include "content_browser_item_details.h"

#include <filesystem/cache.hpp>
#include <graphics/texture.h>

#include <functional>
#include <string>

namespace unravel
{

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
    bool is_loading{};
    bool is_selected{};
    bool is_focused{};
    /// Width of the card in pixels.
    float size{};
};

/**
 * @brief Draws the card of an item as a single ImGui item.
 *
 * The card shows the thumbnail in a well with the summary badged in its corner, a strip in the type's accent color,
 * the name, and the type as a caption. A loading item shows a spinner in place of its thumbnail; selection and focus
 * draw rings around the card.
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
