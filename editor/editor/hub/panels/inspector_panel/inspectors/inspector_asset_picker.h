#pragma once

#include <imgui/imgui.h>

#include <cstddef>
#include <functional>
#include <string>

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
};

/// What the field was asked for this frame.
struct field_request
{
    bool is_pick_pressed{};
    bool is_locate_pressed{};
    bool is_clear_pressed{};
};

//-----------------------------------------------------------------------------
/// <summary>
/// The thumbnail and a field naming the asset, both opening the picker, and under the field
/// buttons to show the asset in the content browser and to clear it, beside its path. Drawn as
/// one group, so a drop target set on the last item takes the whole field.
/// </summary>
//-----------------------------------------------------------------------------
auto draw_field(const field_content& content) -> field_request;

/// Colors the last item as a place to drop, while a drag that fits it goes on.
void draw_drop_highlight(bool is_drop_possible);

/// One tile of the picker grid.
struct tile_content
{
    preview thumbnail;
    std::string name;
    /// The asset the property has now.
    bool is_selected{};
};

/// What the grid of the picker was asked for this frame.
struct grid_result
{
    /// Index of the tile clicked, -1 for none.
    int picked{-1};
    /// Index of the tile under the pointer, -1 for none.
    int hovered{-1};
};

//-----------------------------------------------------------------------------
/// <summary>
/// Opens the picker window when it is not open: a modal with a title, a search field over a
/// grid of tiles, and a footer. Returns true while it is open; end_window() follows then.
/// </summary>
//-----------------------------------------------------------------------------
auto begin_window(const char* popup_id, const char* icon, const std::string& title, ImGuiTextFilter& filter) -> bool;
void end_window();

/// The tiles, sized by the size slider of the footer. Only the visible rows are drawn.
auto draw_grid(std::size_t count, const std::function<tile_content(std::size_t)>& get_tile) -> grid_result;

/// A line about the tile under the pointer, and the slider that sizes the tiles.
void draw_footer(const std::string& text);

} // namespace unravel::asset_picker
