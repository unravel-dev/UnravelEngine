#include "profiler_eviction_section.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <graphics/eviction.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace unravel
{
namespace
{

ImVec4 category_text_color_ex{0.62f, 0.80f, 1.0f, 1.0f};
ImVec4 muted_text_color_ex{0.55f, 0.55f, 0.55f, 1.0f};
ImVec4 warn_text_color_ex{0.93f, 0.74f, 0.20f, 1.0f};
ImVec4 bad_text_color_ex{1.0f, 0.3f, 0.3f, 1.0f};

std::array<const char*, 4> strategy_names{"LRU (least recently used)",
                                                    "LFU (least frequently used)",
                                                    "Largest first",
                                                    "Age TTL"};

void format_bytes_ex(std::uint64_t value, char* buffer, std::size_t buffer_size)
{
    static const std::array<const char*, 5> units{"B", "KB", "MB", "GB", "TB"};
    if(value < 1024)
    {
        std::snprintf(buffer, buffer_size, "%llu B", static_cast<unsigned long long>(value));
        return;
    }
    double scaled = static_cast<double>(value);
    std::size_t unit_index = 0;
    while(scaled >= 1024.0 && unit_index + 1 < units.size())
    {
        scaled /= 1024.0;
        ++unit_index;
    }
    std::snprintf(buffer, buffer_size, "%.2f %s", scaled, units[unit_index]);
}

auto clamp_u64(std::int64_t value) -> std::uint64_t
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, value));
}

void draw_stat_row(const char* label, const char* value, const ImVec4* color = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    const float offset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(value).x;
    if(offset > 0.0f)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
    if(color != nullptr)
    {
        ImGui::TextColored(*color, "%s", value);
        return;
    }
    ImGui::TextUnformatted(value);
}

void draw_count_bytes_row(const char* label,
                          std::uint64_t count,
                          std::uint64_t bytes,
                          const ImVec4* color = nullptr)
{
    std::array<char, 32> bytes_buf{};
    format_bytes_ex(bytes, bytes_buf.data(), bytes_buf.size());
    std::array<char, 64> value_buf{};
    std::snprintf(value_buf.data(),
                  value_buf.size(),
                  "%llu (%s)",
                  static_cast<unsigned long long>(count),
                  bytes_buf.data());
    draw_stat_row(label, value_buf.data(), color);
}

void draw_count_row(const char* label, std::uint64_t count, const ImVec4* color = nullptr)
{
    std::array<char, 32> value_buf{};
    std::snprintf(value_buf.data(), value_buf.size(), "%llu", static_cast<unsigned long long>(count));
    draw_stat_row(label, value_buf.data(), color);
}

void draw_ms_row(const char* label, double ms)
{
    std::array<char, 32> value_buf{};
    std::snprintf(value_buf.data(), value_buf.size(), "%.3f ms", ms);
    draw_stat_row(label, value_buf.data());
}

} // namespace

void profiler_draw_eviction_section(eviction_settings& state)
{
    if(!ImGui::CollapsingHeader(ICON_MDI_SWAP_HORIZONTAL "\tGPU Eviction / Paging"))
    {
        return;
    }

    namespace ev = gfx::eviction;

    if(!ev::is_supported())
    {
        ImGui::TextColored(muted_text_color_ex,
                           ICON_MDI_ALERT
                           " Eviction is not supported on this backend\n(it does not report a GPU "
                           "memory budget). Paging is disabled.");
        return;
    }

    ImGui::Checkbox("Enabled", &state.enabled);
    ImGui::SetItemTooltipEx("When enabled, GPU resources are evicted each frame to keep memory\n"
                            "usage under the configured budget. Evicted resources restore\n"
                            "automatically the next time they are used.");

    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_DELETE_SWEEP " Force Evict All"))
    {
        ev::evict_all();
    }
    ImGui::SetItemTooltipEx("Test: evict every evictable resource immediately, ignoring budget and age.");
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_BACKUP_RESTORE " Restore All"))
    {
        ev::restore_all();
    }
    ImGui::SetItemTooltipEx("Test: restore every evicted resource immediately.");

    ImGui::Combo("Strategy", &state.strategy, strategy_names.data(), static_cast<int>(strategy_names.size()));
    ImGui::SetItemTooltipEx("Policy used to pick eviction victims when over budget.");

    ImGui::Checkbox("Auto budget (GPU memory)", &state.auto_budget);
    ImGui::SetItemTooltipEx("Derive the budget from the backend's reported GPU memory.\n"
                            "Falls back to the manual budget if the backend does not\n"
                            "report a GPU memory limit.");

    const auto* bx = gfx::get_stats();
    const std::uint64_t gpu_max = (bx != nullptr) ? clamp_u64(bx->gpuMemoryMax) : 0;
    const std::uint64_t gpu_used = (bx != nullptr) ? clamp_u64(bx->gpuMemoryUsed) : 0;

    if(state.auto_budget && gpu_max > 0)
    {
        ImGui::SliderFloat("Budget %", &state.budget_fraction, 0.1f, 1.0f, "%.2f");
        ImGui::SliderFloat("Target %", &state.target_fraction, 0.1f, 1.0f, "%.2f");
        state.target_fraction = std::min(state.target_fraction, state.budget_fraction);

        const auto soft = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * state.budget_fraction);
        const auto target = static_cast<std::uint64_t>(static_cast<double>(gpu_max) * state.target_fraction);
        const bool over = gpu_used > soft;
        std::array<char, 32> used_buf{};
        std::array<char, 32> max_buf{};
        std::array<char, 32> soft_buf{};
        std::array<char, 32> target_buf{};
        format_bytes_ex(gpu_used, used_buf.data(), used_buf.size());
        format_bytes_ex(gpu_max, max_buf.data(), max_buf.size());
        format_bytes_ex(soft, soft_buf.data(), soft_buf.size());
        format_bytes_ex(target, target_buf.data(), target_buf.size());
        ImGui::TextColored(over ? bad_text_color_ex : muted_text_color_ex,
                           "GPU: %s / %s   budget %s, target %s",
                           used_buf.data(),
                           max_buf.data(),
                           soft_buf.data(),
                           target_buf.data());
        if(over)
        {
            ImGui::SameLine();
            ImGui::TextColored(bad_text_color_ex, ICON_MDI_ALERT " OVER");
        }
    }
    else
    {
        ImGui::SliderInt("Manual budget (MiB)", &state.manual_budget_mb, 16, 8192);
        if(state.auto_budget)
        {
            ImGui::TextColored(warn_text_color_ex, "Backend reports no GPU memory limit; using manual budget.");
        }
    }

    ImGui::SliderInt("Min age (frames)", &state.min_age_frames, 0, 600);
    ImGui::SetItemTooltipEx("Resources used within this many frames are never evicted (anti-thrash).");
    ImGui::SliderInt("Max evictions / frame", &state.max_evictions, 0, 1024);
    ImGui::SetItemTooltipEx("Caps how many resources may be evicted per frame to bound the cost.\n"
                            "0 means unlimited.");

    ImGui::Separator();

    const auto stats = ev::get_stats();
    constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter;
    if(!ImGui::BeginTable("##eviction_stats", 2, table_flags))
    {
        return;
    }
    ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 140.0f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(category_text_color_ex, "Residency");

    draw_count_bytes_row("Resident", stats.resident_count, stats.resident_bytes);
    draw_count_bytes_row("Evicted", stats.evicted_count, stats.evicted_bytes);
    draw_count_row("Registered", stats.registered_count);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(category_text_color_ex, "Lifetime");

    draw_count_bytes_row("Evictions", stats.total_evictions, stats.total_bytes_evicted);
    draw_count_bytes_row("Restores", stats.total_restores, stats.total_bytes_restored);
    draw_count_row("Failed restores",
                   stats.failed_restores,
                   stats.failed_restores > 0 ? &warn_text_color_ex : nullptr);
    draw_count_row("Thrash events", stats.thrash_events, stats.thrash_events > 0 ? &warn_text_color_ex : nullptr);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(category_text_color_ex, "Last pass");

    draw_count_row("Scanned", stats.last_pass_scanned);
    draw_count_row("Evicted", stats.last_pass_evicted);
    draw_ms_row("Sweep time", stats.last_pass_ms);
    draw_ms_row("Last restore", stats.last_restore_ms);

    ImGui::EndTable();
}

} // namespace unravel
