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
constexpr ImU32 EDITOR_SETTINGS_NOTE_COLOR = IM_COL32(255, 190, 60, 255);

/// Inspects one group of the editor settings and saves them once an edit is done.
template<typename Settings>
auto inspect_and_save(rtti::context& ctx, Settings& settings) -> bool
{
    if(!inspect(ctx, settings).edit_finished)
    {
        return false;
    }
    ctx.get_cached<project_manager>().save_editor_settings();
    return true;
}

void draw_external_tools_settings(rtti::context& ctx)
{
    inspect_and_save(ctx, ctx.get_cached<project_manager>().get_editor_settings().external_tools);
}

void draw_debugger_settings(rtti::context& ctx)
{
    inspect_and_save(ctx, ctx.get_cached<project_manager>().get_editor_settings().debugger);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, EDITOR_SETTINGS_NOTE_COLOR);
    ImGui::TextWrapped("%s", ICON_MDI_ALERT_OUTLINE " Changes apply after the editor restarts.");
    ImGui::PopStyleColor();
}

void draw_scripting_settings(rtti::context& ctx)
{
    auto& settings = ctx.get_cached<project_manager>().get_editor_settings();
    if(inspect(ctx, settings.scripting).edit_finished)
    {
        settings.scripting.reload_app_domain = true;
        ctx.get_cached<project_manager>().save_editor_settings();
    }
}

auto make_editor_settings_categories() -> std::vector<settings_category>
{
    return {
        {"External Tools",
         ICON_MDI_WRENCH_OUTLINE,
         "The code editor scripts open in.",
         "vscode visual studio code editor ide executable path",
         &draw_external_tools_settings},
        {"Scripting",
         ICON_MDI_CODE_BRACES,
         "What reloads when scripts compile and play mode changes.",
         "reload domain app engine compile play mode",
         &draw_scripting_settings},
#if DOTNETPP_BACKEND_MONO
        {"Debugger",
         ICON_MDI_BUG_OUTLINE,
         "Where a script debugger connects.",
         "debug ip port log level attach",
         &draw_debugger_settings},
#endif
    };
}
} // namespace

editor_settings_panel::editor_settings_panel(imgui_panels* parent)
    : parent_(parent)
    , view_(make_editor_settings_categories())
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
        view_.draw(ctx);
        ImGui::EndPopup();
    }
}

} // namespace unravel
