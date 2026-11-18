#include "audit_panel.h"
#include "../packages_panel/packages_widgets.h"
#include "../panel.h"
#include "../panel_toolbar.h"
#include "audit_panel_widgets.h"

#include <audit/audit_config.h>
#include <audit/audit_report_io.h>
#include <audit/audit_run.h>
#include <editor/audit/audit_manager.h>
#include <editor/deploy/deploy_preflight.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/system/project_manager.h>
#include <engine/ecs/scene.h>
#include <filedialog/filedialog.h>
#include <logging/logging.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>

namespace unravel
{
namespace audit_panel_run_detail
{

using namespace audit_widgets;

// Sizes in font units.
constexpr float AUDIT_RUN_CARD_WIDTH = 34.0f;
constexpr float AUDIT_RUN_SPINNER_RADIUS = 0.62f;
constexpr float AUDIT_RUN_BAR_HEIGHT = 0.4f;
constexpr float AUDIT_RUN_STAGE_ICON_COLUMN = 1.8f;
constexpr float AUDIT_RUN_STAGE_COUNT_COLUMN = 9.0f;
constexpr float AUDIT_RUN_STAGE_TIME_COLUMN = 5.0f;
constexpr float AUDIT_RUN_TOP_MARGIN = 2.0f;
constexpr float AUDIT_RUN_STRIP_GAP = 1.1f;

using stage_state = audit_manager::stage_progress::state;

/// What each stage is called in the compact strip, by audit::run_stage.
constexpr std::array<const char*, audit::RUN_STAGE_COUNT> AUDIT_RUN_STAGE_SHORT{
    "Project", "Scenes and Prefabs", "References", "Content", "Assets", "Scripts", "Rules"};

auto get_stage_count_text(audit::run_stage stage, const audit_manager::stage_progress& progress) -> std::string
{
    if(progress.status == stage_state::skipped)
    {
        return "Turned off";
    }
    if(progress.total == 0)
    {
        return {};
    }
    const char* unit = stage == audit::run_stage::content ? "profiles" : stage == audit::run_stage::documents ? "documents" : "assets";
    return fmt::format("{} of {} {}", progress.done, progress.total, unit);
}

/// The stage's state icon at the center of its icon column: a check, a spinner, a ring, or a dash for a skipped one.
void draw_stage_icon(const ImVec2& center, stage_state status)
{
    const float font_size = ImGui::GetFontSize();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 muted = imgui_style::get_muted_text_color_u32();
    switch(status)
    {
        case stage_state::done:
            ImGui::RenderIconCentered(draw_list, center, ICON_MDI_CHECK_CIRCLE, FIXED_COLOR);
            break;
        case stage_state::running:
            draw_spinner(center, font_size * 0.42f, ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()));
            break;
        case stage_state::skipped:
            ImGui::RenderIconCentered(draw_list, center, ICON_MDI_MINUS_CIRCLE_OUTLINE, muted);
            break;
        case stage_state::pending:
        default:
            ImGui::RenderIconCentered(draw_list, center, ICON_MDI_CIRCLE_OUTLINE, with_alpha(muted, 0.6f));
            break;
    }
}

/// One stage on its own row: its state, its title, its count and its time.
void draw_stage_row(audit::run_stage stage, const audit_manager::stage_progress& progress)
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::Dummy(size);
    const float text_y = ImFloor(min.y + (size.y - font_size) * 0.5f);
    draw_stage_icon(ImVec2(min.x + font_size * 0.6f, min.y + size.y * 0.5f), progress.status);
    const bool is_running = progress.status == stage_state::running;
    const bool is_quiet = progress.status == stage_state::pending || progress.status == stage_state::skipped;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 text_color = is_quiet ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text);
    ImFont* font = ImGui::GetFont(is_running ? ImGui::Font::SemiBold : ImGui::Font::Regular);
    const float right = min.x + size.x;
    const float count_left = right - to_pixels(AUDIT_RUN_STAGE_COUNT_COLUMN + AUDIT_RUN_STAGE_TIME_COLUMN);
    const ImVec4 title_clip(min.x, min.y, count_left - font_size * 0.5f, min.y + size.y);
    draw_list->AddText(font,
                       font_size,
                       ImVec2(min.x + to_pixels(AUDIT_RUN_STAGE_ICON_COLUMN), text_y),
                       text_color,
                       audit::get_run_stage_title(stage),
                       nullptr,
                       0.0f,
                       &title_clip);
    const auto count = get_stage_count_text(stage, progress);
    if(!count.empty())
    {
        draw_list->AddText(ImVec2(count_left, text_y), imgui_style::get_muted_text_color_u32(), count.c_str());
    }
    if(progress.status == stage_state::done || is_running)
    {
        const auto time = format_duration(progress.seconds);
        const float width = ImGui::CalcTextSize(time.c_str()).x;
        draw_list->AddText(ImVec2(right - width, text_y), imgui_style::get_muted_text_color_u32(), time.c_str());
    }
}

/// The stages in one line, wrapping: each a state icon and a short name.
void draw_stage_strip(const audit_manager::progress& progress)
{
    const float font_size = ImGui::GetFontSize();
    for(std::size_t index = 0; index < progress.stages.size(); ++index)
    {
        const auto& stage = progress.stages[index];
        const char* name = AUDIT_RUN_STAGE_SHORT[index];
        const float width = font_size * 1.4f + ImGui::CalcTextSize(name).x;
        if(index > 0)
        {
            ImGui::SameLine(0.0f, to_pixels(AUDIT_RUN_STRIP_GAP));
            if(ImGui::GetContentRegionAvail().x < width)
            {
                ImGui::NewLine();
            }
        }
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
        draw_stage_icon(ImVec2(min.x + font_size * 0.5f, min.y + ImGui::GetTextLineHeight() * 0.5f), stage.status);
        const bool is_quiet = stage.status == stage_state::pending || stage.status == stage_state::skipped;
        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + font_size * 1.4f, min.y),
                                            is_quiet ? imgui_style::get_muted_text_color_u32() : ImGui::GetColorU32(ImGuiCol_Text),
                                            name);
    }
}

/// A slim bar of the run's share done, in the accent.
void draw_run_bar(float fraction)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = to_pixels(AUDIT_RUN_BAR_HEIGHT);
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float rounding = height * 0.5f;
    draw_list->AddRectFilled(min, ImVec2(min.x + width, min.y + height), ImGui::GetColorU32(ImGuiCol_Text, 0.08f), rounding);
    const float filled = ImMax(height, width * std::clamp(fraction, 0.0f, 1.0f));
    draw_list->AddRectFilled(min,
                             ImVec2(min.x + filled, min.y + height),
                             ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()),
                             rounding);
}

/// Starts a column `width` wide in the middle of the window, a little below the cursor. Pair with end_centered_column.
auto begin_centered_column(const char* id, float width) -> bool
{
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f),
                               ImGui::GetCursorPosY() + to_pixels(AUDIT_RUN_TOP_MARGIN)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool is_open = ImGui::BeginChild(id, ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return is_open;
}

void end_centered_column()
{
    ImGui::EndChild();
}

/// Draws `text` centered across the window's content.
void draw_centered_text(const char* text)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x) * 0.5f));
    ImGui::TextUnformatted(text);
}

/// The line under the run's title: the stage and what it reads now.
auto get_run_status_text(const audit_manager::progress& progress) -> std::string
{
    const std::string title = audit::get_run_stage_title(progress.current);
    if(progress.item.empty())
    {
        return title + "...";
    }
    return fmt::format("{}: {}", title, progress.item);
}

} // namespace audit_panel_run_detail

audit_panel::audit_panel(imgui_panels* parent)
    : parent_(parent)
{
}

void audit_panel::show(bool s)
{
    show_request_ = s;
}

void audit_panel::show_rule(const std::string& rule_id)
{
    show_issues_of(rule_id, -1);
    show(true);
}

void audit_panel::show_profile(const std::string& profile)
{
    show_issues_of({}, -1);
    shows_severity_[static_cast<std::size_t>(audit::severity::info)] = false;
    profile_filter_ = profile;
    show(true);
}

void audit_panel::select_issue(const std::string& issue_id)
{
    view_ = view::issues;
    selected_issue_ = issue_id;
    scroll_to_selected_ = true;
    show(true);
}

void audit_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        show_request_ = false;
        show_ = true;
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * ImVec2(0.6f, 0.7f), ImGuiCond_Once);
        ImGui::SetWindowFocus(name);
    }
    if(!show_)
    {
        return;
    }
    if(ImGui::Begin(name, &show_, ImGuiWindowFlags_NoScrollbar))
    {
        draw_ui(ctx);
    }
    ImGui::End();
}

void audit_panel::set_message(std::string text, bool is_error)
{
    message_ = std::move(text);
    message_is_error_ = is_error;
}

void audit_panel::draw_ui(rtti::context& ctx)
{
    if(!ctx.get_cached<project_manager>().has_open_project())
    {
        packages_widgets::draw_centered_hint("No project is open", "Open a project to audit it.");
        return;
    }
    auto& manager = ctx.get_cached<audit_manager>();
    draw_toolbar(ctx);
    draw_messages(ctx);
    const auto* report = manager.get_report();
    if(manager.is_running())
    {
        draw_run_progress(ctx, report != nullptr);
    }
    if(report == nullptr)
    {
        if(!manager.is_running())
        {
            draw_empty_state(ctx);
        }
        return;
    }
    switch(view_)
    {
        case view::summary:
            draw_summary(ctx, *report);
            break;
        case view::issues:
            draw_issues(ctx, *report);
            break;
        case view::insights:
            draw_insights(*report);
            break;
        case view::rules:
            draw_rules(ctx);
            break;
    }
}

void audit_panel::draw_toolbar(rtti::context& ctx)
{
    const auto* report = ctx.get_cached<audit_manager>().get_report();
    if(panel_toolbar::begin_strip("##audit_toolbar"))
    {
        draw_run_controls(ctx);
        panel_toolbar::separator();
        draw_view_switch(report);
        panel_toolbar::align_right();
        draw_export_menu(report);
    }
    panel_toolbar::end_strip();
}

void audit_panel::draw_run_controls(rtti::context& ctx)
{
    auto& manager = ctx.get_cached<audit_manager>();
    // While a run goes, its progress card has the Stop button.
    ImGui::BeginDisabled(manager.is_running());
    if(panel_toolbar::button("##audit_run",
                             ICON_MDI_PLAY " Run Audit",
                             manager.is_running() ? "The audit is running."
                                                  : "Check the project's assets, settings, scenes and scripts against every settings profile.",
                             !manager.is_running()))
    {
        std::string error;
        const audit_manager::run_request request{scan_documents_, analyze_code_};
        const bool is_started = manager.start(ctx, request, error);
        set_message(error, !is_started);
    }
    if(panel_toolbar::begin_dropdown("##audit_run_options", nullptr, "What a run checks besides assets and settings."))
    {
        ImGui::SeparatorText("Run");
        ImGui::Checkbox("Load Scenes and Prefabs", &scan_documents_);
        ImGui::SetItemTooltipEx("%s",
                                "Load every scene, prefab, material and controller to find missing references, joints "
                                "that cannot be made and components a profile turns off.");
        ImGui::Checkbox("Analyze Scripts", &analyze_code_);
        ImGui::SetItemTooltipEx("%s",
                                "Read the compiled scripts to find what the update methods do every frame: searches, "
                                "allocations, instantiation, LINQ, reflection, blocking waits and routine logging.");
        panel_toolbar::end_dropdown();
    }
    ImGui::EndDisabled();
}

void audit_panel::draw_view_switch(const audit::report* report)
{
    std::size_t open_issues = 0;
    if(report != nullptr)
    {
        for(const auto& item : report->issues)
        {
            open_issues += item.is_ignored ? 0 : 1;
        }
    }
    const std::string issues_text = report != nullptr ? fmt::format(ICON_MDI_FORMAT_LIST_CHECKS " Issues  {}", open_issues)
                                                      : std::string(ICON_MDI_FORMAT_LIST_CHECKS " Issues");
    struct view_button
    {
        view value;
        const char* id;
        const char* text;
        const char* tooltip;
    };
    const std::array<view_button, 4> views{{
        {view::summary, "##audit_summary", ICON_MDI_VIEW_DASHBOARD_OUTLINE " Summary", "The report at a glance: counts, top issues and what the run could not check."},
        {view::issues, "##audit_issues", issues_text.c_str(), "Every issue, grouped by rule, with what to do about each."},
        {view::insights, "##audit_insights", ICON_MDI_TABLE " Insights", "Tables of the project's assets, builds and scripts."},
        {view::rules, "##audit_rules", ICON_MDI_TUNE " Rules", "The rules, which of them run, and their thresholds."},
    }};
    panel_toolbar::begin_segment();
    for(const auto& button : views)
    {
        if(panel_toolbar::toggle(button.id, button.text, view_ == button.value, button.tooltip))
        {
            view_ = button.value;
        }
    }
    panel_toolbar::end_segment();
}

void audit_panel::draw_export_menu(const audit::report* report)
{
    ImGui::BeginDisabled(report == nullptr);
    if(panel_toolbar::begin_dropdown("##audit_export", ICON_MDI_EXPORT " Export", "Save the report to a file."))
    {
        if(ImGui::MenuItem("Report as JSON..."))
        {
            export_report(*report, false);
        }
        ImGui::SetItemTooltipEx("%s", "The whole report, as --audit writes it: issues, insights and notes.");
        if(ImGui::MenuItem("Issues as CSV..."))
        {
            export_report(*report, true);
        }
        ImGui::SetItemTooltipEx("%s", "The issues as comma-separated values, for a spreadsheet.");
        panel_toolbar::end_dropdown();
    }
    ImGui::EndDisabled();
}

void audit_panel::draw_messages(rtti::context& ctx)
{
    using namespace audit_widgets;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto& run_error = manager.get_run_error();
    const auto& shown = !message_.empty() ? message_ : run_error;
    const bool is_error = !message_.empty() ? message_is_error_ : !run_error.empty();
    const auto draw_line = [](const char* icon, ImU32 color, const std::string& text, const char* close_id) -> bool
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                                  ImVec2(min.x + ImGui::GetFontSize() * 0.6f, min.y + ImGui::GetFrameHeight() * 0.5f),
                                  icon,
                                  color);
        ImGui::SetCursorScreenPos(ImVec2(min.x + ImGui::GetFontSize() * 1.5f, min.y));
        ImGui::AlignTextToFramePadding();
        const float close_width = close_id != nullptr ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImMax(1.0f, ImGui::GetContentRegionAvail().x - close_width));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        if(close_id == nullptr)
        {
            return false;
        }
        ImGui::SameLine(right_x - ImGui::GetFrameHeight());
        return draw_icon_button(close_id, ICON_MDI_CLOSE, "Dismiss the message.");
    };
    if(!shown.empty())
    {
        const char* icon = is_error ? ICON_MDI_ALERT_CIRCLE_OUTLINE : ICON_MDI_CHECK_CIRCLE_OUTLINE;
        const ImU32 color = is_error ? get_severity_style(audit::severity::error).color : FIXED_COLOR;
        if(draw_line(icon, color, shown, !message_.empty() ? "##audit_dismiss" : nullptr))
        {
            message_.clear();
        }
    }
    if(!manager.get_config_error().empty())
    {
        draw_line(ICON_MDI_ALERT_OUTLINE, get_severity_style(audit::severity::warning).color, manager.get_config_error(), nullptr);
    }
}

void audit_panel::draw_empty_state(rtti::context& ctx)
{
    using namespace audit_widgets;
    using namespace audit_panel_run_detail;
    const float font_size = ImGui::GetFontSize();
    const float width = ImMin(ImGui::GetContentRegionAvail().x, to_pixels(AUDIT_RUN_CARD_WIDTH));
    // Every begin pairs with its end, whatever it returns.
    const bool is_column_visible = begin_centered_column("##audit_empty", width);
    if(packages_widgets::begin_card("##audit_empty_card") && is_column_visible)
    {
        const float inner = ImGui::GetContentRegionAvail().x;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::PushWindowFontScale(2.4f);
        ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                                  ImVec2(origin.x + inner * 0.5f, origin.y + font_size * 1.4f),
                                  ICON_MDI_SHIELD_CHECK_OUTLINE,
                                  ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()));
        ImGui::PopWindowFontScale();
        ImGui::Dummy(ImVec2(inner, font_size * 3.0f));
        ImGui::PushFont(ImGui::GetFont(ImGui::Font::Bold), ImGui::GetCurrentContext()->FontSizeBase * 1.3f);
        draw_centered_text("Audit the project");
        ImGui::PopFont();
        ImGui::Spacing();
        packages_widgets::draw_muted_wrapped(
            "Run Audit checks the project against every settings profile: assets too large or uncompressed for a "
            "platform, settings costlier than its starting values, missing references in scenes and prefabs, what "
            "each deploy ships, and the scripts' update methods. Nothing changes until you apply a fix.");
        ImGui::Spacing();
        ImGui::Spacing();
        const char* label = ICON_MDI_PLAY " Run Audit";
        const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (inner - button_width) * 0.5f));
        auto& manager = ctx.get_cached<audit_manager>();
        if(draw_primary_button(label, "Check the project's assets, settings, scenes and scripts against every settings profile."))
        {
            std::string error;
            const bool is_started = manager.start(ctx, {scan_documents_, analyze_code_}, error);
            set_message(error, !is_started);
        }
    }
    packages_widgets::end_card();
    end_centered_column();
}

void audit_panel::draw_run_progress(rtti::context& ctx, bool is_compact)
{
    using namespace audit_widgets;
    using namespace audit_panel_run_detail;
    auto& manager = ctx.get_cached<audit_manager>();
    const auto progress = manager.get_progress();
    const float font_size = ImGui::GetFontSize();
    // Over an earlier report the card spans the window; on its own it is a column in the middle.
    // Every begin pairs with its end, whatever it returns.
    bool is_column_visible = true;
    if(!is_compact)
    {
        is_column_visible = begin_centered_column("##audit_run_column", ImMin(ImGui::GetContentRegionAvail().x, to_pixels(AUDIT_RUN_CARD_WIDTH)));
    }
    if(packages_widgets::begin_card("##audit_run_card") && is_column_visible)
    {
        // The title line: a spinner, what runs, the time so far and the stop button.
        const ImVec2 min = ImGui::GetCursorScreenPos();
        draw_spinner(ImVec2(min.x + font_size * 0.65f, min.y + ImGui::GetFrameHeight() * 0.5f),
                     to_pixels(AUDIT_RUN_SPINNER_RADIUS),
                     ImGui::ColorConvertFloat4ToU32(imgui_style::get_accent_color()));
        ImGui::SetCursorScreenPos(ImVec2(min.x + font_size * 1.8f, min.y));
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::TextUnformatted(fmt::format("Auditing {}", ctx.get_cached<project_manager>().get_name()).c_str());
        ImGui::PopFont();
        const std::string elapsed = format_duration(progress.seconds);
        const char* stop_label = ICON_MDI_STOP " Stop";
        const float stop_width = ImGui::CalcTextSize(stop_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float elapsed_width = ImGui::CalcTextSize(elapsed.c_str()).x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX(),
                              ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - stop_width - elapsed_width -
                                  ImGui::GetStyle().ItemSpacing.x));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", elapsed.c_str());
        ImGui::SameLine();
        if(ImGui::Button(stop_label))
        {
            manager.cancel();
        }
        ImGui::SetItemTooltipEx("%s", "Stop the audit. The last report stays.");

        // What runs now, then the whole run's share.
        const auto status = get_run_status_text(progress);
        const std::string percent = fmt::format("{:.0f}%", progress.done * 100.0f);
        const float percent_width = ImGui::CalcTextSize(percent.c_str()).x;
        const ImVec2 status_min = ImGui::GetCursorScreenPos();
        const float status_right = status_min.x + ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight()));
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(),
                                  status_min,
                                  ImVec2(status_right - percent_width - font_size, status_min.y + ImGui::GetTextLineHeight()),
                                  status_right - percent_width - font_size,
                                  status.c_str(),
                                  nullptr,
                                  nullptr);
        ImGui::PopStyleColor();
        ImGui::GetWindowDrawList()->AddText(ImVec2(status_right - percent_width, status_min.y),
                                            ImGui::GetColorU32(ImGuiCol_Text),
                                            percent.c_str());
        draw_run_bar(progress.done);
        ImGui::Spacing();
        if(is_compact)
        {
            draw_stage_strip(progress);
        }
        else
        {
            ImGui::Spacing();
            for(std::size_t index = 0; index < progress.stages.size(); ++index)
            {
                draw_stage_row(static_cast<audit::run_stage>(index), progress.stages[index]);
            }
            ImGui::Spacing();
            packages_widgets::draw_muted_wrapped("Scenes and prefabs load a few each frame, so the editor stays usable "
                                                 "while the audit runs. The rules then run on a worker thread.");
        }
    }
    packages_widgets::end_card();
    if(is_compact)
    {
        ImGui::Spacing();
    }
    else
    {
        end_centered_column();
    }
}

void audit_panel::show_issues_of(const std::string& rule_id, int severity_only)
{
    view_ = view::issues;
    rule_filter_ = rule_id;
    area_filter_ = 0;
    profile_filter_.clear();
    filter_.Clear();
    for(std::size_t index = 0; index < 3; ++index)
    {
        shows_severity_[index] = severity_only < 0 || static_cast<int>(index) == severity_only;
    }
    collapsed_rules_.clear();
}

void audit_panel::open_script_line(const std::string& key, std::uint32_t line)
{
    const fs::path file = key.rfind("app:", 0) == 0 ? fs::resolve_protocol(key) : fs::path(key);
    editor_actions::open_workspace_on_file(file, static_cast<int>(line));
}

void audit_panel::open_subject(rtti::context& ctx, const audit::issue& item)
{
    const auto& where = item.where;
    message_.clear();
    switch(where.kind)
    {
        case audit::subject_kind::asset:
        {
            std::string error;
            if(!editor_actions::reveal_in_content_browser(ctx, where.key, &error))
            {
                set_message(error, true);
                return;
            }
            editor_actions::select_asset(ctx, where.key);
            return;
        }
        case audit::subject_kind::entity:
        {
            auto* active = ctx.get_cached<editing_manager>().get_active_scene(ctx);
            if(active != nullptr && active->source.id() == where.key && editor_actions::set_selection(ctx, {where.detail}))
            {
                return;
            }
            editor_actions::reveal_in_content_browser(ctx, where.key);
            editor_actions::select_asset(ctx, where.key);
            set_message("The entity is in another document: open it to select the entity.", false);
            return;
        }
        case audit::subject_kind::script:
            open_script_line(where.key, where.line);
            return;
        case audit::subject_kind::setting:
            editor_actions::open_project_settings(ctx, audit_widgets::get_settings_category(where.key));
            if(!item.profiles.empty())
            {
                parent_->get_project_settings_panel().show_profile(item.profiles.front());
            }
            return;
        case audit::subject_kind::profile:
            editor_actions::open_deploy_window(ctx);
            return;
        case audit::subject_kind::project:
        default:
            return;
    }
}

void audit_panel::export_report(const audit::report& report, bool as_csv)
{
    std::string picked;
    const auto extension = as_csv ? std::string("csv") : std::string("json");
    const auto start = (deployment::get_project_root() / (report.project + "-audit." + extension)).string();
    if(!native::save_file_dialog(picked, {"*." + extension}, as_csv ? "Comma-separated values" : "Audit report", "Export the audit", start))
    {
        return;
    }
    fs::path file = picked;
    if(file.extension().empty())
    {
        file.replace_extension("." + extension);
    }
    const bool is_written = audit::write_text_file(file, as_csv ? audit::write_issues_csv(report) : audit::write_report(report));
    set_message(is_written ? fmt::format("The report was saved to {}.", file.generic_string())
                           : fmt::format("The report could not be saved to {}.", file.generic_string()),
                !is_written);
}

} // namespace unravel
