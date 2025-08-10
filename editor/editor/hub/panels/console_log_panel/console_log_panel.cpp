#include "console_log_panel.h"
#include "../panels_defs.h"

#include <editor/editing/editor_actions.h>
#include <editor/system/project_manager.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/engine.h>


#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/utils.h>

#include <filesystem/filesystem.h>
#include <map>

namespace unravel
{
static const std::array<ImColor, size_t(level::n_levels)> colors{ImColor{255, 255, 255},
                                                                 ImColor{255, 255, 255},
                                                                 ImColor{255, 255, 255},
                                                                 ImColor{255, 255, 0},
                                                                 ImColor{255, 0, 0},
                                                                 ImColor{180, 0, 0},
                                                                 ImColor{255, 255, 255}};

static const std::array<const char*, size_t(level::n_levels)> icons{ICON_MDI_ALERT_CIRCLE_CHECK,
                                                                    ICON_MDI_BUG_CHECK_OUTLINE,
                                                                    ICON_MDI_ALERT_CIRCLE,
                                                                    ICON_MDI_ALERT_BOX,
                                                                    ICON_MDI_ALERT_OCTAGON,
                                                                    ICON_MDI_ALERT_OCTAGON,
                                                                    ICON_MDI_ALERT_CIRCLE};

static const std::array<const char*, size_t(level::n_levels)>
    levels{"Trace", "Debug", "Info", "Warning", "Error", "Critical", ""};

auto extract_lines(hpp::string_view text, int num_lines, int& found_lines) -> hpp::string_view
{
    auto pos = hpp::string_view::size_type{0};
    found_lines = 1;
    for(int i = 0; i < num_lines; ++i)
    {
        pos = text.find('\n', pos);
        if(pos == hpp::string_view::npos)
        {
            break;
        }
        ++pos;
        found_lines = i + 1;
    }
    return text.substr(0, pos);
}

void open_log_in_environment(const fs::path& entry, int line)
{
    if(ex::is_format<script>(entry.extension().string()))
    {
        editor_actions::open_workspace_on_file(entry, line);
    }
    else
    {
        // fs::show_in_graphical_env(entry);
    }
}

console_log_panel::console_log_panel()
{
    set_pattern("[%H:%M:%S] %v");

    enabled_categories_.fill(true);
    enabled_categories_[level::trace] = false;
    enabled_categories_[level::debug] = false;
}

void console_log_panel::sink_it_(const details::log_msg& msg)
{
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);

        auto log_msg = msg;
        log_msg.color_range_start = 0;
        log_msg.color_range_end = 0;
        log_msg.source = {};
        memory_buf_t formatted;
        formatter_->format(log_msg, formatted);
        log_entry entry;
        entry.formatted.resize(formatted.size());
        std::memcpy(entry.formatted.data(), formatted.data(), formatted.size() * sizeof(char));

        entry.source.filename = msg.source.filename;
        entry.source.funcname = msg.source.funcname;
        entry.source.line = msg.source.line;

        entry.level = msg.level;

        entry.id = current_id_++;

        if(new_entries_begin_idx_ == -1)
        {
            new_entries_begin_idx_ = int64_t(entries_.size());
        }
        entries_.emplace_back(std::move(entry));
    }
    has_new_entries_ = true;
}

void console_log_panel::flush_()
{
}

void console_log_panel::clear_log()
{
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        entries_.clear();
        selected_log_ = {};
    }
    has_new_entries_ = false;
}

auto console_log_panel::has_new_entries() const -> bool
{
    return has_new_entries_;
}

void console_log_panel::set_has_new_entries(bool val)
{
    has_new_entries_ = val;
}

void console_log_panel::draw_range(const hpp::string_view& formatted, size_t start, size_t end)
{
    if(end > start)
    {
        auto end_clamped = std::min(end, formatted.size());
        auto size = (end_clamped - start);
        auto text_start = formatted.data() + start;
        auto text_end = text_start + size;
        ImGui::TextUnformatted(text_start, text_end);
    }
}

void console_log_panel::draw_filter_button(level::level_enum level)
{
    auto c = colors[level].Value;
    auto multiplier = enabled_categories_[level] ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 0.8f);
    ImGui::PushStyleColor(ImGuiCol_Text, c * multiplier);
    if(ImGui::MenuItem(icons[level], nullptr, enabled_categories_[level]))
    {
        enabled_categories_[level] = !enabled_categories_[level];
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltipEx("%s", fmt::format("Enables/Disables {} logs.", levels[level]).c_str());
}

auto console_log_panel::draw_log(const log_entry& msg, int num_lines) -> bool
{
    ImGui::BeginGroup();
    auto col = colors[size_t(msg.level)];
    auto icon = icons[size_t(msg.level)];
    auto level = levels[size_t(msg.level)];

    ImGui::PushStyleColor(ImGuiCol_Text, col.Value);
    ImGui::AlignTextToFramePadding();

    int found_lines = 1;
    auto view = extract_lines({msg.formatted.data(), msg.formatted.size()}, 1, found_lines);
    ImGui::PushWindowFontSize(ImGui::GetFontSize() * num_lines);
    ImGui::TextUnformatted(icon);
    ImGui::PopWindowFontSize();
    ImGui::SameLine();
    ImGui::BeginGroup();

    draw_range(view, 0, view.size());
    if(found_lines != num_lines)
    {
        ImGui::TextUnformatted(level);
    }

    ImGui::EndGroup();

    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * num_lines});
    ImGui::EndGroup();

    bool clicked = ImGui::IsItemClicked();
    if(clicked)
    {
        select_log(msg);
    }

    if(ImGui::IsItemDoubleClicked())
    {
        open_log(msg);
    }
    return clicked;
}

void console_log_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    name_ = name;
    if(ImGui::Begin(name, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));
        draw();
    }
    ImGui::End();
}

void console_log_panel::draw()
{
    auto avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f)
    {
        return;
    }

    if(ImGui::BeginMenuBar())
    {
        ImGui::DrawFilterWithHint(filter_, ICON_MDI_TEXT_BOX_SEARCH " Search...", 200.0f);
        ImGui::DrawItemActivityOutline();
        ImGui::SameLine();
        if(ImGui::MenuItem("Clear"))
        {
            clear_log();
        }
        ImGui::SameLine();
        if(ImGui::BeginMenu(ICON_MDI_ARROW_DOWN_BOLD, true))
        {
            if(ImGui::MenuItem("Clear on Play", nullptr, clear_on_play_))
            {
                clear_on_play_ = !clear_on_play_;
            }
            if(ImGui::MenuItem("Clear on Recompile", nullptr, clear_on_recompile_))
            {
                clear_on_recompile_ = !clear_on_recompile_;
            }
            ImGui::EndMenu();
        }

        draw_filter_button(level::err);
        draw_filter_button(level::warn);
        draw_filter_button(level::info);
        draw_filter_button(level::trace);
        draw_filter_button(level::debug);

        ImGui::EndMenuBar();
    }

    avail = ImGui::GetContentRegionAvail();

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 100.0f), ImVec2(FLT_MAX, FLT_MAX));
    // ImGui::SetNextWindowSizeConstraints({-1.0f, 100.0f}, {-1.0f, -1.0f});
    // Display every line as a separate entry so we can change their color or add custom widgets. If you only
    // want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
    // NB- if you have thousands of entries this approach may be too inefficient. You can seek and display
    // only the lines that are visible - CalcListClipping() is a helper to compute this information.
    // If your items are of variable size you may want to implement code similar to what CalcListClipping()
    // does. Or split your data into fixed height items to allow random-seeking into your list.

    ImGui::BeginChild("ScrollingRegion", avail * ImVec2(1.0f, 0.8f), ImGuiChildFlags_ResizeY);
    if(ImGui::BeginPopupContextWindowEx())
    {
        if(ImGui::Selectable("Clear"))
            clear_log();
        ImGui::EndPopup();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing

    display_entries_t entries;
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        for(size_t i = 0; i < entries_.size(); ++i)
        {
            const auto& msg = entries_[i];

            if(!enabled_categories_[msg.level])
                continue;

            if(!filter_.PassFilter(msg.formatted.data(), msg.formatted.data() + msg.formatted.size()))
                continue;

            entries.emplace_back(msg);
        }
    }


    ImGuiListClipper clipper;
    clipper.Begin(int(entries.size()));
    while(clipper.Step())
    {
        for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
        {
            const auto& msg = entries[i];

            if(selected_log_)
            {
                const auto& selected = *selected_log_;
                if(selected.id == msg.id)
                {
                    auto min = ImGui::GetCursorScreenPos();
                    auto max = min + ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 2);
                    ImGui::RenderFrame(min, max, ImColor(80, 80, 0));
                }
            }

            draw_log(msg, 2);
        }
    }
    

    if(has_new_entries() && ImGui::GetScrollY() > (ImGui::GetScrollMaxY() - 0.01f))
        ImGui::SetScrollHereY();

    set_has_new_entries(false);

    ImGui::PopStyleVar();
    ImGui::EndChild();

    // ImGui::SetNextWindowSizeConstraints({-1.0f, 100.0f}, {-1.0f, -1.0f});
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 100.0f), ImVec2(FLT_MAX, FLT_MAX));
    avail = ImGui::GetContentRegionAvail();
    avail.y = ImMax(avail.y, 100.0f);
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::BeginChild("DetailsArea", avail, ImGuiChildFlags_None);

    draw_details();
    ImGui::EndChild();
}

auto console_log_panel::draw_last_log() -> bool
{
    log_entry msg;

    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        for(auto it = std::rbegin(entries_); it != std::rend(entries_); ++it)
        {
            const auto& check = *it;
            if(!enabled_categories_[check.level])
            {
                continue;
            }

            if(!filter_.PassFilter(check.formatted.data(), check.formatted.data() + check.formatted.size()))
            {
                continue;
            }
            msg = check;
            break;
        }
    }

    if(msg.formatted.empty())
    {
        return false;
    }

    draw_log(msg, 1);

    return true;
}

void console_log_panel::draw_last_log_button()
{
    auto pos = ImGui::GetCursorPos();

    if(draw_last_log())
    {
        ImGui::SetCursorPos(pos);

        if(ImGui::InvisibleButton("shortcut", ImGui::GetItemRectSize()))
        {
            ImGui::FocusWindow(ImGui::FindWindowByName(name_.c_str()));
        }
    }

    uint64_t errors_count = 0;
    if(new_entries_begin_idx_ != -1)
    {
        std::lock_guard<std::recursive_mutex> lock(entries_mutex_);
        for(size_t i = new_entries_begin_idx_; i < entries_.size(); ++i)
        {
            const auto& msg = entries_[i];
            if(msg.level == level::err)
            {
                errors_count++;
            }
        }

        new_entries_begin_idx_ = -1;
    }

    if(errors_count > 0)
    {
        ImGui::PushNotification({ ImGuiToastType_Error, 2000, fmt::format("{} Error(s)...", errors_count).c_str() });
    }
}

void console_log_panel::draw_details()
{
    std::lock_guard<std::recursive_mutex> lock(entries_mutex_);

    if(selected_log_)
    {
        const auto& msg = *selected_log_;
        string_view_t str(msg.formatted.data(), msg.formatted.size());
        auto desc = fmt::format("{0}{1}() (at [{2}:{3}]({2}:{3}))",
                                str,
                                msg.source.funcname,
                                msg.source.filename,
                                msg.source.line);

        ImGui::MarkdownConfig config{};
        config.linkCallback = [&](const char* link, uint32_t link_length)
        {
            open_log_in_environment(fs::path(msg.source.filename), msg.source.line);
        };
        ImGui::Markdown(desc.data(), int32_t(desc.size()), config);
    }
}

void console_log_panel::select_log(const log_entry& entry)
{
    selected_log_ = entry;
}

void console_log_panel::open_log(const log_entry& entry)
{
    open_log_in_environment(entry.source.filename, entry.source.line);
}

void console_log_panel::on_play()
{
    if(clear_on_play_)
    {
        clear_log();
    }
}

void console_log_panel::on_recompile()
{
    if(clear_on_recompile_)
    {
        clear_log();
    }
}

} // namespace unravel
