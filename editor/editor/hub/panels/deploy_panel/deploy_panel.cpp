#include "deploy_panel.h"
#include "../panel.h"
#include "../panels_defs.h"

#include <editor/system/project_manager.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace unravel
{

deploy_panel::deploy_panel(imgui_panels* parent) : parent_(parent)
{
}

void deploy_panel::show(bool s)
{
    show_request_ = s;

    if(!s)
    {
        deploy_jobs_.clear();
    }
}

void deploy_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(show_request_)
    {
        ImGui::OpenPopup(name);
        show_request_ = false;
    }

    ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.5f);
    bool show = true;
    if(ImGui::BeginPopupModal(name, &show))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        draw_ui(ctx);

        ImGui::EndPopup();
    }
}

auto deploy_panel::get_progress() const -> float
{
    if(deploy_jobs_.empty())
    {
        return 1.0f;
    }


    size_t ready = 0;
    for(const auto& kvp : deploy_jobs_)
    {
        if(kvp.second.is_ready())
        {
            ready++;
        }
    }

    return float(ready) / float(deploy_jobs_.size());
}


auto deploy_panel::is_deploying() const -> bool
{
    float progress = get_progress();
    bool is_in_progress = progress < 0.99f;
    return is_in_progress;
}

void deploy_panel::deploy_and_run(rtti::context& ctx, const deploy_settings& params)
{
    bool is_in_progress = is_deploying();
    if(!is_in_progress && editor_actions::can_deploy_project(ctx, params))
    {
        deploy_jobs_ = editor_actions::deploy_project(ctx, params);
    }
    else
    {
        show(true);
    }
}

void deploy_panel::draw_ui(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_settings();
    auto& deploy_settings = pm.get_deploy_settings();

    if(inspect(ctx, settings.app).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    if(inspect(ctx, settings.standalone).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    if(inspect(ctx, deploy_settings).edit_finished)
    {
        pm.save_deploy_settings();
    }

    bool is_in_progress = is_deploying();
    if(!is_in_progress && editor_actions::can_deploy_project(ctx, deploy_settings))
    {
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           300.0f,
                           [&]()
                           {
                               if(ImGui::Button("Deploy", ImVec2(300.0f, 0.0f)))
                               {
                                   deploy_jobs_ = editor_actions::deploy_project(ctx, deploy_settings);
                               }

                           });
    }

    if(is_in_progress)
    {
        auto sz = ImGui::GetContentRegionAvail().x * 0.6f;
        ImGui::AlignedItem(0.5f,
                           ImGui::GetContentRegionAvail().x,
                           sz,
                           [&]()
                           {
                               ImGui::ProgressBar(get_progress(), ImVec2(sz, 0.0f));
                           });

        for(const auto& kvp : deploy_jobs_)
        {
            const auto& name = kvp.first;
            const auto& job = kvp.second;

            auto text = fmt::format("{} - {}", name.c_str(), (job.is_ready() ? "Done." : "In Progress..."));
            auto sz = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::AlignedItem(0.5f,
                               ImGui::GetContentRegionAvail().x,
                               sz,
                               [&]()
                               {
                                   ImGui::TextUnformatted(text.c_str());
                               });
        }
    }
}

} // namespace unravel
