#include "profiler_eviction_section.h"

#include "../panel_section.h"
#include "../panel_toolbar.h"
#include "../inspector_panel/inspectors/inspector.h"

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

    if(!panel_section::draw_header("##eviction_reserve", ICON_MDI_MEMORY, "Test: GPU Memory Reserve", false).is_open)
    {
        return;
    }

    static int reserve_mb = 1024;
    {
        property_layout layout("Reserve (MiB)",
                               "Test: hold raw, non-evictable GPU memory so the device runs near its\n"
                               "limit, and eviction can be exercised on a GPU with memory to spare.");
        ImGui::SliderInt("##reserve_mb", &reserve_mb, 64, 16384);
    }

    if(panel_toolbar::begin_strip("##reserve_actions", panel_toolbar::strip_style::flat))
    {
        if(panel_toolbar::button("##reserve",
                                 ICON_MDI_PLUS_BOX " Reserve",
                                 "Hold that much non-evictable GPU memory, on top of what is held."))
        {
            ev::debug_consume_memory(static_cast<std::uint64_t>(std::max(0, reserve_mb)) * 1024ull * 1024ull);
        }
        if(panel_toolbar::button("##release", ICON_MDI_DELETE " Release", "Free all of it and reclaim the VRAM now."))
        {
            ev::debug_release_memory();
        }
        panel_toolbar::separator();
        panel_toolbar::label(fmt::format("held {}", format_bytes(ev::debug_consumed_bytes())).c_str());
    }
    panel_toolbar::end_strip();

    if(gpu_max == 0)
    {
        return;
    }

    constexpr std::uint64_t one_gib = 1024ull * 1024ull * 1024ull;
    constexpr std::array<std::uint32_t, 7> presets{1, 2, 4, 6, 8, 12, 16};

    if(panel_toolbar::begin_strip("##simulate_budget", panel_toolbar::strip_style::flat))
    {
        panel_toolbar::label("Simulate a GPU of");
        for(const auto gb : presets)
        {
            if(static_cast<std::uint64_t>(gb) * one_gib >= gpu_max)
            {
                continue; // only simulate budgets smaller than the real one
            }
            const auto id = fmt::format("##simulate_{}", gb);
            const auto label = fmt::format("{} GB", gb);
            if(panel_toolbar::button(id.c_str(),
                                     label.c_str(),
                                     "Test: reserve real VRAM in one click so the device behaves like a\n"
                                     "smaller card, and the engine meets genuine out-of-memory behavior."))
            {
                ev::debug_simulate_budget(static_cast<std::uint64_t>(gb) * one_gib);
            }
        }
    }
    panel_toolbar::end_strip();
}

} // namespace

void profiler_draw_eviction_section(eviction_settings& state)
{
    if(!panel_section::draw_header("##eviction_section", ICON_MDI_SWAP_HORIZONTAL, "GPU Eviction / Paging", false).is_open)
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

    if(panel_toolbar::begin_strip("##eviction_actions", panel_toolbar::strip_style::flat))
    {
        if(panel_toolbar::toggle("##enabled",
                                 ICON_MDI_SWAP_HORIZONTAL " Paging",
                                 state.enabled,
                                 "Evict GPU resources each frame to keep memory under the budget.\n"
                                 "Evicted resources restore on their next use."))
        {
            state.enabled = !state.enabled;
        }
        panel_toolbar::separator();
        if(panel_toolbar::button("##evict_all",
                                 ICON_MDI_DELETE_SWEEP " Force Evict All",
                                 "Test: evict every evictable resource now, whatever the budget and age."))
        {
            ev::evict_all();
        }
        if(panel_toolbar::button("##restore_all",
                                 ICON_MDI_BACKUP_RESTORE " Restore All",
                                 "Test: restore every evicted resource now."))
        {
            ev::restore_all();
        }
    }
    panel_toolbar::end_strip();

    {
        property_layout layout("Strategy", "Policy that picks the victims once over budget.");
        ImGui::Combo("##strategy", &state.strategy, strategy_names.data(), static_cast<int>(strategy_names.size()));
    }
    {
        property_layout layout("Auto Budget",
                               "Derive the budget from the GPU memory the backend reports.\n"
                               "Falls back to the manual budget when it reports no limit.");
        ImGui::Checkbox("##auto_budget", &state.auto_budget);
    }

    const auto* bx = bgfx::getStats();
    const std::uint64_t gpu_max = (bx != nullptr) ? clamp_u64(bx->gpuMemoryMax) : 0;
    const std::uint64_t gpu_used = (bx != nullptr) ? clamp_u64(bx->gpuMemoryUsed) : 0;

    if(state.auto_budget && gpu_max > 0)
    {
        {
            property_layout layout("Budget %", "Start evicting once the GPU is this full.");
            ImGui::SliderFloat("##budget_fraction", &state.budget_fraction, 0.1f, 1.0f, "%.2f");
        }
        {
            property_layout layout("Target %", "Evict down to this, below the budget so it does not thrash.");
            ImGui::SliderFloat("##target_fraction", &state.target_fraction, 0.1f, 1.0f, "%.2f");
        }
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
        {
            property_layout layout("Manual Budget (MiB)", "Budget compared against the evictable resident bytes.");
            ImGui::SliderInt("##manual_budget", &state.manual_budget_mb, 16, 8192);
        }
        if(state.auto_budget)
        {
            ImGui::TextColored(warn_text_color_ex, "Backend reports no GPU memory limit; using manual budget.");
        }
    }

    {
        property_layout layout("Min Age (frames)", "Resources used this recently are never evicted (anti-thrash).");
        ImGui::SliderInt("##min_age", &state.min_age_frames, 0, 600);
    }
    {
        property_layout layout("Max Evictions / Frame", "Caps the cost of one pass. 0 means unlimited.");
        ImGui::SliderInt("##max_evictions", &state.max_evictions, 0, 1024);
    }

    draw_reserve_test_controls(gpu_max);

    const auto stats = ev::get_stats();
    constexpr ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_PadOuterX;
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
