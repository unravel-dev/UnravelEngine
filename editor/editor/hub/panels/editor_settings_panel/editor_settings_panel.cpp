#include "editor_settings_panel.h"
#include "../panel.h"

#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/project_manager.h>
#include <engine/input/input.h>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace unravel
{
namespace
{
void draw_external_tools_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_editor_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.external_tools).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::PopItemWidth();
}

void draw_debugger_settings(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();
    auto& settings = pm.get_editor_settings();

    ImGui::PushItemWidth(150.0f);

    if(inspect(ctx, settings.debugger).edit_finished)
    {
        pm.save_project_settings(ctx);
    }

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", "Requires an editor restart to apply changes.");

    ImGui::PopItemWidth();
}
} // namespace
editor_settings_panel::editor_settings_panel(imgui_panels* parent) : parent_(parent)
{
}

void editor_settings_panel::show(bool s)
{
    show_request_ = s;
}

void editor_settings_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
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

void editor_settings_panel::draw_ui(rtti::context& ctx)
{
    auto avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f)
    {
        return;
    }

    static std::vector<setting_entry> categories{{"External Tools", &draw_external_tools_settings},
                                                 {"Debugger", &draw_debugger_settings}};
    // Child A: the categories list
    // We fix the width of this child, so the right child uses the remaining space.
    ImGui::BeginChild("##LeftSidebar", avail * ImVec2(0.15f, 1.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    {
        // Display categories
        for(const auto& category : categories)
        {
            // 'Selectable' returns true if clicked
            if(ImGui::Selectable(category.id.c_str(), (selected_entry_.id == category.id)))
            {
                selected_entry_ = category;
            }
        }
    }
    ImGui::EndChild();

    // On the same line:
    ImGui::SameLine();

    // Child B: show settings for the selected category
    ImGui::BeginChild("##RightContent");
    {
        if(selected_entry_.callback)
        {
            selected_entry_.callback(ctx);
        }
    }
    ImGui::EndChild();
}

} // namespace unravel
