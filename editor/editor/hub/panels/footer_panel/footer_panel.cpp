#include "footer_panel.h"
#include "../panels_defs.h"

#include <engine/threading/threader.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <logging/logging.h>

namespace unravel
{

void footer_panel::draw_footer_child(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw)
{
    ImGuiWindowFlags header_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoDecoration;
    const std::string child_id = "FOOTER_menubar";
    ImGui::BeginChild(child_id.c_str(), ImVec2(0, 0), false, header_flags);
    on_draw();


    auto& thr = ctx.get_cached<threader>();
    auto pool_jobs = thr.pool->get_jobs_count_detailed();


    constexpr uint64_t notification_id = 99;
    if(!pool_jobs.empty())
    {
        auto callback = [jobs = std::move(pool_jobs)](const ImGuiToast& toast, float opacity, const ImVec4& text_color) 
        {
            size_t total_job_count = 0;
            for(const auto& [name, count] : jobs)
            {
                total_job_count += count;
            }


            ImGui::AlignTextToFramePadding();

            auto spinner_size = ImGui::GetTextLineHeight();

            ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
                ImSpinner::Radius{spinner_size * 0.5f},
                ImSpinner::Thickness{4.0f},
                ImSpinner::Color{ImSpinner::white},
                ImSpinner::Speed{6.0f});
            ImGui::SameLine();

            ImGui::TextColored(text_color, "%s", fmt::format("Jobs : {}", total_job_count).c_str());

            for(const auto& [name, count] : jobs)
            {
                ImGui::TextColored(text_color, "%s", fmt::format("{} : {}", name, count).c_str());
            }
        };
        ImGuiToast toast(ImGuiToastType_None, callback, 500);
        ImGui::PushNotification(notification_id, toast);
        last_notification_time_ = std::chrono::steady_clock::now();
    }

    if(last_notification_time_ != std::chrono::steady_clock::time_point::min())
    {
        if(last_notification_time_ + std::chrono::milliseconds(850) < std::chrono::steady_clock::now())
        {
            auto callback = [](const ImGuiToast& toast, float opacity, const ImVec4& text_color) 
            {
                ImGui::TextColored(ImGuiToast::get_color(ImGuiToastType_Success), "%s", "Jobs Finished.");
            };
            ImGuiToast toast(ImGuiToastType_Info, callback, 2000);
            ImGui::PushNotification(notification_id, toast);
            last_notification_time_ = std::chrono::steady_clock::time_point::min();
        }
    }
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
