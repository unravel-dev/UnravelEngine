#include "inspector_asset_picker.h"
#include "inspector_container_widgets.h"

#include "editor/imgui/integration/fonts/icons/icons_material_design_icons.h"
#include "editor/imgui/integration/imgui.h"
#include "editor/imgui/integration/imgui_style.h"
#include "imgui/imgui_internal.h"
#include "imgui_widgets/tooltips.h"
#include "imgui_widgets/utils.h"
#include <editor/hub/panels/content_browser_panel/content_browser_item.h>
#include <editor/hub/panels/deploy_panel/deploy_panel_widgets.h>
#include <editor/hub/panels/panel_toolbar.h>
#include <editor/imgui/screen_card.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <map>
#include <unordered_map>

namespace unravel::asset_picker
{
namespace
{
using container_widgets::to_pixels;

// Sizes are in units of the font size.
constexpr float PICKER_PREVIEW_ROUNDING = 0.3f;
constexpr float PICKER_FIELD_TEXT_PADDING = 0.5f;
constexpr float PICKER_EMPTY_ICON_SCALE = 1.6f;
constexpr float PICKER_DROP_BORDER = 2.0f;
constexpr float PICKER_MUTED_ALPHA = 0.55f;
constexpr ImU32 PICKER_PREVIEW_COLOR = IM_COL32(0, 0, 0, 40);
constexpr ImU32 PICKER_PREVIEW_HOVERED_BORDER_COLOR = IM_COL32(255, 255, 255, 60);

// The picker window.
constexpr float PICKER_WINDOW_SHARE_X = 0.62f;
constexpr float PICKER_WINDOW_SHARE_Y = 0.72f;
constexpr float PICKER_WINDOW_MIN_WIDTH = 50.0f;
constexpr float PICKER_WINDOW_MIN_HEIGHT = 30.0f;
constexpr float PICKER_WINDOW_ROUNDING = 0.75f;
constexpr float PICKER_TITLE_SCALE = 1.3f;
constexpr float PICKER_CLOSE_INSET = 0.6f;
constexpr float PICKER_SECTION_GAP = 0.7f;
constexpr float PICKER_LINE_GAP = 0.35f;
constexpr float PICKER_SIDEBAR_WIDTH = 13.0f;
constexpr float PICKER_DETAILS_WIDTH = 19.0f;
constexpr float PICKER_PANE_ROUNDING = 0.45f;
constexpr float PICKER_PANE_PADDING = 0.6f;
constexpr float PICKER_SEARCH_MIN_WIDTH = 10.0f;
constexpr float PICKER_SIZE_SLIDER_WIDTH = 6.0f;
constexpr float PICKER_TILE_MIN_SIZE = 5.0f;
constexpr float PICKER_TILE_MAX_SIZE = 12.0f;
constexpr float PICKER_TILE_DEFAULT_SIZE = 7.5f;
constexpr float PICKER_TILE_GAP = 0.6f;
constexpr float PICKER_ROW_HEIGHT = 2.6f;
constexpr float PICKER_ROW_GAP = 0.15f;
constexpr float PICKER_ROW_ROUNDING = 0.35f;
constexpr float PICKER_ROW_INSET = 0.25f;
constexpr float PICKER_ROW_CAPTION_SCALE = 0.85f;
constexpr float PICKER_DETAILS_THUMBNAIL_MAX = 15.0f;
constexpr float PICKER_DETAILS_NAME_SCALE = 1.15f;
constexpr float PICKER_DETAILS_PATH_SCALE = 0.85f;
constexpr float PICKER_DETAILS_GLYPH_SHARE = 0.35f;
constexpr float PICKER_EMPTY_STATE_ICON_SCALE = 2.4f;
constexpr float PICKER_FOOTER_HEIGHT = 2.2f;
/// Pixels, as the content browser's cards draw their strip.
constexpr float PICKER_ACCENT_STRIP_HEIGHT = 2.0f;
/// A thumbnail's well, a step darker than the pane, as on the content browser's cards.
constexpr ImU32 PICKER_WELL_COLOR = IM_COL32(0, 0, 0, 56);
/// The panes of the window, a step darker than the window.
constexpr ImU32 PICKER_PANE_COLOR = IM_COL32(0, 0, 0, 38);
constexpr ImU32 PICKER_ROW_HOVERED_COLOR = IM_COL32(255, 255, 255, 22);
constexpr ImU32 PICKER_ROW_HELD_COLOR = IM_COL32(255, 255, 255, 34);
/// Picks remembered per type of asset for the Recent list, newest first.
constexpr std::size_t PICKER_RECENT_MAX = 12;
/// Frames the highlight is scrolled into view for after the window opens, while the panes settle their sizes.
constexpr int PICKER_REVEAL_FRAMES = 3;

auto get_muted_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, PICKER_MUTED_ALPHA);
}

auto get_accent_color(float alpha = 1.0f) -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(accent);
}

/// An icon at a given size in pixels, centered on the point.
void draw_icon(ImDrawList* draw_list, const ImVec2& center, const char* icon, ImU32 color, float size)
{
    // Relative to the current size, so the UI scale is applied once.
    ImGui::PushWindowFontScale(size / ImGui::GetFontSize());
    ImGui::RenderIconCentered(draw_list, center, icon, color);
    ImGui::PopWindowFontScale();
}

/// The texture fitted into the rectangle by its aspect.
void draw_fitted_image(ImDrawList* draw_list, const ImRect& rect, const preview& content, float rounding)
{
    const ImVec2 area = rect.GetSize();
    const ImVec2 texture_size = content.texture_size.x > 0.0f && content.texture_size.y > 0.0f ? content.texture_size : area;
    const float scale = std::min(area.x / texture_size.x, area.y / texture_size.y);
    const ImVec2 image_size = texture_size * scale;
    const ImVec2 image_min = ImFloor(rect.GetCenter() - image_size * 0.5f);
    draw_list->AddImageRounded(content.texture,
                               image_min,
                               image_min + image_size,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               IM_COL32_WHITE,
                               rounding);
}

/// The preview filling the rectangle: the texture fitted by its aspect, or else the icon.
void draw_preview(ImDrawList* draw_list, const ImRect& rect, const preview& content, float icon_size)
{
    const float rounding = to_pixels(PICKER_PREVIEW_ROUNDING);
    draw_list->AddRectFilled(rect.Min, rect.Max, PICKER_PREVIEW_COLOR, rounding);
    if(content.is_loading)
    {
        draw_icon(draw_list, rect.GetCenter(), ICON_MDI_PROGRESS_CLOCK, get_muted_color(), icon_size);
        return;
    }
    if(content.texture == ImTextureID{})
    {
        if(content.icon != nullptr)
        {
            draw_icon(draw_list, rect.GetCenter(), content.icon, get_muted_color(), icon_size);
        }
        return;
    }
    draw_fitted_image(draw_list, rect, content, rounding);
}

/// Text cut with an ellipsis where it does not fit.
void draw_clipped_text(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(draw_list, min, max, max.x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}

/// An icon button that takes its place in the layout.
auto draw_inline_icon_button(const char* id, const char* icon, const char* tooltip) -> bool
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(side, side));
    ImGui::ItemSize(rect);
    return container_widgets::draw_icon_button({id, icon, tooltip}, rect);
}

/// The tooltip of the parts of the field that open the picker.
void set_pick_tooltip(const field_content& content)
{
    const bool has_asset = !content.name.empty();
    const std::string subject = has_asset ? content.path : fmt::format("None ({})", content.type);
    ImGui::SetItemTooltipEx("%s\n\nClick to pick another %s.", subject.c_str(), content.type.c_str());
}

/// Whether the field holds an asset that opens somewhere.
auto can_open(const field_content& content) -> bool
{
    return !content.name.empty() && !content.open_in.empty();
}

auto make_open_tooltip(const field_content& content) -> std::string
{
    return fmt::format("Open in {}", content.open_in);
}

/// The thumbnail's tooltip: a thumbnail that opens its asset leaves picking to the name.
void set_preview_tooltip(const field_content& content)
{
    if(!can_open(content))
    {
        set_pick_tooltip(content);
        return;
    }
    ImGui::SetItemTooltipEx("%s\n\nDouble-click to open it in %s. Click the name to pick another %s.",
                            content.path.c_str(),
                            content.open_in.c_str(),
                            content.type.c_str());
}

/// The field naming the asset, drawn as an input field with the picker icon at its right end.
auto draw_pick_field(const field_content& content) -> bool
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    const bool is_pressed = ImGui::InvisibleButton("##pick", size);
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_active = ImGui::IsItemActive();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(min, min + size);
    const ImGuiCol fill = is_active ? ImGuiCol_FrameBgActive : (is_hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);

    const float side = size.y;
    ImGui::RenderIconCentered(draw_list,
                              ImVec2(rect.Max.x - side * 0.5f, rect.GetCenter().y),
                              ICON_MDI_TARGET,
                              is_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color());

    const bool has_asset = !content.name.empty();
    const std::string text = has_asset ? content.name : fmt::format("None ({})", content.type);
    const float text_padding = to_pixels(PICKER_FIELD_TEXT_PADDING);
    const float text_y = rect.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f;
    draw_clipped_text(draw_list,
                      ImVec2(rect.Min.x + text_padding, text_y),
                      ImVec2(rect.Max.x - side, rect.Max.y),
                      text.c_str(),
                      has_asset ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_color());
    ImGui::DrawItemActivityOutline();
    set_pick_tooltip(content);
    return is_pressed;
}

/// Under the field: show in the content browser, clear, and where the asset is.
void draw_field_actions(const field_content& content, field_request& request)
{
    const bool has_asset = !content.name.empty();
    ImGui::BeginDisabled(!has_asset);
    request.is_locate_pressed |= draw_inline_icon_button("##locate", ICON_MDI_CROSSHAIRS_GPS, "Show in the content browser");
    ImGui::SameLine(0.0f, 0.0f);
    request.is_clear_pressed = draw_inline_icon_button("##clear", ICON_MDI_CLOSE_CIRCLE_OUTLINE, "Clear");
    if(!content.open_in.empty())
    {
        ImGui::SameLine(0.0f, 0.0f);
        request.is_open_pressed |= draw_inline_icon_button("##open", ICON_MDI_GRAPH, make_open_tooltip(content).c_str());
    }
    ImGui::EndDisabled();
    if(!has_asset)
    {
        return;
    }
    ImGui::SameLine();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::ItemSize(size);
    const float text_y = min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    draw_clipped_text(ImGui::GetWindowDrawList(), ImVec2(min.x, text_y), min + size, content.path.c_str(), get_muted_color());
}

//-----------------------------------------------------------------------------
// The picker window.
//-----------------------------------------------------------------------------

enum class picker_view
{
    /// The content browser's cards.
    grid,
    /// One row per entry with its folder.
    list,
};

/// Which entries the results show.
enum class picker_scope
{
    all,
    /// What was picked for this type of asset before, newest first.
    recent,
    /// The entries in a folder and the folders under it.
    folder,
};

/// What the open picker keeps between frames.
struct picker_state
{
    ImGuiTextFilter search;
    /// The search as it was last frame: a new search highlights its best match.
    std::string last_search;
    picker_scope scope{picker_scope::all};
    /// The folder shown while the scope is a folder, "app:/data/Materials".
    std::string folder;
    /// The path of the entry highlighted; empty for the None entry.
    hpp::optional<std::string> highlighted;
    /// Frames left to scroll the highlight into view, and whether to the middle of the results.
    int reveal_frames{};
    bool is_reveal_centered{};
    bool is_search_focus_requested{};
    /// The grid's columns and the rows a page of the results holds, as drawn last frame, for the keys.
    int columns{1};
    int page_rows{1};
};

picker_state g_picker;
/// Kept for the session: every picker opens the way the last one was left.
picker_view g_view = picker_view::grid;
float g_tile_size = PICKER_TILE_DEFAULT_SIZE;
/// Paths picked before, by type of asset, newest first.
std::map<std::string, std::vector<std::string>> g_recent_paths;

/// Where an entry is: its root ("app:/data", the Project) and the folders under it.
struct entry_location
{
    /// "app:/data"; a path without a protocol has the empty root.
    std::string root;
    /// "Project", "Engine".
    std::string root_label;
    /// "app:/data/Materials/Metals"; the root for an entry right in it.
    std::string folder;
    /// {"Materials", "Metals"}
    std::vector<std::string> folder_names;
};

/// What the picker derives from an entry for its searches, folders and captions.
struct entry_info
{
    entry_location location;
    /// The entry's folder's name, or its root's label right in the root: the caption of its card.
    std::string caption;
    /// "Project/Materials/Metals", for the list.
    std::string folder_label;
    std::string lower_name;
    std::string lower_path;
};

auto get_root_label(const std::string& protocol) -> std::string
{
    if(protocol == "app")
    {
        return "Project";
    }
    if(protocol == "engine")
    {
        return "Engine";
    }
    return protocol;
}

auto get_root_icon(const std::string& root) -> const char*
{
    if(root.rfind("app:", 0) == 0)
    {
        return ICON_MDI_HOME;
    }
    if(root.rfind("engine:", 0) == 0)
    {
        return ICON_MDI_ENGINE_OUTLINE;
    }
    return ICON_MDI_PACKAGE_VARIANT_CLOSED;
}

/// The root and folders of a path such as "app:/data/Materials/Metals/M_Gold.mat". The data folder every protocol
/// keeps its assets in belongs to the root.
auto locate_entry(const std::string& path) -> entry_location
{
    entry_location location;
    const std::size_t protocol_end = path.find(":/");
    std::size_t pos = 0;
    if(protocol_end != std::string::npos)
    {
        location.root_label = get_root_label(path.substr(0, protocol_end));
        location.root = path.substr(0, protocol_end + 1);
        pos = protocol_end + 2;
        const std::string data_folder = "data/";
        if(path.compare(pos, data_folder.size(), data_folder) == 0)
        {
            location.root = path.substr(0, pos + data_folder.size() - 1);
            pos += data_folder.size();
        }
    }
    location.folder = location.root;
    for(std::size_t slash = path.find('/', pos); slash != std::string::npos; slash = path.find('/', pos))
    {
        location.folder_names.emplace_back(path.substr(pos, slash - pos));
        location.folder += "/" + location.folder_names.back();
        pos = slash + 1;
    }
    return location;
}

auto make_entry_info(const entry& item) -> entry_info
{
    entry_info info;
    info.lower_name = string_utils::to_lower(item.name);
    if(item.path.empty())
    {
        return info;
    }
    info.location = locate_entry(item.path);
    info.lower_path = string_utils::to_lower(item.path);
    info.caption = info.location.folder_names.empty() ? info.location.root_label : info.location.folder_names.back();
    info.folder_label = info.location.root_label;
    for(const auto& name : info.location.folder_names)
    {
        info.folder_label += "/" + name;
    }
    return info;
}

/// The lowered words of a search.
auto split_search_terms(const char* text) -> std::vector<std::string>
{
    std::vector<std::string> terms;
    std::string term;
    for(const char* c = text; *c != '\0'; ++c)
    {
        if(std::isspace(static_cast<unsigned char>(*c)) != 0)
        {
            if(!term.empty())
            {
                terms.push_back(std::move(term));
                term.clear();
            }
            continue;
        }
        term.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));
    }
    if(!term.empty())
    {
        terms.push_back(std::move(term));
    }
    return terms;
}

/// Whether every word of the search is in the entry's path, which holds its name and its folders.
auto matches_search(const entry_info& info, const std::vector<std::string>& terms) -> bool
{
    return !info.lower_path.empty() && std::all_of(terms.begin(),
                                                   terms.end(),
                                                   [&info](const std::string& term)
                                                   {
                                                       return info.lower_path.find(term) != std::string::npos;
                                                   });
}

/// How well a match fits the search, best first: the whole name, a name starting with the first word, every word in
/// the name, and words found in the folders only.
auto rank_match(const entry_info& info, const std::vector<std::string>& terms) -> int
{
    if(terms.size() == 1 && info.lower_name == terms.front())
    {
        return 0;
    }
    if(info.lower_name.rfind(terms.front(), 0) == 0)
    {
        return 1;
    }
    const bool is_all_in_name = std::all_of(terms.begin(),
                                            terms.end(),
                                            [&info](const std::string& term)
                                            {
                                                return info.lower_name.find(term) != std::string::npos;
                                            });
    return is_all_in_name ? 2 : 3;
}

/// Whether the entry is in the folder or a folder under it.
auto is_in_folder(const entry_info& info, const std::string& folder) -> bool
{
    const std::string& own = info.location.folder;
    return !info.lower_path.empty() && own.size() >= folder.size() && own.compare(0, folder.size(), folder) == 0 &&
           (own.size() == folder.size() || own[folder.size()] == '/');
}

/// One folder of the sidebar.
struct folder_node
{
    std::string key;
    std::string name;
    /// The root's icon; folders show theirs open or closed.
    const char* icon{};
    bool is_root{};
    /// Entries in it and under it, and how many of them pass the search.
    std::size_t total{};
    std::size_t matched{};
    std::vector<std::size_t> children;
};

struct folder_tree
{
    std::vector<folder_node> nodes;
    std::vector<std::size_t> roots;
};

/// Project first, then the engine, then the rest by name.
auto get_root_order(const folder_node& node) -> int
{
    if(node.key.rfind("app:", 0) == 0)
    {
        return 0;
    }
    if(node.key.rfind("engine:", 0) == 0)
    {
        return 1;
    }
    return 2;
}

auto is_name_less(const std::string& lhs, const std::string& rhs) -> bool
{
    return std::lexicographical_compare(lhs.begin(),
                                        lhs.end(),
                                        rhs.begin(),
                                        rhs.end(),
                                        [](char a, char b)
                                        {
                                            return std::tolower(static_cast<unsigned char>(a)) <
                                                   std::tolower(static_cast<unsigned char>(b));
                                        });
}

/// The folders that hold entries, each counting the entries in it and under it.
auto build_folder_tree(const std::vector<entry_info>& infos, const std::vector<bool>& matched) -> folder_tree
{
    folder_tree tree;
    std::unordered_map<std::string, std::size_t> by_key;
    const auto find_or_add = [&](const std::string& key, const std::string& name, const char* icon, hpp::optional<std::size_t> parent)
    {
        const auto found = by_key.find(key);
        if(found != by_key.end())
        {
            return found->second;
        }
        const std::size_t index = tree.nodes.size();
        tree.nodes.push_back({key, name, icon, !parent.has_value()});
        by_key.emplace(key, index);
        if(parent)
        {
            tree.nodes[*parent].children.push_back(index);
        }
        else
        {
            tree.roots.push_back(index);
        }
        return index;
    };
    for(std::size_t index = 0; index < infos.size(); ++index)
    {
        const entry_info& info = infos[index];
        if(info.lower_path.empty())
        {
            continue;
        }
        const entry_location& location = info.location;
        std::size_t node = find_or_add(location.root, location.root_label, get_root_icon(location.root), hpp::nullopt);
        std::vector<std::size_t> chain{node};
        std::string key = location.root;
        for(const auto& name : location.folder_names)
        {
            key += "/" + name;
            node = find_or_add(key, name, ICON_MDI_FOLDER, node);
            chain.push_back(node);
        }
        for(const std::size_t step : chain)
        {
            ++tree.nodes[step].total;
            tree.nodes[step].matched += matched[index] ? 1 : 0;
        }
    }
    for(auto& node : tree.nodes)
    {
        std::sort(node.children.begin(),
                  node.children.end(),
                  [&tree](std::size_t lhs, std::size_t rhs)
                  {
                      return is_name_less(tree.nodes[lhs].name, tree.nodes[rhs].name);
                  });
    }
    std::sort(tree.roots.begin(),
              tree.roots.end(),
              [&tree](std::size_t lhs, std::size_t rhs)
              {
                  const int lhs_order = get_root_order(tree.nodes[lhs]);
                  const int rhs_order = get_root_order(tree.nodes[rhs]);
                  return lhs_order != rhs_order ? lhs_order < rhs_order : is_name_less(tree.nodes[lhs].name, tree.nodes[rhs].name);
              });
    return tree;
}

/// Everything one frame of the picker draws from.
struct picker_frame
{
    const window_content& content;
    const std::vector<entry>& entries;
    const entry_source& source;
    std::vector<entry_info> infos;
    std::vector<std::string> terms;
    /// Per entry, whether it passes the search; all pass while there is none.
    std::vector<bool> matched;
    folder_tree folders;
    /// The entries the results show, in order.
    std::vector<std::size_t> shown;
    /// The recent picks still among the entries, newest first.
    std::vector<std::size_t> recent;
    /// The entry highlighted, if it is shown.
    hpp::optional<std::size_t> highlighted;
    /// The position of the highlighted entry in shown.
    hpp::optional<std::size_t> highlighted_position;

    auto is_searching() const -> bool
    {
        return !terms.empty();
    }
};

auto find_entry_by_path(const std::vector<entry>& entries, const std::string& path) -> hpp::optional<std::size_t>
{
    const auto found = std::find_if(entries.begin(),
                                    entries.end(),
                                    [&path](const entry& item)
                                    {
                                        return item.path == path;
                                    });
    return found == entries.end() ? hpp::nullopt : hpp::optional<std::size_t>(std::size_t(found - entries.begin()));
}

auto collect_recent(const picker_frame& frame) -> std::vector<std::size_t>
{
    std::vector<std::size_t> recent;
    const auto found = g_recent_paths.find(frame.content.type);
    if(found == g_recent_paths.end())
    {
        return recent;
    }
    for(const auto& path : found->second)
    {
        if(const auto index = find_entry_by_path(frame.entries, path))
        {
            recent.push_back(*index);
        }
    }
    return recent;
}

/// Whether the results show the entry in the scope chosen, apart from the search.
auto is_in_scope(const picker_frame& frame, std::size_t index) -> bool
{
    const bool is_none = frame.entries[index].path.empty();
    switch(g_picker.scope)
    {
        case picker_scope::folder:
            return is_in_folder(frame.infos[index], g_picker.folder);
        case picker_scope::all:
        default:
            // None clears the field; it stands first while nothing is searched.
            return !is_none || !frame.is_searching();
    }
}

/// The entries of the scope that pass the search: the recent ones newest first, the others by how well they match
/// the search and then by name, None first.
auto collect_shown(const picker_frame& frame) -> std::vector<std::size_t>
{
    std::vector<std::size_t> shown;
    if(g_picker.scope == picker_scope::recent)
    {
        std::copy_if(frame.recent.begin(),
                     frame.recent.end(),
                     std::back_inserter(shown),
                     [&frame](std::size_t index)
                     {
                         return frame.matched[index];
                     });
        return shown;
    }
    for(std::size_t index = 0; index < frame.entries.size(); ++index)
    {
        if(frame.matched[index] && is_in_scope(frame, index))
        {
            shown.push_back(index);
        }
    }
    std::vector<int> ranks(frame.entries.size(), 0);
    if(frame.is_searching())
    {
        for(const std::size_t index : shown)
        {
            ranks[index] = rank_match(frame.infos[index], frame.terms);
        }
    }
    std::stable_sort(shown.begin(),
                     shown.end(),
                     [&frame, &ranks](std::size_t lhs, std::size_t rhs)
                     {
                         const bool lhs_none = frame.entries[lhs].path.empty();
                         const bool rhs_none = frame.entries[rhs].path.empty();
                         if(lhs_none != rhs_none)
                         {
                             return lhs_none;
                         }
                         if(ranks[lhs] != ranks[rhs])
                         {
                             return ranks[lhs] < ranks[rhs];
                         }
                         if(frame.infos[lhs].lower_name != frame.infos[rhs].lower_name)
                         {
                             return frame.infos[lhs].lower_name < frame.infos[rhs].lower_name;
                         }
                         return frame.entries[lhs].path < frame.entries[rhs].path;
                     });
    return shown;
}

auto make_frame(const window_content& content, const std::vector<entry>& entries, const entry_source& source) -> picker_frame
{
    picker_frame frame{content, entries, source};
    frame.infos.reserve(entries.size());
    for(const auto& item : entries)
    {
        frame.infos.push_back(make_entry_info(item));
    }
    frame.terms = split_search_terms(g_picker.search.InputBuf);
    frame.matched.resize(entries.size(), true);
    if(frame.is_searching())
    {
        for(std::size_t index = 0; index < entries.size(); ++index)
        {
            frame.matched[index] = matches_search(frame.infos[index], frame.terms);
        }
    }
    frame.folders = build_folder_tree(frame.infos, frame.matched);
    frame.recent = collect_recent(frame);
    if(g_picker.scope == picker_scope::recent && frame.recent.empty())
    {
        g_picker.scope = picker_scope::all;
    }
    frame.shown = collect_shown(frame);
    return frame;
}

/// Scrolls the highlight into view over the next frames: to the middle as the window opens, by as little as it takes
/// after a key. A click on the toolbar or the folders comes before the results are drawn with what it chose, so the
/// reveal lasts into the frame after it.
void request_reveal(bool is_centered, int frames = 2)
{
    g_picker.reveal_frames = frames;
    g_picker.is_reveal_centered = is_centered;
}

/// Highlights the shown entry at the position.
void highlight_position(picker_frame& frame, std::size_t position)
{
    const std::size_t index = frame.shown[position];
    g_picker.highlighted = frame.entries[index].path;
    frame.highlighted = index;
    frame.highlighted_position = position;
}

/// Keeps the highlight on a shown entry: a new search highlights its best match, and a highlight the results no longer
/// show moves to their first entry.
void update_highlight(picker_frame& frame)
{
    const std::string search = g_picker.search.InputBuf;
    const bool is_search_changed = search != g_picker.last_search;
    g_picker.last_search = search;
    frame.highlighted.reset();
    frame.highlighted_position.reset();
    if(frame.shown.empty())
    {
        return;
    }
    if(!is_search_changed && g_picker.highlighted)
    {
        for(std::size_t position = 0; position < frame.shown.size(); ++position)
        {
            if(frame.entries[frame.shown[position]].path == *g_picker.highlighted)
            {
                highlight_position(frame, position);
                return;
            }
        }
    }
    highlight_position(frame, 0);
    request_reveal(false);
}

/// Opens the picker as it starts: no search, every entry, the field's entry highlighted in the middle.
void start_picker(const window_content& content, const std::vector<entry>& entries)
{
    g_picker.search.Clear();
    g_picker.last_search.clear();
    g_picker.scope = picker_scope::all;
    g_picker.folder.clear();
    g_picker.highlighted.reset();
    if(content.current && *content.current < entries.size())
    {
        g_picker.highlighted = entries[*content.current].path;
    }
    g_picker.is_search_focus_requested = true;
    g_picker.columns = 1;
    g_picker.page_rows = 1;
    request_reveal(true, PICKER_REVEAL_FRAMES);
}

/// Remembers the pick for the Recent list of its type.
void remember_pick(const std::string& type, const std::string& path)
{
    if(path.empty())
    {
        return;
    }
    auto& paths = g_recent_paths[type];
    paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
    paths.insert(paths.begin(), path);
    if(paths.size() > PICKER_RECENT_MAX)
    {
        paths.resize(PICKER_RECENT_MAX);
    }
}

auto begin_picker_window(const char* popup_id) -> bool
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 min_size = ImMin(ImVec2(to_pixels(PICKER_WINDOW_MIN_WIDTH), to_pixels(PICKER_WINDOW_MIN_HEIGHT)), viewport->WorkSize);
    const ImVec2 size = ImMax(viewport->WorkSize * ImVec2(PICKER_WINDOW_SHARE_X, PICKER_WINDOW_SHARE_Y), min_size);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(min_size, viewport->WorkSize);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, to_pixels(PICKER_WINDOW_ROUNDING));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const bool is_open = ImGui::BeginPopupModal(popup_id, nullptr, flags);
    // Only the window itself: tooltips and panes opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    return is_open;
}

/// Whether Escape was pressed this frame. The window under the picker closes on Escape too, so this frame the key is
/// the picker's alone. Called before the search field, which would take the key first.
auto take_escape() -> bool
{
    if(!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        return false;
    }
    ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("##asset_picker_escape"), ImGuiInputFlags_LockThisFrame);
    return true;
}

/// The icon and title, and the description under them.
void draw_header(const window_content& content)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(PICKER_TITLE_SCALE);
    deploy_widgets::draw_icon_line(content.icon != nullptr ? content.icon : ICON_MDI_FILE_OUTLINE,
                                   deploy_widgets::get_accent_u32(),
                                   content.title.c_str(),
                                   ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    if(!content.description.empty())
    {
        deploy_widgets::draw_muted_text(content.description.c_str());
    }
}

/// The close button in the top right corner of the window.
auto draw_close_button() -> bool
{
    const float inset = to_pixels(PICKER_CLOSE_INSET);
    const float side = ImGui::GetFrameHeight();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 min(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset);
    return container_widgets::draw_icon_button({"##asset_picker_close", ICON_MDI_CLOSE, "Close"}, ImRect(min, min + ImVec2(side, side)));
}

/// The search across the window, then the card size and the view.
void draw_toolbar()
{
    if(panel_toolbar::begin_strip("##asset_picker_toolbar"))
    {
        if(g_picker.is_search_focus_requested)
        {
            ImGui::SetKeyboardFocusHere();
            g_picker.is_search_focus_requested = false;
        }
        panel_toolbar::search_field(g_picker.search,
                                    ICON_MDI_MAGNIFY " Search names and folders",
                                    "Shows the assets whose name or folder holds every word typed.",
                                    to_pixels(PICKER_SEARCH_MIN_WIDTH),
                                    FLT_MAX);
        panel_toolbar::align_right();
        panel_toolbar::label(ICON_MDI_IMAGE_SIZE_SELECT_LARGE);
        ImGui::BeginDisabled(g_view != picker_view::grid);
        panel_toolbar::begin_field(to_pixels(PICKER_SIZE_SLIDER_WIDTH));
        ImGui::SliderFloat("##asset_picker_tile_size", &g_tile_size, PICKER_TILE_MIN_SIZE, PICKER_TILE_MAX_SIZE, "");
        panel_toolbar::end_field();
        ImGui::EndDisabled();
        ImGui::SetItemTooltipEx("%s", "Sets how large the cards are.");
        panel_toolbar::begin_segment();
        if(panel_toolbar::toggle("##asset_picker_grid", ICON_MDI_VIEW_GRID_OUTLINE, g_view == picker_view::grid, "Shows the assets as cards."))
        {
            g_view = picker_view::grid;
            request_reveal(true);
        }
        if(panel_toolbar::toggle("##asset_picker_list",
                                 ICON_MDI_FORMAT_LIST_BULLETED,
                                 g_view == picker_view::list,
                                 "Shows the assets as a list, each with its folder."))
        {
            g_view = picker_view::list;
            request_reveal(true);
        }
        panel_toolbar::end_segment();
    }
    panel_toolbar::end_strip();
}

/// Begins a pane: a rounded area a step darker than the window. Pair with end_pane().
auto begin_pane(const char* id, const ImVec2& size) -> bool
{
    const float padding = to_pixels(PICKER_PANE_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, to_pixels(PICKER_PANE_ROUNDING));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, PICKER_PANE_COLOR);
    const bool is_visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return is_visible;
}

void end_pane()
{
    ImGui::EndChild();
}

/// The count of a sidebar row at its right end, when it fits beside the label that ends at label_end_x.
void draw_row_count(std::size_t count, float label_end_x, bool is_dimmed)
{
    const std::string text = std::to_string(count);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float width = ImGui::CalcTextSize(text.c_str()).x;
    const float x = max.x - ImGui::GetStyle().FramePadding.x - width;
    if(x < label_end_x + ImGui::GetStyle().ItemSpacing.x)
    {
        return;
    }
    const float y = min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
    const ImU32 color = is_dimmed ? ImGui::GetColorU32(ImGuiCol_Text, PICKER_MUTED_ALPHA * 0.6f) : get_muted_color();
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), color, text.c_str());
}

/// A row of the sidebar as a tree node, its count at its right end. @return Whether it was clicked.
auto draw_sidebar_row(const std::string& id,
                      const char* icon,
                      const std::string& name,
                      std::size_t count,
                      bool is_selected,
                      bool is_leaf,
                      bool is_dimmed,
                      bool& is_open) -> bool
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanFullWidth;
    if(is_leaf)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if(is_selected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string label = fmt::format("{} {}", icon, name);
    const float label_end_x = ImGui::GetCursorScreenPos().x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(label.c_str()).x;
    if(is_dimmed)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
    }
    is_open = ImGui::TreeNodeEx(fmt::format("{}###{}", label, id).c_str(), flags);
    if(is_dimmed)
    {
        ImGui::PopStyleColor();
    }
    const bool is_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
    draw_row_count(count, label_end_x, is_dimmed);
    return is_clicked;
}

/// Shows the folder in the results.
void choose_folder(const std::string& key)
{
    g_picker.scope = picker_scope::folder;
    g_picker.folder = key;
    request_reveal(false);
}

void draw_folder_node(const picker_frame& frame, std::size_t index)
{
    const folder_node& node = frame.folders.nodes[index];
    const bool is_leaf = node.children.empty();
    if(node.is_root)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    const std::string id = "folder:" + node.key;
    const bool was_open = !is_leaf && ImGui::TreeNodeGetOpen(ImGui::GetID(("###" + id).c_str()));
    const char* icon = node.is_root ? node.icon : (was_open ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER);
    const bool is_selected = g_picker.scope == picker_scope::folder && g_picker.folder == node.key;
    const std::size_t count = frame.is_searching() ? node.matched : node.total;
    bool is_open = false;
    if(draw_sidebar_row(id, icon, node.name, count, is_selected, is_leaf, frame.is_searching() && node.matched == 0, is_open))
    {
        choose_folder(node.key);
    }
    ImGui::SetItemTooltipEx("%s", node.key.c_str());
    if(!is_open || is_leaf)
    {
        return;
    }
    for(const std::size_t child : node.children)
    {
        draw_folder_node(frame, child);
    }
    ImGui::TreePop();
}

/// Every entry, the recent picks, then the folders.
void draw_sidebar(const picker_frame& frame, float height)
{
    if(begin_pane("##asset_picker_sidebar", ImVec2(to_pixels(PICKER_SIDEBAR_WIDTH), height)))
    {
        const auto passing = static_cast<std::size_t>(std::count(frame.matched.begin(), frame.matched.end(), true));
        const bool has_none = std::any_of(frame.entries.begin(),
                                          frame.entries.end(),
                                          [](const entry& item)
                                          {
                                              return item.path.empty();
                                          });
        // None is not an asset; while searching, it is not even shown.
        const std::size_t all_count = frame.is_searching() ? passing : passing - (has_none ? 1 : 0);
        bool is_open = false;
        if(draw_sidebar_row("scope:all", ICON_MDI_VIEW_GRID_OUTLINE, "All", all_count, g_picker.scope == picker_scope::all, true, false, is_open))
        {
            g_picker.scope = picker_scope::all;
            request_reveal(false);
        }
        ImGui::SetItemTooltipEx("%s", "Shows the assets of every folder.");
        if(!frame.recent.empty())
        {
            const auto recent_count = static_cast<std::size_t>(std::count_if(frame.recent.begin(),
                                                                             frame.recent.end(),
                                                                             [&frame](std::size_t index)
                                                                             {
                                                                                 return frame.matched[index];
                                                                             }));
            if(draw_sidebar_row("scope:recent",
                                ICON_MDI_HISTORY,
                                "Recent",
                                recent_count,
                                g_picker.scope == picker_scope::recent,
                                true,
                                frame.is_searching() && recent_count == 0,
                                is_open))
            {
                g_picker.scope = picker_scope::recent;
                request_reveal(false);
            }
            ImGui::SetItemTooltipEx("%s", "Shows what was picked for this type of asset before, newest first.");
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        for(const std::size_t root : frame.folders.roots)
        {
            draw_folder_node(frame, root);
        }
    }
    end_pane();
}

/// What the pointer did to an entry of the results.
struct entry_events
{
    bool is_clicked{};
    bool is_double_clicked{};
};

auto get_preview(const picker_frame& frame, std::size_t index) -> preview
{
    return frame.source.get_preview ? frame.source.get_preview(index) : preview{};
}

auto get_summary(const picker_frame& frame, std::size_t index) -> std::string
{
    const bool is_none = frame.entries[index].path.empty();
    return frame.source.get_summary && !is_none ? frame.source.get_summary(index) : std::string{};
}

/// Reads the pointer on the entry drawn last, scrolls it into view when asked, and gives it a tooltip.
auto read_entry_events(const picker_frame& frame, std::size_t index, bool is_pressed) -> entry_events
{
    entry_events events;
    events.is_clicked = is_pressed;
    events.is_double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool is_highlighted = frame.highlighted == index;
    if(is_highlighted && g_picker.reveal_frames > 0)
    {
        const ImGuiScrollFlags flags = g_picker.is_reveal_centered ? ImGuiScrollFlags_AlwaysCenterY : ImGuiScrollFlags_KeepVisibleEdgeY;
        ImGui::ScrollToRect(ImGui::GetCurrentWindow(), ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), flags);
    }
    const auto& item = frame.entries[index];
    ImGui::SetItemTooltipEx("%s", item.path.empty() ? "Clears the field." : item.path.c_str());
    return events;
}

/// An entry as the content browser's card, its folder under its name.
auto draw_entry_card(const picker_frame& frame, std::size_t index, float width) -> entry_events
{
    const entry& item = frame.entries[index];
    const bool is_none = item.path.empty();
    const preview thumbnail = get_preview(frame, index);
    const std::string summary = get_summary(frame, index);
    content_card_view card;
    card.name = item.name;
    if(!is_none)
    {
        card.type = frame.content.type;
        card.caption = frame.infos[index].caption;
    }
    card.summary = summary;
    card.texture = thumbnail.texture;
    card.texture_size = thumbnail.texture_size;
    card.glyph = thumbnail.icon;
    card.is_loading = thumbnail.is_loading;
    card.is_selected = frame.highlighted == index;
    card.is_current = frame.content.current == index;
    card.size = width;
    const bool is_pressed = draw_content_card(card, ImGui::GetID("##card"));
    return read_entry_events(frame, index, is_pressed);
}

/// The thumbnail of a list row or of the details: the texture fitted into a dark well, a spinner while the asset
/// loads, or else its glyph.
void draw_thumbnail_well(ImDrawList* draw_list, const ImRect& rect, const preview& thumbnail, float glyph_share)
{
    const float rounding = to_pixels(PICKER_PREVIEW_ROUNDING);
    draw_list->AddRectFilled(rect.Min, rect.Max, PICKER_WELL_COLOR, rounding);
    const float inset = ImFloor(rect.GetWidth() * 0.04f);
    const ImRect area(rect.Min + ImVec2(inset, inset), rect.Max - ImVec2(inset, inset));
    if(thumbnail.is_loading)
    {
        draw_loading_spinner(draw_list, area.Min, area.Max);
        return;
    }
    if(thumbnail.texture == ImTextureID{})
    {
        if(thumbnail.icon != nullptr)
        {
            draw_icon(draw_list, area.GetCenter(), thumbnail.icon, get_muted_color(), area.GetWidth() * glyph_share);
        }
        return;
    }
    draw_fitted_image(draw_list, area, thumbnail, rounding);
}

/// An entry as a row: the thumbnail, the name over the folder, the summary and the current mark at the right end.
auto draw_entry_row(const picker_frame& frame, std::size_t index, float width, float height) -> entry_events
{
    const entry& item = frame.entries[index];
    const bool is_none = item.path.empty();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImRect rect(min, min + ImVec2(width, height));
    const bool is_pressed = ImGui::InvisibleButton("##row", rect.GetSize());
    const bool is_hovered = ImGui::IsItemHovered();
    const bool is_held = ImGui::IsItemActive();
    const bool is_highlighted = frame.highlighted == index;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(PICKER_ROW_ROUNDING);
    if(is_highlighted)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(ImGuiCol_Header), rounding);
    }
    else if(is_held || is_hovered)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, is_held ? PICKER_ROW_HELD_COLOR : PICKER_ROW_HOVERED_COLOR, rounding);
    }

    const float inset = to_pixels(PICKER_ROW_INSET);
    const float side = height - inset * 2.0f;
    const ImRect thumbnail_rect(min + ImVec2(inset, inset), min + ImVec2(inset + side, inset + side));
    draw_thumbnail_well(draw_list, thumbnail_rect, get_preview(frame, index), 0.6f);
    if(!is_none)
    {
        draw_list->AddRectFilled(ImVec2(thumbnail_rect.Min.x, thumbnail_rect.Max.y - PICKER_ACCENT_STRIP_HEIGHT),
                                 thumbnail_rect.Max,
                                 get_asset_type_accent(frame.content.type));
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float right_x = rect.Max.x - style.FramePadding.x;
    const float line_height = ImGui::GetTextLineHeight();
    if(frame.content.current == index)
    {
        const float icon_width = ImGui::GetFontSize();
        ImGui::RenderIconCentered(draw_list, ImVec2(right_x - icon_width * 0.5f, rect.GetCenter().y), ICON_MDI_CHECK_BOLD, get_accent_color());
        right_x -= icon_width + style.ItemSpacing.x;
    }
    const std::string summary = get_summary(frame, index);
    if(!summary.empty())
    {
        const float summary_width = ImGui::CalcTextSize(summary.c_str()).x;
        draw_list->AddText(ImVec2(right_x - summary_width, rect.GetCenter().y - line_height * 0.5f), get_muted_color(), summary.c_str());
        right_x -= summary_width + style.ItemSpacing.x;
    }

    const float text_x = thumbnail_rect.Max.x + style.ItemSpacing.x;
    const std::string& folder = frame.infos[index].folder_label;
    if(folder.empty())
    {
        const float y = rect.GetCenter().y - line_height * 0.5f;
        draw_clipped_text(draw_list, ImVec2(text_x, y), ImVec2(right_x, y + line_height), item.name.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
        return read_entry_events(frame, index, is_pressed);
    }
    const float caption_size = ImGui::GetFontSize() * PICKER_ROW_CAPTION_SCALE;
    const float text_top = rect.GetCenter().y - (line_height + caption_size) * 0.5f;
    draw_clipped_text(draw_list,
                      ImVec2(text_x, text_top),
                      ImVec2(right_x, text_top + line_height),
                      item.name.c_str(),
                      ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PushFont(nullptr, ImGui::GetCurrentContext()->FontSizeBase * PICKER_ROW_CAPTION_SCALE);
    const float caption_y = text_top + line_height;
    draw_clipped_text(draw_list, ImVec2(text_x, caption_y), ImVec2(right_x, caption_y + caption_size), folder.c_str(), get_muted_color());
    ImGui::PopFont();
    return read_entry_events(frame, index, is_pressed);
}

/// What the results were asked for this frame.
struct results_events
{
    hpp::optional<std::size_t> clicked;
    hpp::optional<std::size_t> double_clicked;
};

void collect_events(results_events& results, std::size_t index, const entry_events& events)
{
    if(events.is_clicked)
    {
        results.clicked = index;
    }
    if(events.is_double_clicked)
    {
        results.double_clicked = index;
    }
}

/// The shown entries as cards in rows that fill the width; only the visible rows are drawn.
auto draw_grid(const picker_frame& frame) -> results_events
{
    results_events results;
    const float gap = to_pixels(PICKER_TILE_GAP);
    const float tile_width = ImFloor(to_pixels(g_tile_size));
    const float avail = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((avail + gap) / (tile_width + gap)));
    const std::size_t count = frame.shown.size();
    const auto column_count = static_cast<std::size_t>(columns);
    const int rows = static_cast<int>((count + column_count - 1) / column_count);
    // The spare width goes between the cards of a full row, as in the content browser.
    float spacing = gap;
    if(count >= column_count && columns > 1)
    {
        spacing += ImMax(0.0f, avail - float(columns) * tile_width - float(columns - 1) * gap) / float(columns - 1);
    }
    const float tile_height = get_content_card_height(tile_width);
    g_picker.columns = columns;
    g_picker.page_rows = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / (tile_height + gap)));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, gap));
    ImGuiListClipper clipper;
    clipper.Begin(rows, tile_height + gap);
    if(g_picker.reveal_frames > 0 && frame.highlighted_position)
    {
        // The clipper submits no row out of view, which would leave nothing to scroll to.
        clipper.IncludeItemByIndex(static_cast<int>(*frame.highlighted_position / column_count));
    }
    while(clipper.Step())
    {
        for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            for(std::size_t column = 0; column < column_count; ++column)
            {
                const std::size_t position = static_cast<std::size_t>(row) * column_count + column;
                if(position >= count)
                {
                    break;
                }
                if(column > 0)
                {
                    ImGui::SameLine(0.0f, spacing);
                }
                const std::size_t index = frame.shown[position];
                ImGui::PushID(static_cast<int>(index));
                collect_events(results, index, draw_entry_card(frame, index, tile_width));
                ImGui::PopID();
            }
        }
    }
    ImGui::PopStyleVar();
    return results;
}

/// The shown entries as rows; only the visible rows are drawn.
auto draw_rows(const picker_frame& frame) -> results_events
{
    results_events results;
    const float row_height = to_pixels(PICKER_ROW_HEIGHT);
    const float gap = to_pixels(PICKER_ROW_GAP);
    const float width = ImGui::GetContentRegionAvail().x;
    g_picker.columns = 1;
    g_picker.page_rows = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / (row_height + gap)));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, gap));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(frame.shown.size()), row_height + gap);
    if(g_picker.reveal_frames > 0 && frame.highlighted_position)
    {
        clipper.IncludeItemByIndex(static_cast<int>(*frame.highlighted_position));
    }
    while(clipper.Step())
    {
        for(int position = clipper.DisplayStart; position < clipper.DisplayEnd; ++position)
        {
            const std::size_t index = frame.shown[static_cast<std::size_t>(position)];
            ImGui::PushID(static_cast<int>(index));
            collect_events(results, index, draw_entry_row(frame, index, width, row_height));
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
    return results;
}

/// A large icon and a line or two in the middle of the results, when they show nothing.
void draw_empty_results(const picker_frame& frame)
{
    const char* icon = ICON_MDI_FOLDER_OUTLINE;
    std::string title = frame.content.empty_text.empty() ? std::string("There is nothing to pick here.") : frame.content.empty_text;
    std::string note;
    if(frame.is_searching())
    {
        icon = ICON_MDI_MAGNIFY_CLOSE;
        title = fmt::format("Nothing matches \"{}\".", g_picker.search.InputBuf);
        note = g_picker.scope == picker_scope::all ? "The search looks at the names and folders of the assets."
                                                   : "Choose All on the left to search every folder.";
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float icon_size = ImGui::GetFontSize() * PICKER_EMPTY_STATE_ICON_SCALE;
    const float wrap_width = avail.x * 0.8f;
    const ImVec2 title_size = ImGui::CalcTextSize(title.c_str(), nullptr, false, wrap_width);
    const ImVec2 note_size = note.empty() ? ImVec2() : ImGui::CalcTextSize(note.c_str(), nullptr, false, wrap_width);
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float total = icon_size + spacing + title_size.y + (note.empty() ? 0.0f : spacing + note_size.y);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    float y = origin.y + ImMax(0.0f, (avail.y - total) * 0.45f);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_icon(draw_list, ImVec2(origin.x + avail.x * 0.5f, y + icon_size * 0.5f), icon, get_muted_color(), icon_size);
    y += icon_size + spacing;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - title_size.x) * 0.5f, y));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopTextWrapPos();
    if(!note.empty())
    {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - note_size.x) * 0.5f, ImGui::GetCursorScreenPos().y));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
        ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
        ImGui::TextUnformatted(note.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }
}

auto draw_results(const picker_frame& frame, const ImVec2& size) -> results_events
{
    results_events results;
    if(begin_pane("##asset_picker_results", size))
    {
        if(frame.shown.empty())
        {
            draw_empty_results(frame);
        }
        else
        {
            results = g_view == picker_view::grid ? draw_grid(frame) : draw_rows(frame);
        }
    }
    end_pane();
    return results;
}

/// The highlighted entry's thumbnail, name, type, path and facts.
void draw_details_content(const picker_frame& frame, std::size_t index)
{
    const entry& item = frame.entries[index];
    const bool is_none = item.path.empty();
    const float width = ImGui::GetContentRegionAvail().x;
    const float side = ImFloor(ImMin(width, to_pixels(PICKER_DETAILS_THUMBNAIL_MAX)));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect well(ImVec2(origin.x + ImFloor((width - side) * 0.5f), origin.y),
                      ImVec2(origin.x + ImFloor((width - side) * 0.5f) + side, origin.y + side));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_thumbnail_well(draw_list, well, get_preview(frame, index), PICKER_DETAILS_GLYPH_SHARE);
    if(!is_none)
    {
        draw_list->AddRectFilled(ImVec2(well.Min.x, well.Max.y - PICKER_ACCENT_STRIP_HEIGHT), well.Max, get_asset_type_accent(frame.content.type));
    }
    ImGui::Dummy(ImVec2(width, side));
    deploy_widgets::draw_gap(PICKER_LINE_GAP);

    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * PICKER_DETAILS_NAME_SCALE);
    ImGui::TextUnformatted(item.name.c_str());
    ImGui::PopFont();
    if(is_none)
    {
        deploy_widgets::draw_muted_text("Picking None clears the field.");
        ImGui::PopTextWrapPos();
        return;
    }
    draw_asset_type_line(frame.content.type, get_summary(frame, index), width);
    ImGui::PushFont(nullptr, ImGui::GetCurrentContext()->FontSizeBase * PICKER_DETAILS_PATH_SCALE);
    ImGui::PushStyleColor(ImGuiCol_Text, get_muted_color());
    ImGui::TextUnformatted(item.path.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    if(frame.content.current == index)
    {
        deploy_widgets::draw_gap(PICKER_LINE_GAP);
        deploy_widgets::draw_icon_line(ICON_MDI_CHECK_BOLD, get_accent_color(), "The field holds this now.", get_muted_color());
    }
    if(frame.source.collect_details)
    {
        content_item_sections sections;
        frame.source.collect_details(index, sections);
        draw_content_sections(sections, width);
    }
}

void draw_details(const picker_frame& frame, float height)
{
    if(begin_pane("##asset_picker_details", ImVec2(to_pixels(PICKER_DETAILS_WIDTH), height)))
    {
        if(frame.highlighted)
        {
            draw_details_content(frame, *frame.highlighted);
        }
        else
        {
            deploy_widgets::draw_muted_text("Click an asset to see its details here.");
        }
    }
    end_pane();
}

/// How the keys move the highlight this frame.
struct highlight_move
{
    int delta{};
    /// A page key moved it: it stops at the ends instead of staying.
    bool is_page{};
    /// While typing, the left and right arrows belong to the search field: a row move that has no row to go to
    /// steps one entry instead, so a single row of cards can still be walked.
    bool is_stepping_at_ends{};
    bool to_first{};
    bool to_last{};
};

/// Takes the keys that move the highlight, so ImGui's navigation leaves them alone. The search field keeps the left
/// and right arrows, Home and End while typing.
void claim_keys(ImGuiID owner, bool is_typing)
{
    const ImGuiKey keys[] = {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown, ImGuiKey_Enter, ImGuiKey_KeypadEnter};
    for(const ImGuiKey key : keys)
    {
        ImGui::SetKeyOwner(key, owner);
    }
    if(is_typing)
    {
        return;
    }
    const ImGuiKey browse_keys[] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Home, ImGuiKey_End};
    for(const ImGuiKey key : browse_keys)
    {
        ImGui::SetKeyOwner(key, owner);
    }
}

auto is_pressed(ImGuiKey key, ImGuiID owner) -> bool
{
    return ImGui::IsKeyPressed(key, ImGuiInputFlags_Repeat, owner);
}

auto read_highlight_move(ImGuiID owner, bool is_typing) -> highlight_move
{
    const int row_step = g_view == picker_view::grid ? g_picker.columns : 1;
    const int page_step = row_step * g_picker.page_rows;
    highlight_move move;
    move.delta += is_pressed(ImGuiKey_DownArrow, owner) ? row_step : 0;
    move.delta -= is_pressed(ImGuiKey_UpArrow, owner) ? row_step : 0;
    if(is_pressed(ImGuiKey_PageDown, owner) || is_pressed(ImGuiKey_PageUp, owner))
    {
        move.is_page = true;
        move.delta += is_pressed(ImGuiKey_PageDown, owner) ? page_step : -page_step;
    }
    if(is_typing)
    {
        move.is_stepping_at_ends = true;
        return move;
    }
    if(g_view == picker_view::grid)
    {
        move.delta += is_pressed(ImGuiKey_RightArrow, owner) ? 1 : 0;
        move.delta -= is_pressed(ImGuiKey_LeftArrow, owner) ? 1 : 0;
    }
    move.to_first = is_pressed(ImGuiKey_Home, owner);
    move.to_last = is_pressed(ImGuiKey_End, owner);
    return move;
}

/// Where a move takes the highlight. An arrow past the first row stays; one past the last full row goes to the
/// last entry. While stepping at the ends, a row move with no row to go to moves one entry.
auto apply_move(const highlight_move& move, std::size_t position, std::size_t count) -> std::size_t
{
    if(move.to_first)
    {
        return 0;
    }
    if(move.to_last)
    {
        return count - 1;
    }
    const long long last = static_cast<long long>(count) - 1;
    const long long current = static_cast<long long>(position);
    const long long target = current + move.delta;
    if(move.is_page)
    {
        return static_cast<std::size_t>(std::clamp(target, 0LL, last));
    }
    if(target < 0)
    {
        return move.is_stepping_at_ends && current > 0 ? position - 1 : position;
    }
    if(target > last)
    {
        const long long columns = std::max(1, g_picker.columns);
        if(current / columns < last / columns)
        {
            return static_cast<std::size_t>(last);
        }
        return move.is_stepping_at_ends && current < last ? position + 1 : position;
    }
    return static_cast<std::size_t>(target);
}

/// Text typed while the search field is not active goes to it: called before the field, which takes it this frame.
void send_typing_to_search()
{
    const bool is_typing = ImGui::IsAnyItemActive();
    if(!is_typing && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       (!ImGui::GetIO().InputQueueCharacters.empty() || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
    {
        g_picker.is_search_focus_requested = true;
    }
}

/// Moves the highlight by the keys. @return Whether Enter took the highlight.
auto handle_keys(picker_frame& frame) -> bool
{
    if(!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        return false;
    }
    const ImGuiID owner = ImGui::GetID("##asset_picker_keys");
    const bool is_typing = ImGui::IsAnyItemActive();
    claim_keys(owner, is_typing);
    const bool is_entered = is_pressed(ImGuiKey_Enter, owner) || is_pressed(ImGuiKey_KeypadEnter, owner);
    const highlight_move move = read_highlight_move(owner, is_typing);
    if(frame.shown.empty() || (move.delta == 0 && !move.to_first && !move.to_last))
    {
        return is_entered && frame.highlighted.has_value();
    }
    const std::size_t position = frame.highlighted_position.value_or(0);
    highlight_position(frame, apply_move(move, position, frame.shown.size()));
    request_reveal(false);
    return is_entered && frame.highlighted.has_value();
}

/// What the footer was asked for this frame.
struct footer_events
{
    bool is_canceled{};
    bool is_confirmed{};
};

auto make_status_text(const picker_frame& frame) -> std::string
{
    const auto count = static_cast<std::size_t>(std::count_if(frame.shown.begin(),
                                                              frame.shown.end(),
                                                              [&frame](std::size_t index)
                                                              {
                                                                  return !frame.entries[index].path.empty();
                                                              }));
    const char* noun = count == 1 ? "asset" : "assets";
    std::string where;
    if(g_picker.scope == picker_scope::folder)
    {
        where = fmt::format(" in {}", g_picker.folder);
    }
    else if(g_picker.scope == picker_scope::recent)
    {
        where = " picked recently";
    }
    return frame.is_searching() ? fmt::format("{} {} match the search{}", count, noun, where) : fmt::format("{} {}{}", count, noun, where);
}

/// The count of what is shown, then Cancel and the confirm button.
auto draw_footer(const picker_frame& frame) -> footer_events
{
    footer_events events;
    const float height = to_pixels(PICKER_FOOTER_HEIGHT);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const char* cancel_label = "Cancel";
    const std::string confirm_label = frame.content.confirm_label.empty() ? std::string("Select") : frame.content.confirm_label;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_width =
        deploy_widgets::calc_button_width(cancel_label) + spacing + deploy_widgets::calc_button_width(confirm_label.c_str());
    const std::string status = make_status_text(frame);
    const float text_y = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
    draw_clipped_text(ImGui::GetWindowDrawList(),
                      ImVec2(min.x, text_y),
                      ImVec2(min.x + ImMax(0.0f, width - buttons_width - spacing), text_y + ImGui::GetTextLineHeight()),
                      status.c_str(),
                      get_muted_color());
    ImGui::SetCursorScreenPos(ImVec2(min.x + ImMax(0.0f, width - buttons_width), min.y));
    events.is_canceled = deploy_widgets::draw_button(cancel_label, false, true);
    ImGui::SetItemTooltipEx("%s", "Closes the window and leaves the field as it is.");
    ImGui::SameLine();
    events.is_confirmed = deploy_widgets::draw_button(confirm_label.c_str(), true, frame.highlighted.has_value());
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip("%s",
                          frame.highlighted ? "Takes the highlighted asset. Enter or a double-click on it does the same."
                                            : "Click an asset first.");
    }
    return events;
}
} // namespace

auto draw_field(const field_content& content) -> field_request
{
    field_request request{};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float side = ImGui::GetFrameHeight() * 2.0f + style.ItemSpacing.y;

    ImGui::BeginGroup();
    const ImVec2 preview_min = ImGui::GetCursorScreenPos();
    // The thumbnail is the largest target of the field, so it opens the picker like the name
    // does; showing the asset in the content browser has its own button. An asset that opens
    // somewhere opens on a double-click instead, which a picker opened by its first click would take.
    const bool is_opening_preview = can_open(content);
    const ImGuiButtonFlags preview_flags = is_opening_preview ? ImGuiButtonFlags(ImGuiButtonFlags_PressedOnDoubleClick)
                                                              : ImGuiButtonFlags(ImGuiButtonFlags_None);
    const bool is_preview_pressed = ImGui::InvisibleButton("##thumbnail", ImVec2(side, side), preview_flags);
    const bool is_preview_hovered = ImGui::IsItemHovered();
    set_preview_tooltip(content);
    const ImRect preview_rect(preview_min, preview_min + ImVec2(side, side));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_preview(draw_list, preview_rect, content.thumbnail, ImGui::GetFontSize() * PICKER_EMPTY_ICON_SCALE);
    if(is_preview_hovered)
    {
        draw_list->AddRect(preview_rect.Min, preview_rect.Max, PICKER_PREVIEW_HOVERED_BORDER_COLOR, to_pixels(PICKER_PREVIEW_ROUNDING));
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    request.is_pick_pressed = draw_pick_field(content) || (is_preview_pressed && !is_opening_preview);
    request.is_open_pressed = is_preview_pressed && is_opening_preview;
    draw_field_actions(content, request);
    ImGui::EndGroup();
    ImGui::EndGroup();
    return request;
}

void draw_drop_highlight(bool is_drop_possible)
{
    if(!is_drop_possible)
    {
        return;
    }
    const ImVec2 grow(PICKER_DROP_BORDER, PICKER_DROP_BORDER);
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - grow,
                                        ImGui::GetItemRectMax() + grow,
                                        get_accent_color(),
                                        to_pixels(PICKER_PREVIEW_ROUNDING),
                                        0,
                                        PICKER_DROP_BORDER);
}

void open_window(const char* popup_id)
{
    ImGui::OpenPopup(popup_id);
}

auto is_window_open(const char* popup_id) -> bool
{
    return ImGui::IsPopupOpen(popup_id);
}

auto draw_window(const char* popup_id, const window_content& content, const std::vector<entry>& entries, const entry_source& source)
    -> hpp::optional<std::size_t>
{
    if(!begin_picker_window(popup_id))
    {
        return hpp::nullopt;
    }
    if(ImGui::IsWindowAppearing())
    {
        start_picker(content, entries);
    }
    const bool is_escape_pressed = take_escape();
    send_typing_to_search();
    draw_header(content);
    deploy_widgets::draw_gap(PICKER_SECTION_GAP);
    draw_toolbar();
    deploy_widgets::draw_gap(PICKER_LINE_GAP);

    picker_frame frame = make_frame(content, entries, source);
    update_highlight(frame);
    const bool is_entered = handle_keys(frame);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footer_height = to_pixels(PICKER_FOOTER_HEIGHT) + to_pixels(PICKER_SECTION_GAP);
    const float body_height = ImMax(ImGui::GetContentRegionAvail().y - footer_height, ImGui::GetFrameHeight());
    draw_sidebar(frame, body_height);
    ImGui::SameLine();
    const float details_width = to_pixels(PICKER_DETAILS_WIDTH);
    const float results_width = ImMax(ImGui::GetContentRegionAvail().x - details_width - style.ItemSpacing.x, ImGui::GetFrameHeight());
    const results_events results = draw_results(frame, ImVec2(results_width, body_height));
    ImGui::SameLine();
    draw_details(frame, body_height);
    if(g_picker.reveal_frames > 0)
    {
        --g_picker.reveal_frames;
    }
    if(results.clicked)
    {
        g_picker.highlighted = entries[*results.clicked].path;
        frame.highlighted = results.clicked;
    }

    deploy_widgets::draw_gap(PICKER_SECTION_GAP);
    const footer_events footer = draw_footer(frame);

    hpp::optional<std::size_t> picked;
    if(results.double_clicked)
    {
        picked = results.double_clicked;
    }
    else if((is_entered || footer.is_confirmed) && frame.highlighted)
    {
        picked = frame.highlighted;
    }
    if(picked)
    {
        remember_pick(content.type, entries[*picked].path);
    }
    const bool is_close_pressed = draw_close_button();
    if(picked || footer.is_canceled || is_close_pressed || is_escape_pressed)
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return picked;
}

} // namespace unravel::asset_picker
