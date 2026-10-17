#include "start_page.h"
#include "start_page_widgets.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/screen_art.h>
#include <editor/imgui/screen_card.h>
#include <editor/shortcuts.h>
#include <editor/system/project_manager.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace unravel
{
namespace widgets = start_page_widgets;

namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float PROJECTS_VIEW_SECTION_GAP = 1.3f;
constexpr float PROJECTS_VIEW_TOOLBAR_GAP = 0.9f;
constexpr float PROJECTS_VIEW_SEARCH_WIDTH = 22.0f;
// The search never takes more than this share of the toolbar.
constexpr float PROJECTS_VIEW_SEARCH_SHARE = 0.45f;
constexpr float PROJECTS_VIEW_SORT_WIDTH = 9.5f;
constexpr float PROJECTS_VIEW_TOGGLE_GAP = 0.15f;
// In pixels: just room for the border of a hovered or selected card.
constexpr float PROJECTS_VIEW_CONTENT_INSET_PIXELS = 2.0f;
constexpr float PROJECTS_VIEW_GRID_GAP = 1.1f;
constexpr float PROJECTS_VIEW_CARD_MIN_WIDTH = 15.5f;
constexpr float PROJECTS_VIEW_CARD_PADDING = 0.8f;
constexpr float PROJECTS_VIEW_LINE_GAP = 0.25f;
constexpr float PROJECTS_VIEW_META_GAP = 0.6f;
constexpr float PROJECTS_VIEW_ROUNDING = 0.6f;
constexpr float PROJECTS_VIEW_IMAGE_ROUNDING = 0.35f;
constexpr float PROJECTS_VIEW_IMAGE_ASPECT = 9.0f / 16.0f;
constexpr float PROJECTS_VIEW_ROW_HEIGHT = 3.9f;
constexpr float PROJECTS_VIEW_ROW_PADDING = 0.6f;
constexpr float PROJECTS_VIEW_ROW_TEXT_GAP = 1.0f;
constexpr float PROJECTS_VIEW_ROW_GAP = 0.45f;
constexpr float PROJECTS_VIEW_EMPTY_BUTTONS_GAP = 1.4f;
constexpr int PROJECTS_VIEW_MISSING_IMAGE_ALPHA = 80;
constexpr ImU32 PROJECTS_VIEW_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
constexpr ImU32 PROJECTS_VIEW_MISSING_COLOR = IM_COL32(255, 110, 110, 255);
constexpr ImU32 PROJECTS_VIEW_MISSING_FILL_COLOR = IM_COL32(60, 20, 20, 220);
constexpr ImU32 PROJECTS_VIEW_BADGE_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 PROJECTS_VIEW_TRANSPARENT_COLOR = IM_COL32(0, 0, 0, 0);
constexpr const char* PROJECTS_VIEW_OPEN_LABEL = ICON_MDI_FOLDER_OPEN_OUTLINE "  Open...";
constexpr const char* PROJECTS_VIEW_NEW_LABEL = ICON_MDI_PLUS "  New Project";
constexpr const char* PROJECTS_VIEW_SAMPLES_LABEL = ICON_MDI_AUTO_FIX "  Explore Samples";
constexpr std::array<const char*, 2> PROJECTS_VIEW_SORT_LABELS{"Last opened", "Name"};
// The keys the projects take while they listen, away from the keyboard navigation of ImGui.
constexpr std::array<ImGuiKey, 7> PROJECTS_VIEW_KEYS{ImGuiKey_UpArrow,
                                                     ImGuiKey_DownArrow,
                                                     ImGuiKey_LeftArrow,
                                                     ImGuiKey_RightArrow,
                                                     ImGuiKey_Enter,
                                                     ImGuiKey_KeypadEnter,
                                                     shortcuts::delete_item};

/// What the meta line of a project says: when it was saved, and with which engine.
struct project_meta
{
    const std::string& modified_label;
    const std::string& engine_version;
    bool is_missing{};
    bool is_engine_older{};
};

auto calc_project_card_info_height() -> float
{
    const float padding = to_pixels(PROJECTS_VIEW_CARD_PADDING);
    return 2.0f * padding + 3.0f * ImGui::GetFontSize() + to_pixels(PROJECTS_VIEW_LINE_GAP) + to_pixels(PROJECTS_VIEW_META_GAP);
}

/// The version without its commit ("1.2.0-163-g69829521" is "1.2.0-163"): the tooltip has it whole.
auto shorten_version(const std::string& engine_version) -> std::string
{
    const size_t commit_start = engine_version.rfind("-g");
    if(commit_start == std::string::npos || commit_start + 2 >= engine_version.size())
    {
        return engine_version;
    }
    const bool is_commit = std::all_of(engine_version.begin() + static_cast<std::ptrdiff_t>(commit_start + 2),
                                       engine_version.end(),
                                       [](char character)
                                       {
                                           return std::isxdigit(static_cast<unsigned char>(character)) != 0;
                                       });
    return is_commit ? engine_version.substr(0, commit_start) : engine_version;
}

auto make_version_badge(const std::string& engine_version) -> std::string
{
    return engine_version.empty() ? std::string{} : fmt::format("{} {}", ICON_MDI_ENGINE_OUTLINE, shorten_version(engine_version));
}

/// The engine version as a pill at the right end of the line at y; returns its left edge.
auto draw_version_badge(ImDrawList* draw_list, float max_x, float y, const project_meta& meta) -> float
{
    const std::string badge = make_version_badge(meta.engine_version);
    if(badge.empty())
    {
        return max_x;
    }
    const ImVec2 size = widgets::calc_pill_size(badge.c_str());
    const ImVec2 pos(max_x - size.x, ImFloor(y + (ImGui::GetFontSize() - size.y) * 0.5f));
    const ImU32 text_color = meta.is_engine_older ? PROJECTS_VIEW_WARNING_COLOR : widgets::get_muted_color();
    widgets::draw_pill(draw_list, pos, badge.c_str(), PROJECTS_VIEW_BADGE_COLOR, text_color);
    return pos.x;
}

/// When the project was saved, with a clock in front (a warning when its folder is gone).
auto format_modified_label(const project_meta& meta) -> std::string
{
    return fmt::format("{}  {}", meta.is_missing ? ICON_MDI_ALERT_OUTLINE : ICON_MDI_CLOCK_OUTLINE, meta.modified_label);
}

void draw_modified_label(ImDrawList* draw_list, const ImVec2& pos, float max_x, const project_meta& meta)
{
    const std::string label = format_modified_label(meta);
    const ImU32 color = meta.is_missing ? PROJECTS_VIEW_MISSING_COLOR : widgets::get_muted_color();
    widgets::draw_text_clipped(draw_list, pos, max_x, color, label.c_str());
}

/// The meta line of a card: the modified label on the left, the engine version on the right.
void draw_project_meta_line(ImDrawList* draw_list, const ImVec2& pos, float max_x, const project_meta& meta)
{
    const float badge_min_x = draw_version_badge(draw_list, max_x, pos.y, meta);
    draw_modified_label(draw_list, pos, badge_min_x - to_pixels(PROJECTS_VIEW_LINE_GAP), meta);
}

/// The pill a project whose folder is gone shows over its picture.
void draw_missing_overlay(ImDrawList* draw_list, const ImRect& rect)
{
    const std::string text = fmt::format("{}  Folder not found", ICON_MDI_ALERT_OUTLINE);
    const ImVec2 size = widgets::calc_pill_size(text.c_str());
    const ImVec2 pos(ImFloor(rect.GetCenter().x - size.x * 0.5f), ImFloor(rect.GetCenter().y - size.y * 0.5f));
    widgets::draw_pill(draw_list, pos, text.c_str(), PROJECTS_VIEW_MISSING_FILL_COLOR, PROJECTS_VIEW_MISSING_COLOR);
}
} // namespace

void start_page::draw_projects_view(rtti::context& ctx)
{
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
    {
        focuses_search_ = true;
    }
    draw_projects_header(ctx);
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PROJECTS_VIEW_SECTION_GAP)));
    draw_projects_toolbar();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PROJECTS_VIEW_TOOLBAR_GAP)));
    draw_projects_content(ctx);
}

void start_page::draw_projects_header(rtti::context& ctx)
{
    const ImVec2 start = ImGui::GetCursorPos();
    const float avail_width = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_width = widgets::calc_button_width(PROJECTS_VIEW_OPEN_LABEL) + spacing +
                                widgets::calc_button_width(PROJECTS_VIEW_NEW_LABEL);
    const std::string subtitle =
        projects_.empty() ? std::string("Open a project from disk, or create a new one.")
                          : fmt::format("{} recent project{}. Double-click one to open it, right-click it for more, "
                                        "Delete to remove it.",
                                        projects_.size(),
                                        projects_.size() == 1 ? "" : "s");
    widgets::draw_page_title("Projects", subtitle.c_str(), avail_width - buttons_width - spacing);
    const float end_y = ImGui::GetCursorPosY();
    const float button_y = start.y + ImMax(0.0f, (end_y - start.y - widgets::get_button_height()) * 0.5f);
    ImGui::SetCursorPos(ImVec2(start.x + avail_width - buttons_width, button_y));
    if(widgets::draw_button(PROJECTS_VIEW_OPEN_LABEL, widgets::button_kind::secondary))
    {
        browse_for_project(ctx);
    }
    ImGui::SetItemTooltipEx("%s", "Open a project folder that is not in the list (Ctrl+O)");
    ImGui::SameLine();
    if(widgets::draw_button(PROJECTS_VIEW_NEW_LABEL, widgets::button_kind::primary))
    {
        show_view(view_state::create_project);
    }
    ImGui::SetItemTooltipEx("%s", "Create a project (Ctrl+N)");
    ImGui::SetCursorPos(ImVec2(start.x, ImMax(end_y, ImGui::GetCursorPosY())));
}

void start_page::draw_projects_toolbar()
{
    const float start_x = ImGui::GetCursorPosX();
    const float avail_width = ImGui::GetContentRegionAvail().x;
    const float toggle_size = widgets::get_button_height();
    const float toggle_gap = to_pixels(PROJECTS_VIEW_TOGGLE_GAP);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float sort_width = to_pixels(PROJECTS_VIEW_SORT_WIDTH);
    const float tools_width = sort_width + spacing + 2.0f * toggle_size + toggle_gap;
    const float search_width = ImMin(to_pixels(PROJECTS_VIEW_SEARCH_WIDTH), avail_width * PROJECTS_VIEW_SEARCH_SHARE);
    widgets::push_field_style();
    if(focuses_search_)
    {
        ImGui::SetKeyboardFocusHere();
        focuses_search_ = false;
    }
    ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY "  Search projects", search_width);
    ImGui::SetItemTooltipEx("%s", "Filter by name or folder (Ctrl+F)");
    ImGui::SameLine();
    ImGui::SetCursorPosX(start_x + avail_width - tools_width);
    ImGui::SetNextItemWidth(sort_width);
    const auto sort_index = static_cast<size_t>(sort_);
    if(ImGui::BeginCombo("##sort", PROJECTS_VIEW_SORT_LABELS[sort_index]))
    {
        for(size_t index = 0; index < PROJECTS_VIEW_SORT_LABELS.size(); ++index)
        {
            if(ImGui::Selectable(PROJECTS_VIEW_SORT_LABELS[index], index == sort_index))
            {
                sort_ = static_cast<sort_mode>(index);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltipEx("%s", "Order of the projects");
    widgets::pop_field_style();
    ImGui::SameLine();
    if(widgets::draw_icon_toggle("##grid", ICON_MDI_VIEW_GRID_OUTLINE, layout_ == layout_mode::grid, "Show as a grid of thumbnails"))
    {
        layout_ = layout_mode::grid;
    }
    ImGui::SameLine(0.0f, toggle_gap);
    if(widgets::draw_icon_toggle("##list", ICON_MDI_FORMAT_LIST_BULLETED, layout_ == layout_mode::list, "Show as a list"))
    {
        layout_ = layout_mode::list;
    }
}

void start_page::draw_projects_content(rtti::context& ctx)
{
    const std::vector<size_t> shown_projects = collect_shown_projects();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PROJECTS_VIEW_CONTENT_INSET_PIXELS, PROJECTS_VIEW_CONTENT_INSET_PIXELS));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, PROJECTS_VIEW_TRANSPARENT_COLOR);
    const bool is_visible = ImGui::BeginChild("##projects_content",
                                              ImVec2(0.0f, 0.0f),
                                              ImGuiChildFlags_AlwaysUseWindowPadding,
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if(is_visible)
    {
        if(projects_.empty())
        {
            draw_no_projects();
        }
        else if(shown_projects.empty())
        {
            widgets::draw_empty_state(ICON_MDI_MAGNIFY, "No projects match the search", "Try another name or part of the folder.");
        }
        else if(layout_ == layout_mode::grid)
        {
            draw_projects_grid(ctx, shown_projects);
        }
        else
        {
            draw_projects_list(ctx, shown_projects);
        }
    }
    ImGui::EndChild();
    // The items hold references into projects_, so a removal waits until they are through.
    if(!pending_recent_removal_.empty())
    {
        remove_from_recents(ctx.get_cached<project_manager>(), pending_recent_removal_);
        pending_recent_removal_.clear();
    }
}

void start_page::draw_projects_grid(rtti::context& ctx, const std::vector<size_t>& shown_projects)
{
    const float gap = to_pixels(PROJECTS_VIEW_GRID_GAP);
    const float avail_width = ImGui::GetContentRegionAvail().x;
    const int columns = ImMax(1, static_cast<int>((avail_width + gap) / (to_pixels(PROJECTS_VIEW_CARD_MIN_WIDTH) + gap)));
    const float card_width = ImFloor((avail_width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns));
    const ImVec2 card_size(card_width, ImFloor(card_width * PROJECTS_VIEW_IMAGE_ASPECT) + calc_project_card_info_height());
    handle_projects_keys(ctx, shown_projects, columns);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    for(size_t position = 0; position < shown_projects.size(); ++position)
    {
        if(position % static_cast<size_t>(columns) != 0)
        {
            ImGui::SameLine();
        }
        draw_project_item(ctx, projects_[shown_projects[position]], card_size);
    }
    ImGui::PopStyleVar();
}

void start_page::draw_projects_list(rtti::context& ctx, const std::vector<size_t>& shown_projects)
{
    handle_projects_keys(ctx, shown_projects, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(PROJECTS_VIEW_ROW_GAP)));
    for(const size_t index : shown_projects)
    {
        draw_project_item(ctx, projects_[index], ImVec2{});
    }
    ImGui::PopStyleVar();
}

void start_page::draw_project_item(rtti::context& ctx, const project_entry& entry, const ImVec2& card_size)
{
    item_state state{};
    state.is_selected = entry.path == selected_project_;
    ImGui::PushID(entry.path.c_str());
    if(layout_ == layout_mode::grid)
    {
        draw_project_card(entry, card_size, state);
    }
    else
    {
        draw_project_row(entry, state);
    }
    handle_project_item_input(ctx, entry);
    draw_project_context_menu(ctx, entry);
    ImGui::PopID();
    if(state.is_selected && scrolls_to_selection_)
    {
        ImGui::SetScrollHereY();
        scrolls_to_selection_ = false;
    }
}

void start_page::draw_project_image(ImDrawList* draw_list, const project_entry& entry, const ImRect& rect, float rounding, ImDrawFlags corners)
{
    const gfx::texture::ptr image = images_.get(entry.thumbnail);
    const ImU32 tint = entry.is_missing ? IM_COL32(255, 255, 255, PROJECTS_VIEW_MISSING_IMAGE_ALPHA) : IM_COL32_WHITE;
    if(image)
    {
        screen_art::draw_image_cover(draw_list, image, rect, rounding, corners, tint);
    }
    else
    {
        screen_art::draw_placeholder(draw_list, rect, entry.name, rounding, corners);
    }
    if(entry.is_missing)
    {
        draw_missing_overlay(draw_list, rect);
    }
}

void start_page::draw_project_card(const project_entry& entry, const ImVec2& size, const item_state& state)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##project", size);
    const bool is_hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(pos, pos + size);
    const float rounding = to_pixels(PROJECTS_VIEW_ROUNDING);
    widgets::draw_card_frame(draw_list, rect, rounding, is_hovered, state.is_selected);
    // Inside the border, so the frame stays whole around the picture.
    const ImRect image_rect(pos + ImVec2(1.0f, 1.0f), ImVec2(rect.Max.x - 1.0f, pos.y + ImFloor(size.x * PROJECTS_VIEW_IMAGE_ASPECT)));
    draw_project_image(draw_list, entry, image_rect, rounding - 1.0f, ImDrawFlags_RoundCornersTop);
    const float padding = to_pixels(PROJECTS_VIEW_CARD_PADDING);
    const float text_x = pos.x + padding;
    const float max_x = rect.Max.x - padding;
    float text_y = image_rect.Max.y + padding;
    ImGui::PushFont(ImGui::Font::SemiBold);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, text_y), max_x, widgets::get_text_color(1.0f), entry.name.c_str());
    ImGui::PopFont();
    text_y += ImGui::GetFontSize() + to_pixels(PROJECTS_VIEW_LINE_GAP);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, text_y), max_x, widgets::get_muted_color(), entry.directory.c_str());
    text_y += ImGui::GetFontSize() + to_pixels(PROJECTS_VIEW_META_GAP);
    draw_project_meta_line(draw_list, ImVec2(text_x, text_y), max_x, {entry.modified_label, entry.engine_version, entry.is_missing, entry.is_engine_older});
}

void start_page::draw_project_row(const project_entry& entry, const item_state& state)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, to_pixels(PROJECTS_VIEW_ROW_HEIGHT));
    if(state.is_interactive)
    {
        ImGui::InvisibleButton("##project", size);
    }
    else
    {
        ImGui::Dummy(size);
    }
    const bool is_hovered = state.is_interactive && ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(pos, pos + size);
    widgets::draw_card_frame(draw_list, rect, to_pixels(PROJECTS_VIEW_ROUNDING), is_hovered, state.is_selected);
    const float padding = to_pixels(PROJECTS_VIEW_ROW_PADDING);
    const float image_height = size.y - 2.0f * padding;
    const ImVec2 image_min(pos.x + padding, pos.y + padding);
    const ImRect image_rect(image_min, image_min + ImVec2(ImFloor(image_height / PROJECTS_VIEW_IMAGE_ASPECT), image_height));
    draw_project_image(draw_list, entry, image_rect, to_pixels(PROJECTS_VIEW_IMAGE_ROUNDING), ImDrawFlags_RoundCornersAll);
    const project_meta meta{entry.modified_label, entry.engine_version, entry.is_missing, entry.is_engine_older};
    const float line_gap = to_pixels(PROJECTS_VIEW_LINE_GAP);
    const float top = ImFloor(rect.GetCenter().y - ImGui::GetFontSize() - line_gap * 0.5f);
    const float bottom = top + ImGui::GetFontSize() + line_gap;
    const float right = rect.Max.x - padding;
    // The right end: when it was saved over the engine it was saved with.
    const float modified_width = ImGui::CalcTextSize(format_modified_label(meta).c_str()).x;
    const float trailer_width = ImMax(modified_width, widgets::calc_pill_size(make_version_badge(entry.engine_version).c_str()).x);
    draw_modified_label(draw_list, ImVec2(right - modified_width, top), right, meta);
    draw_version_badge(draw_list, right, bottom, meta);
    const float text_x = image_rect.Max.x + to_pixels(PROJECTS_VIEW_ROW_TEXT_GAP);
    const float text_max_x = right - trailer_width - to_pixels(PROJECTS_VIEW_ROW_TEXT_GAP);
    ImGui::PushFont(ImGui::Font::SemiBold);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, top), text_max_x, widgets::get_text_color(1.0f), entry.name.c_str());
    ImGui::PopFont();
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, bottom), text_max_x, widgets::get_muted_color(), entry.directory.c_str());
}

void start_page::handle_project_item_input(rtti::context& ctx, const project_entry& entry)
{
    if(ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        selected_project_ = entry.path;
    }
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        open_project(ctx, entry.path);
    }
    if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        selected_project_ = entry.path;
        ImGui::OpenPopup("##project_context_menu");
    }
    const char* engine_note = entry.is_engine_older ? "\nLast opened with an older engine: opening it asks first." : "";
    const std::string engine_line = entry.engine_version.empty() ? std::string{} : "\nEngine " + entry.engine_version;
    ImGui::SetItemTooltipEx("%s%s%s", entry.path.c_str(), engine_line.c_str(), engine_note);
}

void start_page::draw_project_context_menu(rtti::context& ctx, const project_entry& entry)
{
    if(!ImGui::BeginPopup("##project_context_menu"))
    {
        return;
    }
    if(ImGui::MenuItem(ICON_MDI_FOLDER_OPEN_OUTLINE "  Open"))
    {
        open_project(ctx, entry.path);
    }
    if(ImGui::MenuItem(ICON_MDI_OPEN_IN_NEW "  Show in Explorer"))
    {
        fs::show_in_graphical_env(fs::path(entry.path));
    }
    if(ImGui::MenuItem(ICON_MDI_CONTENT_COPY "  Copy Path"))
    {
        ImGui::SetClipboardText(entry.path.c_str());
    }
    if(ImGui::MenuItem(ICON_MDI_IMAGE_EDIT_OUTLINE "  Set Thumbnail...", nullptr, false, !entry.is_missing))
    {
        pick_thumbnail(ctx.get_cached<project_manager>(), entry.path);
    }
    ImGui::Separator();
    if(ImGui::MenuItem(ICON_MDI_PLAYLIST_REMOVE "  Remove from Recents"))
    {
        pending_recent_removal_ = entry.path;
    }
    if(ImGui::MenuItem(ICON_MDI_DELETE_OUTLINE "  Remove..."))
    {
        project_to_remove_ = entry.path;
        view_ = view_state::remove_project;
    }
    ImGui::EndPopup();
}

void start_page::handle_projects_keys(rtti::context& ctx, const std::vector<size_t>& shown_projects, int columns)
{
    // Also before the first click on the page: then no window has the focus at all.
    const bool is_page_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                                 !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow);
    const bool is_listening = is_page_focused && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput && !shown_projects.empty();
    if(!is_listening)
    {
        return;
    }
    // Owned, the keys move the selection only, not the navigation cursor of ImGui as well. ImGui
    // lets go of a key on its own a frame after it is up, once the projects stop listening.
    const ImGuiID keys_owner = ImGui::GetID("##project_keys");
    for(const ImGuiKey key : PROJECTS_VIEW_KEYS)
    {
        ImGui::SetKeyOwner(key, keys_owner);
    }
    const auto is_pressed = [keys_owner](ImGuiKey key, bool is_repeating) -> bool
    {
        return ImGui::IsKeyPressed(key, is_repeating ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None, keys_owner);
    };
    const auto selected_it = std::find_if(shown_projects.begin(),
                                          shown_projects.end(),
                                          [&](size_t index)
                                          {
                                              return projects_[index].path == selected_project_;
                                          });
    const bool has_selection = selected_it != shown_projects.end();
    if(has_selection && (is_pressed(ImGuiKey_Enter, false) || is_pressed(ImGuiKey_KeypadEnter, false)))
    {
        open_project(ctx, selected_project_);
        return;
    }
    // The Remove Project page asks first: off the list, or the folder deleted as well.
    if(has_selection && is_pressed(shortcuts::delete_item, false))
    {
        project_to_remove_ = selected_project_;
        show_view(view_state::remove_project);
        return;
    }
    const int row_step = (is_pressed(ImGuiKey_DownArrow, true) ? columns : 0) - (is_pressed(ImGuiKey_UpArrow, true) ? columns : 0);
    const bool is_grid = columns > 1;
    const int column_step = is_grid ? (is_pressed(ImGuiKey_RightArrow, true) ? 1 : 0) - (is_pressed(ImGuiKey_LeftArrow, true) ? 1 : 0) : 0;
    const int step = row_step + column_step;
    if(step == 0)
    {
        return;
    }
    const int last = static_cast<int>(shown_projects.size()) - 1;
    const int current = has_selection ? static_cast<int>(selected_it - shown_projects.begin()) : (step > 0 ? -1 : last + 1);
    selected_project_ = projects_[shown_projects[static_cast<size_t>(ImClamp(current + step, 0, last))]].path;
    scrolls_to_selection_ = true;
}

void start_page::draw_no_projects()
{
    widgets::draw_empty_state(ICON_MDI_FOLDER_OPEN_OUTLINE, "No projects yet", "Create a project, open one from disk, or start from a sample.");
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PROJECTS_VIEW_EMPTY_BUTTONS_GAP)));
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float buttons_width = widgets::calc_button_width(PROJECTS_VIEW_NEW_LABEL) + spacing +
                                widgets::calc_button_width(PROJECTS_VIEW_SAMPLES_LABEL);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetContentRegionAvail().x - buttons_width) * 0.5f));
    if(widgets::draw_button(PROJECTS_VIEW_NEW_LABEL, widgets::button_kind::primary))
    {
        show_view(view_state::create_project);
    }
    ImGui::SameLine();
    if(widgets::draw_button(PROJECTS_VIEW_SAMPLES_LABEL, widgets::button_kind::secondary))
    {
        show_view(view_state::samples);
    }
}
} // namespace unravel
