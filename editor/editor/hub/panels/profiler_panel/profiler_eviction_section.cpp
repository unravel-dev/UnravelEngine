#include "profiler_eviction_section.h"

#include <editor/format/format_bytes.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <graphics/eviction.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>
#include <imgui_widgets/tooltips.h>

#include <logging/logging.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

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
    const auto value = fmt::format("{} ({})", count, format_bytes(bytes));
    draw_stat_row(label, value.c_str(), color);
}

void draw_count_row(const char* label, std::uint64_t count, const ImVec4* color = nullptr)
{
    const auto value = fmt::format("{}", count);
    draw_stat_row(label, value.c_str(), color);
}

void draw_ms_row(const char* label, double ms)
{
    const auto value = fmt::format("{:.3f} ms", ms);
    draw_stat_row(label, value.c_str());
}

void draw_reserve_test_controls(std::uint64_t gpu_max)
{
    namespace ev = gfx::eviction;

    ImGui::TextColored(category_text_color_ex, ICON_MDI_MEMORY " Test: GPU memory reserve");

    static int reserve_mb = 1024;
    ImGui::SliderInt("Reserve (MiB)", &reserve_mb, 64, 16384);
    ImGui::SetItemTooltipEx("Test: reserve raw, non-evictable GPU memory to push device occupancy\n"
                            "toward the limit, so eviction and near-the-limit allocations (e.g. a\n"
                            "large frame buffer) can be exercised on GPUs with plenty of memory.");

    if(ImGui::SmallButton(ICON_MDI_PLUS_BOX " Reserve"))
    {
        ev::debug_consume_memory(static_cast<std::uint64_t>(std::max(0, reserve_mb)) * 1024ull * 1024ull);
    }
    ImGui::SetItemTooltipEx("Reserve the above amount of non-evictable GPU memory (additive).");
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_DELETE " Release"))
    {
        ev::debug_release_memory();
    }
    ImGui::SetItemTooltipEx("Free all reserved memory and reclaim its VRAM immediately.");
    ImGui::SameLine();
    ImGui::TextUnformatted(fmt::format("held: {}", format_bytes(ev::debug_consumed_bytes())).c_str());

    if(gpu_max == 0)
    {
        return;
    }

    constexpr std::uint64_t one_gib = 1024ull * 1024ull * 1024ull;
    constexpr std::array<std::uint32_t, 7> presets{1, 2, 4, 6, 8, 12, 16};

    ImGui::TextUnformatted("Simulate as if GPU had:");
    ImGui::SetItemTooltipEx("Test: reserve real VRAM in one click so the device behaves like a\n"
                            "smaller card. Each preset reserves (GPU max - target) of memory, so\n"
                            "the engine hits genuine out-of-memory behavior at the chosen limit.\n"
                            "Only presets smaller than this GPU are shown.");
    for(const auto gb : presets)
    {
        if(static_cast<std::uint64_t>(gb) * one_gib >= gpu_max)
        {
            continue; // only simulate budgets smaller than the real one
        }
        const auto label = fmt::format("{} GB", gb);
        ImGui::SameLine();
        if(ImGui::SmallButton(label.c_str()))
        {
            ev::debug_simulate_budget(static_cast<std::uint64_t>(gb) * one_gib);
        }
    }
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
        const auto gpu_line = fmt::format("GPU: {} / {}   budget {}, target {}",
                                          format_bytes(gpu_used),
                                          format_bytes(gpu_max),
                                          format_bytes(soft),
                                          format_bytes(target));
        ImGui::TextColored(over ? bad_text_color_ex : muted_text_color_ex, "%s", gpu_line.c_str());
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
    draw_reserve_test_controls(gpu_max);

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
