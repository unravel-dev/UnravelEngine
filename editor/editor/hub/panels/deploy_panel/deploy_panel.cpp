#include "deploy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspector_container_widgets.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <string_view>

namespace unravel
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size, like the start page and the About window.
constexpr float DEPLOY_WIDTH = 42.0f;
constexpr float DEPLOY_ROUNDING = 0.75f;
constexpr float DEPLOY_TITLE_SCALE = 1.3f;
constexpr float DEPLOY_TITLE_ICON_GAP = 0.45f;
constexpr float DEPLOY_SECTION_GAP = 1.0f;
constexpr float DEPLOY_LINE_GAP = 0.35f;
constexpr float DEPLOY_BUTTON_HEIGHT = 2.2f;
constexpr float DEPLOY_BUTTON_PADDING_X = 1.0f;
constexpr float DEPLOY_BUTTON_ROUNDING = 0.4f;
constexpr float DEPLOY_CLOSE_INSET = 0.6f;
constexpr float DEPLOY_MUTED_ALPHA = 0.55f;
constexpr float DEPLOY_ACCENT_ACTIVE_SHADE = 0.85f;
constexpr ImU32 DEPLOY_WARNING_COLOR = IM_COL32(255, 190, 60, 255);
// The theme's fields are darker than a window and all but vanish on the lighter popup, so they
// are washed light here, as on the start page.
constexpr ImU32 DEPLOY_FIELD_COLOR = IM_COL32(255, 255, 255, 14);
constexpr ImU32 DEPLOY_FIELD_HOVERED_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 DEPLOY_FIELD_ACTIVE_COLOR = IM_COL32(255, 255, 255, 30);
// Jobs are named "Deploying X"; the steps show X.
constexpr std::string_view DEPLOY_JOB_PREFIX = "Deploying ";

auto get_muted_text_color() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, DEPLOY_MUTED_ALPHA);
}

void draw_muted_text(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, get_muted_text_color());
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void draw_gap(float font_units)
{
    ImGui::Dummy(ImVec2(0.0f, to_pixels(font_units)));
}

/// An icon and a line of text, the icon centered on the line.
void draw_icon_line(const char* icon, ImU32 icon_color, const char* text, ImU32 text_color)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float icon_width = ImGui::GetFontSize();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + ImGui::GetTextLineHeight() * 0.5f),
                              icon,
                              icon_color);
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width + to_pixels(DEPLOY_TITLE_ICON_GAP), min.y));
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    // Long paths wrap under themselves, not under the icon.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void draw_header()
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::PushWindowFontScale(DEPLOY_TITLE_SCALE);
    draw_icon_line(ICON_MDI_PACKAGE_VARIANT,
                   ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                   "Deploy Project",
                   ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    draw_muted_text("Builds the game for this platform into a folder.");
}

void draw_section_title(const char* title)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    draw_gap(DEPLOY_LINE_GAP);
}

/// The close button in the top right corner of the window.
auto draw_close_button() -> bool
{
    const float inset = to_pixels(DEPLOY_CLOSE_INSET);
    const float side = ImGui::GetFrameHeight();
    const ImVec2 window_min = ImGui::GetWindowPos();
    const ImVec2 min(window_min.x + ImGui::GetWindowWidth() - inset - side, window_min.y + inset);
    return container_widgets::draw_icon_button({"##deploy_close", ICON_MDI_CLOSE, "Close"}, ImRect(min, min + ImVec2(side, side)));
}

auto calc_button_width(const char* label) -> float
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2.0f * to_pixels(DEPLOY_BUTTON_PADDING_X);
}

/// A button of the window: the primary one takes the accent color.
auto draw_button(const char* label, bool is_primary, bool is_enabled) -> bool
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, to_pixels(DEPLOY_BUTTON_ROUNDING));
    if(is_primary)
    {
        const ImVec4 accent = imgui_style::get_accent_color();
        const ImVec4 active(accent.x * DEPLOY_ACCENT_ACTIVE_SHADE,
                            accent.y * DEPLOY_ACCENT_ACTIVE_SHADE,
                            accent.z * DEPLOY_ACCENT_ACTIVE_SHADE,
                            1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    }
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label, ImVec2(calc_button_width(label), to_pixels(DEPLOY_BUTTON_HEIGHT)));
    ImGui::EndDisabled();
    if(is_primary)
    {
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
    return is_pressed;
}

auto get_step_name(std::string_view job_name) -> std::string
{
    if(job_name.starts_with(DEPLOY_JOB_PREFIX))
    {
        job_name.remove_prefix(DEPLOY_JOB_PREFIX.size());
    }
    while(job_name.ends_with('.'))
    {
        job_name.remove_suffix(1);
    }
    return std::string(job_name);
}
} // namespace

deploy_panel::deploy_panel(imgui_panels* parent) : parent_(parent)
{
}

void deploy_panel::show(bool s)
{
    show_request_ = s;

    if(!s)
    {
        deploy_jobs_.clear();
    }
}

void deploy_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        ImGui::OpenPopup(name);
        show_request_ = false;
    }

    const float width = to_pixels(DEPLOY_WIDTH);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, screen_card::get_padding());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, to_pixels(DEPLOY_ROUNDING));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    const bool is_open = ImGui::BeginPopupModal(name, nullptr, flags);
    // Only the window itself: tooltips and pickers opened from it keep the theme's padding.
    ImGui::PopStyleVar(2);
    if(!is_open)
    {
        return;
    }

    draw_ui(ctx);

    const bool is_escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive();
    if(draw_close_button() || is_escape_pressed)
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

auto deploy_panel::get_progress() const -> float
{
    if(deploy_jobs_.empty())
    {
        return 1.0f;
    }

    size_t ready = 0;
    for(const auto& kvp : deploy_jobs_)
    {
        if(kvp.second.is_ready())
        {
            ready++;
        }
    }

    return float(ready) / float(deploy_jobs_.size());
}

auto deploy_panel::is_deploying() const -> bool
{
    float progress = get_progress();
    bool is_in_progress = progress < 0.99f;
    return is_in_progress;
}

void deploy_panel::deploy_and_run(rtti::context& ctx, const deploy_settings& params)
{
    bool is_in_progress = is_deploying();
    if(!is_in_progress && editor_actions::can_deploy_project(ctx, params))
    {
        deploy_jobs_ = editor_actions::deploy_project(ctx, params);
    }
    else
    {
        show(true);
    }
}

void deploy_panel::draw_ui(rtti::context& ctx)
{
    draw_header();
    draw_gap(DEPLOY_SECTION_GAP);
    draw_settings(ctx);
    draw_gap(DEPLOY_SECTION_GAP);
    ImGui::Separator();
    draw_gap(DEPLOY_LINE_GAP);
    draw_status(ctx);
    draw_gap(DEPLOY_SECTION_GAP);
    draw_actions(ctx);
}

void deploy_panel::draw_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();

    // What is being deployed does not change under a running deploy.
    ImGui::BeginDisabled(is_deploying());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, DEPLOY_FIELD_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, DEPLOY_FIELD_HOVERED_COLOR);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, DEPLOY_FIELD_ACTIVE_COLOR);
    draw_section_title("Product");
    if(inspect(ctx, settings.app).edit_finished)
    {
        pm.save_project_settings(ctx);
    }
    if(inspect(ctx, settings.standalone).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    draw_gap(DEPLOY_SECTION_GAP);
    draw_section_title("Output");
    if(inspect(ctx, pm.get_deploy_settings()).edit_finished)
    {
        pm.save_deploy_settings();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
}

//-----------------------------------------------------------------------------
/// <summary>
/// The steps and their progress while a deploy runs; otherwise what keeps the project from
/// deploying, the steps of the last deploy, or that it is ready.
/// </summary>
//-----------------------------------------------------------------------------
void deploy_panel::draw_status(rtti::context& ctx)
{
    const auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const bool is_in_progress = is_deploying();
    if(!is_in_progress)
    {
        const auto problems = editor_actions::get_deploy_problems(ctx, params);
        if(!problems.empty())
        {
            for(const auto& problem : problems)
            {
                draw_icon_line(ICON_MDI_ALERT_CIRCLE_OUTLINE, DEPLOY_WARNING_COLOR, problem.c_str(), ImGui::GetColorU32(ImGuiCol_Text));
            }
            return;
        }
    }

    const std::string location = params.deploy_location.generic_string();
    if(deploy_jobs_.empty())
    {
        draw_icon_line(ICON_MDI_CHECK_CIRCLE_OUTLINE,
                       ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                       fmt::format("Ready to deploy to {}", location).c_str(),
                       get_muted_text_color());
        return;
    }

    const auto step_count = deploy_jobs_.size();
    const auto done_count = static_cast<std::size_t>(get_progress() * static_cast<float>(step_count) + 0.5f);
    const std::string summary = is_in_progress ? fmt::format("Deploying to {}", location)
                                               : fmt::format("Deployed to {}", location);
    ImGui::PushStyleColor(ImGuiCol_Text, get_muted_text_color());
    ImGui::TextWrapped("%s", summary.c_str());
    ImGui::PopStyleColor();
    draw_gap(DEPLOY_LINE_GAP);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, imgui_style::get_accent_color());
    ImGui::ProgressBar(get_progress(), ImVec2(-FLT_MIN, 0.0f), fmt::format("{} of {} steps", done_count, step_count).c_str());
    ImGui::PopStyleColor();
    draw_gap(DEPLOY_LINE_GAP);

    const ImU32 done_color = ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color());
    for(const auto& [name, job] : deploy_jobs_)
    {
        const bool is_done = job.is_ready();
        draw_icon_line(is_done ? ICON_MDI_CHECK_CIRCLE : ICON_MDI_PROGRESS_CLOCK,
                       is_done ? done_color : get_muted_text_color(),
                       get_step_name(name).c_str(),
                       is_done ? ImGui::GetColorU32(ImGuiCol_Text) : get_muted_text_color());
    }
}

/// Open Folder and Deploy, at the right end.
void deploy_panel::draw_actions(rtti::context& ctx)
{
    auto& params = ctx.get_cached<project_manager>().get_deploy_settings();
    const bool can_deploy = !is_deploying() && editor_actions::can_deploy_project(ctx, params);
    fs::error_code ec;
    const bool has_folder = fs::is_directory(params.deploy_location, ec);

    const char* open_label = ICON_MDI_FOLDER_OPEN_OUTLINE " Open Folder";
    const char* deploy_label = params.deploy_and_run ? ICON_MDI_PLAY " Deploy and Run" : ICON_MDI_PACKAGE_VARIANT " Deploy";
    const float buttons_width =
        calc_button_width(open_label) + ImGui::GetStyle().ItemSpacing.x + calc_button_width(deploy_label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - buttons_width));

    if(draw_button(open_label, false, has_folder))
    {
        ImGui::OpenInShell(params.deploy_location.string().c_str());
    }
    ImGui::SetItemTooltipEx("%s", "Show the deploy folder in the file browser");
    ImGui::SameLine();
    if(draw_button(deploy_label, true, can_deploy))
    {
        deploy_jobs_ = editor_actions::deploy_project(ctx, params);
    }
    if(!can_deploy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", is_deploying() ? "A deploy is running." : "See what is missing above.");
    }
}

} // namespace unravel
