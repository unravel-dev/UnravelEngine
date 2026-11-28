#pragma once

#include <editor/hub/panels/content_browser_panel/content_browser_item_details.h>
#include <hpp/optional.hpp>
#include <imgui/imgui.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/// The field of an asset property and the window that picks one. Drawing only: the templated
/// pick_asset in inspector_assets.cpp feeds them the assets of its type and applies the result.
namespace unravel::asset_picker
{

/// A thumbnail, or an icon while there is none.
struct preview
{
    ImTextureID texture{};
    /// Size of the texture in pixels, for its aspect.
    ImVec2 texture_size{};
    bool is_loading{};
    /// Shown when there is no texture.
    const char* icon{};
};

/// What the field of an asset property shows.
struct field_content
{
    preview thumbnail;
    /// Empty when no asset is set.
    std::string name;
    /// The type of asset, "Material".
    std::string type;
    /// Where the asset is, "app:/data/Materials/M_Gold.mat".
    std::string path;
    /// Where opening the asset shows it, "the Material Graph"; empty for a type nothing opens.
    std::string open_in;
    /// The row says its asset is wrong for it: the field is outlined in the warning color.
    bool has_problem{};
    /// The field shows its asset, locates and opens it, and neither picks nor clears.
    bool is_read_only{};
};

/// What the field was asked for this frame.
struct field_request
{
    bool is_pick_pressed{};
    bool is_locate_pressed{};
    bool is_clear_pressed{};
    bool is_open_pressed{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// The thumbnail and a field naming the asset, both opening the picker, and under the field
/// buttons to show the asset in the content browser and to clear it, beside its path. Drawn as
/// one group, so a drop target set on the last item takes the whole field.
///
/// An asset that opens somewhere (content.open_in) gets a button for it, and opens on a
/// double-click of its thumbnail: a click there is then the first half of the double-click, and
/// the name alone opens the picker.
/// </summary>
//-----------------------------------------------------------------------------
auto draw_field(const field_content& content) -> field_request;

/// Colors the last item as a place to drop, while a drag that fits it goes on.
void draw_drop_highlight(bool is_drop_possible);

/// Whether an entry suits the field the picker fills.
enum class fit_state
{
    /// Not known yet: the asset still loads. Shown with the ones that suit until it is known.
    unknown,
    fits,
    unfit,
};

/// An entry's fit, with a sentence the details pane shows: what the asset does in the field, or why it does not suit it.
struct entry_fit
{
    fit_state state{fit_state::unknown};
    std::string reason;
};

/// One choice of the picker.
struct entry
{
    /// Where the asset is, "app:/data/Materials/M_Gold.mat"; empty for the None entry, which clears the field.
    std::string path;
    std::string name;
};

/// What the picker window says around its entries.
struct window_content
{
    const char* icon{};
    /// The type of asset, "Material": the accent of the cards and the type the details name.
    std::string type;
    /// "Select Material".
    std::string title;
    /// A sentence under the title, such as what the field holds now.
    std::string description;
    /// The button that takes the highlighted entry, "Select".
    std::string confirm_label;
    /// What the results say while there is nothing to pick, without a search.
    std::string empty_text;
    /// Index of the entry the field holds now: highlighted when the window opens and marked on its card.
    hpp::optional<std::size_t> current;
    /// What the field needs its asset for, "Particles", when only some entries suit it (entry_source::get_fit). The
    /// picker then opens on the ones that suit, under a sidebar row named for it, tags their cards with it and veils
    /// the others, which All still shows. Empty for a field that takes every entry alike.
    std::string fit_use;
};

/// What the picker asks of the entries it draws, by their index. Each runs only for what is on screen: asking for a
/// thumbnail is what starts loading an asset nothing else uses.
struct entry_source
{
    std::function<preview(std::size_t)> get_preview;
    /// A short fact badged on the card, such as a triangle count; may be empty.
    std::function<std::string(std::size_t)> get_summary;
    /// The facts the details pane lists for the highlighted entry; may be empty.
    std::function<void(std::size_t, content_item_sections&)> collect_details;
    /// Whether the entry suits the field; empty for a field that takes every entry alike. Asked for every entry each
    /// frame the picker is open.
    std::function<entry_fit(std::size_t)> get_fit;
};

/// Opens the picker with this id. Call from the ID scope draw_window() is called from.
void open_window(const char* popup_id);

/// Whether the picker with this id is open, so its entries are gathered only then.
auto is_window_open(const char* popup_id) -> bool;

//-----------------------------------------------------------------------------
/// <summary>
/// Draws the picker while it is open: a modal with a title, a search field, the folders of the
/// entries beside them as cards or a list, a pane with the details of the highlighted entry, and
/// Cancel and confirm buttons. A click highlights an entry; a double-click, Enter or the confirm
/// button picks it and closes the window. The arrow keys move the highlight while typing.
/// </summary>
/// <returns>The index of the entry picked this frame, if one was</returns>
//-----------------------------------------------------------------------------
auto draw_window(const char* popup_id,
                 const window_content& content,
                 const std::vector<entry>& entries,
                 const entry_source& source) -> hpp::optional<std::size_t>;

} // namespace unravel::asset_picker
