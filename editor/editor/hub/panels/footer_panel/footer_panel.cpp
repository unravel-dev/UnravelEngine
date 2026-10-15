#include "footer_panel.h"
#include "../panels_defs.h"

#include <engine/threading/threader.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/imgui/integration/imgui_style.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <filesystem/filesystem.h>
#include <logging/logging.h>

#include <algorithm>
#include <vector>

namespace unravel
{
// A unity build merges the anonymous namespaces of the editor's sources; the card's own namespace keeps
// its helper names from clashing.
namespace job_status
{
namespace
{
constexpr uint64_t JOB_STATUS_TOAST_ID = 99;
/// The card is pushed again every busy frame; this only bridges a frame without jobs.
constexpr int JOB_CARD_DISMISS_MILLISECONDS = 500;
constexpr int JOB_SUMMARY_DISMISS_MILLISECONDS = 2500;
/// The pool idle this long ends the batch; jobs that follow sooner join it.
constexpr auto JOB_BATCH_IDLE_TIME = std::chrono::milliseconds(850);
constexpr size_t JOB_CARD_MAX_KINDS = 4;
constexpr const char* JOB_UNNAMED_KIND = "Background work";
constexpr int SECONDS_PER_MINUTE = 60;
// The card, in units of the font size.
constexpr float JOB_CARD_WIDTH = 18.0f;
constexpr float JOB_ROW_GAP = 0.45f;
constexpr float JOB_BAR_GAP = 0.2f;
constexpr float JOB_BAR_HEIGHT = 0.2f;
constexpr float JOB_SPINNER_THICKNESS = 0.16f;
constexpr float JOB_SPINNER_SPEED = 6.0f;
constexpr ImU32 JOB_BAR_TRACK_COLOR = IM_COL32(255, 255, 255, 24);

/// One row of the card: a kind of job, how far the batch has come with it, and a file one of its
/// jobs is working on.
struct job_row
{
    std::string name;
    size_t done{};
    size_t total{};
    std::string detail;
};

/// What the card shows, taken when it is pushed.
struct job_card
{
    float elapsed_seconds{};
    size_t pending{};
    std::vector<job_row> rows;
    size_t hidden_kind_count{};
};

auto format_duration(float seconds) -> std::string
{
    if(seconds < float(SECONDS_PER_MINUTE))
    {
        return fmt::format("{:.1f} s", seconds);
    }
    const int whole_seconds = int(seconds);
    return fmt::format("{}:{:02}", whole_seconds / SECONDS_PER_MINUTE, whole_seconds % SECONDS_PER_MINUTE);
}

auto format_job_count(size_t count) -> std::string
{
    return fmt::format("{} {}", count, count == 1 ? "job" : "jobs");
}

/// The file a running job of @p kind works on, by its name alone; empty when none of its jobs runs.
auto find_running_detail(const std::vector<tpp::running_job>& running, const std::string& kind) -> std::string
{
    const auto job = std::find_if(running.begin(),
                                  running.end(),
                                  [&kind](const tpp::running_job& entry)
                                  {
                                      return entry.name == kind && !entry.detail.empty();
                                  });
    if(job == running.end())
    {
        return {};
    }
    const std::string name = fs::path(job->detail).filename().string();
    return name.empty() ? job->detail : name;
}

/// A line as wide as the card: the left text ellipsized to leave room for the right one.
void draw_card_line(const std::string& left, ImU32 left_color, const std::string& right, ImU32 right_color, float width)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float line_height = ImGui::GetTextLineHeight();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float right_width = right.empty() ? 0.0f : ImGui::CalcTextSize(right.c_str()).x;
    const float gap = right.empty() ? 0.0f : ImGui::GetFontSize() * JOB_ROW_GAP;
    const float left_max_x = pos.x + width - right_width - gap;
    ImGui::PushStyleColor(ImGuiCol_Text, left_color);
    ImGui::RenderTextEllipsis(draw_list,
                              pos,
                              ImVec2(left_max_x, pos.y + line_height),
                              left_max_x,
                              left.c_str(),
                              nullptr,
                              nullptr);
    ImGui::PopStyleColor();
    if(!right.empty())
    {
        draw_list->AddText(ImVec2(pos.x + width - right_width, pos.y), right_color, right.c_str());
    }
    ImGui::Dummy(ImVec2(width, line_height));
}

/// A thin rounded bar, filled in the accent for the share done.
void draw_progress_bar(float fraction, float width)
{
    const float font_size = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos() + ImVec2(0.0f, font_size * JOB_BAR_GAP);
    const float height = ImMax(2.0f, ImFloor(font_size * JOB_BAR_HEIGHT));
    const float rounding = height * 0.5f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(min, min + ImVec2(width, height), ImGui::GetColorU32(JOB_BAR_TRACK_COLOR), rounding);
    const float fill = ImFloor(width * ImClamp(fraction, 0.0f, 1.0f));
    if(fill > 0.0f)
    {
        draw_list->AddRectFilled(min,
                                 min + ImVec2(ImMax(fill, height), height),
                                 ImGui::GetColorU32(imgui_style::get_accent_color()),
                                 rounding);
    }
    ImGui::Dummy(ImVec2(width, height + font_size * JOB_BAR_GAP * 2.0f));
}

/// The spinner, then how many jobs are pending, with the time the batch has taken on the right.
void draw_card_header(const job_card& card, float width)
{
    const float line_height = ImGui::GetTextLineHeight();
    ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>(
        "##job_spinner",
        ImSpinner::Radius{line_height * 0.5f},
        ImSpinner::Thickness{ImMax(2.0f, ImGui::GetFontSize() * JOB_SPINNER_THICKNESS)},
        ImSpinner::Color{ImColor(ImGui::GetColorU32(imgui_style::get_accent_color()))},
        ImSpinner::Speed{JOB_SPINNER_SPEED});
    ImGui::SameLine();
    const float header_width = width - (ImGui::GetCursorScreenPos().x - ImGui::GetItemRectMin().x);
    ImGui::PushFont(ImGui::Font::Bold);
    draw_card_line("Working on " + format_job_count(card.pending),
                   ImGui::GetColorU32(ImGuiCol_Text),
                   format_duration(card.elapsed_seconds),
                   imgui_style::get_muted_text_color_u32(),
                   header_width);
    ImGui::PopFont();
}

void draw_card_row(const job_row& row, float width)
{
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * JOB_ROW_GAP * 0.5f));
    draw_card_line(row.name,
                   ImGui::GetColorU32(ImGuiCol_Text),
                   fmt::format("{} / {}", row.done, row.total),
                   imgui_style::get_muted_text_color_u32(),
                   width);
    draw_progress_bar(row.total > 0 ? float(row.done) / float(row.total) : 0.0f, width);
    if(!row.detail.empty())
    {
        draw_card_line(row.detail, imgui_style::get_muted_text_color_u32(), {}, 0, width);
    }
}

/// The card of a busy pool. Drawn inside the toast; the toast's fade comes in through the style alpha.
void draw_job_card(const job_card& card, float opacity)
{
    const float width = ImGui::GetFontSize() * JOB_CARD_WIDTH;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * opacity);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    draw_card_header(card, width);
    for(const job_row& row : card.rows)
    {
        draw_card_row(row, width);
    }
    if(card.hidden_kind_count > 0)
    {
        draw_card_line(fmt::format("+{} more", card.hidden_kind_count), imgui_style::get_muted_text_color_u32(), {}, 0, width);
    }
    ImGui::PopStyleVar(2);
}
} // namespace
} // namespace job_status

void footer_panel::track_jobs(const std::map<std::string, size_t>& counts)
{
    const auto now = std::chrono::steady_clock::now();
    if(!job_batch_.is_active)
    {
        job_batch_ = {};
        job_batch_.is_active = true;
        job_batch_.started_at = now;
    }
    job_batch_.last_busy_at = now;
    for(auto& [name, kind] : job_batch_.kinds)
    {
        if(counts.find(name) == counts.end())
        {
            kind.pending = 0;
        }
    }
    for(const auto& [name, count] : counts)
    {
        job_kind_progress& kind = job_batch_.kinds[name];
        // Every rise between two looks is jobs added; jobs added and finished in between go uncounted.
        if(count > kind.pending)
        {
            kind.total += count - kind.pending;
        }
        kind.pending = count;
    }
}

void footer_panel::update_job_status(rtti::context& ctx)
{
    auto& thr = ctx.get_cached<threader>();
    const auto counts = thr.pool->get_jobs_count_detailed();
    const auto now = std::chrono::steady_clock::now();
    if(!counts.empty())
    {
        track_jobs(counts);
        const auto running = thr.pool->get_running_jobs();
        job_status::job_card card;
        card.elapsed_seconds = std::chrono::duration<float>(now - job_batch_.started_at).count();
        // The kinds with jobs left first, the busiest at the top.
        std::vector<std::pair<std::string, job_kind_progress>> kinds(job_batch_.kinds.begin(), job_batch_.kinds.end());
        std::stable_sort(kinds.begin(),
                         kinds.end(),
                         [](const auto& lhs, const auto& rhs)
                         {
                             return lhs.second.pending > rhs.second.pending;
                         });
        for(const auto& [name, kind] : kinds)
        {
            card.pending += kind.pending;
            if(kind.pending == 0)
            {
                continue;
            }
            if(card.rows.size() == job_status::JOB_CARD_MAX_KINDS)
            {
                ++card.hidden_kind_count;
                continue;
            }
            card.rows.push_back({name.empty() ? job_status::JOB_UNNAMED_KIND : name,
                                 kind.total - kind.pending,
                                 kind.total,
                                 job_status::find_running_detail(running, name)});
        }
        auto callback = [card = std::move(card)](const ImGuiToast&, float opacity, const ImVec4&)
        {
            job_status::draw_job_card(card, opacity);
        };
        ImGui::PushNotification(job_status::JOB_STATUS_TOAST_ID, ImGuiToast(ImGuiToastType_None, callback, job_status::JOB_CARD_DISMISS_MILLISECONDS));
        return;
    }
    if(!job_batch_.is_active || now - job_batch_.last_busy_at < job_status::JOB_BATCH_IDLE_TIME)
    {
        return;
    }
    size_t total = 0;
    for(const auto& [name, kind] : job_batch_.kinds)
    {
        total += kind.total;
    }
    const float seconds = std::chrono::duration<float>(job_batch_.last_busy_at - job_batch_.started_at).count();
    ImGuiToast toast(ImGuiToastType_Success, job_status::JOB_SUMMARY_DISMISS_MILLISECONDS);
    toast.set_title("%s", "Jobs finished");
    toast.set_content("%s", fmt::format("{} in {}", job_status::format_job_count(total), job_status::format_duration(seconds)).c_str());
    ImGui::PushNotification(job_status::JOB_STATUS_TOAST_ID, toast);
    job_batch_ = {};
}

void footer_panel::draw_footer_child(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw)
{
    ImGuiWindowFlags header_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoDecoration;
    const std::string child_id = "FOOTER_menubar";
    ImGui::BeginChild(child_id.c_str(), ImVec2(0, 0), false, header_flags);
    on_draw();
    update_job_status(ctx);
    ImGui::EndChild();
}

void footer_panel::on_frame_ui_render(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw)
{
    ImGuiWindowFlags footer_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDecoration;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - footer_size));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, footer_size));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    ImGui::SetNextWindowViewport(viewport->ID);
    if(ImGui::Begin("FOOTER", nullptr, footer_flags))
    {
        // Draw a sep. child for the menu bar.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        draw_footer_child(ctx, footer_size, on_draw);

        ImGui::PopStyleColor();
    }

    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace unravel
