#include "loading_page.h"
#include "integration/imgui_style.h"
#include "screen_art.h"
#include "screen_card.h"

#include <base/platform/process_memory.hpp>
#include <editor/format/format_bytes.h>
#include <editor/system/project_manager.h>
#include <editor/system/project_thumbnail.h>
#include <engine/loading_screen.h>
#include <engine/threading/threader.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <logging/logging.h>
#include <version/version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace unravel
{
namespace loading_page_detail
{
namespace
{
using screen_card::to_pixels;
using duration_t = loading_screen::clock::duration;

// Sizes are in units of the font size, see screen_card.
constexpr float LOADING_PAGE_WIDTH = 60.0f;
// Content narrower than this drops the column of steps and gives its room to the work.
constexpr float LOADING_PAGE_TWO_COLUMN_MIN_WIDTH = 42.0f;
constexpr float LOADING_PAGE_STEPS_WIDTH = 17.0f;
constexpr float LOADING_PAGE_COLUMN_GAP = 2.2f;
constexpr float LOADING_PAGE_BRAND_LOGO_SIZE = 1.4f;
constexpr float LOADING_PAGE_BRAND_GAP = 0.55f;
constexpr float LOADING_PAGE_SECTION_GAP = 1.3f;
constexpr float LOADING_PAGE_HERO_HEIGHT = 5.4f;
constexpr float LOADING_PAGE_HERO_ASPECT = 16.0f / 9.0f;
constexpr float LOADING_PAGE_HERO_GAP = 1.3f;
constexpr float LOADING_PAGE_HERO_LOGO_SCALE = 0.55f;
constexpr float LOADING_PAGE_IMAGE_ROUNDING = 0.45f;
constexpr float LOADING_PAGE_TITLE_SCALE = 1.4f;
constexpr float LOADING_PAGE_EYEBROW_SCALE = 0.82f;
constexpr float LOADING_PAGE_TIMER_SCALE = 1.4f;
constexpr float LOADING_PAGE_LINE_GAP = 0.3f;
constexpr float LOADING_PAGE_HEADING_GAP = 0.55f;
constexpr float LOADING_PAGE_STAGE_SCALE = 1.1f;
constexpr float LOADING_PAGE_PROGRESS_GAP = 0.6f;
constexpr float LOADING_PAGE_PROGRESS_HEIGHT = 0.4f;
constexpr float LOADING_PAGE_ROW_HEIGHT = 1.5f;
constexpr float LOADING_PAGE_DOT_RADIUS = 0.2f;
constexpr float LOADING_PAGE_ICON_WIDTH = 1.3f;
constexpr float LOADING_PAGE_TEXT_GAP = 1.0f;
// The share of a worker row the kind of the job may take; the item gets the rest.
constexpr float LOADING_PAGE_KIND_SHARE = 0.42f;
constexpr float LOADING_PAGE_SPINNER_THICKNESS = 0.14f;
constexpr float LOADING_PAGE_SEPARATOR_HEIGHT = 1.0f;
constexpr float LOADING_PAGE_METER_BAR_GAP = 0.4f;
constexpr float LOADING_PAGE_METER_BAR_HEIGHT = 0.25f;
constexpr float LOADING_PAGE_METER_GAP = 2.2f;
// The bar of a stage without a count: a segment of this share of the track sweeps across it.
constexpr float LOADING_PAGE_SWEEP_SHARE = 0.25f;
constexpr float LOADING_PAGE_SWEEP_SPEED = 0.7f;
// Where the baseline sits in the height of a line: texts of two sizes on one row share it.
constexpr float LOADING_PAGE_BASELINE_RATIO = 0.8f;
// The narrowest a share of a bar gets, in pixels: a process that holds 1% of the memory still shows.
constexpr float LOADING_PAGE_MIN_SHARE_WIDTH = 2.0f;
constexpr float LOADING_PAGE_MUTED_ALPHA = 0.5f;
constexpr float LOADING_PAGE_FAINT_ALPHA = 0.3f;
constexpr float LOADING_PAGE_VALUE_ALPHA = 0.72f;
constexpr ImU32 LOADING_PAGE_TRACK_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 LOADING_PAGE_SEPARATOR_COLOR = IM_COL32(255, 255, 255, 18);
constexpr ImU32 LOADING_PAGE_IMAGE_BORDER_COLOR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 LOADING_PAGE_DONE_COLOR = IM_COL32(104, 190, 128, 255);
constexpr ImU32 LOADING_PAGE_OTHER_RAM_COLOR = IM_COL32(145, 118, 72, 230);
constexpr ImU32 LOADING_PAGE_PROCESS_RAM_COLOR = IM_COL32(88, 168, 196, 255);
constexpr size_t LOADING_PAGE_WORKER_ROWS = 6;
constexpr size_t LOADING_PAGE_METER_COUNT = 2;
constexpr size_t LOADING_PAGE_QUEUE_KINDS = 3;
constexpr long long LOADING_PAGE_SECONDS_PER_MINUTE = 60;
// Between the parts of a line: "Indexing asset metadata - textures/rock.png".
constexpr const char* LOADING_PAGE_SEPARATOR = "  \xC2\xB7  ";
constexpr const char* LOADING_PAGE_LOGO_KEY = "editor:/data/icons/unravel_logo.png";

struct bar_share
{
    float fraction{};
    ImU32 color{};
};

/// One memory readout: a label, its value, and a bar of up to two shares stacked from the left.
struct memory_meter
{
    bool is_shown{};
    const char* label{};
    std::string value_text{};
    std::string tooltip{};
    std::array<bar_share, 2> shares{};
};

/// A worker of the thread pool: the job it runs, or nothing.
struct worker_row
{
    bool is_busy{};
    /// The item of the job (a file name), or its kind when it has no item.
    std::string title{};
    std::string kind{};
    std::string time{};
};

struct step_row
{
    std::string name{};
    duration_t duration{};
    bool is_current{};
};

/// Everything the page shows, read once per frame before anything is laid out.
struct page_content
{
    std::string eyebrow{};
    std::string title{};
    std::string subtitle{};
    bool is_project{};
    gfx::texture::ptr image{};
    gfx::texture::ptr logo{};
    std::string session_time{};
    std::string stage{};
    std::string stage_time{};
    bool has_count{};
    float fraction{};
    std::string percent_text{};
    std::string count_text{};
    std::string step_text{};
    std::string workers_summary{};
    std::vector<worker_row> workers{};
    std::string queue_text{};
    std::vector<step_row> steps{};
    std::array<memory_meter, LOADING_PAGE_METER_COUNT> meters{};
};

/// Where the next row goes. The page is laid out by hand: the card needs its height before it
/// is drawn, and no spacing of a theme gets into it.
struct page_cursor
{
    /// Null measures the page without drawing it.
    ImDrawList* draw_list{};
    float min_x{};
    float max_x{};
    float y{};
};

auto get_text_color(float alpha) -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, alpha);
}

auto get_accent_fill_color() -> ImU32
{
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w = 1.0f;
    return ImGui::GetColorU32(accent);
}

auto calc_fraction(int64_t part, int64_t whole) -> float
{
    if(whole <= 0)
    {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(part) / static_cast<float>(whole), 0.0f, 1.0f);
}

auto to_seconds(duration_t elapsed) -> double
{
    return std::chrono::duration<double>(elapsed).count();
}

/// "340 ms", "4.2 s", "42 s", "1:05".
auto format_duration(duration_t elapsed) -> std::string
{
    const double seconds = to_seconds(elapsed);
    if(seconds < 1.0)
    {
        return fmt::format("{} ms", static_cast<int>(seconds * 1000.0));
    }
    if(seconds < 10.0)
    {
        return fmt::format("{:.1f} s", seconds);
    }
    if(seconds < static_cast<double>(LOADING_PAGE_SECONDS_PER_MINUTE))
    {
        return fmt::format("{:.0f} s", seconds);
    }
    const auto whole_seconds = static_cast<long long>(seconds);
    return fmt::format("{}:{:02}", whole_seconds / LOADING_PAGE_SECONDS_PER_MINUTE, whole_seconds % LOADING_PAGE_SECONDS_PER_MINUTE);
}

/// "00:07": a clock that keeps its width while it runs.
auto format_clock(duration_t elapsed) -> std::string
{
    const auto whole_seconds = static_cast<long long>(to_seconds(elapsed));
    return fmt::format("{:02}:{:02}", whole_seconds / LOADING_PAGE_SECONDS_PER_MINUTE, whole_seconds % LOADING_PAGE_SECONDS_PER_MINUTE);
}

auto to_upper(std::string text) -> std::string
{
    std::transform(text.begin(),
                   text.end(),
                   text.begin(),
                   [](char character)
                   {
                       return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                   });
    return text;
}

auto read_gpu_meter() -> memory_meter
{
    memory_meter meter{};
    const auto* stats = bgfx::getStats();
    if(stats == nullptr || stats->gpuMemoryUsed <= 0)
    {
        return meter;
    }
    meter.is_shown = true;
    meter.label = "GPU";
    meter.tooltip = "Video memory allocated by the renderer, of the budget of the device.";
    meter.value_text = format_bytes(stats->gpuMemoryUsed);
    if(stats->gpuMemoryMax > 0)
    {
        const float used = calc_fraction(stats->gpuMemoryUsed, stats->gpuMemoryMax);
        meter.value_text = fmt::format("{} / {} ({:.0f}%)",
                                       format_bytes(stats->gpuMemoryUsed),
                                       format_bytes(stats->gpuMemoryMax),
                                       static_cast<double>(used * 100.0f));
        meter.shares[0] = {used, get_accent_fill_color()};
    }
    return meter;
}

auto read_ram_meter() -> memory_meter
{
    memory_meter meter{};
    const int64_t process_bytes = platform::get_process_resident_set_bytes();
    const int64_t total_bytes = platform::get_system_physical_memory_bytes();
    const int64_t used_bytes = platform::get_system_used_physical_memory_bytes();
    if(process_bytes <= 0 && used_bytes <= 0)
    {
        return meter;
    }
    meter.is_shown = true;
    meter.label = "RAM";
    if(total_bytes <= 0 || used_bytes <= 0)
    {
        meter.value_text = format_bytes(process_bytes);
        meter.tooltip = "Memory this process keeps resident.";
        return meter;
    }
    const int64_t other_bytes = std::max<int64_t>(0, used_bytes - process_bytes);
    meter.value_text = fmt::format("{} / {}  ({} this process)",
                                   format_bytes(used_bytes),
                                   format_bytes(total_bytes),
                                   format_bytes(process_bytes));
    meter.tooltip = fmt::format("The bar is the memory of the system.\n"
                                "Amber: used by other processes ({}).\n"
                                "Cyan: used by this process ({}).",
                                format_bytes(other_bytes),
                                format_bytes(process_bytes));
    meter.shares[0] = {calc_fraction(other_bytes, total_bytes), LOADING_PAGE_OTHER_RAM_COLOR};
    meter.shares[1] = {calc_fraction(process_bytes, total_bytes), LOADING_PAGE_PROCESS_RAM_COLOR};
    return meter;
}

/// What is loading: the editor, or a project with its thumbnail.
void read_session(page_content& content, rtti::context& ctx, const loading_screen& screen, image_file_cache& images)
{
    content.eyebrow = to_upper(screen.session_title());
    content.session_time = format_clock(screen.session_elapsed());
    content.logo = images.get(fs::resolve_protocol(LOADING_PAGE_LOGO_KEY));
    const auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        content.title = "Unravel Editor";
        content.subtitle = bgfx::getRendererName(bgfx::getRendererType());
        return;
    }
    fs::path project_dir = fs::resolve_protocol("app:/");
    if(!project_dir.has_filename())
    {
        project_dir = project_dir.parent_path();
    }
    content.is_project = true;
    content.title = pm.get_name();
    content.subtitle = project_dir.generic_string();
    content.image = images.get(project_thumbnail::find(project_dir));
}

void read_stage(page_content& content, const loading_screen& screen)
{
    content.stage = screen.current_module().empty() ? std::string("Preparing") : screen.current_module();
    content.stage_time = format_duration(screen.module_elapsed());
    content.has_count = screen.total() > 0;
    content.fraction = calc_fraction(static_cast<int64_t>(screen.completed()), static_cast<int64_t>(screen.total()));
    if(content.has_count)
    {
        content.percent_text = fmt::format("{:.0f}%", static_cast<double>(content.fraction * 100.0f));
        content.count_text = fmt::format("{} / {}", screen.completed(), screen.total());
    }
    const std::string& step = screen.current_step();
    const std::string& item = screen.current_item();
    if(!step.empty() && !item.empty())
    {
        content.step_text = fmt::format("{}{}{}", step, LOADING_PAGE_SEPARATOR, item);
    }
    else
    {
        content.step_text = step.empty() ? item : step;
    }
}

auto make_worker_row(const tpp::running_job& job) -> worker_row
{
    worker_row row{};
    row.is_busy = true;
    row.time = format_duration(job.elapsed);
    const std::string kind = job.name.empty() ? std::string("Job") : job.name;
    if(job.detail.empty())
    {
        row.title = kind;
        return row;
    }
    row.title = fs::path(job.detail).filename().generic_string();
    row.kind = kind;
    return row;
}

/// The jobs waiting per kind: the counts of the pool hold the running ones too.
auto count_waiting_jobs(const tpp::thread_pool& pool, const std::vector<tpp::running_job>& running)
    -> std::vector<std::pair<std::string, size_t>>
{
    std::map<std::string, size_t> counts = pool.get_jobs_count_detailed();
    for(const tpp::running_job& job : running)
    {
        auto it = counts.find(job.name);
        if(it != counts.end() && it->second > 0)
        {
            --it->second;
        }
    }
    std::vector<std::pair<std::string, size_t>> waiting;
    std::copy_if(counts.begin(),
                 counts.end(),
                 std::back_inserter(waiting),
                 [](const auto& count)
                 {
                     return count.second > 0;
                 });
    std::sort(waiting.begin(),
              waiting.end(),
              [](const auto& lhs, const auto& rhs)
              {
                  return lhs.second > rhs.second;
              });
    return waiting;
}

auto format_queue(const std::vector<std::pair<std::string, size_t>>& waiting) -> std::string
{
    std::string text;
    for(size_t index = 0; index < std::min(waiting.size(), LOADING_PAGE_QUEUE_KINDS); ++index)
    {
        const auto& [name, count] = waiting[index];
        text += fmt::format("{}{} {}", text.empty() ? "Waiting:  " : LOADING_PAGE_SEPARATOR, name.empty() ? "Job" : name, count);
    }
    if(waiting.size() > LOADING_PAGE_QUEUE_KINDS)
    {
        text += fmt::format("{}+{} more", LOADING_PAGE_SEPARATOR, waiting.size() - LOADING_PAGE_QUEUE_KINDS);
    }
    return text;
}

/// What the workers run, the longest running first, then the idle ones; always the same rows, so
/// the card keeps its height.
void read_workers(page_content& content, rtti::context& ctx)
{
    const tpp::thread_pool& pool = *ctx.get_cached<threader>().pool;
    const std::vector<tpp::running_job> running = pool.get_running_jobs();
    const size_t worker_count = pool.get_workers_count();
    const auto waiting = count_waiting_jobs(pool, running);
    size_t waiting_count = 0;
    for(const auto& kind : waiting)
    {
        waiting_count += kind.second;
    }
    content.workers_summary = fmt::format("{} of {} busy{}{} waiting", running.size(), worker_count, LOADING_PAGE_SEPARATOR, waiting_count);
    content.queue_text = format_queue(waiting);
    const size_t row_count = std::clamp<size_t>(worker_count, 1, LOADING_PAGE_WORKER_ROWS);
    const bool has_overflow = running.size() > row_count;
    for(size_t index = 0; index < row_count; ++index)
    {
        if(has_overflow && index + 1 == row_count)
        {
            worker_row more{};
            more.is_busy = true;
            more.title = fmt::format("+{} more running", running.size() - index);
            content.workers.push_back(more);
            break;
        }
        if(index < running.size())
        {
            content.workers.push_back(make_worker_row(running[index]));
            continue;
        }
        worker_row idle{};
        idle.title = "Idle";
        content.workers.push_back(idle);
    }
}

void read_steps(page_content& content, const loading_screen& screen)
{
    for(const loading_screen::module_record& module : screen.finished_modules())
    {
        content.steps.push_back({module.name, module.duration, false});
    }
    if(!screen.current_module().empty())
    {
        content.steps.push_back({screen.current_module(), screen.module_elapsed(), true});
    }
}

auto read_content(rtti::context& ctx, const loading_screen& screen, image_file_cache& images) -> page_content
{
    page_content content{};
    read_session(content, ctx, screen, images);
    read_stage(content, screen);
    read_workers(content, ctx);
    read_steps(content, screen);
    content.meters = {read_gpu_meter(), read_ram_meter()};
    return content;
}

void add_gap(page_cursor& cursor, float font_units)
{
    cursor.y += to_pixels(font_units);
}

void draw_text(ImDrawList* draw_list, const ImVec2& pos, ImU32 color, const char* text)
{
    if(draw_list == nullptr)
    {
        return;
    }
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(ImFloor(pos.x), ImFloor(pos.y)), color, text);
}

void draw_text_right(ImDrawList* draw_list, float max_x, float y, ImU32 color, const char* text)
{
    draw_text(draw_list, ImVec2(max_x - ImGui::CalcTextSize(text).x, y), color, text);
}

/// Text from min_x that ends in an ellipsis when it would run past max_x.
void draw_text_clipped(ImDrawList* draw_list, float min_x, float max_x, float y, ImU32 color, const char* text)
{
    if(draw_list == nullptr || max_x <= min_x)
    {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const ImVec2 pos_min(ImFloor(min_x), ImFloor(y));
    const ImVec2 pos_max(max_x, y + ImGui::GetFontSize());
    ImGui::RenderTextEllipsis(draw_list, pos_min, pos_max, max_x, text, nullptr, nullptr);
    ImGui::PopStyleColor();
}

/// A glyph of the icon font, which only some of the weights carry.
void draw_icon(ImDrawList* draw_list, const ImVec2& pos, ImU32 color, const char* icon)
{
    draw_list->AddText(ImGui::GetFont(ImGui::Font::Regular), ImGui::GetFontSize(), ImVec2(ImFloor(pos.x), ImFloor(pos.y)), color, icon);
}

/// A track over the width of the cursor with the shares stacked on it from the left.
void draw_bar(const page_cursor& cursor, float height, const std::array<bar_share, 2>& shares)
{
    const ImVec2 track_min(cursor.min_x, cursor.y);
    const ImVec2 track_max(cursor.max_x, cursor.y + height);
    const float rounding = height * 0.5f;
    cursor.draw_list->AddRectFilled(track_min, track_max, LOADING_PAGE_TRACK_COLOR, rounding);
    const auto is_visible = [](const bar_share& share) -> bool { return share.fraction > 0.0f; };
    float share_min_x = track_min.x;
    for(size_t index = 0; index < shares.size(); ++index)
    {
        const bar_share& share = shares[index];
        if(!is_visible(share))
        {
            continue;
        }
        const float width = ImMax((track_max.x - track_min.x) * share.fraction, LOADING_PAGE_MIN_SHARE_WIDTH);
        const float share_max_x = ImMin(share_min_x + width, track_max.x);
        // The ends of the fill are round, the seam between two shares is not.
        const bool is_last = std::none_of(shares.begin() + index + 1, shares.end(), is_visible);
        const ImDrawFlags left_corners = share_min_x > track_min.x ? ImDrawFlags_None : ImDrawFlags_RoundCornersLeft;
        const ImDrawFlags right_corners = is_last ? ImDrawFlags_RoundCornersRight : ImDrawFlags_None;
        const ImDrawFlags corners = left_corners | right_corners;
        cursor.draw_list->AddRectFilled(ImVec2(share_min_x, track_min.y),
                                        ImVec2(share_max_x, track_max.y),
                                        share.color,
                                        corners == ImDrawFlags_None ? 0.0f : rounding,
                                        corners);
        share_min_x = share_max_x;
    }
}

/// A segment that sweeps over the track: a stage that has nothing to count yet.
void draw_sweeping_bar(const page_cursor& cursor, float height)
{
    const ImVec2 track_min(cursor.min_x, cursor.y);
    const ImVec2 track_max(cursor.max_x, cursor.y + height);
    const float rounding = height * 0.5f;
    cursor.draw_list->AddRectFilled(track_min, track_max, LOADING_PAGE_TRACK_COLOR, rounding);
    const float width = track_max.x - track_min.x;
    const float cycle = std::fmod(static_cast<float>(ImGui::GetTime()) * LOADING_PAGE_SWEEP_SPEED, 1.0f + LOADING_PAGE_SWEEP_SHARE);
    const float segment_min_x = ImMax(track_min.x, track_min.x + width * (cycle - LOADING_PAGE_SWEEP_SHARE));
    const float segment_max_x = ImMin(track_max.x, track_min.x + width * cycle);
    if(segment_max_x > segment_min_x)
    {
        cursor.draw_list->AddRectFilled(ImVec2(segment_min_x, track_min.y),
                                        ImVec2(segment_max_x, track_max.y),
                                        get_accent_fill_color(),
                                        rounding);
    }
}

/// The logo and the name of the engine, the version it runs at the right end.
void add_brand_row(page_cursor& cursor, const page_content& content)
{
    const float logo_size = to_pixels(LOADING_PAGE_BRAND_LOGO_SIZE);
    if(cursor.draw_list != nullptr)
    {
        const float text_y = cursor.y + (logo_size - ImGui::GetFontSize()) * 0.5f;
        float text_x = cursor.min_x;
        if(content.logo)
        {
            const ImRect logo_rect(ImVec2(cursor.min_x, cursor.y), ImVec2(cursor.min_x + logo_size, cursor.y + logo_size));
            screen_art::draw_image_cover(cursor.draw_list, content.logo, logo_rect, 0.0f);
            text_x += logo_size + to_pixels(LOADING_PAGE_BRAND_GAP);
        }
        ImGui::PushFont(ImGui::Font::Bold);
        draw_text(cursor.draw_list, ImVec2(text_x, text_y), get_text_color(LOADING_PAGE_VALUE_ALPHA), "Unravel Engine");
        ImGui::PopFont();
        draw_text_right(cursor.draw_list, cursor.max_x, text_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), version::get_full().c_str());
    }
    cursor.y += logo_size;
}

void draw_separator(const page_cursor& cursor)
{
    cursor.draw_list->AddRectFilled(ImVec2(cursor.min_x, cursor.y),
                                    ImVec2(cursor.max_x, cursor.y + LOADING_PAGE_SEPARATOR_HEIGHT),
                                    LOADING_PAGE_SEPARATOR_COLOR);
}

void add_separator(page_cursor& cursor)
{
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
    if(cursor.draw_list != nullptr)
    {
        draw_separator(cursor);
    }
    cursor.y += LOADING_PAGE_SEPARATOR_HEIGHT;
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
}

/// The picture of what is loading: the thumbnail of the project, a stand-in for one without, or
/// the logo of the engine on its accent.
void draw_hero_image(ImDrawList* draw_list, const ImRect& rect, const page_content& content)
{
    const float rounding = to_pixels(LOADING_PAGE_IMAGE_ROUNDING);
    if(content.is_project && content.image)
    {
        screen_art::draw_image_cover(draw_list, content.image, rect, rounding);
    }
    else if(content.is_project)
    {
        screen_art::draw_placeholder(draw_list, rect, content.title, rounding);
    }
    else
    {
        screen_art::draw_gradient(draw_list, rect, get_accent_fill_color(), rounding);
        const float logo_size = ImFloor(rect.GetHeight() * LOADING_PAGE_HERO_LOGO_SCALE);
        const ImVec2 logo_min(ImFloor(rect.GetCenter().x - logo_size * 0.5f), ImFloor(rect.GetCenter().y - logo_size * 0.5f));
        screen_art::draw_image_cover(draw_list, content.logo, ImRect(logo_min, logo_min + ImVec2(logo_size, logo_size)), 0.0f);
    }
    draw_list->AddRect(rect.Min, rect.Max, LOADING_PAGE_IMAGE_BORDER_COLOR, rounding);
}

/// An eyebrow over a big line over a muted one, from min_x; returns the height it took. A null
/// draw list only measures.
auto draw_title_block(ImDrawList* draw_list,
                      float min_x,
                      float max_x,
                      float y,
                      const std::string& eyebrow,
                      const std::string& title,
                      const std::string& subtitle) -> float
{
    const float top = y;
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(LOADING_PAGE_EYEBROW_SCALE);
    draw_text_clipped(draw_list, min_x, max_x, y, get_text_color(LOADING_PAGE_MUTED_ALPHA), eyebrow.c_str());
    y += ImGui::GetFontSize() + to_pixels(LOADING_PAGE_LINE_GAP);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    ImGui::PushFont(ImGui::Font::Black);
    ImGui::PushWindowFontScale(LOADING_PAGE_TITLE_SCALE);
    draw_text_clipped(draw_list, min_x, max_x, y, get_text_color(1.0f), title.c_str());
    y += ImGui::GetFontSize() + to_pixels(LOADING_PAGE_LINE_GAP);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    draw_text_clipped(draw_list, min_x, max_x, y, get_text_color(LOADING_PAGE_MUTED_ALPHA), subtitle.c_str());
    y += ImGui::GetFontSize();
    return y - top;
}

/// The time the session runs, right aligned under its eyebrow; returns the width it took.
auto draw_session_timer(ImDrawList* draw_list, float max_x, float y, const page_content& content) -> float
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(LOADING_PAGE_EYEBROW_SCALE);
    const float label_width = ImGui::CalcTextSize("ELAPSED").x;
    draw_text_right(draw_list, max_x, y, get_text_color(LOADING_PAGE_MUTED_ALPHA), "ELAPSED");
    y += ImGui::GetFontSize() + to_pixels(LOADING_PAGE_LINE_GAP);
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    ImGui::PushFont(ImGui::Font::Light);
    ImGui::PushWindowFontScale(LOADING_PAGE_TIMER_SCALE);
    const float clock_width = ImGui::CalcTextSize(content.session_time.c_str()).x;
    draw_text_right(draw_list, max_x, y, get_text_color(LOADING_PAGE_VALUE_ALPHA), content.session_time.c_str());
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    return ImMax(label_width, clock_width);
}

/// The picture, what is loading, and the time it has taken so far.
void add_hero_row(page_cursor& cursor, const page_content& content)
{
    const float height = to_pixels(LOADING_PAGE_HERO_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        const float width = ImFloor(height * LOADING_PAGE_HERO_ASPECT);
        const ImRect image_rect(ImVec2(cursor.min_x, cursor.y), ImVec2(cursor.min_x + width, cursor.y + height));
        draw_hero_image(cursor.draw_list, image_rect, content);
        const float text_min_x = image_rect.Max.x + to_pixels(LOADING_PAGE_HERO_GAP);
        // Measured first: the block is centered on the picture.
        const float block_height = draw_title_block(nullptr, 0.0f, 0.0f, 0.0f, {}, {}, {});
        const float block_y = ImFloor(cursor.y + (height - block_height) * 0.5f);
        const float timer_width = draw_session_timer(cursor.draw_list, cursor.max_x, block_y, content);
        const float text_max_x = cursor.max_x - timer_width - to_pixels(LOADING_PAGE_TEXT_GAP);
        draw_title_block(cursor.draw_list, text_min_x, text_max_x, block_y, content.eyebrow, content.title, content.subtitle);
    }
    cursor.y += height;
}

/// A spinner and the stage with its time, the share of it that is done at the right end.
void add_stage_row(page_cursor& cursor, const page_content& content)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(LOADING_PAGE_STAGE_SCALE);
    const float line_height = ImGui::GetFontSize();
    if(cursor.draw_list != nullptr)
    {
        const float thickness = ImMax(to_pixels(LOADING_PAGE_SPINNER_THICKNESS), 1.0f);
        const float radius = line_height * 0.5f - thickness;
        screen_art::draw_spinner(cursor.draw_list,
                                 ImVec2(cursor.min_x + line_height * 0.5f, cursor.y + line_height * 0.5f),
                                 radius,
                                 thickness,
                                 get_accent_fill_color());
        draw_text_right(cursor.draw_list, cursor.max_x, cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), content.percent_text.c_str());
        const float text_min_x = cursor.min_x + line_height + to_pixels(LOADING_PAGE_LINE_GAP * 2.0f);
        const float percent_width = ImGui::CalcTextSize(content.percent_text.c_str()).x;
        const float text_max_x = cursor.max_x - percent_width - to_pixels(LOADING_PAGE_TEXT_GAP);
        const float stage_width = ImMin(ImGui::CalcTextSize(content.stage.c_str()).x, text_max_x - text_min_x);
        draw_text_clipped(cursor.draw_list, text_min_x, text_max_x, cursor.y, get_text_color(1.0f), content.stage.c_str());
        ImGui::PopWindowFontScale();
        ImGui::PopFont();
        // The time of the stage after its name, on the baseline of the bigger text.
        const float time_y = cursor.y + (line_height - ImGui::GetFontSize()) * LOADING_PAGE_BASELINE_RATIO;
        const float time_x = text_min_x + stage_width + to_pixels(LOADING_PAGE_LINE_GAP * 2.0f);
        draw_text_clipped(cursor.draw_list, time_x, text_max_x, time_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), content.stage_time.c_str());
    }
    else
    {
        ImGui::PopWindowFontScale();
        ImGui::PopFont();
    }
    cursor.y += line_height;
}

void add_progress_bar(page_cursor& cursor, const page_content& content)
{
    const float height = to_pixels(LOADING_PAGE_PROGRESS_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        if(content.has_count)
        {
            draw_bar(cursor, height, {bar_share{content.fraction, get_accent_fill_color()}, bar_share{}});
        }
        else
        {
            draw_sweeping_bar(cursor, height);
        }
    }
    cursor.y += height;
}

/// The step of the stage and its item on one line, however long, and the count at the right end.
void add_step_row(page_cursor& cursor, const page_content& content)
{
    if(cursor.draw_list != nullptr)
    {
        draw_text_right(cursor.draw_list, cursor.max_x, cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), content.count_text.c_str());
        const float text_max_x = cursor.max_x - ImGui::CalcTextSize(content.count_text.c_str()).x - to_pixels(LOADING_PAGE_TEXT_GAP);
        draw_text_clipped(cursor.draw_list, cursor.min_x, text_max_x, cursor.y, get_text_color(LOADING_PAGE_MUTED_ALPHA), content.step_text.c_str());
    }
    cursor.y += ImGui::GetFontSize();
}

/// A small caps heading with a muted note at the right end.
void add_heading_row(page_cursor& cursor, const char* heading, const std::string& note)
{
    ImGui::PushFont(ImGui::Font::SemiBold);
    ImGui::PushWindowFontScale(LOADING_PAGE_EYEBROW_SCALE);
    const float heading_height = ImGui::GetFontSize();
    const float heading_width = ImGui::CalcTextSize(heading).x;
    if(cursor.draw_list != nullptr)
    {
        draw_text(cursor.draw_list, ImVec2(cursor.min_x, cursor.y), get_text_color(LOADING_PAGE_MUTED_ALPHA), heading);
    }
    ImGui::PopWindowFontScale();
    ImGui::PopFont();
    if(cursor.draw_list != nullptr && !note.empty())
    {
        const float note_y = cursor.y + (heading_height - ImGui::GetFontSize()) * LOADING_PAGE_BASELINE_RATIO;
        const float note_width = ImGui::CalcTextSize(note.c_str()).x;
        const float note_min_x = ImMax(cursor.min_x + heading_width + to_pixels(LOADING_PAGE_TEXT_GAP), cursor.max_x - note_width);
        draw_text_clipped(cursor.draw_list, note_min_x, cursor.max_x, note_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), note.c_str());
    }
    cursor.y += ImMax(heading_height, ImGui::GetFontSize());
}

/// A dot for the worker, the item of its job, the kind of the job, and the time it runs.
void add_worker_row(page_cursor& cursor, const worker_row& row)
{
    const float row_height = to_pixels(LOADING_PAGE_ROW_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        const float text_y = cursor.y + (row_height - ImGui::GetFontSize()) * 0.5f;
        const float dot_radius = to_pixels(LOADING_PAGE_DOT_RADIUS);
        const ImVec2 dot_center(cursor.min_x + dot_radius, cursor.y + row_height * 0.5f);
        if(row.is_busy)
        {
            cursor.draw_list->AddCircleFilled(dot_center, dot_radius, get_accent_fill_color());
        }
        else
        {
            cursor.draw_list->AddCircle(dot_center, dot_radius, get_text_color(LOADING_PAGE_FAINT_ALPHA));
        }
        const float time_width = ImGui::CalcTextSize("0:00:00").x;
        draw_text_right(cursor.draw_list, cursor.max_x, text_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), row.time.c_str());
        const float text_min_x = cursor.min_x + to_pixels(LOADING_PAGE_ICON_WIDTH);
        const float text_max_x = cursor.max_x - time_width;
        float title_max_x = text_max_x;
        if(!row.kind.empty())
        {
            const float kind_width = ImMin(ImGui::CalcTextSize(row.kind.c_str()).x, (text_max_x - text_min_x) * LOADING_PAGE_KIND_SHARE);
            const float kind_min_x = text_max_x - kind_width;
            draw_text_clipped(cursor.draw_list, kind_min_x, text_max_x, text_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), row.kind.c_str());
            title_max_x = kind_min_x - to_pixels(LOADING_PAGE_TEXT_GAP);
        }
        const ImU32 title_color = row.is_busy ? get_text_color(LOADING_PAGE_VALUE_ALPHA) : get_text_color(LOADING_PAGE_FAINT_ALPHA);
        draw_text_clipped(cursor.draw_list, text_min_x, title_max_x, text_y, title_color, row.title.c_str());
    }
    cursor.y += row_height;
}

void add_note_row(page_cursor& cursor, const std::string& note)
{
    if(cursor.draw_list != nullptr)
    {
        draw_text_clipped(cursor.draw_list, cursor.min_x, cursor.max_x, cursor.y, get_text_color(LOADING_PAGE_FAINT_ALPHA), note.c_str());
    }
    cursor.y += ImGui::GetFontSize();
}

/// The stage with its progress, then what the workers run and what waits behind them.
void add_work_column(page_cursor& cursor, const page_content& content)
{
    add_stage_row(cursor, content);
    add_gap(cursor, LOADING_PAGE_PROGRESS_GAP);
    add_progress_bar(cursor, content);
    add_gap(cursor, LOADING_PAGE_PROGRESS_GAP);
    add_step_row(cursor, content);
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
    add_heading_row(cursor, "WORKERS", content.workers_summary);
    add_gap(cursor, LOADING_PAGE_HEADING_GAP);
    for(const worker_row& row : content.workers)
    {
        add_worker_row(cursor, row);
    }
    add_note_row(cursor, content.queue_text);
}

/// A check for a finished step or a spinner for the one in work, its name and its time.
void add_step_record_row(page_cursor& cursor, const step_row& step, const std::string& name)
{
    const float row_height = to_pixels(LOADING_PAGE_ROW_HEIGHT);
    if(cursor.draw_list != nullptr)
    {
        const float line_height = ImGui::GetFontSize();
        const float text_y = cursor.y + (row_height - line_height) * 0.5f;
        if(step.is_current)
        {
            const float thickness = ImMax(to_pixels(LOADING_PAGE_SPINNER_THICKNESS) * 0.8f, 1.0f);
            screen_art::draw_spinner(cursor.draw_list,
                                     ImVec2(cursor.min_x + line_height * 0.4f, cursor.y + row_height * 0.5f),
                                     line_height * 0.4f - thickness,
                                     thickness,
                                     get_accent_fill_color());
        }
        else
        {
            draw_icon(cursor.draw_list, ImVec2(cursor.min_x, text_y), LOADING_PAGE_DONE_COLOR, ICON_MDI_CHECK);
        }
        const std::string time = format_duration(step.duration);
        draw_text_right(cursor.draw_list, cursor.max_x, text_y, get_text_color(LOADING_PAGE_MUTED_ALPHA), time.c_str());
        const float text_min_x = cursor.min_x + to_pixels(LOADING_PAGE_ICON_WIDTH);
        const float text_max_x = cursor.max_x - ImGui::CalcTextSize(time.c_str()).x - to_pixels(LOADING_PAGE_TEXT_GAP);
        const ImU32 name_color = step.is_current ? get_text_color(1.0f) : get_text_color(LOADING_PAGE_VALUE_ALPHA);
        draw_text_clipped(cursor.draw_list, text_min_x, text_max_x, text_y, name_color, name.c_str());
    }
    cursor.y += row_height;
}

/// The steps of the session down to the bottom of the work column: the latest ones, the earlier
/// ones folded into the first row when they do not all fit.
void add_steps_column(page_cursor& cursor, const page_content& content, float bottom)
{
    add_heading_row(cursor, "STEPS", fmt::format("{} done", content.steps.empty() ? 0 : content.steps.size() - (content.steps.back().is_current ? 1 : 0)));
    add_gap(cursor, LOADING_PAGE_HEADING_GAP);
    const float row_height = to_pixels(LOADING_PAGE_ROW_HEIGHT);
    const auto capacity = static_cast<size_t>(ImMax(1.0f, std::floor((bottom - cursor.y) / row_height)));
    size_t first = 0;
    if(content.steps.size() > capacity)
    {
        first = content.steps.size() - (capacity - 1);
        duration_t folded{};
        for(size_t index = 0; index < first; ++index)
        {
            folded += content.steps[index].duration;
        }
        step_row earlier{};
        earlier.duration = folded;
        add_step_record_row(cursor, earlier, fmt::format("{} earlier steps", first));
    }
    for(size_t index = first; index < content.steps.size(); ++index)
    {
        add_step_record_row(cursor, content.steps[index], content.steps[index].name);
    }
}

/// The steps on the left when there is room for them, the work on the right.
void add_columns(page_cursor& cursor, const page_content& content)
{
    const bool has_steps = cursor.max_x - cursor.min_x >= to_pixels(LOADING_PAGE_TWO_COLUMN_MIN_WIDTH);
    page_cursor work = cursor;
    if(has_steps)
    {
        work.min_x = cursor.min_x + to_pixels(LOADING_PAGE_STEPS_WIDTH) + to_pixels(LOADING_PAGE_COLUMN_GAP);
    }
    add_work_column(work, content);
    if(has_steps)
    {
        page_cursor steps = cursor;
        steps.max_x = cursor.min_x + to_pixels(LOADING_PAGE_STEPS_WIDTH);
        add_steps_column(steps, content, work.y);
    }
    cursor.y = work.y;
}

/// The memory meters side by side, each a label and its value over a bar.
void add_meters_row(page_cursor& cursor, const std::array<memory_meter, LOADING_PAGE_METER_COUNT>& meters)
{
    const float row_min_y = cursor.y;
    const float row_height = ImGui::GetFontSize() + to_pixels(LOADING_PAGE_METER_BAR_GAP) + to_pixels(LOADING_PAGE_METER_BAR_HEIGHT);
    const auto shown_count = static_cast<float>(std::count_if(meters.begin(),
                                                              meters.end(),
                                                              [](const memory_meter& meter)
                                                              {
                                                                  return meter.is_shown;
                                                              }));
    if(cursor.draw_list != nullptr && shown_count > 0.0f)
    {
        const float gap = to_pixels(LOADING_PAGE_METER_GAP);
        const float meter_width = (cursor.max_x - cursor.min_x - gap * (shown_count - 1.0f)) / shown_count;
        page_cursor meter_cursor = cursor;
        for(const memory_meter& meter : meters)
        {
            if(!meter.is_shown)
            {
                continue;
            }
            meter_cursor.max_x = meter_cursor.min_x + meter_width;
            meter_cursor.y = row_min_y;
            draw_text(cursor.draw_list, ImVec2(meter_cursor.min_x, meter_cursor.y), get_text_color(LOADING_PAGE_MUTED_ALPHA), meter.label);
            const float label_width = ImGui::CalcTextSize(meter.label).x + to_pixels(LOADING_PAGE_TEXT_GAP);
            const float value_width = ImGui::CalcTextSize(meter.value_text.c_str()).x;
            const float value_min_x = ImMax(meter_cursor.min_x + label_width, meter_cursor.max_x - value_width);
            draw_text_clipped(cursor.draw_list, value_min_x, meter_cursor.max_x, meter_cursor.y, get_text_color(LOADING_PAGE_VALUE_ALPHA), meter.value_text.c_str());
            meter_cursor.y += ImGui::GetFontSize() + to_pixels(LOADING_PAGE_METER_BAR_GAP);
            draw_bar(meter_cursor, to_pixels(LOADING_PAGE_METER_BAR_HEIGHT), meter.shares);
            if(ImGui::IsMouseHoveringRect(ImVec2(meter_cursor.min_x, row_min_y), ImVec2(meter_cursor.max_x, row_min_y + row_height)))
            {
                ImGui::SetTooltip("%s", meter.tooltip.c_str());
            }
            meter_cursor.min_x = meter_cursor.max_x + gap;
        }
    }
    cursor.y += row_height;
}

/// The one place that knows the rows of the page and their order: it measures with a cursor
/// that has no draw list, and draws with one that has.
void layout_page(page_cursor& cursor, const page_content& content)
{
    add_brand_row(cursor, content);
    add_gap(cursor, LOADING_PAGE_SECTION_GAP);
    add_hero_row(cursor, content);
    add_separator(cursor);
    add_columns(cursor, content);
    add_separator(cursor);
    add_meters_row(cursor, content.meters);
}

auto calc_card_width() -> float
{
    return screen_card::fit_to_viewport(ImVec2(to_pixels(LOADING_PAGE_WIDTH), 0.0f)).x;
}

auto measure_page(const page_content& content, float card_width) -> float
{
    page_cursor cursor{};
    cursor.max_x = card_width - 2.0f * screen_card::get_padding().x;
    layout_page(cursor, content);
    return cursor.y + 2.0f * screen_card::get_padding().y;
}
} // namespace
} // namespace loading_page_detail

void loading_page::draw(rtti::context& ctx, const loading_screen& screen)
{
    namespace detail = loading_page_detail;
    const detail::page_content content = detail::read_content(ctx, screen, images_);
    const float card_width = detail::calc_card_width();
    screen_card::card_layout layout{};
    layout.size = screen_card::fit_to_viewport(ImVec2(card_width, detail::measure_page(content, card_width)));
    if(screen_card::begin("##loading_page", layout))
    {
        detail::page_cursor cursor{};
        cursor.draw_list = ImGui::GetWindowDrawList();
        cursor.min_x = ImGui::GetCursorScreenPos().x;
        cursor.max_x = cursor.min_x + ImGui::GetContentRegionAvail().x;
        cursor.y = ImGui::GetCursorScreenPos().y;
        detail::layout_page(cursor, content);
    }
    screen_card::end();
}

void loading_page::release()
{
    images_.clear();
}
} // namespace unravel
