#include "start_page.h"

#include <editor/editing/editing_manager.h>
#include <editor/hub/engine_links.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>
#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>
#include <logging/logging.h>
#include <version/version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float START_PAGE_SHARE_X = 0.62f;
constexpr float START_PAGE_SHARE_Y = 0.7f;
constexpr float START_PAGE_MIN_WIDTH = 50.0f;
constexpr float START_PAGE_MAX_WIDTH = 80.0f;
constexpr float START_PAGE_MIN_HEIGHT = 30.0f;
constexpr float START_PAGE_MAX_HEIGHT = 46.0f;
constexpr float START_PAGE_SECTION_GAP = 1.1f;
// Between the list and the actions beside it.
constexpr float START_PAGE_COLUMN_GAP = 2.0f;
constexpr float START_PAGE_SIDEBAR_WIDTH = 13.5f;
constexpr float START_PAGE_SEARCH_WIDTH = 16.0f;
constexpr float START_PAGE_FORM_WIDTH = 40.0f;
constexpr float START_PAGE_TITLE_SCALE = 1.45f;
constexpr float START_PAGE_BUTTON_HEIGHT = 2.2f;
constexpr float START_PAGE_PRIMARY_BUTTON_HEIGHT = 2.6f;
constexpr float START_PAGE_BUTTON_PADDING_X = 0.9f;
constexpr float START_PAGE_BUTTON_ROUNDING = 0.4f;
// The theme's input fields are darker than a window; on the dark page they would vanish, so the
// page washes them light, the way the panel toolbars do.
constexpr ImU32 START_PAGE_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 START_PAGE_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 START_PAGE_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
// The labels say how long ago a project was touched, so they age while the page stays open.
constexpr double START_PAGE_REFRESH_SECONDS = 20.0;

constexpr float PROJECT_ROW_PADDING = 0.75f;
constexpr float PROJECT_ROW_AVATAR = 2.5f;
constexpr float PROJECT_ROW_AVATAR_ROUNDING = 0.5f;
constexpr float PROJECT_ROW_GAP = 0.45f;
constexpr float PROJECT_ROW_ROUNDING = 0.5f;
constexpr float PROJECT_ROW_LINE_GAP = 0.3f;
constexpr float PROJECT_ROW_BADGE_PADDING_X = 0.5f;
constexpr float PROJECT_ROW_BADGE_PADDING_Y = 0.15f;
constexpr ImU32 PROJECT_ROW_COLOR = IM_COL32(255, 255, 255, 10);
constexpr ImU32 PROJECT_ROW_BORDER_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 PROJECT_ROW_HOVERED_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 PROJECT_ROW_HOVERED_BORDER_COLOR = IM_COL32(255, 255, 255, 40);
constexpr ImU32 PROJECT_ROW_BADGE_COLOR = IM_COL32(255, 255, 255, 14);
constexpr float PROJECT_ROW_SELECTED_FILL_ALPHA = 0.35f;
constexpr ImU32 START_PAGE_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
constexpr ImU32 START_PAGE_MISSING_COLOR = IM_COL32(255, 110, 110, 255);
constexpr ImVec4 START_PAGE_DANGER_COLOR{0.70f, 0.24f, 0.24f, 1.0f};
constexpr ImVec4 START_PAGE_DANGER_HOVERED_COLOR{0.84f, 0.31f, 0.31f, 1.0f};
constexpr ImVec4 START_PAGE_DANGER_ACTIVE_COLOR{0.60f, 0.20f, 0.20f, 1.0f};

// An avatar takes its color from the name, so a project keeps it wherever it shows up.
constexpr std::array<ImU32, 8> START_PAGE_AVATAR_COLORS{IM_COL32(58, 121, 187, 255),
                                                        IM_COL32(76, 153, 120, 255),
                                                        IM_COL32(184, 120, 64, 255),
                                                        IM_COL32(150, 96, 170, 255),
                                                        IM_COL32(190, 84, 84, 255),
                                                        IM_COL32(70, 150, 160, 255),
                                                        IM_COL32(170, 150, 70, 255),
                                                        IM_COL32(110, 120, 140, 255)};

struct sample_link
{
    const char* name;
    const char* description;
    const char* url;
};

constexpr std::array<sample_link, 4> START_PAGE_SAMPLES{{
    {"Demo Project",
     "Full demo project with PBR rendering, physics, audio, and scripting examples.",
     "https://github.com/unravel-dev/DemoProject"},
    {"UnravelEngine Repository",
     "Source code and documentation for the UnravelEngine.",
     engine_links::REPOSITORY},
    {"Engine API Docs",
     "C++ engine API documentation.",
     engine_links::ENGINE_API_DOCS},
    {"Scripting API Docs",
     "C# scripting API documentation.",
     engine_links::SCRIPTING_API_DOCS},
}};

/// What a card shows: a title over a dimmed line, and optionally a badge over a dimmed line at
/// its right end.
struct card_content
{
    const char* avatar_text{};
    ImU32 avatar_color{};
    const char* title{};
    const char* subtitle{};
    const char* badge{};
    ImU32 badge_text_color{};
    const char* note{};
    ImU32 note_color{};
};

auto get_avatar_color(const std::string& name) -> ImU32
{
    return START_PAGE_AVATAR_COLORS[ImHashStr(name.c_str()) % START_PAGE_AVATAR_COLORS.size()];
}

auto format_time_ago(long long count, const char* unit) -> std::string
{
    return fmt::format("{} {}{} ago", count, unit, count == 1 ? "" : "s");
}

auto format_modified_label(const std::chrono::system_clock::time_point& modified) -> std::string
{
    using namespace std::chrono;
    const auto age = system_clock::now() - modified;
    if(age < minutes(1))
    {
        return "Just now";
    }
    if(age < hours(1))
    {
        return format_time_ago(duration_cast<minutes>(age).count(), "minute");
    }
    if(age < hours(24))
    {
        return format_time_ago(duration_cast<hours>(age).count(), "hour");
    }
    const auto days = duration_cast<hours>(age).count() / 24;
    if(days < 30)
    {
        return format_time_ago(days, "day");
    }
    try
    {
        return fmt::format("{:%b %d, %Y}", modified);
    }
    catch(const std::exception&)
    {
        return "Unknown";
    }
}

/// When the project was last saved, by the settings file every save touches.
auto read_modified_label(const fs::path& project_path) -> std::string
{
    fs::error_code error;
    const auto file_time = fs::last_write_time(project_path / "settings" / "settings.cfg", error);
    if(error)
    {
        return "Unknown";
    }
    // The file clock is not the system clock: carry the age over.
    const auto modified = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return format_modified_label(modified);
}

void draw_card_background(const ImRect& rect, bool is_hovered, bool is_selected)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = to_pixels(PROJECT_ROW_ROUNDING);
    draw_list->AddRectFilled(rect.Min, rect.Max, PROJECT_ROW_COLOR, rounding);
    if(is_selected)
    {
        ImVec4 fill_color = imgui_style::get_accent_color();
        fill_color.w *= PROJECT_ROW_SELECTED_FILL_ALPHA;
        draw_list->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill_color), rounding);
        draw_list->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(imgui_style::get_accent_color()), rounding, 0, 1.5f);
        return;
    }
    if(is_hovered)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, PROJECT_ROW_HOVERED_COLOR, rounding);
    }
    draw_list->AddRect(rect.Min,
                       rect.Max,
                       is_hovered ? PROJECT_ROW_HOVERED_BORDER_COLOR : PROJECT_ROW_BORDER_COLOR,
                       rounding);
}

void draw_avatar(const ImRect& rect, const char* text, ImU32 color)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect.Min, rect.Max, color, to_pixels(PROJECT_ROW_AVATAR_ROUNDING));
    ImGui::PushFont(ImGui::Font::Bold);
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 text_pos(ImFloor(rect.GetCenter().x - text_size.x * 0.5f),
                          ImFloor(rect.GetCenter().y - text_size.y * 0.5f));
    draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), text);
    ImGui::PopFont();
}

/// The right end of a card: a badge over a note, both right aligned. Returns the width it took.
auto draw_card_trailer(const ImRect& rect, const card_content& content) -> float
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = to_pixels(PROJECT_ROW_PADDING);
    const float font_size = ImGui::GetFontSize();
    const float line_gap = to_pixels(PROJECT_ROW_LINE_GAP);
    const float top = ImFloor(rect.GetCenter().y - font_size - line_gap * 0.5f);
    const float right = rect.Max.x - padding;
    float width = 0.0f;
    if(content.badge != nullptr && content.badge[0] != 0)
    {
        const ImVec2 badge_padding(to_pixels(PROJECT_ROW_BADGE_PADDING_X), to_pixels(PROJECT_ROW_BADGE_PADDING_Y));
        const ImVec2 text_size = ImGui::CalcTextSize(content.badge);
        const ImRect badge_rect(ImVec2(right - text_size.x - 2.0f * badge_padding.x, top - badge_padding.y),
                                ImVec2(right, top + text_size.y + badge_padding.y));
        draw_list->AddRectFilled(badge_rect.Min, badge_rect.Max, PROJECT_ROW_BADGE_COLOR, badge_rect.GetHeight() * 0.5f);
        draw_list->AddText(ImVec2(badge_rect.Min.x + badge_padding.x, top), content.badge_text_color, content.badge);
        width = badge_rect.GetWidth();
    }
    if(content.note != nullptr && content.note[0] != 0)
    {
        const ImVec2 text_size = ImGui::CalcTextSize(content.note);
        draw_list->AddText(ImVec2(right - text_size.x, top + font_size + line_gap), content.note_color, content.note);
        width = ImMax(width, text_size.x);
    }
    return width;
}

/// Avatar, title over subtitle, trailer. The texts give way with an ellipsis, so a card never
/// needs more width than it is given.
void draw_card_content(const ImRect& rect, const card_content& content)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float padding = to_pixels(PROJECT_ROW_PADDING);
    const float avatar_size = to_pixels(PROJECT_ROW_AVATAR);
    const float font_size = ImGui::GetFontSize();
    const float line_gap = to_pixels(PROJECT_ROW_LINE_GAP);
    const ImVec2 avatar_min(rect.Min.x + padding, rect.Min.y + padding);
    draw_avatar(ImRect(avatar_min, avatar_min + ImVec2(avatar_size, avatar_size)), content.avatar_text, content.avatar_color);
    const float trailer_width = draw_card_trailer(rect, content);
    const float text_min_x = avatar_min.x + avatar_size + padding;
    const float text_max_x = rect.Max.x - padding - trailer_width - padding;
    const float top = ImFloor(rect.GetCenter().y - font_size - line_gap * 0.5f);
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(text_min_x, top),
                              ImVec2(text_max_x, rect.Max.y),
                              text_max_x,
                              content.title,
                              nullptr,
                              nullptr);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::RenderTextEllipsis(draw_list,
                              ImVec2(text_min_x, top + font_size + line_gap),
                              ImVec2(text_max_x, rect.Max.y),
                              text_max_x,
                              content.subtitle,
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
}

auto calc_card_height() -> float
{
    return to_pixels(PROJECT_ROW_AVATAR) + 2.0f * to_pixels(PROJECT_ROW_PADDING);
}

void push_field_style()
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, START_PAGE_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, START_PAGE_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, START_PAGE_FIELD_ACTIVE_COLOR);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(START_PAGE_BUTTON_ROUNDING));
}

void pop_field_style()
{
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void push_button_shape()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(START_PAGE_BUTTON_ROUNDING));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(to_pixels(START_PAGE_BUTTON_PADDING_X), ImGui::GetStyle().FramePadding.y));
}

void pop_button_shape()
{
    ImGui::PopStyleVar(3);
}

/// A button of the page in the theme's button colors.
auto draw_page_button(const char* label, float width, bool is_enabled = true) -> bool
{
    push_button_shape();
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(width, to_pixels(START_PAGE_BUTTON_HEIGHT)));
    ImGui::EndDisabled();
    pop_button_shape();
    return is_pressed;
}

/// A button filled with its own color: the accent for the one action a page is about, red for
/// the one that destroys something.
auto draw_page_filled_button(const char* label,
                        float width,
                        const std::array<ImVec4, 3>& colors,
                        bool is_enabled = true) -> bool
{
    push_button_shape();
    ImGui::PushStyleColor(ImGuiCol_Button, colors[0]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[1]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors[2]);
    // Bold carries the icon glyphs, semi bold does not.
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(width, to_pixels(START_PAGE_PRIMARY_BUTTON_HEIGHT)));
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    pop_button_shape();
    return is_pressed;
}

auto get_accent_button_colors() -> std::array<ImVec4, 3>
{
    const ImVec4 accent = imgui_style::get_accent_color();
    const ImVec4 hovered(accent.x, accent.y, accent.z, 1.0f);
    const ImVec4 active(accent.x * 0.85f, accent.y * 0.85f, accent.z * 0.85f, 1.0f);
    return {accent, hovered, active};
}

auto get_danger_button_colors() -> std::array<ImVec4, 3>
{
    return {START_PAGE_DANGER_COLOR, START_PAGE_DANGER_HOVERED_COLOR, START_PAGE_DANGER_ACTIVE_COLOR};
}

void draw_page_title(const char* title)
{
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(START_PAGE_TITLE_SCALE);
    ImGui::TextUnformatted(title);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
}

/// Dimmed, wrapped text. wrap_width 0 wraps at the edge of the window.
void draw_muted_text(const char* text, float wrap_width = 0.0f)
{
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::PushTextWrapPos(wrap_width > 0.0f ? ImGui::GetCursorPosX() + wrap_width : 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void draw_section_gap()
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(START_PAGE_SECTION_GAP)));
}

/// Title and subtitle of the page, the running engine version at its right end.
void draw_page_header(const char* title, const char* subtitle)
{
    const std::string version_text = version::get_full();
    const float version_width = ImGui::CalcTextSize(version_text.c_str()).x;
    const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - version_width;
    draw_page_title(title);
    ImGui::SameLine();
    ImGui::SetCursorPosX(right_x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(imgui_style::get_muted_text_color_u32()), "%s", version_text.c_str());
    draw_muted_text(subtitle);
    draw_section_gap();
}

/// Header of a sub page: the way back, then title and subtitle. Returns true when the user
/// wants back, by the button or by Escape.
auto draw_sub_page_header(const char* title, const char* subtitle) -> bool
{
    const bool is_back_pressed = ImGui::Button(ICON_MDI_ARROW_LEFT " Back");
    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive();
    draw_section_gap();
    draw_page_title(title);
    draw_muted_text(subtitle);
    draw_section_gap();
    return is_back_pressed || is_escape_pressed;
}

void draw_centered_hint(const char* icon, const char* headline, const char* text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.3f);
    for(const char* line : {icon, headline, text})
    {
        const float line_width = ImGui::CalcTextSize(line).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (avail.x - line_width) * 0.5f));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(imgui_style::get_muted_text_color_u32()), "%s", line);
    }
}

auto calc_page_size(const ImVec2& viewport_size) -> ImVec2
{
    const ImVec2 wanted(ImClamp(viewport_size.x * START_PAGE_SHARE_X,
                                to_pixels(START_PAGE_MIN_WIDTH),
                                to_pixels(START_PAGE_MAX_WIDTH)),
                        ImClamp(viewport_size.y * START_PAGE_SHARE_Y,
                                to_pixels(START_PAGE_MIN_HEIGHT),
                                to_pixels(START_PAGE_MAX_HEIGHT)));
    return screen_card::fit_to_viewport(wanted);
}
} // namespace

void start_page::draw(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    refresh_projects_if_stale(pm);
    // The page is a card, not a modal: a modal here would own the popup stack and break every
    // other modal opened while it shows (the engine version confirmation, for one).
    screen_card::card_layout layout{};
    layout.size = calc_page_size(ImGui::GetMainViewport()->WorkSize);
    if(screen_card::begin("START PAGE", layout))
    {
        switch(view_)
        {
            case view_state::projects:
                draw_projects_view(ctx);
                break;
            case view_state::create_project:
                draw_create_project_view(ctx);
                break;
            case view_state::remove_project:
                draw_remove_project_view(ctx);
                break;
            case view_state::samples:
                draw_samples_view();
                break;
        }
    }
    screen_card::end();
    last_drawn_frame_ = ImGui::GetFrameCount();
}

void start_page::refresh_projects_if_stale(project_manager& pm)
{
    const auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
    const auto is_same_project = [](const project_entry& entry, const fs::path& path) -> bool
    {
        return entry.path == path.string();
    };
    const bool is_list_changed = !std::equal(projects_.begin(),
                                             projects_.end(),
                                             recent_projects.begin(),
                                             recent_projects.end(),
                                             is_same_project);
    const bool is_page_back = ImGui::GetFrameCount() - last_drawn_frame_ > 1;
    const bool is_aged = ImGui::GetTime() - refresh_time_ > START_PAGE_REFRESH_SECONDS;
    if(is_list_changed || is_page_back || is_aged)
    {
        refresh_projects(pm);
    }
}

void start_page::refresh_projects(project_manager& pm)
{
    projects_.clear();
    for(const fs::path& project_path : pm.get_editor_settings().projects.recent_projects)
    {
        project_entry entry{};
        entry.path = project_path.string();
        entry.name = project_path.stem().string();
        entry.directory = project_path.parent_path().generic_string();
        const auto report = pm.inspect_project(project_path);
        if(report.status != project_manager::project_compat::no_info_file)
        {
            const auto& engine_version = report.on_disk.engine_version_opened;
            entry.engine_version = engine_version.original.empty() ? engine_version.to_string() : engine_version.original;
        }
        entry.is_engine_older = report.status == project_manager::project_compat::engine_older;
        fs::error_code error;
        entry.is_missing = !fs::exists(project_path, error);
        entry.modified_label = entry.is_missing ? std::string("Folder not found") : read_modified_label(project_path);
        projects_.emplace_back(std::move(entry));
    }
    refresh_time_ = ImGui::GetTime();
}

auto start_page::find_project(const std::string& path) const -> const project_entry*
{
    const auto it = std::find_if(projects_.begin(),
                                 projects_.end(),
                                 [&](const project_entry& entry)
                                 {
                                     return entry.path == path;
                                 });
    return it != projects_.end() ? &*it : nullptr;
}

auto start_page::collect_shown_projects() const -> std::vector<size_t>
{
    std::vector<size_t> shown_projects;
    shown_projects.reserve(projects_.size());
    for(size_t index = 0; index < projects_.size(); ++index)
    {
        const project_entry& entry = projects_[index];
        if(filter_.PassFilter(entry.name.c_str()) || filter_.PassFilter(entry.directory.c_str()))
        {
            shown_projects.emplace_back(index);
        }
    }
    return shown_projects;
}

void start_page::show_view(view_state view)
{
    view_ = view;
    new_project_name_.clear();
    new_project_directory_.clear();
    if(view != view_state::remove_project)
    {
        project_to_remove_.clear();
    }
}

void start_page::open_project(rtti::context& ctx, const std::string& path)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    const fs::path project_path = fs::path(path).make_preferred();
    const auto queue_open = [&ctx, &pm, &em, project_path]()
    {
        em.queue_action<untracked_editor_state_action_t>("Open Project",
                        [&ctx, &pm, project_path]()
                        {
                            pm.open_project(ctx, project_path);
                        });
    };
    // Inspected before opening, so a suspicious folder gets a question instead of a fresh
    // signature stamped onto it.
    const auto report = pm.inspect_project(project_path);
    if(report.status == project_manager::project_compat::ok)
    {
        queue_open();
        return;
    }
    const std::string running = version::get_current().to_string();
    std::string title;
    std::string message;
    std::string cancel_log;
    if(report.status == project_manager::project_compat::no_info_file)
    {
        title = "Unrecognized project folder";
        message = "The selected folder does not contain a project.cfg file.\n\n"
                  "  Path:             " + project_path.string() + "\n"
                  "  Running engine:   " + running + "\n\n"
                  "This is either a legacy project created before project.cfg was introduced, "
                  "or a folder that isn't an Unravel project at all.\n\n"
                  "If you proceed, the engine will assume it is a project and create a fresh "
                  "project.cfg inside it.\n\n"
                  "Open this folder as a project?";
        cancel_log = "Project open cancelled by user (no project.cfg).";
    }
    else
    {
        const auto& opened_version = report.on_disk.engine_version_opened;
        const std::string opened_by = opened_version.original.empty() ? opened_version.to_string() : opened_version.original;
        title = "Project engine-version mismatch";
        message = "This project was last opened with an older engine version than the one you are running.\n\n"
                  "  Project saved by: " + opened_by + "\n"
                  "  Running engine:   " + running + "\n\n"
                  "Opening it may cause data loss if the on-disk format has changed since then.\n"
                  "It is strongly recommended to back up the project folder yourself before proceeding.\n\n"
                  "Open the project anyway?";
        cancel_log = "Project open cancelled by user (engine-version mismatch).";
    }
    ImBox::ShowConfirmation(
        title,
        message,
        [queue_open, cancel_log](ImBox::ModalResult result)
        {
            if(!ImBox::IsConfirmation(result))
            {
                APPLOG_INFO("{}", cancel_log);
                return;
            }
            queue_open();
        },
        ImBox::MessageType::Warning);
}

void start_page::remove_from_recents(project_manager& pm, const std::string& path)
{
    auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
    const auto it = std::find(recent_projects.begin(), recent_projects.end(), fs::path(path));
    if(it != recent_projects.end())
    {
        recent_projects.erase(it);
        pm.save_editor_settings();
    }
    if(selected_project_ == path)
    {
        selected_project_.clear();
    }
    refresh_projects(pm);
}

void start_page::draw_projects_view(rtti::context& ctx)
{
    draw_page_header("Unravel Engine", "Open a recent project or create a new one.");
    const float sidebar_width = to_pixels(START_PAGE_SIDEBAR_WIDTH);
    const float gap = to_pixels(START_PAGE_COLUMN_GAP);
    const float list_width = ImGui::GetContentRegionAvail().x - sidebar_width - gap;
    ImGui::BeginGroup();
    draw_projects_toolbar(list_width);
    draw_projects_list(ctx, ImVec2(list_width, ImGui::GetContentRegionAvail().y));
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginGroup();
    draw_actions(ctx, sidebar_width);
    ImGui::EndGroup();
}

void start_page::draw_projects_toolbar(float width)
{
    const float row_start_x = ImGui::GetCursorPosX();
    const float search_width = ImMin(to_pixels(START_PAGE_SEARCH_WIDTH), width * 0.5f);
    const std::string heading = fmt::format("Recent Projects  {}", projects_.size());
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted(heading.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(row_start_x + width - search_width);
    push_field_style();
    ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search projects...", search_width);
    pop_field_style();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PROJECT_ROW_GAP)));
}

void start_page::draw_projects_list(rtti::context& ctx, const ImVec2& size)
{
    // No border and no padding: a row is as wide as the list, whatever the theme says.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(PROJECT_ROW_GAP)));
    if(ImGui::BeginChild("##projects_list", size, ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings))
    {
        const std::vector<size_t> shown_projects = collect_shown_projects();
        if(projects_.empty())
        {
            draw_centered_hint(ICON_MDI_FOLDER_OPEN_OUTLINE, "No recent projects", "Create a new project or browse for an existing one.");
        }
        else if(shown_projects.empty())
        {
            draw_centered_hint(ICON_MDI_MAGNIFY, "No projects match the search", "Try another name or path.");
        }
        handle_list_keys(ctx, shown_projects);
        for(const size_t index : shown_projects)
        {
            const project_entry& entry = projects_[index];
            project_row_state state{};
            state.is_selected = entry.path == selected_project_;
            ImGui::PushID(entry.path.c_str());
            draw_project_row(entry, state);
            handle_project_row_input(ctx, entry);
            draw_project_context_menu(ctx, entry);
            ImGui::PopID();
            if(state.is_selected && scrolls_to_selection_)
            {
                ImGui::SetScrollHereY();
                scrolls_to_selection_ = false;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // The rows hold references into projects_, so a removal waits until they are through.
    if(!pending_recent_removal_.empty())
    {
        remove_from_recents(ctx.get_cached<project_manager>(), pending_recent_removal_);
        pending_recent_removal_.clear();
    }
}

auto start_page::draw_project_row(const project_entry& entry, const project_row_state& state) -> bool
{
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_size(ImGui::GetContentRegionAvail().x, calc_card_height());
    const ImRect row_rect(row_min, row_min + row_size);
    if(state.is_interactive)
    {
        ImGui::InvisibleButton("##project_row", row_size);
    }
    else
    {
        ImGui::Dummy(row_size);
    }
    const bool is_hovered = state.is_interactive && ImGui::IsItemHovered();
    draw_card_background(row_rect, is_hovered, state.is_selected);
    const std::string initial = entry.name.empty() ? std::string("?") : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(entry.name.front()))));
    const std::string badge = entry.engine_version.empty() ? std::string{} : fmt::format("{} {}", ICON_MDI_ENGINE_OUTLINE, entry.engine_version);
    card_content content{};
    content.avatar_text = initial.c_str();
    content.avatar_color = get_avatar_color(entry.name);
    content.title = entry.name.c_str();
    content.subtitle = entry.directory.c_str();
    content.badge = badge.c_str();
    content.badge_text_color = entry.is_engine_older ? START_PAGE_WARNING_COLOR : imgui_style::get_muted_text_color_u32();
    content.note = entry.modified_label.c_str();
    content.note_color = entry.is_missing ? START_PAGE_MISSING_COLOR : imgui_style::get_muted_text_color_u32();
    draw_card_content(row_rect, content);
    return is_hovered;
}

void start_page::handle_project_row_input(rtti::context& ctx, const project_entry& entry)
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
    ImGui::SetItemTooltipEx("%s%s", entry.path.c_str(), engine_note);
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

void start_page::handle_list_keys(rtti::context& ctx, const std::vector<size_t>& shown_projects)
{
    const bool is_listening = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive();
    if(!is_listening || shown_projects.empty())
    {
        return;
    }
    const auto selected_it = std::find_if(shown_projects.begin(),
                                          shown_projects.end(),
                                          [&](size_t index)
                                          {
                                              return projects_[index].path == selected_project_;
                                          });
    const bool has_selection = selected_it != shown_projects.end();
    if(has_selection && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
    {
        open_project(ctx, selected_project_);
        return;
    }
    const int step = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0);
    if(step == 0)
    {
        return;
    }
    const int last = static_cast<int>(shown_projects.size()) - 1;
    const int current = has_selection ? static_cast<int>(selected_it - shown_projects.begin()) : (step > 0 ? -1 : last + 1);
    selected_project_ = projects_[shown_projects[static_cast<size_t>(ImClamp(current + step, 0, last))]].path;
    scrolls_to_selection_ = true;
}

void start_page::draw_actions(rtti::context& ctx, float width)
{
    const bool has_selection = find_project(selected_project_) != nullptr;
    // The heading keeps the first button level with the first row of the list.
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted("Actions");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(PROJECT_ROW_GAP)));
    if(draw_page_filled_button(ICON_MDI_PLUS "  New Project", width, get_accent_button_colors()))
    {
        show_view(view_state::create_project);
    }
    if(draw_page_button(ICON_MDI_FOLDER_OPEN_OUTLINE "  Open", width, has_selection))
    {
        open_project(ctx, selected_project_);
    }
    ImGui::SetItemTooltipEx("%s", "Open the selected project (Enter, or double-click it)");
    if(draw_page_button(ICON_MDI_FOLDER_SEARCH_OUTLINE "  Browse...", width))
    {
        std::string picked_path;
        if(native::pick_folder_dialog(picked_path))
        {
            open_project(ctx, picked_path);
        }
    }
    ImGui::SetItemTooltipEx("%s", "Open a project folder that is not in the list");
    if(draw_page_button(ICON_MDI_BOOK_OPEN_VARIANT "  Samples", width))
    {
        show_view(view_state::samples);
    }
    ImGui::SetItemTooltipEx("%s", "Sample projects and documentation");
    if(draw_page_button(ICON_MDI_DELETE_OUTLINE "  Remove...", width, has_selection))
    {
        project_to_remove_ = selected_project_;
        view_ = view_state::remove_project;
    }
    ImGui::SetItemTooltipEx("%s", "Remove the selected project from the list, or delete it");
    draw_section_gap();
    draw_muted_text("Double-click a project to open it. Right-click it for more options.", width);
}

void start_page::draw_create_project_view(rtti::context& ctx)
{
    if(draw_sub_page_header("New Project", "Pick a name and the folder the project gets created in."))
    {
        show_view(view_state::projects);
        return;
    }
    const float form_width = ImMin(to_pixels(START_PAGE_FORM_WIDTH), ImGui::GetContentRegionAvail().x);
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted("Project Name");
    ImGui::PopFont();
    push_field_style();
    ImGui::SetNextItemWidth(form_width);
    ImGui::InputTextWidget("##project_name", new_project_name_, false);
    pop_field_style();
    draw_section_gap();
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::TextUnformatted("Location");
    ImGui::PopFont();
    const float browse_width = ImGui::GetFrameHeight() * 1.6f;
    push_field_style();
    ImGui::SetNextItemWidth(form_width - browse_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWidget("##project_directory", new_project_directory_, false);
    pop_field_style();
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_FOLDER_OPEN_OUTLINE "##pick_directory", ImVec2(browse_width, 0.0f)))
    {
        std::string picked_directory;
        if(native::pick_folder_dialog(picked_directory))
        {
            new_project_directory_ = picked_directory;
        }
    }
    ImGui::SetItemTooltipEx("%s", "Browse for a folder...");
    const bool is_complete = !new_project_name_.empty() && !new_project_directory_.empty();
    const fs::path project_path = fs::path(new_project_directory_) / new_project_name_;
    // A folder with content in it would get a project poured over whatever it holds.
    fs::error_code error;
    const bool is_occupied = is_complete && fs::exists(project_path, error) && !fs::is_empty(project_path, error);
    if(is_occupied)
    {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(START_PAGE_WARNING_COLOR),
                           "%s",
                           ICON_MDI_ALERT_OUTLINE " That folder already exists and is not empty.");
    }
    else
    {
        const std::string preview = is_complete ? fmt::format("The project will be created at {}", project_path.generic_string())
                                                : std::string("The project gets a folder of its own inside the location.");
        draw_muted_text(preview.c_str());
    }
    draw_section_gap();
    const float button_width = (form_width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if(draw_page_filled_button(ICON_MDI_PLUS "  Create Project", button_width, get_accent_button_colors(), is_complete && !is_occupied))
    {
        auto& pm = ctx.get_cached<project_manager>();
        auto& em = ctx.get_cached<editing_manager>();
        const fs::path created_path = fs::path(project_path).make_preferred();
        em.queue_action<untracked_editor_state_action_t>("Create Project",
                        [&ctx, &pm, created_path]()
                        {
                            pm.create_project(ctx, created_path);
                        });
        show_view(view_state::projects);
        return;
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (to_pixels(START_PAGE_PRIMARY_BUTTON_HEIGHT) - to_pixels(START_PAGE_BUTTON_HEIGHT)) * 0.5f);
    if(draw_page_button("Cancel", button_width))
    {
        show_view(view_state::projects);
    }
}

void start_page::draw_remove_project_view(rtti::context& ctx)
{
    const project_entry* entry = find_project(project_to_remove_);
    const bool wants_back = draw_sub_page_header("Remove Project", "Take the project off the list, or delete it from the disk as well.");
    if(wants_back || entry == nullptr)
    {
        show_view(view_state::projects);
        return;
    }
    auto& pm = ctx.get_cached<project_manager>();
    const float form_width = ImMin(to_pixels(START_PAGE_FORM_WIDTH) * 1.4f, ImGui::GetContentRegionAvail().x);
    ImGui::BeginChild("##project_to_remove", ImVec2(form_width, calc_card_height()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    project_row_state state{};
    state.is_interactive = false;
    draw_project_row(*entry, state);
    ImGui::EndChild();
    draw_section_gap();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float button_width = (form_width - 2.0f * spacing) / 3.0f;
    // A copy: both removals re-read the entries and leave the pointer dangling.
    const std::string path = entry->path;
    if(draw_page_filled_button(ICON_MDI_PLAYLIST_REMOVE "  Remove from Recents", button_width, get_accent_button_colors()))
    {
        remove_from_recents(pm, path);
        show_view(view_state::projects);
        return;
    }
    ImGui::SetItemTooltipEx("%s", "Take it off the list. The project folder stays untouched.");
    ImGui::SameLine();
    if(draw_page_filled_button(ICON_MDI_DELETE_OUTLINE "  Delete Folder", button_width, get_danger_button_colors()))
    {
        remove_from_recents(pm, path);
        fs::error_code error;
        fs::remove_all(fs::path(path), error);
        show_view(view_state::projects);
        return;
    }
    ImGui::SetItemTooltipEx("%s", "DANGER: permanently deletes the project folder and everything in it.");
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (to_pixels(START_PAGE_PRIMARY_BUTTON_HEIGHT) - to_pixels(START_PAGE_BUTTON_HEIGHT)) * 0.5f);
    if(draw_page_button("Cancel", button_width))
    {
        show_view(view_state::projects);
        return;
    }
    draw_section_gap();
    draw_muted_text("Removing from the recents is the safe choice: the files stay where they are and the project can be "
                    "opened again with Browse.");
}

void start_page::draw_samples_view()
{
    if(draw_sub_page_header("Samples", "A click opens the download or the documentation in the browser."))
    {
        show_view(view_state::projects);
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(PROJECT_ROW_GAP)));
    if(ImGui::BeginChild("##samples_list", ImGui::GetContentRegionAvail(), ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings))
    {
        for(const sample_link& sample : START_PAGE_SAMPLES)
        {
            const ImVec2 row_min = ImGui::GetCursorScreenPos();
            const ImVec2 row_size(ImGui::GetContentRegionAvail().x, calc_card_height());
            ImGui::PushID(sample.url);
            const bool is_pressed = ImGui::InvisibleButton("##sample_row", row_size);
            ImGui::PopID();
            draw_card_background(ImRect(row_min, row_min + row_size), ImGui::IsItemHovered(), false);
            card_content content{};
            content.avatar_text = ICON_MDI_BOOK_OPEN_VARIANT;
            content.avatar_color = get_avatar_color(sample.name);
            content.title = sample.name;
            content.subtitle = sample.description;
            content.note = ICON_MDI_OPEN_IN_NEW;
            content.note_color = imgui_style::get_muted_text_color_u32();
            draw_card_content(ImRect(row_min, row_min + row_size), content);
            ImGui::SetItemTooltipEx("%s", sample.url);
            if(is_pressed)
            {
                ImGui::OpenInShell(sample.url);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
} // namespace unravel
