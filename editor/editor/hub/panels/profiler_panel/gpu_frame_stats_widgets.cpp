#include "gpu_frame_stats_widgets.h"

#include "profiler_statistics_utils.h"
#include <bx/bx.h>
#include <imgui/imgui.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace unravel
{
namespace
{
constexpr float profiler_scale = 3.0f;
constexpr float profiler_max_width = 30.0f;
constexpr float render_pass_table_max_height = 520.0f;
constexpr float render_pass_table_min_height = 180.0f;
constexpr ImVec4 cpu_color{0.2f, 0.8f, 0.2f, 1.0f};
constexpr ImVec4 gpu_color{0.2f, 0.6f, 1.0f, 1.0f};
constexpr ImVec4 warning_color{1.0f, 0.7f, 0.0f, 1.0f};

struct render_pass_entry
{
    uint16_t view = 0;
    uint16_t index = 0;
    std::string name;
    std::string display_name;
    float cpu_ms = 0.0f;
    float gpu_ms = 0.0f;
};

struct render_pass_node_item
{
    bool is_child = false;
    size_t index = 0;
};

struct timer_scale
{
    double cpu_to_ms = 0.0;
    double gpu_to_ms = 0.0;
};

struct widget_layout
{
    float item_height = 0.0f;
    float item_height_with_spacing = 0.0f;
};

struct render_pass_totals
{
    float cpu_ms = 0.0f;
    float gpu_ms = 0.0f;
};

struct render_pass_node
{
    std::string name;
    std::vector<render_pass_node> children;
    std::vector<render_pass_entry> entries;
    std::vector<render_pass_node_item> items;
    float cpu_ms = 0.0f;
    float gpu_ms = 0.0f;
    size_t pass_count = 0;
};

auto to_lower_copy(const std::string& value) -> std::string
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return result;
}

auto contains_case_insensitive(const std::string& value, const char* filter) -> bool
{
    if(filter == nullptr || filter[0] == '\0')
    {
        return true;
    }
    const std::string lower_value = to_lower_copy(value);
    const std::string lower_filter = to_lower_copy(filter);
    return lower_value.find(lower_filter) != std::string::npos;
}

auto trim_copy(const std::string& value) -> std::string
{
    const auto first = std::find_if(value.begin(), value.end(),
                                    [](unsigned char c) -> bool { return std::isspace(c) == 0; });
    const auto last = std::find_if(value.rbegin(), value.rend(),
                                   [](unsigned char c) -> bool { return std::isspace(c) == 0; }).base();
    if(first >= last)
    {
        return {};
    }
    return std::string(first, last);
}

auto split_render_pass_path(const std::string& pass_name, bool group_by_prefix) -> std::vector<std::string>
{
    std::vector<std::string> parts;
    if(!group_by_prefix)
    {
        parts.push_back(pass_name);
        return parts;
    }
    size_t start = 0;
    while(start <= pass_name.size())
    {
        const size_t separator_pos = pass_name.find('/', start);
        const size_t end = separator_pos == std::string::npos ? pass_name.size() : separator_pos;
        std::string part = trim_copy(pass_name.substr(start, end - start));
        if(!part.empty())
        {
            parts.push_back(std::move(part));
        }
        if(separator_pos == std::string::npos)
        {
            break;
        }
        start = separator_pos + 1;
    }
    if(parts.empty())
    {
        parts.push_back(pass_name);
    }
    return parts;
}

auto make_render_pass_entries(const gfx::stats* stats, const timer_scale& scale, bool group_by_prefix)
    -> std::vector<render_pass_entry>
{
    std::vector<render_pass_entry> entries;
    entries.reserve(stats->numViews);
    for(uint16_t pos = 0; pos < stats->numViews; ++pos)
    {
        const auto& view_stats = stats->viewStats[pos];
        render_pass_entry entry;
        entry.view = view_stats.view;
        entry.index = pos;
        entry.name = view_stats.name;
        const std::vector<std::string> parts = split_render_pass_path(entry.name, group_by_prefix);
        entry.display_name = parts.empty() ? entry.name : parts.back();
        entry.cpu_ms =
            static_cast<float>(static_cast<double>(view_stats.cpuTimeEnd - view_stats.cpuTimeBegin) * scale.cpu_to_ms);
        entry.gpu_ms =
            static_cast<float>(static_cast<double>(view_stats.gpuTimeEnd - view_stats.gpuTimeBegin) * scale.gpu_to_ms);
        entries.push_back(std::move(entry));
    }
    return entries;
}

void add_render_pass_entry(render_pass_node& node,
                           const std::vector<std::string>& path,
                           size_t path_index,
                           const render_pass_entry& entry)
{
    node.cpu_ms += entry.cpu_ms;
    node.gpu_ms += entry.gpu_ms;
    node.pass_count++;
    if(path_index >= path.size())
    {
        node.entries.push_back(entry);
        node.items.push_back({false, node.entries.size() - 1});
        return;
    }
    const std::string& child_name = path[path_index];
    if(node.children.empty() || node.children.back().name != child_name)
    {
        render_pass_node child;
        child.name = child_name;
        node.children.push_back(std::move(child));
        node.items.push_back({true, node.children.size() - 1});
    }
    add_render_pass_entry(node.children.back(), path, path_index + 1, entry);
}

auto extract_single_pass_entry(const render_pass_node& node, const std::string& prefix) -> render_pass_entry
{
    const std::string display_prefix = prefix.empty() ? node.name : prefix + "/" + node.name;
    if(!node.entries.empty())
    {
        render_pass_entry entry = node.entries.front();
        entry.display_name = display_prefix + "/" + entry.display_name;
        return entry;
    }
    return extract_single_pass_entry(node.children.front(), display_prefix);
}

void collapse_single_pass_children(render_pass_node& node)
{
    for(render_pass_node& child : node.children)
    {
        collapse_single_pass_children(child);
    }
    for(render_pass_node_item& item : node.items)
    {
        if(!item.is_child)
        {
            continue;
        }
        const render_pass_node& child = node.children[item.index];
        if(child.pass_count > 1)
        {
            continue;
        }
        node.entries.push_back(extract_single_pass_entry(child, {}));
        item.is_child = false;
        item.index = node.entries.size() - 1;
    }
}

auto make_filtered_render_pass_groups(const std::vector<render_pass_entry>& entries, const char* filter, bool group_by_prefix)
    -> std::vector<render_pass_node>
{
    std::vector<render_pass_node> groups;
    for(const render_pass_entry& entry : entries)
    {
        const std::vector<std::string> parts = split_render_pass_path(entry.name, group_by_prefix);
        const std::string& top_level_name = parts.front();
        const bool group_matches = contains_case_insensitive(top_level_name, filter);
        const bool pass_matches = contains_case_insensitive(entry.name, filter);
        if(!group_matches && !pass_matches)
        {
            continue;
        }
        if(groups.empty() || groups.back().name != top_level_name)
        {
            render_pass_node group;
            group.name = top_level_name;
            groups.push_back(std::move(group));
        }
        std::vector<std::string> child_path;
        if(parts.size() > 2)
        {
            child_path.assign(parts.begin() + 1, parts.end() - 1);
        }
        add_render_pass_entry(groups.back(), child_path, 0, entry);
    }
    for(render_pass_node& group : groups)
    {
        collapse_single_pass_children(group);
    }
    return groups;
}

void draw_timing_bar(float value_ms, float max_ms, const ImVec4& color, const char* tooltip)
{
    const float bar_fraction = max_ms > 0.0f ? std::clamp(value_ms / max_ms, 0.0f, 1.0f) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(bar_fraction, ImVec2(-1.0f, ImGui::GetFrameHeight() * 0.72f), "");
    ImGui::PopStyleColor();
    if(ImGui::IsItemHovered())
    {
        ImGui::SetItemTooltipEx("%s: %.3f ms", tooltip, value_ms);
    }
}

auto percent_of(float value, float total) -> float
{
    return total > 0.0f ? (value / total) * 100.0f : 0.0f;
}

void draw_pass_row(const render_pass_entry& entry, const render_pass_totals& totals, float max_bar_ms)
{
    ImGui::PushID(entry.index);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TreeNodeEx("pass", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_SpanFullWidth,
                      "%3u. %s", entry.view, entry.display_name.c_str());
    ImGui::SetItemTooltipEx("View %u\n%s", entry.view, entry.name.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%.3f ms", entry.cpu_ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", percent_of(entry.cpu_ms, totals.cpu_ms));
    ImGui::TableNextColumn();
    draw_timing_bar(entry.cpu_ms, max_bar_ms, cpu_color, "CPU submit");
    ImGui::TableNextColumn();
    ImGui::Text("%.3f ms", entry.gpu_ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", percent_of(entry.gpu_ms, totals.gpu_ms));
    ImGui::TableNextColumn();
    draw_timing_bar(entry.gpu_ms, max_bar_ms, gpu_color, "GPU execute");
    ImGui::PopID();
}

void draw_render_pass_node(const render_pass_node& node, const render_pass_totals& totals, float max_bar_ms)
{
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen |
                                     ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    const bool is_open = ImGui::TreeNodeEx("node", flags, "%s  (%zu passes)", node.name.c_str(), node.pass_count);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f ms", node.cpu_ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", percent_of(node.cpu_ms, totals.cpu_ms));
    ImGui::TableNextColumn();
    draw_timing_bar(node.cpu_ms, max_bar_ms, cpu_color, "Group CPU submit");
    ImGui::TableNextColumn();
    ImGui::Text("%.3f ms", node.gpu_ms);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f%%", percent_of(node.gpu_ms, totals.gpu_ms));
    ImGui::TableNextColumn();
    draw_timing_bar(node.gpu_ms, max_bar_ms, gpu_color, "Group GPU execute");
    if(!is_open)
    {
        return;
    }
    for(const render_pass_node_item& item : node.items)
    {
        if(item.is_child)
        {
            ImGui::PushID(static_cast<int>(item.index));
            draw_render_pass_node(node.children[item.index], totals, max_bar_ms);
            ImGui::PopID();
        }
        else
        {
            draw_pass_row(node.entries[item.index], totals, max_bar_ms);
        }
    }
    ImGui::TreePop();
}

void draw_encoder_stats(const gfx::stats* stats, const widget_layout& layout, const timer_scale& scale)
{
    if(ImGui::BeginListBox("Encoders##GpuProfiler",
                           ImVec2(ImGui::GetWindowWidth(),
                                  static_cast<float>(stats->numEncoders) * layout.item_height_with_spacing)))
    {
        ImGuiListClipper clipper;
        clipper.Begin(stats->numEncoders, layout.item_height);

        while(clipper.Step())
        {
            for(int32_t pos = clipper.DisplayStart; pos < clipper.DisplayEnd; ++pos)
            {
                const auto& encoder_stats = stats->encoderStats[pos];
                ImGui::PushID(pos);
                ImGui::Text("%3d", pos);
                ImGui::SameLine(64.0f);

                const float max_width = profiler_max_width * profiler_scale;
                const float cpu_ms =
                    static_cast<float>(static_cast<double>(encoder_stats.cpuTimeEnd - encoder_stats.cpuTimeBegin) *
                                       scale.cpu_to_ms);
                const float cpu_width = bx::clamp(cpu_ms * profiler_scale, 1.0f, max_width);

                if(profiler_statistics_utils::draw_progress_bar(cpu_width, max_width, layout.item_height, cpu_color))
                {
                    ImGui::SetItemTooltipEx(
                        "Encoder %d\nCPU submit (render thread): %.3f ms",
                        pos,
                        cpu_ms);
                }

                ImGui::PopID();
            }
        }
        ImGui::EndListBox();
    }
}

void draw_view_stats(const gfx::stats* stats, const widget_layout& layout, const timer_scale& scale)
{
    (void)layout;
    static std::array<char, 128> filter = {};
    static bool group_by_prefix = true;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Render Passes");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##render_pass_filter", "Filter pass or group", filter.data(), filter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Group by prefix", &group_by_prefix);
    const std::vector<render_pass_entry> entries = make_render_pass_entries(stats, scale, group_by_prefix);
    std::vector<render_pass_node> groups = make_filtered_render_pass_groups(entries, filter.data(), group_by_prefix);
    render_pass_totals totals;
    float max_bar_ms = 0.0001f;
    size_t visible_pass_count = 0;
    for(const render_pass_node& group : groups)
    {
        totals.cpu_ms += group.cpu_ms;
        totals.gpu_ms += group.gpu_ms;
        max_bar_ms = std::max(max_bar_ms, std::max(group.cpu_ms, group.gpu_ms));
        visible_pass_count += group.pass_count;
    }
    ImGui::TextDisabled("Visible cost: CPU %8.3f ms | GPU %8.3f ms | %3zu passes in %3zu groups",
                        totals.cpu_ms,
                        totals.gpu_ms,
                        visible_pass_count,
                        groups.size());
    ImGui::SameLine();
    ImGui::TextColored(cpu_color, "CPU submit");
    ImGui::SameLine();
    ImGui::TextUnformatted("/");
    ImGui::SameLine();
    ImGui::TextColored(gpu_color, "GPU execute");
    if(groups.empty())
    {
        ImGui::TextColored(warning_color, "No render passes match the current filter.");
        return;
    }
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float table_h = std::max(render_pass_table_min_height,
                                   std::min(render_pass_table_max_height, std::max(240.0f, avail_h)));
    constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH |
                                            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable;
    if(ImGui::BeginTable("##render_pass_groups", 7, table_flags, ImVec2(-1.0f, table_h)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pass / Group", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("CPU Time", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("CPU Bar", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("GPU %", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("GPU Bar", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();
        for(size_t group_index = 0; group_index < groups.size(); ++group_index)
        {
            const render_pass_node& group = groups[group_index];
            ImGui::PushID(static_cast<int>(group_index));
            draw_render_pass_node(group, totals, max_bar_ms);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

} // namespace

void draw_gpu_submit_profiler_ui(const gfx::stats* stats, bool* enable_profiler)
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("View/encoder timing:");
    ImGui::SameLine();
    if(enable_profiler == nullptr)
    {
        return;
    }
    if(ImGui::Checkbox("Enable##GpuProfiler", enable_profiler))
    {
        gfx::set_debug(*enable_profiler ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
    }
    if(!*enable_profiler)
    {
        ImGui::TextColored(warning_color, "Enable to record per-view CPU submit and GPU execute times.");
        return;
    }

    if(!stats)
    {
        ImGui::TextColored(warning_color, "No stats.");
        return;
    }

    if(stats->numViews == 0)
    {
        ImGui::TextColored(warning_color, "No GPU profiling data yet (initializing or no views).");
        return;
    }

    const widget_layout layout{ImGui::GetTextLineHeightWithSpacing(), ImGui::GetFrameHeightWithSpacing()};
    const timer_scale scale{1000.0 / static_cast<double>(stats->cpuTimerFreq),
                            1000.0 / static_cast<double>(stats->gpuTimerFreq)};

    draw_encoder_stats(stats, layout, scale);

    ImGui::Separator();

    draw_view_stats(stats, layout, scale);
}

} // namespace unravel
