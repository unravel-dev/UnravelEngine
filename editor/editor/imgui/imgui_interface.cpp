#include "imgui_interface.h"
#include "editor/format/format_bytes.h"
#include "imgui/imgui.h"
#include "imgui_widgets/utils.h"
#include "integration/imgui_style.h"
#include <editor/events.h>
#include <engine/profiler/profiler.h>

#include <base/platform/process_memory.hpp>
#include <engine/events.h>
#include <engine/rendering/renderer.h>
#include <graphics/graphics.h>

#include <logging/logging.h>

#include <algorithm>
#include <string>

namespace unravel
{

namespace
{

constexpr ImVec4 memory_label_color{0.42f, 0.42f, 0.42f, 1.0f};
constexpr ImVec4 memory_value_color{0.50f, 0.50f, 0.50f, 1.0f};
constexpr ImU32 gpu_bar_color = IM_COL32(58, 121, 187, 165);
constexpr ImU32 other_system_ram_bar_color = IM_COL32(145, 118, 72, 210);
constexpr ImU32 process_ram_bar_color = IM_COL32(88, 168, 196, 220);
constexpr ImU32 memory_bar_track_color = IM_COL32(32, 32, 32, 255);
constexpr float memory_bar_height = 3.0f;
constexpr float memory_section_alpha = 0.92f;

void draw_memory_bar(float fraction, ImU32 fill_color)
{
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float bar_width = ImGui::GetContentRegionAvail().x;
    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(bar_pos,
                             ImVec2(bar_pos.x + bar_width, bar_pos.y + memory_bar_height),
                             memory_bar_track_color,
                             memory_bar_height * 0.5f);

    if(fraction > 0.0f)
    {
        draw_list->AddRectFilled(bar_pos,
                                 ImVec2(bar_pos.x + bar_width * fraction, bar_pos.y + memory_bar_height),
                                 fill_color,
                                 memory_bar_height * 0.5f);
    }

    ImGui::Dummy(ImVec2(bar_width, memory_bar_height));
}

void draw_stacked_ram_bar(float process_fraction, float system_used_fraction)
{
    process_fraction = std::clamp(process_fraction, 0.0f, 1.0f);
    system_used_fraction = std::clamp(system_used_fraction, process_fraction, 1.0f);
    const float other_used_fraction = system_used_fraction - process_fraction;

    const float bar_width = ImGui::GetContentRegionAvail().x;
    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float bar_radius = memory_bar_height * 0.5f;
    const float bar_bottom = bar_pos.y + memory_bar_height;
    const ImVec2 bar_max(bar_pos.x + bar_width, bar_bottom);

    // Full width = total system RAM. Dark track = free memory.
    draw_list->AddRectFilled(bar_pos, bar_max, memory_bar_track_color, bar_radius);

    float x = bar_pos.x;

    auto draw_segment = [&](float fraction, ImU32 color) -> void
    {
        if(fraction <= 0.0f)
        {
            return;
        }

        float segment_width = bar_width * fraction;
        if(segment_width > 0.0f && segment_width < 1.0f)
        {
            segment_width = 1.0f;
        }

        draw_list->AddRectFilled(ImVec2(x, bar_pos.y),
                                 ImVec2(x + segment_width, bar_bottom),
                                 color);
        x += segment_width;
    };

    // Stacked from the left: [other used][process][free]
    draw_segment(other_used_fraction, other_system_ram_bar_color);
    draw_segment(process_fraction, process_ram_bar_color);

    ImGui::Dummy(ImVec2(bar_width, memory_bar_height));
}

void draw_memory_stat_row(const char* label, const std::string& value_text, const char* tooltip = nullptr)
{
    ImGui::PushStyleColor(ImGuiCol_Text, memory_label_color);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }

    const float value_width = ImGui::CalcTextSize(value_text.c_str()).x;
    ImGui::PushStyleColor(ImGuiCol_Text, memory_value_color);
    ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, value_width, [&]() -> void {
        ImGui::TextUnformatted(value_text.c_str());
    });
    ImGui::PopStyleColor();
}

void draw_loading_memory_stats()
{
    const auto* stats = gfx::get_stats();
    if(stats != nullptr && stats->gpuMemoryUsed > 0)
    {
        std::string gpu_text;
        float gpu_fraction = 0.0f;

        if(stats->gpuMemoryMax > 0)
        {
            const float pct = (static_cast<float>(stats->gpuMemoryUsed) /
                               static_cast<float>(stats->gpuMemoryMax)) *
                              100.0f;
            gpu_text = fmt::format("{} / {} ({:.0f}%)",
                                   format_bytes(stats->gpuMemoryUsed),
                                   format_bytes(stats->gpuMemoryMax),
                                   static_cast<double>(pct));
            gpu_fraction = pct / 100.0f;
        }
        else
        {
            gpu_text = format_bytes(stats->gpuMemoryUsed);
        }

        draw_memory_stat_row("GPU",
                             gpu_text,
                             "GPU video memory allocated by the renderer vs the device budget.");
        if(stats->gpuMemoryMax > 0)
        {
            draw_memory_bar(gpu_fraction, gpu_bar_color);
        }
    }

    const int64_t rss_bytes = platform::get_process_resident_set_bytes();
    const int64_t system_total_bytes = platform::get_system_physical_memory_bytes();
    const int64_t system_used_bytes = platform::get_system_used_physical_memory_bytes();
    if(rss_bytes > 0 || system_used_bytes > 0)
    {
        if(stats != nullptr && stats->gpuMemoryUsed > 0)
        {
            ImGui::Spacing();
        }

        if(system_total_bytes > 0 && system_used_bytes > 0)
        {
            const int64_t other_used_bytes = std::max<int64_t>(0, system_used_bytes - rss_bytes);
            const float system_pct =
                (static_cast<float>(system_used_bytes) / static_cast<float>(system_total_bytes)) * 100.0f;
            const float process_pct =
                (static_cast<float>(rss_bytes) / static_cast<float>(system_total_bytes)) * 100.0f;

            std::string ram_text;
            if(rss_bytes > 0)
            {
                ram_text = fmt::format("{} ({} Process) / {}",
                                       format_bytes(system_used_bytes),
                                       format_bytes(rss_bytes),
                                       format_bytes(system_total_bytes));
            }
            else
            {
                ram_text = fmt::format("{} / {}",
                                       format_bytes(system_used_bytes),
                                       format_bytes(system_total_bytes));
            }

            const std::string ram_tooltip = fmt::format(
                "Bar = total system RAM.\n"
                "Amber: other system usage. Cyan (at the used edge): this process.\n"
                "Dark: free memory.\n"
                "Used: {:.0f}% of system ({} other + {} process).",
                static_cast<double>(system_pct),
                format_bytes(other_used_bytes),
                format_bytes(rss_bytes));

            draw_memory_stat_row("RAM", ram_text, ram_tooltip.c_str());
            draw_stacked_ram_bar(process_pct / 100.0f, system_pct / 100.0f);
        }
        else if(rss_bytes > 0)
        {
            draw_memory_stat_row("RAM", format_bytes(rss_bytes), "Process resident memory (RSS).");
        }
    }
}

auto has_loading_memory_stats() -> bool
{
    const auto* stats = gfx::get_stats();
    if(stats != nullptr && stats->gpuMemoryUsed > 0)
    {
        return true;
    }
    if(platform::get_process_resident_set_bytes() > 0)
    {
        return true;
    }
    return platform::get_system_used_physical_memory_bytes() > 0;
}

} // namespace

imgui_interface::imgui_interface(rtti::context& ctx)
{
    auto& ev = ctx.get_cached<events>();

    ev.on_os_event.connect(sentinel_, 1000, this, &imgui_interface::on_os_event);
    ev.on_frame_render.connect(sentinel_, -100000, this, &imgui_interface::on_frame_ui_render);
}

imgui_interface::~imgui_interface()
{
    if(inited_)
    {
        imguiDestroy();
    }
}

auto imgui_interface::init_basic(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    imguiCreate(main_window, 14.0f);

    imgui_style::set_unity_theme();

    inited_ = true;
    return true;
}

auto imgui_interface::init_finalize(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    imguiCreateCubemapProgram();
    return true;
}

auto imgui_interface::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    return true;
}

void imgui_interface::render_loading_frame(rtti::context& ctx,
                                           const std::string& stage,
                                           size_t completed,
                                           size_t total,
                                           const std::string& current_job)
{


    auto now = std::chrono::steady_clock::now();
    auto dt = now - last_frame_time_;

    if(dt < std::chrono::milliseconds(32) && completed != total)
    {
        return;
    }

    last_frame_time_ = now;

    const auto& rend = ctx.get_cached<renderer>();
    auto window = rend.get_main_window();
    if(!window)
    {
        return;
    }

    os::event e{};
    while(os::poll_event(e))
    {
        imguiProcessEvent(e);
    }

    auto& present_pass = window->begin_present_pass();
    present_pass.clear();

    for(int i = 0; i < 1; ++i)
    {
        imguiBeginFrame(1.0f / 60.0f);
        draw_loading_overlay(stage, completed, total, current_job);
    
        auto& main_surface = window->get_surface();
        gfx::render_pass pass("ImGui/Loading Pass");
        pass.bind(main_surface.get());
        imguiEndFrame(pass.id);
    
        gfx::render_pass end_pass(gfx::render_pass::get_max_pass_id(), "Backbuffer/Loading Present");
        end_pass.bind();
        gfx::frame();
    }

}

void imgui_interface::draw_loading_overlay(const std::string& stage,
                                           size_t completed,
                                           size_t total,
                                           const std::string& current_job)
{
    const auto* viewport = ImGui::GetMainViewport();

    // Full-screen dimmed backdrop
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));

    ImGuiWindowFlags backdrop_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##loading_backdrop", nullptr, backdrop_flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Centered card
    constexpr float card_width = 480.0f;
    float card_x = viewport->WorkPos.x + (viewport->WorkSize.x - card_width) * 0.5f;
    float card_y = viewport->WorkPos.y + viewport->WorkSize.y * 0.38f;
    ImGui::SetNextWindowPos(ImVec2(card_x, card_y));

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 28.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));

    if(ImGui::BeginChild("##loading_card", ImVec2(card_width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        // Title
        ImGui::PushFont(ImGui::Font::Bold);
        auto title_text = "Unravel Engine";
        float title_width = ImGui::CalcTextSize(title_text).x;

        ImGui::AlignedItem(0.5f, ImGui::GetContentRegionAvail().x, title_width, [&]() 
        {
            ImGui::TextUnformatted(title_text);
        });
      
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Spacing();

        // Separator
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        // Stage name
        ImGui::PushFont(ImGui::Font::Medium);
        ImGui::TextUnformatted(stage.c_str());

        auto spinner_size = ImGui::GetTextLineHeight();

        ImGui::SameLine();
        ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, spinner_size, [&]() {
        ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
                ImSpinner::Radius{spinner_size * 0.5f},
                ImSpinner::Thickness{4.0f},
                ImSpinner::Color{ImSpinner::white},
                ImSpinner::Speed{6.0f});
        });
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::Spacing();

        // Custom progress bar
        float fraction = (total > 0) ? static_cast<float>(completed) / static_cast<float>(total) : 0.0f;
        constexpr float bar_height = 6.0f;
        float bar_width = ImGui::GetContentRegionAvail().x;
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Background track
        draw_list->AddRectFilled(bar_pos,
                                 ImVec2(bar_pos.x + bar_width, bar_pos.y + bar_height),
                                 IM_COL32(30, 30, 30, 255),
                                 bar_height * 0.5f);

        // Filled portion with accent color
        if(fraction > 0.0f)
        {
            float fill_width = bar_width * fraction;
            draw_list->AddRectFilled(bar_pos,
                                     ImVec2(bar_pos.x + fill_width, bar_pos.y + bar_height),
                                     IM_COL32(58, 121, 187, 255),
                                     bar_height * 0.5f);
        }

        ImGui::Dummy(ImVec2(bar_width, bar_height));

        ImGui::Spacing();

        
        // Current job name
        if(!current_job.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", current_job.c_str());
            ImGui::PopStyleColor();
        }

        if(total > 0)
        {
            ImGui::SameLine();
            // Progress count -- right-aligned
            auto progress_text = fmt::format("{} / {}", completed, total);
            float count_width = ImGui::CalcTextSize(progress_text.c_str()).x;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, count_width, [&]() {
                ImGui::TextUnformatted(progress_text.c_str());
            });
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        if(has_loading_memory_stats())
        {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            ImGui::PushFont(ImGui::Font::Regular);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, memory_section_alpha);
            draw_loading_memory_stats();
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }

    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void imgui_interface::on_os_event(rtti::context& ctx, os::event& e)
{
    imguiProcessEvent(e);
}

void imgui_interface::on_frame_ui_render(rtti::context& ctx, delta_t dt)
{
    const auto& ev = ctx.get_cached<ui_events>();

    const auto& rend = ctx.get_cached<renderer>();
    const auto& main_window = rend.get_main_window();
    const auto& main_surface = main_window->get_surface();

    APP_SCOPE_PERF("ImGui Frame");
    imguiBeginFrame(dt.count());

    ev.on_frame_ui_render(ctx, dt);

    gfx::render_pass pass("ImGui/Pass");
    pass.bind(main_surface.get());
    imguiEndFrame(pass.id);
}

} // namespace unravel
