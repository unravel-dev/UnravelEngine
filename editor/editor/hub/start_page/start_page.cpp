#include "start_page.h"
#include "start_page_widgets.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/screen_art.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>
#include <editor/system/project_thumbnail.h>
#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <logging/logging.h>
#include <version/version.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>

namespace unravel
{
namespace widgets = start_page_widgets;

namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, see screen_card.
constexpr float START_PAGE_SHARE_X = 0.88f;
constexpr float START_PAGE_SHARE_Y = 0.86f;
constexpr float START_PAGE_MIN_WIDTH = 64.0f;
constexpr float START_PAGE_MAX_WIDTH = 112.0f;
constexpr float START_PAGE_MIN_HEIGHT = 38.0f;
constexpr float START_PAGE_MAX_HEIGHT = 70.0f;
constexpr float START_PAGE_SIDEBAR_WIDTH = 15.5f;
constexpr float START_PAGE_SIDEBAR_PADDING_X = 1.1f;
constexpr float START_PAGE_SIDEBAR_PADDING_Y = 1.5f;
constexpr float START_PAGE_MAIN_PADDING_X = 2.2f;
constexpr float START_PAGE_MAIN_PADDING_Y = 1.7f;
constexpr float START_PAGE_BRAND_LOGO_SIZE = 2.4f;
constexpr float START_PAGE_BRAND_GAP = 0.7f;
constexpr float START_PAGE_BRAND_TITLE_SCALE = 1.1f;
constexpr float START_PAGE_NAV_TOP_GAP = 2.0f;
constexpr float START_PAGE_NAV_HEIGHT = 2.4f;
constexpr float START_PAGE_NAV_GAP = 0.3f;
constexpr float START_PAGE_NAV_ICON_X = 0.8f;
constexpr float START_PAGE_NAV_LABEL_X = 2.6f;
constexpr float START_PAGE_NAV_BADGE_INSET = 0.6f;
constexpr float START_PAGE_NAV_BAR_WIDTH = 0.2f;
constexpr float START_PAGE_ROUNDING = 0.45f;
constexpr float START_PAGE_FEATURE_PADDING = 0.8f;
constexpr float START_PAGE_FEATURE_LINE_GAP = 0.25f;
constexpr float START_PAGE_FEATURE_MIN_GAP = 1.5f;
constexpr float START_PAGE_EYEBROW_SCALE = 0.8f;
constexpr float START_PAGE_IMAGE_ASPECT = 9.0f / 16.0f;
// How long the sidebar keeps a sample before the next one, and how long the next one fades in.
constexpr double START_PAGE_FEATURE_SECONDS = 8.0;
constexpr double START_PAGE_FEATURE_FADE_SECONDS = 0.5;
constexpr float START_PAGE_NAV_SELECTED_ALPHA = 0.16f;
constexpr float START_PAGE_NAV_IDLE_ALPHA = 0.72f;
constexpr ImU32 START_PAGE_SIDEBAR_COLOR = IM_COL32(0, 0, 0, 46);
constexpr ImU32 START_PAGE_DIVIDER_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 START_PAGE_NAV_HOVERED_COLOR = IM_COL32(255, 255, 255, 12);
constexpr ImU32 START_PAGE_BADGE_COLOR = IM_COL32(255, 255, 255, 20);
// The labels say how long ago a project was touched, so they age while the page stays open.
constexpr double START_PAGE_REFRESH_SECONDS = 20.0;
constexpr const char* START_PAGE_LOGO_KEY = "editor:/data/icons/unravel_logo.png";

auto format_project_time_ago(long long count, const char* unit) -> std::string
{
    return fmt::format("{} {}{} ago", count, unit, count == 1 ? "" : "s");
}

auto format_project_modified_label(const std::chrono::system_clock::time_point& modified) -> std::string
{
    using namespace std::chrono;
    const auto age = system_clock::now() - modified;
    if(age < minutes(1))
    {
        return "Just now";
    }
    if(age < hours(1))
    {
        return format_project_time_ago(duration_cast<minutes>(age).count(), "minute");
    }
    if(age < hours(24))
    {
        return format_project_time_ago(duration_cast<hours>(age).count(), "hour");
    }
    const auto days = duration_cast<hours>(age).count() / 24;
    if(days < 30)
    {
        return format_project_time_ago(days, "day");
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
auto read_project_modified_label(const fs::path& project_path) -> std::string
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
    return format_project_modified_label(modified);
}

auto to_lower_ascii(std::string text) -> std::string
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](char character)
                   {
                       return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                   });
    return text;
}

auto calc_start_page_size(const ImVec2& viewport_size) -> ImVec2
{
    const ImVec2 wanted(ImClamp(viewport_size.x * START_PAGE_SHARE_X, to_pixels(START_PAGE_MIN_WIDTH), to_pixels(START_PAGE_MAX_WIDTH)),
                        ImClamp(viewport_size.y * START_PAGE_SHARE_Y, to_pixels(START_PAGE_MIN_HEIGHT), to_pixels(START_PAGE_MAX_HEIGHT)));
    return screen_card::fit_to_viewport(wanted);
}

/// A region of the card with its own padding; the style is taken by Begin and popped right after.
auto begin_start_page_region(const char* id, const ImVec2& size, const ImVec2& padding) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings;
    const bool is_visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, flags);
    ImGui::PopStyleVar();
    return is_visible;
}

void draw_start_page_eyebrow(ImDrawList* draw_list, const ImVec2& pos, ImU32 color, const char* text)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(START_PAGE_EYEBROW_SCALE);
    draw_list->AddText(pos, color, text);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
}
} // namespace

void start_page::draw(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    refresh_projects_if_stale(pm);
    handle_shortcuts(ctx);
    // The page is a card, not a modal: a modal here would own the popup stack and break every
    // other modal opened while it shows (the engine version confirmation, for one).
    screen_card::card_layout layout{};
    layout.size = calc_start_page_size(ImGui::GetMainViewport()->WorkSize);
    layout.has_padding = false;
    if(screen_card::begin("START PAGE", layout))
    {
        const ImVec2 card_min = ImGui::GetWindowPos();
        const ImVec2 card_size = ImGui::GetWindowSize();
        const float sidebar_width = to_pixels(START_PAGE_SIDEBAR_WIDTH);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(card_min,
                                 ImVec2(card_min.x + sidebar_width, card_min.y + card_size.y),
                                 START_PAGE_SIDEBAR_COLOR,
                                 screen_card::get_rounding(),
                                 ImDrawFlags_RoundCornersLeft);
        draw_list->AddLine(ImVec2(card_min.x + sidebar_width, card_min.y),
                           ImVec2(card_min.x + sidebar_width, card_min.y + card_size.y),
                           START_PAGE_DIVIDER_COLOR);
        draw_sidebar(ctx, ImVec2(sidebar_width, card_size.y));
        ImGui::SameLine(0.0f, 0.0f);
        draw_main(ctx);
    }
    screen_card::end();
    last_drawn_frame_ = ImGui::GetFrameCount();
}

void start_page::release()
{
    images_.clear();
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
        entry.modified_label = entry.is_missing ? std::string("Folder not found") : read_project_modified_label(project_path);
        entry.thumbnail = entry.is_missing ? fs::path{} : project_thumbnail::find(project_path);
        projects_.emplace_back(std::move(entry));
    }
    // A thumbnail captured or replaced while the page was away shows up with the next read.
    images_.refresh();
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
    if(sort_ == sort_mode::name)
    {
        std::stable_sort(shown_projects.begin(),
                         shown_projects.end(),
                         [this](size_t lhs, size_t rhs)
                         {
                             return to_lower_ascii(projects_[lhs].name) < to_lower_ascii(projects_[rhs].name);
                         });
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

void start_page::browse_for_project(rtti::context& ctx)
{
    std::string picked_path;
    if(native::pick_folder_dialog(picked_path))
    {
        open_project(ctx, picked_path);
    }
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

void start_page::pick_thumbnail(project_manager& pm, const std::string& path)
{
    std::string picked_path;
    if(!native::open_file_dialog(picked_path, {"*.png"}, "PNG images", "Choose a thumbnail for the project", path))
    {
        return;
    }
    if(project_thumbnail::set_custom(fs::path(path), fs::path(picked_path)))
    {
        refresh_projects(pm);
    }
}

void start_page::handle_shortcuts(rtti::context& ctx)
{
    // Not while a field takes the keys or a popup (a confirmation) is up.
    const bool is_listening = !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if(!is_listening)
    {
        return;
    }
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_1))
    {
        show_view(view_state::projects);
    }
    else if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_2))
    {
        show_view(view_state::samples);
    }
    else if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_3))
    {
        show_view(view_state::learn);
    }
    else if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
    {
        show_view(view_state::create_project);
    }
    else if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    {
        browse_for_project(ctx);
    }
}

void start_page::draw_sidebar(rtti::context& ctx, const ImVec2& size)
{
    const ImVec2 padding(to_pixels(START_PAGE_SIDEBAR_PADDING_X), to_pixels(START_PAGE_SIDEBAR_PADDING_Y));
    if(begin_start_page_region("##sidebar", size, padding))
    {
        draw_brand();
        ImGui::Dummy(ImVec2(0.0f, to_pixels(START_PAGE_NAV_TOP_GAP)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, to_pixels(START_PAGE_NAV_GAP)));
        const bool is_projects = view_ == view_state::projects || view_ == view_state::create_project ||
                                 view_ == view_state::remove_project;
        if(draw_nav_item("##nav_projects", ICON_MDI_FOLDER_MULTIPLE_OUTLINE, "Projects", std::to_string(projects_.size()), "Your recent projects (Ctrl+1)", is_projects, false))
        {
            show_view(view_state::projects);
        }
        const std::string sample_count = std::to_string(sample_catalog::get_samples().size());
        if(draw_nav_item("##nav_samples", ICON_MDI_AUTO_FIX, "Samples", sample_count, "Sample projects to learn from (Ctrl+2)", view_ == view_state::samples, true))
        {
            show_view(view_state::samples);
        }
        if(draw_nav_item("##nav_learn", ICON_MDI_SCHOOL_OUTLINE, "Learn", {}, "References and source code (Ctrl+3)", view_ == view_state::learn, false))
        {
            show_view(view_state::learn);
        }
        ImGui::PopStyleVar();
        if(view_ != view_state::samples)
        {
            draw_featured_sample(ctx);
        }
    }
    ImGui::EndChild();
}

void start_page::draw_brand()
{
    const float logo_size = to_pixels(START_PAGE_BRAND_LOGO_SIZE);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const gfx::texture::ptr logo = images_.get(fs::resolve_protocol(START_PAGE_LOGO_KEY));
    screen_art::draw_image_cover(draw_list, logo, ImRect(pos, pos + ImVec2(logo_size, logo_size)), 0.0f);
    const float text_x = pos.x + logo_size + to_pixels(START_PAGE_BRAND_GAP);
    const float max_x = pos.x + ImGui::GetContentRegionAvail().x;
    // The name over the version, centered on the logo.
    const float title_height = ImFloor(ImGui::GetFontSize() * START_PAGE_BRAND_TITLE_SCALE);
    const float title_y = ImFloor(pos.y + (logo_size - title_height - ImGui::GetFontSize()) * 0.5f);
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(START_PAGE_BRAND_TITLE_SCALE);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, title_y), max_x, widgets::get_text_color(1.0f), "Unravel Engine");
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, title_y + title_height), max_x, widgets::get_muted_color(), version::get_full().c_str());
    ImGui::Dummy(ImVec2(0.0f, logo_size));
}

auto start_page::draw_nav_item(const char* id,
                               const char* icon,
                               const char* label,
                               const std::string& badge,
                               const char* tooltip,
                               bool is_selected,
                               bool is_highlighted) -> bool
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, to_pixels(START_PAGE_NAV_HEIGHT));
    const bool is_pressed = ImGui::InvisibleButton(id, size);
    const bool is_hovered = ImGui::IsItemHovered();
    ImGui::SetItemTooltipEx("%s", tooltip);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(pos, pos + size);
    const float rounding = to_pixels(START_PAGE_ROUNDING);
    if(is_selected)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, widgets::get_accent_color(START_PAGE_NAV_SELECTED_ALPHA), rounding);
        draw_list->AddRectFilled(rect.Min,
                                 ImVec2(rect.Min.x + to_pixels(START_PAGE_NAV_BAR_WIDTH), rect.Max.y),
                                 widgets::get_accent_color(1.0f),
                                 rounding,
                                 ImDrawFlags_RoundCornersLeft);
    }
    else if(is_hovered)
    {
        draw_list->AddRectFilled(rect.Min, rect.Max, START_PAGE_NAV_HOVERED_COLOR, rounding);
    }
    const float text_y = ImFloor(pos.y + (size.y - ImGui::GetFontSize()) * 0.5f);
    const ImU32 label_color = widgets::get_text_color(is_selected || is_hovered ? 1.0f : START_PAGE_NAV_IDLE_ALPHA);
    ImGui::PushFont(ImGui::Font::Regular);
    draw_list->AddText(ImVec2(pos.x + to_pixels(START_PAGE_NAV_ICON_X), text_y),
                       is_highlighted ? widgets::get_accent_color(1.0f) : label_color,
                       icon);
    ImGui::PopFont();
    ImGui::PushFont(ImGui::Font::SemiBold);
    draw_list->AddText(ImVec2(pos.x + to_pixels(START_PAGE_NAV_LABEL_X), text_y), label_color, label);
    ImGui::PopFont();
    if(!badge.empty())
    {
        const ImVec2 badge_size = widgets::calc_pill_size(badge.c_str());
        const ImVec2 badge_pos(rect.Max.x - to_pixels(START_PAGE_NAV_BADGE_INSET) - badge_size.x,
                               ImFloor(pos.y + (size.y - badge_size.y) * 0.5f));
        const ImU32 fill = is_highlighted ? widgets::get_accent_color(1.0f) : START_PAGE_BADGE_COLOR;
        widgets::draw_pill(draw_list, badge_pos, badge.c_str(), fill, is_highlighted ? IM_COL32_WHITE : widgets::get_muted_color());
    }
    return is_pressed;
}

auto start_page::get_sample_image(project_manager& pm, const sample_catalog::sample_info& sample) -> gfx::texture::ptr
{
    // The picture the editor ships, chosen for the sample; the one a copy on disk captured stands
    // in when the editor has none.
    const fs::path shipped = fs::resolve_protocol(sample.thumbnail);
    fs::error_code error;
    if(fs::exists(shipped, error))
    {
        return images_.get(shipped);
    }
    const fs::path installed = sample_catalog::find_installed(sample, pm.get_editor_settings().projects.recent_projects);
    return installed.empty() ? nullptr : images_.get(project_thumbnail::find(installed));
}

void start_page::draw_featured_sample(rtti::context& ctx)
{
    const auto& samples = sample_catalog::get_samples();
    if(samples.empty())
    {
        return;
    }
    const double time = ImGui::GetTime();
    const auto index = static_cast<size_t>(time / START_PAGE_FEATURE_SECONDS) % samples.size();
    const sample_catalog::sample_info& sample = samples[index];
    const float padding = to_pixels(START_PAGE_FEATURE_PADDING);
    const float width = ImGui::GetContentRegionAvail().x;
    const float image_height = ImFloor(width * START_PAGE_IMAGE_ASPECT);
    const float line_gap = to_pixels(START_PAGE_FEATURE_LINE_GAP);
    const float text_height = 3.0f * ImGui::GetFontSize() + 2.0f * line_gap;
    const float height = image_height + 2.0f * padding + text_height;
    // Kept at the foot of the sidebar, and left out when the window is too short for it.
    const float top = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - height;
    if(top < ImGui::GetCursorPosY() + to_pixels(START_PAGE_FEATURE_MIN_GAP))
    {
        return;
    }
    ImGui::SetCursorPosY(top);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool is_pressed = ImGui::InvisibleButton("##featured_sample", ImVec2(width, height));
    const bool is_hovered = ImGui::IsItemHovered();
    ImGui::SetItemTooltipEx("%s", "See all the samples");
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImRect rect(pos, pos + ImVec2(width, height));
    const float rounding = to_pixels(START_PAGE_ROUNDING);
    widgets::draw_card_frame(draw_list, rect, rounding, is_hovered, false);
    const ImRect image_rect(pos + ImVec2(1.0f, 1.0f), ImVec2(rect.Max.x - 1.0f, pos.y + image_height));
    const float fade = static_cast<float>(ImMin(1.0, std::fmod(time, START_PAGE_FEATURE_SECONDS) / START_PAGE_FEATURE_FADE_SECONDS));
    const gfx::texture::ptr image = get_sample_image(ctx.get_cached<project_manager>(), sample);
    if(image)
    {
        screen_art::draw_image_cover(draw_list, image, image_rect, rounding, ImDrawFlags_RoundCornersTop, IM_COL32(255, 255, 255, static_cast<int>(fade * 255.0f)));
    }
    else
    {
        screen_art::draw_placeholder(draw_list, image_rect, sample.title, rounding, ImDrawFlags_RoundCornersTop, sample.icon);
    }
    if(sample.is_new)
    {
        widgets::draw_pill(draw_list, image_rect.Min + ImVec2(padding, padding), "NEW", widgets::get_accent_color(1.0f), IM_COL32_WHITE);
    }
    float text_y = image_rect.Max.y + padding;
    const float text_x = pos.x + padding;
    const float max_x = rect.Max.x - padding;
    draw_start_page_eyebrow(draw_list, ImVec2(text_x, text_y), widgets::get_accent_color(1.0f), "FEATURED SAMPLE");
    text_y += ImGui::GetFontSize() + line_gap;
    ImGui::PushFont(ImGui::Font::Bold);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, text_y), max_x, widgets::get_text_color(1.0f), sample.title);
    ImGui::PopFont();
    text_y += ImGui::GetFontSize() + line_gap;
    const std::string link = fmt::format("Explore samples  {}", ICON_MDI_ARROW_RIGHT);
    ImGui::PushFont(ImGui::Font::Regular);
    widgets::draw_text_clipped(draw_list, ImVec2(text_x, text_y), max_x, is_hovered ? widgets::get_text_color(1.0f) : widgets::get_muted_color(), link.c_str());
    ImGui::PopFont();
    if(is_pressed)
    {
        show_view(view_state::samples);
    }
}

void start_page::draw_main(rtti::context& ctx)
{
    const ImVec2 padding(to_pixels(START_PAGE_MAIN_PADDING_X), to_pixels(START_PAGE_MAIN_PADDING_Y));
    if(begin_start_page_region("##main", ImVec2(0.0f, 0.0f), padding))
    {
        switch(view_)
        {
            case view_state::projects:
                draw_projects_view(ctx);
                break;
            case view_state::samples:
                draw_samples_view(ctx);
                break;
            case view_state::learn:
                draw_learn_view();
                break;
            case view_state::create_project:
                draw_create_project_view(ctx);
                break;
            case view_state::remove_project:
                draw_remove_project_view(ctx);
                break;
        }
    }
    ImGui::EndChild();
}
} // namespace unravel
